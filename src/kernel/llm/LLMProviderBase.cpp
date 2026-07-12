#include "animus_kernel/llm/LLMProviderBase.h"

#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

// libcurl
#include <curl/curl.h>

namespace animus::kernel::llm {

// ============================================================================
// Curl write callbacks
// ============================================================================

struct LLMProviderBase::HTTPImpl {
  CURL* easy{nullptr};
  curl_slist* headers{nullptr};
  std::string responseBody;
  std::string errorBuffer;

  // For streaming
  LLMTokenCallback tokenCallback;
  std::string sseBuffer;     // partial line buffer
  std::string accumulated;   // accumulated response content
};

namespace {

// Write callback for non-streaming: accumulate into string
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t totalSize = size * nmemb;
  auto* body = static_cast<std::string*>(userp);
  body->append(static_cast<char*>(contents), totalSize);
  return totalSize;
}

// Streaming context — only carries pointers to the base class.
// All state lives in the provider's HTTPImpl; access is via public methods.
struct StreamWriteContext {
  LLMProviderBase* provider;
  bool done{false};
};

size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb,
                           void* userp) {
  size_t totalSize = size * nmemb;
  auto* ctx = static_cast<StreamWriteContext*>(userp);
  if (ctx->done) return totalSize;
  ctx->provider->AppendSSEData(static_cast<const char*>(contents), totalSize,
                               &ctx->done);
  return totalSize;
}

} // anonymous namespace

// ============================================================================
// LLMProviderBase
// ============================================================================

LLMProviderBase::LLMProviderBase(const LLMProviderConfig& config)
    : m_config(config), m_http(new HTTPImpl()) {
  m_http->easy = curl_easy_init();
  if (!m_http->easy) {
    delete m_http;
    m_http = nullptr;
    throw std::runtime_error("Failed to initialize curl handle");
  }
}

LLMProviderBase::~LLMProviderBase() {
  if (m_http) {
    if (m_http->easy) {
      curl_easy_cleanup(m_http->easy);
    }
    if (m_http->headers) {
      curl_slist_free_all(m_http->headers);
    }
    delete m_http;
    m_http = nullptr;
  }
}

// ---------------------------------------------------------------------------
// ILLMProvider
// ---------------------------------------------------------------------------

LLMMessage LLMProviderBase::Complete(const LLMRequest& request,
                                     std::string* error) {
  if (!m_available) {
    if (error) *error = "Provider " + m_config.provider_id + " is unavailable";
    return {};
  }

  // Reset accumulated tool calls and finish reason
  m_lastToolCalls.clear();
  m_lastFinishReason.clear();
  m_toolCallAccumulator = SSEToolCallAccumulator{};

  std::string body = BuildRequestBody(request);
  std::string responseStr;

  int status =
      DoHTTPRequest(body, /*stream=*/false, {}, &responseStr, error);
  if (status != 200) {
    if (error) {
      *error = "HTTP " + std::to_string(status) + ": " +
               (error->empty() ? responseStr : *error);
    }
    return {};
  }

  // Parse tool calls and finish reason from the complete response
  ParseToolCallsFromResponse(responseStr, m_lastToolCalls, m_lastFinishReason);

  return ParseResponse(responseStr, error);
}

LLMMessage LLMProviderBase::StreamComplete(const LLMRequest& request,
                                           LLMTokenCallback callback,
                                           std::string* error) {
  if (!m_available) {
    if (error) *error = "Provider " + m_config.provider_id + " is unavailable";
    return {};
  }

  // Reset accumulated tool calls, finish reason, and tool call accumulator
  m_lastToolCalls.clear();
  m_lastFinishReason.clear();
  m_toolCallAccumulator = SSEToolCallAccumulator{};

  // Build a streaming request (force stream=true)
  LLMRequest streamReq = request;
  streamReq.stream = true;

  std::string body = BuildRequestBody(streamReq);

  // Wrap the callback to:
  //   1. Feed SSE lines to the tool call accumulator
  //   2. Capture any inline tool calls from final tokens (provider-specific)
  //   3. Capture finish reason
  //   4. Forward content tokens to the original callback
  LLMTokenCallback wrappedCallback;
  if (callback) {
    wrappedCallback = [this, cb = std::move(callback)](const LLMToken& token) {
      // Capture finish reason
      if (!token.finish_reason.empty()) {
        m_lastFinishReason = token.finish_reason;
      }

      // Capture any inline tool calls from final tokens
      // (Some providers send complete tool calls in the final chunk)
      if (!token.tool_calls.empty()) {
        for (const auto& tc : token.tool_calls) {
          m_lastToolCalls.push_back(tc);
        }
      }

      cb(token);
    };
  } else {
    wrappedCallback = [this](const LLMToken& token) {
      if (!token.finish_reason.empty()) {
        m_lastFinishReason = token.finish_reason;
      }
      if (!token.tool_calls.empty()) {
        for (const auto& tc : token.tool_calls) {
          m_lastToolCalls.push_back(tc);
        }
      }
    };
  }

  int status = DoHTTPRequest(body, /*stream=*/true, std::move(wrappedCallback), nullptr, error);
  if (status != 200) {
    if (error) {
      *error = "HTTP " + std::to_string(status) +
               (error->empty() ? "" : ": " + *error);
    }
    return {};
  }

  // Finalize tool call accumulation — merge accumulated delta fragments
  // with any inline tool calls captured from final tokens.
  auto accumulatedCalls = m_toolCallAccumulator.Finalize();
  if (!accumulatedCalls.empty()) {
    m_lastToolCalls = std::move(accumulatedCalls);
  }

  // The streaming response was assembled in m_http->accumulated
  // (only non-thinking content is accumulated)
  LLMMessage result;
  result.role = "assistant";
  result.content = m_http->accumulated;
  return result;
}

bool LLMProviderBase::IsAvailable() const {
  return m_available;
}

// ---------------------------------------------------------------------------
// Template methods (defaults)
// ---------------------------------------------------------------------------

bool LLMProviderBase::IsSSEDone(const std::string& line) const {
  // Trim whitespace
  auto start = line.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return false;
  std::string_view trimmed(line.data() + start, line.size() - start);
  return trimmed == "[DONE]";
}

std::string LLMProviderBase::GetEndpointURL() const {
  return m_config.base_url + "/chat/completions";
}

std::vector<std::pair<std::string, std::string>>
LLMProviderBase::GetHeaders() const {
  std::vector<std::pair<std::string, std::string>> headers;
  headers.emplace_back("Content-Type", "application/json");
  if (!m_config.api_key.empty()) {
    headers.emplace_back("Authorization",
                         "Bearer " + m_config.api_key);
  }
  return headers;
}

void LLMProviderBase::SetAvailable(bool available) {
  m_available = available;
}

// ---------------------------------------------------------------------------
// Capabilities — base implementation returns safe defaults
// ---------------------------------------------------------------------------

bool LLMProviderBase::FetchCapabilities(const std::string& modelId) {
  // Default: assume everything is supported. Providers override to query their API.
  m_capabilities.model_id = modelId;
  m_capabilities.supports_tools = true;
  m_capabilities.supports_tool_choice = true;
  m_capabilities.supports_reasoning = true;
  m_capabilities.supports_streaming = true;
  return true;
}

// ---------------------------------------------------------------------------
// SSE processing
// ---------------------------------------------------------------------------

bool LLMProviderBase::ProcessSSELine(const std::string& line,
                                     std::string& accumulated,
                                     LLMTokenCallback& callback) {
  // Skip empty lines (SSE separators)
  if (line.empty() || line == "\r") return false;

  // Check for stream end
  if (IsSSEDone(line)) return true;

  // Strip "data: " prefix if present
  std::string data = line;
  if (data.size() > 6 && data.substr(0, 6) == "data: ") {
    data = data.substr(6);
  } else if (data.size() > 5 && data.substr(0, 5) == "data:") {
    data = data.substr(5);
  }

  // Trim trailing whitespace
  while (!data.empty() && (data.back() == '\r' || data.back() == ' ')) {
    data.pop_back();
  }

  if (data.empty()) return false;

  // Diagnostic: log each SSE event type
  auto typePos = data.find("\"type\"");
  if (typePos != std::string::npos) {
    auto valStart = data.find('\"', typePos + 7);
    if (valStart != std::string::npos) {
      auto valEnd = data.find('\"', valStart + 1);
      if (valEnd != std::string::npos) {
        std::cerr << "[sse] event: " << data.substr(valStart + 1, valEnd - valStart - 1) << std::endl;
      }
    }
  }

  // Feed every SSE line to the tool call accumulator.
  m_toolCallAccumulator.ProcessLine(data);

  auto token = ParseSSELine(data);
  if (token.has_value()) {
    // Only accumulate non-thinking tokens into the response content.
    // Thinking tokens are streamed to the UI via the callback but
    // should not be part of the accumulated response text.
    if (!token->is_thinking) {
      accumulated += token->content;
    }
    if (callback) {
      callback(token.value());
    }
  }
  return false;
}

void LLMProviderBase::AppendSSEData(const char* data, size_t len, bool* done) {
  m_http->sseBuffer.append(data, len);
  m_http->responseBody.append(data, len);

  std::string& buf = m_http->sseBuffer;
  size_t pos = 0;
  while (pos < buf.size()) {
    size_t eol = buf.find('\n', pos);
    if (eol == std::string::npos) break; // incomplete line

    std::string line = buf.substr(pos, eol - pos);
    pos = eol + 1;

    if (ProcessSSELine(line, m_http->accumulated, m_http->tokenCallback)) {
      *done = true;
      break;
    }
  }

  if (pos > 0) {
    buf.erase(0, pos);
  }
}

// ---------------------------------------------------------------------------
// Shared HTTP machinery
// ---------------------------------------------------------------------------

int LLMProviderBase::DoHTTPRequest(const std::string& body,
                                   bool stream,
                                   LLMTokenCallback tokenCallback,
                                   std::string* responseBody,
                                   std::string* error) {
  if (!m_http || !m_http->easy) {
    if (error) *error = "Curl handle not initialized";
    return 0;
  }

  CURL* curl = m_http->easy;

  // Reset handle for reuse
  curl_easy_reset(curl);

  // Free old headers
  if (m_http->headers) {
    curl_slist_free_all(m_http->headers);
    m_http->headers = nullptr;
  }

  // Set URL
  std::string url = GetEndpointURL();
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

  // Set POST body
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                   static_cast<curl_off_t>(body.size()));

  // Build headers
  auto headers = GetHeaders();
  for (const auto& [key, value] : headers) {
    std::string header = key + ": " + value;
    m_http->headers =
        curl_slist_append(m_http->headers, header.c_str());
  }
  if (m_http->headers) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, m_http->headers);
  }

  // Timeouts
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                   static_cast<long>(m_config.connect_timeout_ms));
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                   stream ? 0L
                          : static_cast<long>(m_config.connect_timeout_ms) *
                                4L);
  // For streaming: abort if no data received for 90 seconds (stall detection)
  if (stream) {
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);  // 1 byte/sec
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 90L);   // for 90 seconds
  }

  // Error buffer
  char errbuf[CURL_ERROR_SIZE] = {};
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

  // Verbose request logging (truncated — full body can be 100KB+)
  const std::size_t bodyLogLen = std::min(body.size(), static_cast<std::size_t>(200));
  std::cerr << "[llm] POST " << url << " body_size=" << body.size();
  if (body.size() > bodyLogLen) {
    std::cerr << " body_preview=" << body.substr(0, bodyLogLen) << "...";
  } else {
    std::cerr << " body=" << body;
  }
  std::cerr << std::endl;
  if (stream) {
    m_http->tokenCallback = std::move(tokenCallback);
    m_http->sseBuffer.clear();
    m_http->accumulated.clear();
    m_http->responseBody.clear();

    StreamWriteContext ctx{this, false};

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      if (error) {
        *error = std::string(errbuf[0] ? errbuf : curl_easy_strerror(res));
      }
      return 0;
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode >= 400) std::cerr << "[llm] HTTP " << httpCode << " response=" << m_http->responseBody.substr(0, 1000) << std::endl;
    return static_cast<int>(httpCode);

  } else {
    m_http->responseBody.clear();

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &m_http->responseBody);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      if (error) {
        *error = std::string(errbuf[0] ? errbuf : curl_easy_strerror(res));
      }
      return 0;
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    if (responseBody) {
      *responseBody = m_http->responseBody;
    }

    if (httpCode >= 400) std::cerr << "[llm] HTTP " << httpCode << " response=" << m_http->responseBody.substr(0, 1000) << std::endl;
    return static_cast<int>(httpCode);
  }
}

// ---------------------------------------------------------------------------
// HTTP GET
// ---------------------------------------------------------------------------

int LLMProviderBase::DoHTTPGet(
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& headers,
    std::string* responseBody,
    std::string* error) {
  if (!m_http || !m_http->easy) {
    if (error) *error = "Curl handle not initialized";
    return 0;
  }

  CURL* curl = m_http->easy;
  curl_easy_reset(curl);

  // Free old headers
  if (m_http->headers) {
    curl_slist_free_all(m_http->headers);
    m_http->headers = nullptr;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);

  // Build headers
  for (const auto& [key, value] : headers) {
    std::string header = key + ": " + value;
    m_http->headers =
        curl_slist_append(m_http->headers, header.c_str());
  }
  if (m_http->headers) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, m_http->headers);
  }

  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                   static_cast<long>(m_config.connect_timeout_ms));
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                   static_cast<long>(m_config.connect_timeout_ms) * 2L);

  char errbuf[CURL_ERROR_SIZE] = {};
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

  m_http->responseBody.clear();
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &m_http->responseBody);

  std::cerr << "[llm] GET " << url << std::endl;

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    if (error) {
      *error = std::string(errbuf[0] ? errbuf : curl_easy_strerror(res));
    }
    return 0;
  }

  long httpCode = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

  if (responseBody) {
    *responseBody = m_http->responseBody;
  }

  std::cerr << "[llm] GET " << url << " -> " << httpCode << std::endl;
  return static_cast<int>(httpCode);
}

int LLMProviderBase::DoHTTPPost(
    const std::string& url,
    const std::string& body,
    const std::vector<std::pair<std::string, std::string>>& headers,
    std::string* responseBody,
    std::string* error) {
  if (!m_http || !m_http->easy) {
    if (error) *error = "Curl handle not initialized";
    return 0;
  }

  CURL* curl = m_http->easy;
  curl_easy_reset(curl);

  if (m_http->headers) {
    curl_slist_free_all(m_http->headers);
    m_http->headers = nullptr;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                   static_cast<curl_off_t>(body.size()));

  for (const auto& [key, value] : headers) {
    std::string header = key + ": " + value;
    m_http->headers =
        curl_slist_append(m_http->headers, header.c_str());
  }
  if (m_http->headers) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, m_http->headers);
  }

  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                   static_cast<long>(m_config.connect_timeout_ms));
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                   static_cast<long>(m_config.connect_timeout_ms) * 2L);

  char errbuf[CURL_ERROR_SIZE] = {};
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

  m_http->responseBody.clear();
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &m_http->responseBody);

  std::cerr << "[llm] POST " << url << std::endl;

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    if (error) {
      *error = std::string(errbuf[0] ? errbuf : curl_easy_strerror(res));
    }
    return 0;
  }

  long httpCode = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

  if (responseBody) {
    *responseBody = m_http->responseBody;
  }

  std::cerr << "[llm] POST " << url << " -> " << httpCode << std::endl;
  return static_cast<int>(httpCode);
}

// ---------------------------------------------------------------------------
// GetVisionModelIds — default implementation
// ---------------------------------------------------------------------------

std::vector<std::string> LLMProviderBase::GetVisionModelIds() const {
  std::vector<std::string> result;
  if (m_capabilities.supports_vision && !m_capabilities.model_id.empty()) {
    result.push_back(m_capabilities.model_id);
  }
  return result;
}

} // namespace animus::kernel::llm

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

  // For streaming: the owning base class parses SSE lines
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

// Write callback for streaming: feed chunks into SSE line buffer,
// parse complete lines immediately.
//
// The userp is a pointer to a struct that holds the base class pointer
// and the impl, so we can call ParseSSELine/IsSSEDone incrementally.
struct StreamContext {
  LLMProviderBase* provider;
  LLMProviderBase::HTTPImpl* impl;
  bool done{false};
};

size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb,
                           void* userp) {
  size_t totalSize = size * nmemb;
  auto* ctx = static_cast<StreamContext*>(userp);
  if (ctx->done) return totalSize;

  ctx->impl->sseBuffer.append(static_cast<char*>(contents), totalSize);

  // Parse complete lines from the buffer
  std::string& buf = ctx->impl->sseBuffer;
  size_t pos = 0;
  while (pos < buf.size()) {
    size_t eol = buf.find('\n', pos);
    if (eol == std::string::npos) break; // incomplete line, wait for more

    std::string line = buf.substr(pos, eol - pos);
    pos = eol + 1;

    // Skip empty lines (SSE separators)
    if (line.empty() || line == "\r") continue;

    // Check for stream end
    if (ctx->provider->IsSSEDone(line)) {
      ctx->done = true;
      break;
    }

    // Strip "data: " prefix if present
    std::string data = line;
    if (data.size() > 6 && data.substr(0, 6) == "data: ") {
      data = data.substr(6);
    } else if (data.size() > 5 && data.substr(0, 5) == "data:") {
      data = data.substr(5);
    }

    // Trim whitespace
    while (!data.empty() && (data.back() == '\r' || data.back() == ' ')) {
      data.pop_back();
    }

    if (data.empty()) continue;

    auto token = ctx->provider->ParseSSELine(data);
    if (token.has_value()) {
      ctx->impl->accumulated += token->content;
      if (ctx->impl->tokenCallback) {
        ctx->impl->tokenCallback(token.value());
      }
    }
  }

  // Remove processed data from buffer
  if (pos > 0) {
    buf.erase(0, pos);
  }

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

  return ParseResponse(responseStr, error);
}

LLMMessage LLMProviderBase::StreamComplete(const LLMRequest& request,
                                           LLMTokenCallback callback,
                                           std::string* error) {
  if (!m_available) {
    if (error) *error = "Provider " + m_config.provider_id + " is unavailable";
    return {};
  }

  // Build a streaming request (force stream=true)
  LLMRequest streamReq = request;
  streamReq.stream = true;

  std::string body = BuildRequestBody(streamReq);

  int status = DoHTTPRequest(body, /*stream=*/true, callback, nullptr, error);
  if (status != 200) {
    if (error) {
      *error = "HTTP " + std::to_string(status) +
               (error->empty() ? "" : ": " + *error);
    }
    return {};
  }

  // The streaming response was assembled in m_http->accumulated
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

  // Error buffer
  char errbuf[CURL_ERROR_SIZE] = {};
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

  if (stream) {
    // Streaming mode — parse SSE lines incrementally as data arrives
    m_http->tokenCallback = std::move(tokenCallback);
    m_http->sseBuffer.clear();
    m_http->accumulated.clear();
    m_http->responseBody.clear();

    StreamContext ctx{this, m_http, false};

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
    return static_cast<int>(httpCode);

  } else {
    // Non-streaming mode
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

    return static_cast<int>(httpCode);
  }
}

} // namespace animus::kernel::llm

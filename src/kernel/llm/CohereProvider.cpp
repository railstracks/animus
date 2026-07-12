#include "animus_kernel/llm/CohereProvider.h"
#include "animus_kernel/llm/OpenAICompat.h"

#include <iostream>
#include <sstream>
#include <json/reader.h>
#include <json/value.h>

using namespace animus::kernel::llm::openai_compat;

namespace animus::kernel::llm {

std::string CohereProvider::GetEndpointURL() const {
  return Config().base_url + "/chat";
}

std::string CohereProvider::BuildRequestBody(
    const LLMRequest& request) const {
  std::string body = BuildOpenAIRequestBody(request);

  // --- Capability-aware parameter stripping ---

  // Strip tool_choice if the model doesn't support it
  if (!GetCapabilities().supports_tool_choice) {
    auto stripField = [&body](const std::string& pattern) {
      auto pos = body.find(pattern);
      if (pos != std::string::npos) {
        auto end = pos + pattern.size();
        if (pos > 0 && body[pos - 1] == ',') {
          pos--;
          end++;
        }
        body.erase(pos, end - pos);
      }
    };
    stripField("\"tool_choice\":\"auto\"");
    stripField("\"tool_choice\":\"none\"");
    stripField("\"tool_choice\":\"required\"");
    // Object form: "tool_choice":{"type":"function",...}
    auto tcObjPos = body.find("\"tool_choice\":{\"type\":\"function\"");
    if (tcObjPos != std::string::npos) {
      int depth = 0;
      auto searchStart = body.find('{', tcObjPos + 14);
      if (searchStart != std::string::npos) {
        for (size_t i = searchStart; i < body.size(); ++i) {
          if (body[i] == '{') depth++;
          else if (body[i] == '}') {
            depth--;
            if (depth == 0) {
              auto end = i + 1;
              auto start = tcObjPos;
              if (start > 0 && body[start - 1] == ',') start--;
              body.erase(start, end - start);
              break;
            }
          }
        }
      }
    }
  }

  // When reasoning effort is set, add the "thinking" parameter.
  // Cohere v2 supports: "thinking": {"type": "enabled"}
  // Cohere does not support effort levels — all non-empty values enable thinking.
  if (IsReasoningEnabled(request.reasoning_effort) &&
      GetCapabilities().supports_reasoning) {
    body.insert(body.size() - 1, ",\"thinking\":{\"type\":\"enabled\"}");
  }

  return body;
}

LLMMessage CohereProvider::ParseResponse(const std::string& body,
                                          std::string* error) const {
  auto msgPos = body.find("\"message\"");
  if (msgPos == std::string::npos) {
    if (error) *error = "No message in Cohere response";
    return {};
  }

  LLMMessage msg;
  msg.role = "assistant";

  // Cohere v2 returns content as an array of typed objects when thinking is enabled:
  //   "content": [{"type": "thinking", "thinking": "..."}, {"type": "text", "text": "..."}]
  // When thinking is disabled, content may be a plain string.
  auto contentPos = body.find("\"content\"", msgPos);
  if (contentPos == std::string::npos) {
    return msg;
  }

  // Check if content is an array (starts with [ after the colon)
  auto colonPos = body.find(':', contentPos + 9);
  if (colonPos == std::string::npos) return msg;

  auto afterColon = colonPos + 1;
  while (afterColon < body.size() && (body[afterColon] == ' ' || body[afterColon] == '\t' || body[afterColon] == '\n' || body[afterColon] == '\r')) {
    afterColon++;
  }

  if (afterColon < body.size() && body[afterColon] == '[') {
    // Content is an array — extract text entries (skip thinking entries)
    std::string arrayContent = body.substr(afterColon);
    // Find the matching ]
    int depth = 0;
    size_t end = 0;
    for (size_t i = 0; i < arrayContent.size(); ++i) {
      if (arrayContent[i] == '[') depth++;
      else if (arrayContent[i] == ']') {
        depth--;
        if (depth == 0) { end = i; break; }
      }
    }
    if (end > 0) {
      arrayContent = arrayContent.substr(0, end + 1);
      // Extract text from each content entry
      size_t pos = 0;
      while (pos < arrayContent.size()) {
        auto typePos = arrayContent.find("\"type\"", pos);
        if (typePos == std::string::npos) break;

        auto typeValStart = arrayContent.find('"', typePos + 6);
        if (typeValStart == std::string::npos) break;
        auto typeValEnd = arrayContent.find('"', typeValStart + 1);
        if (typeValEnd == std::string::npos) break;

        std::string typeVal = arrayContent.substr(typeValStart + 1, typeValEnd - typeValStart - 1);

        if (typeVal == "text") {
          auto textContent = ExtractJsonString(arrayContent.substr(typePos), "text");
          if (!textContent.empty()) {
            msg.content += textContent;
          }
        } else if (typeVal == "thinking") {
          // Extract thinking content from non-streaming response.
          msg.thinking_content += ExtractJsonString(arrayContent.substr(typePos), "thinking");
        }

        pos = typeValEnd + 1;
      }
    }
  } else {
    // Content is a plain string
    msg.content = ExtractJsonString(body.substr(msgPos), "content");
    if (msg.content.empty()) {
      auto textPos = body.find("\"text\"", msgPos);
      if (textPos != std::string::npos) {
        msg.content = ExtractJsonString(body.substr(textPos), "text");
      }
    }
  }

  return msg;
}

std::optional<LLMToken> CohereProvider::ParseSSELine(
    const std::string& line) const {
  auto type = ExtractJsonString(line, "type");

  // --- Stream start ---
  if (type == "message-start") {
    m_cohereToolCallAccumulator.Finalize();
    return std::nullopt;
  }

  // --- Content streaming ---
  // With native thinking, Cohere v2 may send:
  //   content-delta with delta.type = "thinking_delta" and delta.thinking = "..."
  //   content-delta with delta.type = "text_delta" and delta.text = "..."
  // Without thinking, it sends:
  //   content-delta with delta.text = "..."

  if (type == "content-delta") {
    // Cohere v2 reasoning: both thinking and text come as content-delta events.
    // Thinking: delta.message.content.thinking = "..."
    // Text: delta.message.content.text = "..."
    // They share the same event type — distinguished by which field is present.
    //
    // Note: ExtractJsonString finds the first occurrence of the key in the line.
    // Since the line contains "thinking" as both a possible field name and the
    // JSON structure path, we rely on checking both and prioritizing thinking.
    auto thinking = ExtractJsonString(line, "thinking");
    if (!thinking.empty()) {
      // Verify this is actually a thinking content field, not a coincidence.
      // Cohere thinking events have: "thinking":"<actual thinking text>"
      // at the content level (delta.message.content.thinking).
      // We check that the key appears after "content" to avoid false matches.
      auto contentPos = line.find("\"content\"");
      if (contentPos != std::string::npos) {
        auto thinkingKeyPos = line.find("\"thinking\"", contentPos);
        if (thinkingKeyPos != std::string::npos) {
          LLMToken token;
          token.content = thinking;
          token.is_thinking = true;
          return token;
        }
      }
    }

    // Regular text content
    auto text = ExtractJsonString(line, "text");
    if (text.empty()) return std::nullopt;

    LLMToken token;
    token.content = text;
    return token;
  }

  // --- Tool plan streaming (model's reasoning about which tools to call) ---
  if (type == "tool-plan-delta") {
    auto plan = ExtractJsonString(line, "tool_plan");
    if (plan.empty()) return std::nullopt;

    LLMToken token;
    token.content = plan;
    token.is_thinking = true;
    return token;
  }

  // --- Tool call streaming events ---
  if (type == "tool-call-start" || type == "tool-call-delta" || type == "tool-call-end") {
    return std::nullopt;
  }

  // --- Stream end ---
  if (type == "message-end") {
    LLMToken token;
    token.is_final = true;
    token.finish_reason = "stop";

    auto finishReason = ExtractJsonString(line, "finish_reason");
    if (finishReason == "TOOL_CALL" || finishReason == "tool_call") {
      token.finish_reason = "tool_calls";
    }

    auto ptStr = ExtractJsonNumber(line, "input_tokens");
    auto ctStr = ExtractJsonNumber(line, "output_tokens");
    if (!ptStr.empty()) token.prompt_tokens = std::stoi(ptStr);
    if (!ctStr.empty()) token.completion_tokens = std::stoi(ctStr);

    return token;
  }

  return std::nullopt;
}

bool CohereProvider::IsSSEDone(const std::string& line) const {
  auto type = ExtractJsonString(line, "type");
  return type == "message-end";
}

bool CohereProvider::ProcessSSELine(const std::string& line,
                                     std::string& accumulated,
                                     LLMTokenCallback& callback) {
  if (line.empty() || line == "\r") return false;

  if (IsSSEDone(line)) {
    auto cohereCalls = m_cohereToolCallAccumulator.Finalize();
    if (!cohereCalls.empty()) {
      LLMToken toolToken;
      toolToken.is_final = false;
      toolToken.finish_reason = "tool_calls";
      toolToken.tool_calls = std::move(cohereCalls);
      if (callback) {
        callback(toolToken);
      }
    }
    return true;
  }

  std::string data = line;
  if (data.size() > 6 && data.substr(0, 6) == "data: ") {
    data = data.substr(6);
  } else if (data.size() > 5 && data.substr(0, 5) == "data:") {
    data = data.substr(5);
  }

  while (!data.empty() && (data.back() == '\r' || data.back() == ' ')) {
    data.pop_back();
  }

  if (data.empty()) return false;

  m_cohereToolCallAccumulator.ProcessLine(data);

  auto token = ParseSSELine(data);
  if (token.has_value()) {
    // Don't accumulate thinking tokens into content
    if (!token->is_thinking) {
      accumulated += token->content;
    }
    if (callback) {
      callback(token.value());
    }
  }
  return false;
}

void CohereProvider::ParseToolCallsFromResponse(
    const std::string& body,
    std::vector<LLMToolCall>& tool_calls,
    std::string& finish_reason) const {
  auto tcPos = body.find("\"tool_calls\"");
  if (tcPos == std::string::npos) return;

  auto fr = ExtractJsonString(body, "finish_reason");
  if (fr == "TOOL_CALL" || fr == "tool_call") {
    finish_reason = "tool_calls";
  }

  tool_calls = ParseToolCalls(body);
}

// ---------------------------------------------------------------------------
// FetchCapabilities — query Cohere /v1/models endpoint
// ---------------------------------------------------------------------------

bool CohereProvider::FetchCapabilities(const std::string& modelId) {
  m_capabilities = ProviderCapabilities{};  // Reset to defaults
  m_capabilities.model_id = modelId;

  // Build URL for the Cohere models endpoint
  // base_url is e.g. "https://api.cohere.com/v2" → models API is /v1/models
  std::string modelsUrl = "https://api.cohere.com/v1/models";
  modelsUrl += "?endpoint=chat&page_size=1000";

  std::vector<std::pair<std::string, std::string>> headers;
  headers.emplace_back("Authorization", "Bearer " + Config().api_key);
  headers.emplace_back("Accept", "application/json");

  std::string responseBody;
  std::string error;
  int status = DoHTTPGet(modelsUrl, headers, &responseBody, &error);

  if (status != 200) {
    std::cerr << "[cohere] Failed to fetch models (HTTP " << status
              << "): " << error << std::endl;
    // Fall back to safe defaults — assume everything supported
    m_capabilities.supports_tools = true;
    m_capabilities.supports_tool_choice = true;
    m_capabilities.supports_reasoning = true;
    m_capabilities.supports_streaming = true;
    return false;
  }

  // Parse the response
  Json::Value root;
  Json::CharReaderBuilder builder;
  std::string parseErrors;
  std::istringstream stream(responseBody);
  if (!Json::parseFromStream(builder, stream, &root, &parseErrors)) {
    std::cerr << "[cohere] Failed to parse models response: " << parseErrors << std::endl;
    m_capabilities.supports_tools = true;
    m_capabilities.supports_tool_choice = true;
    m_capabilities.supports_reasoning = true;
    m_capabilities.supports_streaming = true;
    return false;
  }

  // Parse the response and build both capabilities and model catalog
  const Json::Value& models = root["models"];
  if (!models.isArray()) {
    std::cerr << "[cohere] No 'models' array in response" << std::endl;
    return false;
  }

  for (Json::ArrayIndex i = 0; i < models.size(); ++i) {
    const Json::Value& model = models[i];
    // Build model catalog in raw_features
    m_capabilities.raw_features.push_back(model["name"].asString());

    if (model["name"].asString() == modelId) {
      // Found our model — extract features
      m_capabilities.context_length = model.get("context_length", 0).asInt();

      const Json::Value& features = model["features"];
      if (features.isArray()) {
        for (Json::ArrayIndex j = 0; j < features.size(); ++j) {
          std::string feat = features[j].asString();

          if (feat == "tools") m_capabilities.supports_tools = true;
          else if (feat == "tool_choice") m_capabilities.supports_tool_choice = true;
          else if (feat == "reasoning") m_capabilities.supports_reasoning = true;
          else if (feat == "vision") m_capabilities.supports_vision = true;
          else if (feat == "json_mode") m_capabilities.supports_json_mode = true;
        }
      }

      std::cerr << "[cohere] Capabilities for " << modelId << ": "
                << "tools=" << m_capabilities.supports_tools
                << " tool_choice=" << m_capabilities.supports_tool_choice
                << " reasoning=" << m_capabilities.supports_reasoning
                << " context=" << m_capabilities.context_length
                << " (" << m_capabilities.raw_features.size() << " models listed)"
                << std::endl;
      return true;
    }
  }

  // Model not found in the list — fall back to safe defaults
  std::cerr << "[cohere] Model '" << modelId << "' not found in models list, using defaults" << std::endl;
  m_capabilities.supports_tools = true;
  m_capabilities.supports_tool_choice = true;
  m_capabilities.supports_reasoning = true;
  m_capabilities.supports_streaming = true;
  return false;
}

} // namespace animus::kernel::llm

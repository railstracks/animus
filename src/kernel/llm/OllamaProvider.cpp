#include "animus_kernel/llm/OllamaProvider.h"
#include "animus_kernel/llm/OpenAICompat.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <json/reader.h>
#include <json/value.h>

using namespace animus::kernel::llm::openai_compat;

namespace animus::kernel::llm {

std::string OllamaProvider::BuildRequestBody(
    const LLMRequest& request) const {
  // Ollama's /v1/chat/completions endpoint is OpenAI-compatible,
  // including tools, tool_choice, tool_call_id, etc.
  // Delegate to the shared builder.
  std::string body = BuildOpenAIRequestBody(request);

  // When reasoning effort is set, add the "reasoning_effort" parameter.
  // Ollama's /v1 endpoint accepts: "none", "low", "medium", "high".
  // ("xhigh" is OpenAI-specific; Ollama silently ignores unknown values.)
  if (IsReasoningEnabled(request.reasoning_effort)) {
    body.insert(body.size() - 1,
      ",\"reasoning_effort\":\"" + request.reasoning_effort + "\"");
  }

  return body;
}

LLMMessage OllamaProvider::ParseResponse(const std::string& body,
                                          std::string* error) const {
  auto choicesPos = body.find("\"choices\"");
  if (choicesPos == std::string::npos) {
    auto errMsg = ExtractJsonString(body, "message");
    if (error) *error = errMsg.empty() ? "No choices in response" : errMsg;
    return {};
  }

  LLMMessage msg;
  msg.role = "assistant";
  msg.content = ExtractJsonString(body.substr(choicesPos), "content");

  // Extract reasoning from non-streaming response.
  // Ollama uses "reasoning" (not "reasoning_content") in non-streaming.
  msg.thinking_content = ExtractJsonString(body.substr(choicesPos), "reasoning");

  return msg;
}

std::optional<LLMToken> OllamaProvider::ParseSSELine(
    const std::string& line) const {

  // Check for reasoning content from Ollama thinking models.
  // Ollama /v1 SSE uses "reasoning_content" in the delta (OpenAI convention).
  // Fallback: also check "reasoning" in case of format variations.
  auto reasoningContent = ExtractJsonString(line, "reasoning_content");
  if (reasoningContent.empty()) {
    reasoningContent = ExtractJsonString(line, "reasoning");
  }
  if (!reasoningContent.empty()) {
    LLMToken token;
    token.content = reasoningContent;
    token.is_thinking = true;
    return token;
  }

  // Check for finish reasons
  auto frToolCalls = line.find("\"finish_reason\":\"tool_calls\"");
  bool isToolCalls = (frToolCalls != std::string::npos);
  auto frStop = line.find("\"finish_reason\":\"stop\"");
  bool isFinal = (frStop != std::string::npos) || isToolCalls;

  auto content = ExtractJsonString(line, "content");

  LLMToken token;
  token.content = content;
  token.is_final = isFinal;

  if (frStop != std::string::npos) {
    token.finish_reason = "stop";
  } else if (isToolCalls) {
    token.finish_reason = "tool_calls";
  }

  // Parse tool calls from the final SSE chunk if present.
  if (isToolCalls) {
    token.tool_calls = ParseToolCalls(line);
  }

  if (content.empty() && !isFinal) {
    // Check for usage stats
    auto ptStr = ExtractJsonNumber(line, "prompt_tokens");
    auto ctStr = ExtractJsonNumber(line, "completion_tokens");
    if (!ptStr.empty()) token.prompt_tokens = std::stoi(ptStr);
    if (!ctStr.empty()) token.completion_tokens = std::stoi(ctStr);
    if (ptStr.empty() && ctStr.empty()) return std::nullopt;
  }

  return token;
}

void OllamaProvider::ParseToolCallsFromResponse(
    const std::string& body,
    std::vector<LLMToolCall>& tool_calls,
    std::string& finish_reason) const {
  tool_calls = ParseToolCalls(body);
  finish_reason = ExtractFinishReason(body);
}

// ---------------------------------------------------------------------------
// FetchCapabilities — query Ollama /v1/models endpoint
// ---------------------------------------------------------------------------

bool OllamaProvider::FetchCapabilities(const std::string& modelId) {
  // Ollama defaults: everything supported, reasoning is safe (silently ignored
  // if the model doesn't support it).
  m_capabilities = ProviderCapabilities{};
  m_capabilities.model_id = modelId;
  m_capabilities.supports_tools = true;
  m_capabilities.supports_tool_choice = true;
  m_capabilities.supports_reasoning = true;
  m_capabilities.supports_streaming = true;

  // Query /v1/models to verify the model exists
  // base_url is typically "http://localhost:11434/v1"
  std::string modelsUrl = Config().base_url + "/models";

  std::vector<std::pair<std::string, std::string>> headers;
  headers.emplace_back("Accept", "application/json");

  std::string responseBody;
  std::string error;
  int status = DoHTTPGet(modelsUrl, headers, &responseBody, &error);

  if (status != 200) {
    std::cerr << "[ollama] Failed to fetch models (HTTP " << status
              << "): " << error << std::endl;
    // Keep safe defaults
    return false;
  }

  // Parse OpenAI-compatible response: {"object":"list","data":[{"id":"model:tag",...}]}
  Json::Value root;
  Json::CharReaderBuilder builder;
  std::string parseErrors;
  std::istringstream stream(responseBody);
  if (!Json::parseFromStream(builder, stream, &root, &parseErrors)) {
    std::cerr << "[ollama] Failed to parse models response: " << parseErrors << std::endl;
    return false;
  }

  const Json::Value& data = root["data"];
  if (!data.isArray()) {
    std::cerr << "[ollama] No 'data' array in models response" << std::endl;
    return false;
  }

  // Check if the model exists in the list
  bool found = false;
  for (Json::ArrayIndex i = 0; i < data.size(); ++i) {
    if (data[i]["id"].asString() == modelId) {
      found = true;
      break;
    }
  }

  if (found) {
    std::cerr << "[ollama] Model '" << modelId << "' found in local models" << std::endl;

    // Query /api/show to get model info including context length.
    // The Ollama /api/show endpoint is separate from the OpenAI-compatible /v1/models.
    // base_url is "https://ollama.com/v1" — strip "/v1" to get the native API base.
    std::string nativeBase = Config().base_url;
    if (nativeBase.size() >= 3 && nativeBase.substr(nativeBase.size() - 3) == "/v1") {
      nativeBase = nativeBase.substr(0, nativeBase.size() - 3);
    }
    std::string showUrl = nativeBase + "/api/show";
    std::string showBody = "{\"model\":\"" + modelId + "\"}";
    std::vector<std::pair<std::string, std::string>> showHeaders;
    showHeaders.emplace_back("Content-Type", "application/json");
    showHeaders.emplace_back("Accept", "application/json");

    std::string showResponse;
    std::string showError;
    int showStatus = DoHTTPPost(showUrl, showBody, showHeaders, &showResponse, &showError);
    if (showStatus == 200 && !showResponse.empty()) {
      Json::Value showRoot;
      Json::CharReaderBuilder showBuilder;
      std::string showParseErrors;
      std::istringstream showStream(showResponse);
      if (Json::parseFromStream(showBuilder, showStream, &showRoot, &showParseErrors)) {
        const Json::Value& modelInfo = showRoot["model_info"];
        if (modelInfo.isObject()) {
          std::string arch = modelInfo["general.architecture"].asString();
          if (!arch.empty()) {
            std::string ctxKey = arch + ".context_length";
            const Json::Value& ctxVal = modelInfo[ctxKey];
            if (ctxVal.isNumeric() && ctxVal.asUInt64() > 0) {
              m_capabilities.context_length = static_cast<std::uint32_t>(ctxVal.asUInt64());
              std::cerr << "[ollama] Context length for '" << modelId
                        << "': " << m_capabilities.context_length << std::endl;
            }
          }
        }
      }
    } else {
      std::cerr << "[ollama] /api/show failed (HTTP " << showStatus
                << "): " << showError << std::endl;
    }

    // Vision capability detection: Ollama doesn't expose modality in /v1/models.
    // Check known vision model families by prefix/pattern.
    // This is conservative — models not in this list won't be blocked if the
    // user explicitly requests them, but supports_vision won't be auto-set.
    static const std::vector<std::string> kVisionPrefixes = {
        "llava", "gemma3", "gemma4",          // Google + open vision models
        "minicpm-v", "minicpm",                 // MiniCPM vision
        "qwen2.5-vl", "qwen2-vl", "qwen-vl",   // Qwen vision
        "moondream", "internvl",                 // Lightweight vision
        "llama3.2-vision", "llama3-vision",     // Meta vision
        "pixtral",                                // Mistral vision (via Ollama)
        "rho-mv",                                 // Other
    };
    std::string lowerId = modelId;
    std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(), ::tolower);
    for (const auto& prefix : kVisionPrefixes) {
      if (lowerId.rfind(prefix, 0) == 0) {
        m_capabilities.supports_vision = true;
        break;
      }
    }
    std::cerr << "[ollama] Vision "
              << (m_capabilities.supports_vision ? "detected" : "not detected")
              << " for '" << modelId << "'" << std::endl;
  } else {
    std::cerr << "[ollama] Model '" << modelId
              << "' not in local models list (may need 'ollama pull')" << std::endl;
    // Don't disable anything — the model might still work if it was pulled
    // after the listing, or Ollama might auto-pull. Log the warning only.
  }

  // Store all model IDs as raw features for UI display
  m_capabilities.raw_features.clear();
  for (Json::ArrayIndex i = 0; i < data.size(); ++i) {
    m_capabilities.raw_features.push_back(data[i]["id"].asString());
  }

  return found;
}

// ---------------------------------------------------------------------------
// GetVisionModelIds — check all local models for vision capability
// ---------------------------------------------------------------------------

std::vector<std::string> OllamaProvider::GetVisionModelIds() const {
  static const std::vector<std::string> kVisionPrefixes = {
      "llava", "gemma3", "gemma4",
      "minicpm-v", "minicpm",
      "qwen2.5-vl", "qwen2-vl", "qwen-vl",
      "moondream", "internvl",
      "llama3.2-vision", "llama3-vision",
      "pixtral",
      "rho-mv",
  };

  std::vector<std::string> result;
  for (const auto& model : m_capabilities.raw_features) {
    std::string lowerId = model;
    std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(), ::tolower);
    for (const auto& prefix : kVisionPrefixes) {
      if (lowerId.rfind(prefix, 0) == 0) {
        result.push_back(model);
        break;
      }
    }
  }
  return result;
}

} // namespace animus::kernel::llm

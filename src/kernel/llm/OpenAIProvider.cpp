#include "animus_kernel/llm/OpenAIProvider.h"
#include "animus_kernel/llm/OpenAICompat.h"

#include <algorithm>
#include <cctype>
#include <iostream>

using namespace animus::kernel::llm::openai_compat;

namespace animus::kernel::llm {

namespace {

// Check if a model name supports vision. OpenAI vision-capable families:
// GPT-4o, GPT-4o-mini, GPT-4.1, GPT-5.x, o1, o3, o4 (all multimodal).
bool IsVisionModel(const std::string& modelId) {
  std::string lower = modelId;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                  [](unsigned char c) { return std::tolower(c); });

  // GPT-4o family (includes mini)
  if (lower.rfind("gpt-4o", 0) == 0) return true;
  // GPT-4.1+
  if (lower.rfind("gpt-4.1", 0) == 0) return true;
  // GPT-5.x
  if (lower.rfind("gpt-5", 0) == 0) return true;
  // o-series reasoning models (o1, o3, o4 — all support vision)
  if (lower.rfind("o1", 0) == 0 || lower.rfind("o3", 0) == 0 ||
      lower.rfind("o4", 0) == 0) return true;
  return false;
}

} // namespace

std::string OpenAIProvider::BuildRequestBody(const LLMRequest& request) const {
  return BuildOpenAIRequestBody(request);
}

LLMMessage OpenAIProvider::ParseResponse(const std::string& body,
                                          std::string* error) const {
  auto choicesPos = body.find("\"choices\"");
  if (choicesPos == std::string::npos) {
    auto errMsg = ExtractJsonString(body, "message");
    if (error) *error = errMsg.empty() ? "No choices in response" : errMsg;
    return {};
  }

  LLMMessage msg;
  msg.role = ExtractJsonString(body.substr(choicesPos), "role");
  if (msg.role.empty()) msg.role = "assistant";
  msg.content = ExtractJsonString(body.substr(choicesPos), "content");
  return msg;
}

void OpenAIProvider::ParseToolCallsFromResponse(
    const std::string& body,
    std::vector<LLMToolCall>& tool_calls,
    std::string& finish_reason) const {
  tool_calls = ParseToolCalls(body);
  finish_reason = ExtractFinishReason(body);
}

std::optional<LLMToken> OpenAIProvider::ParseSSELine(
    const std::string& line) const {
  // Check for tool_calls finish reason
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

    // Parse tool calls from this SSE line (they arrive in the final delta)
    token.tool_calls = ParseToolCalls(line);
  }

  if (content.empty() && !isFinal) {
    auto ptStr = ExtractJsonNumber(line, "prompt_tokens");
    auto ctStr = ExtractJsonNumber(line, "completion_tokens");
    if (!ptStr.empty()) token.prompt_tokens = std::stoi(ptStr);
    if (!ctStr.empty()) token.completion_tokens = std::stoi(ctStr);
    if (ptStr.empty() && ctStr.empty()) return std::nullopt;
  }

  return token;
}

// ---------------------------------------------------------------------------
// FetchCapabilities — OpenAI model capability detection
// ---------------------------------------------------------------------------

bool OpenAIProvider::FetchCapabilities(const std::string& modelId) {
  // Start with base defaults + model list fetch (/v1/models)
  bool ok = LLMProviderBase::FetchCapabilities(modelId);

  // OpenAI vision detection from model name
  m_capabilities.supports_vision = IsVisionModel(modelId);

  // GPT-5.x and o-series support reasoning
  std::string lower = modelId;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                  [](unsigned char c) { return std::tolower(c); });
  m_capabilities.supports_reasoning =
      (lower.rfind("gpt-5", 0) == 0 ||
       lower.rfind("o1", 0) == 0 ||
       lower.rfind("o3", 0) == 0 ||
       lower.rfind("o4", 0) == 0);

  std::cerr << "[openai] Capabilities for " << modelId << ": vision="
            << m_capabilities.supports_vision
            << " reasoning=" << m_capabilities.supports_reasoning
            << std::endl;
  return ok;
}

} // namespace animus::kernel::llm
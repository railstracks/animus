#include "animus_kernel/llm/DeepSeekProvider.h"
#include "animus_kernel/llm/OpenAICompat.h"

#include <iostream>

using namespace animus::kernel::llm::openai_compat;

namespace animus::kernel::llm {

// ---------------------------------------------------------------------------
// BuildRequestBody — standard OpenAI + thinking parameter
// ---------------------------------------------------------------------------

std::string DeepSeekProvider::BuildRequestBody(const LLMRequest& request) const {
  std::string body = BuildOpenAIRequestBody(request);

  // DeepSeek thinking mode: "thinking": {"type": "enabled"}
  // Same format as Z.ai. Mapped from reasoning_effort config.
  // Unlike Alibaba, thinking tokens are billed at the standard output
  // rate — no premium multiplier. Safe to enable.
  if (IsReasoningEnabled(request.reasoning_effort)) {
    body.insert(body.size() - 1,
                ",\"thinking\":{\"type\":\"enabled\"}");
  }

  return body;
}

// ---------------------------------------------------------------------------
// ParseResponse / ParseSSELine — delegated to openai_compat shared helpers
// ---------------------------------------------------------------------------

LLMMessage DeepSeekProvider::ParseResponse(const std::string& body,
                                           std::string* error) const {
  return ParseResponseWithReasoning(body, error);
}

std::optional<LLMToken> DeepSeekProvider::ParseSSELine(
    const std::string& line) const {
  return ParseSSELineWithReasoning(line);
}

// ---------------------------------------------------------------------------
// ParseToolCallsFromResponse — non-streaming tool calls
// ---------------------------------------------------------------------------

void DeepSeekProvider::ParseToolCallsFromResponse(
    const std::string& body,
    std::vector<LLMToolCall>& tool_calls,
    std::string& finish_reason) const {
  tool_calls = ParseToolCalls(body);
  finish_reason = ExtractFinishReason(body);
}

// ---------------------------------------------------------------------------
// FetchCapabilities — DeepSeek V4 model capabilities
// ---------------------------------------------------------------------------

bool DeepSeekProvider::FetchCapabilities(const std::string& modelId) {
  bool ok = LLMProviderBase::FetchCapabilities(modelId);

  // Both DeepSeek V4 models support tool calling, streaming, and thinking.
  // Neither has vision capability (text-only as of July 2026).
  m_capabilities.supports_vision = false;
  m_capabilities.supports_reasoning = true;

  std::cerr << "[deepseek] Capabilities for " << modelId << ": vision="
            << m_capabilities.supports_vision
            << " reasoning=" << m_capabilities.supports_reasoning
            << std::endl;
  return ok;
}

} // namespace animus::kernel::llm
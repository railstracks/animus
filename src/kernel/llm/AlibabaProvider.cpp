#include "animus_kernel/llm/AlibabaProvider.h"
#include "animus_kernel/llm/OpenAICompat.h"

#include <algorithm>
#include <cctype>
#include <iostream>

using namespace animus::kernel::llm::openai_compat;

namespace animus::kernel::llm {

namespace {

// Qwen vision-capable models:
//   qwen3-vl-*       — dedicated vision-language models
//   qwen3.6-plus     — native multimodal
//   qwen3.6-flash    — native multimodal (cost-optimized)
//   qwen3.7-plus     — native multimodal
//   qwen3.7-max      — flagship (text-only as of July 2026, but check)
bool IsQwenVisionModel(const std::string& modelId) {
  std::string lower = modelId;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                  [](unsigned char c) { return std::tolower(c); });

  // Dedicated VL models
  if (lower.find("vl") != std::string::npos) return true;
  // Native multimodal models
  if (lower.rfind("qwen3.6-plus", 0) == 0) return true;
  if (lower.rfind("qwen3.6-flash", 0) == 0) return true;
  if (lower.rfind("qwen3.7-plus", 0) == 0) return true;
  return false;
}

// Qwen3+ models support thinking mode (enable_thinking parameter).
// This includes all qwen3-* models and the stable aliases (qwen-plus, etc.)
// since aliases now point to Qwen3 generation.
bool IsQwenThinkingModel(const std::string& modelId) {
  std::string lower = modelId;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                  [](unsigned char c) { return std::tolower(c); });

  // Qwen3 generation and later
  if (lower.rfind("qwen3", 0) == 0) return true;
  // Stable aliases point to Qwen3+ models
  if (lower == "qwen-plus" || lower == "qwen-max" ||
      lower == "qwen-turbo") return true;
  return false;
}

} // namespace

// ---------------------------------------------------------------------------
// BuildRequestBody — standard OpenAI format + optional thinking parameter
// ---------------------------------------------------------------------------

std::string AlibabaProvider::BuildRequestBody(const LLMRequest& request) const {
  std::string body = BuildOpenAIRequestBody(request);

  // Qwen thinking mode: "enable_thinking": true
  // Mapped from reasoning_effort — same semantic as Z.ai's "thinking" param.
  // Note: thinking tokens are billed at a higher output rate (3-10x).
  if (IsReasoningEnabled(request.reasoning_effort) &&
      IsQwenThinkingModel(request.model)) {
    body.insert(body.size() - 1,
                ",\"enable_thinking\":true");
  }

  return body;
}

// ---------------------------------------------------------------------------
// ParseResponse / ParseSSELine — delegated to openai_compat shared helpers
// ---------------------------------------------------------------------------

LLMMessage AlibabaProvider::ParseResponse(const std::string& body,
                                          std::string* error) const {
  return ParseResponseWithReasoning(body, error);
}

std::optional<LLMToken> AlibabaProvider::ParseSSELine(
    const std::string& line) const {
  return ParseSSELineWithReasoning(line);
}

// ---------------------------------------------------------------------------
// ParseToolCallsFromResponse — non-streaming tool calls
// ---------------------------------------------------------------------------

void AlibabaProvider::ParseToolCallsFromResponse(
    const std::string& body,
    std::vector<LLMToolCall>& tool_calls,
    std::string& finish_reason) const {
  tool_calls = ParseToolCalls(body);
  finish_reason = ExtractFinishReason(body);
}

// ---------------------------------------------------------------------------
// FetchCapabilities — Qwen model capability detection
// ---------------------------------------------------------------------------

bool AlibabaProvider::FetchCapabilities(const std::string& modelId) {
  bool ok = LLMProviderBase::FetchCapabilities(modelId);

  m_capabilities.supports_vision = IsQwenVisionModel(modelId);
  m_capabilities.supports_reasoning = IsQwenThinkingModel(modelId);

  std::cerr << "[alibaba] Capabilities for " << modelId << ": vision="
            << m_capabilities.supports_vision
            << " reasoning=" << m_capabilities.supports_reasoning
            << std::endl;
  return ok;
}

} // namespace animus::kernel::llm

#pragma once

#include "animus_kernel/llm/LLMProviderBase.h"

namespace animus::kernel::llm {

// ============================================================================
// DeepSeekProvider — DeepSeek V4 via OpenAI-compatible endpoint
// ============================================================================
//
// DeepSeek exposes an OpenAI-compatible API at:
//   https://api.deepseek.com/v1
//
// The /chat/completions endpoint accepts standard OpenAI request format.
// Tool calling, streaming, and JSON output all work out of the box.
//
// Both DeepSeek V4 models support "thinking mode" via the thinking
// parameter: {"type": "enabled"}. This is the same format as Z.ai.
// Thinking content is returned in the reasoning_content field of SSE
// deltas — same as Z.ai and Alibaba.
//
// Unlike Alibaba, DeepSeek bills thinking tokens at the standard output
// rate (no 3-10x multiplier). This makes thinking mode safe to enable
// by default.
//
// Models (July 2026):
//   deepseek-v4-flash  — cheapest serious LLM ($0.14/$0.28 per 1M)
//   deepseek-v4-pro    — flagship ($0.435/$0.87 per 1M)
//
// Note: deepseek-chat and deepseek-reasoner deprecated 2026-07-24.
// Use deepseek-v4-flash with thinking mode toggle instead.

class DeepSeekProvider : public LLMProviderBase {
public:
  explicit DeepSeekProvider(const LLMProviderConfig& config)
      : LLMProviderBase(config) {}

  std::string ProviderId() const override { return "deepseek"; }

  bool FetchCapabilities(const std::string& modelId) override;

protected:
  std::string BuildRequestBody(const LLMRequest& request) const override;
  LLMMessage ParseResponse(const std::string& body,
                           std::string* error) const override;
  std::optional<LLMToken> ParseSSELine(
      const std::string& line) const override;
  void ParseToolCallsFromResponse(
      const std::string& body,
      std::vector<LLMToolCall>& tool_calls,
      std::string& finish_reason) const override;
};

} // namespace animus::kernel::llm
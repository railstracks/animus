#pragma once

#include "animus_kernel/llm/LLMProviderBase.h"

namespace animus::kernel::llm {

// ============================================================================
// AlibabaProvider — DashScope (Qwen) via OpenAI-compatible endpoint
// ============================================================================
//
// Alibaba Cloud Model Studio provides an OpenAI-compatible interface at:
//   International: https://dashscope-intl.aliyuncs.com/compatible-mode/v1
//   China:         https://dashscope.aliyuncs.com/compatible-mode/v1
//
// The /chat/completions endpoint accepts standard OpenAI request format.
// Tool calling, streaming, and vision (content-array) all work out of the box.
//
// Qwen3+ models support "thinking mode" via enable_thinking parameter.
// This is mapped from the reasoning_effort config, same as Z.ai's thinking.
//
// Model aliases (qwen-plus, qwen-max, qwen-turbo) are stable production
// pointers that always route to the current best model in their tier.

class AlibabaProvider : public LLMProviderBase {
public:
  explicit AlibabaProvider(const LLMProviderConfig& config)
      : LLMProviderBase(config) {}

  std::string ProviderId() const override { return "alibaba"; }

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

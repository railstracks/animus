#pragma once

#include "animus_kernel/llm/LLMProviderBase.h"

namespace animus::kernel::llm {

// ============================================================================
// OpenAIProvider — standard /v1/chat/completions with API key auth
// ============================================================================

class OpenAIProvider : public LLMProviderBase {
public:
  explicit OpenAIProvider(const LLMProviderConfig& config)
      : LLMProviderBase(config) {}

  std::string ProviderId() const override { return "openai"; }

  bool FetchCapabilities(const std::string& modelId) override;

protected:
  std::string BuildRequestBody(const LLMRequest& request) const override;
  LLMMessage ParseResponse(const std::string& body,
                           std::string* error) const override;
  std::optional<LLMToken> ParseSSELine(
      const std::string& line) const override;

  /// Parse tool calls from the response. Called by ParseResponse.
  /// Returns tool calls and finish_reason.
  void ParseToolCallsFromResponse(
      const std::string& body,
      std::vector<LLMToolCall>& tool_calls,
      std::string& finish_reason) const override;
};

} // namespace animus::kernel::llm
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

protected:
  std::string BuildRequestBody(const LLMRequest& request) const override;
  LLMMessage ParseResponse(const std::string& body,
                           std::string* error) const override;
  std::optional<LLMToken> ParseSSELine(
      const std::string& line) const override;
};

} // namespace animus::kernel::llm

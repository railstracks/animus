#pragma once

#include "animus_kernel/llm/LLMProviderBase.h"

namespace animus::kernel::llm {

class OllamaProvider : public LLMProviderBase {
public:
  explicit OllamaProvider(const LLMProviderConfig& config)
      : LLMProviderBase(config) {}

  std::string ProviderId() const override { return "ollama"; }

protected:
  std::string BuildRequestBody(const LLMRequest& request) const override;
  LLMMessage ParseResponse(const std::string& body,
                           std::string* error) const override;
  std::optional<LLMToken> ParseSSELine(
      const std::string& line) const override;
  std::vector<std::pair<std::string, std::string>> GetHeaders() const override;
};

} // namespace animus::kernel::llm

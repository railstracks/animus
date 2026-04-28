#pragma once

#include "animus_kernel/llm/LLMProviderBase.h"

namespace animus::kernel::llm {

class CohereProvider : public LLMProviderBase {
public:
  explicit CohereProvider(const LLMProviderConfig& config)
      : LLMProviderBase(config) {}

  std::string ProviderId() const override { return "cohere"; }

protected:
  std::string GetEndpointURL() const override;
  std::string BuildRequestBody(const LLMRequest& request) const override;
  LLMMessage ParseResponse(const std::string& body,
                           std::string* error) const override;
  std::optional<LLMToken> ParseSSELine(
      const std::string& line) const override;
  bool IsSSEDone(const std::string& line) const override;
};

} // namespace animus::kernel::llm

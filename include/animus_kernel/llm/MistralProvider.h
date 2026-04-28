#pragma once

#include "animus_kernel/llm/OpenAIProvider.h"

namespace animus::kernel::llm {

// Mistral uses the same protocol as OpenAI. Inherits from OpenAIProvider
// for shared BuildRequestBody/ParseResponse/ParseSSELine logic.
// Separate class for future FIM/OCR endpoint extensions.
class MistralProvider : public OpenAIProvider {
public:
  explicit MistralProvider(const LLMProviderConfig& config)
      : OpenAIProvider(config) {}

  std::string ProviderId() const override { return "mistral"; }
};

} // namespace animus::kernel::llm

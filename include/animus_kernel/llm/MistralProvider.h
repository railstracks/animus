#pragma once

#include "animus_kernel/llm/OpenAIProvider.h"

#include <algorithm>
#include <cctype>

namespace animus::kernel::llm {

// Mistral uses the same protocol as OpenAI. Inherits from OpenAIProvider
// for shared BuildRequestBody/ParseResponse/ParseSSELine logic.
// Separate class for future FIM/OCR endpoint extensions.
class MistralProvider : public OpenAIProvider {
public:
  explicit MistralProvider(const LLMProviderConfig& config)
      : OpenAIProvider(config) {}

  std::string ProviderId() const override { return "mistral"; }

  bool FetchCapabilities(const std::string& modelId) override {
    bool ok = OpenAIProvider::FetchCapabilities(modelId);

    // Mistral vision-capable families: Pixtral, Large 3, Medium 3.1+, Small 3.2+
    std::string lower = modelId;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) { return std::tolower(c); });

    bool vision = false;
    if (lower.rfind("pixtral", 0) == 0) vision = true;
    if (lower.rfind("mistral-large", 0) == 0) vision = true;
    if (lower.rfind("mistral-medium", 0) == 0) vision = true;
    if (lower.rfind("mistral-small", 0) == 0) vision = true;
    if (lower.rfind("ministral", 0) == 0) vision = true;

    m_capabilities.supports_vision = vision;
    // Mistral doesn't use the reasoning_effort parameter
    m_capabilities.supports_reasoning = false;
    return ok;
  }
};

} // namespace animus::kernel::llm

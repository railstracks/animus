#pragma once

#include "animus_kernel/llm/ZaiProvider.h"

namespace animus::kernel::llm {

// ============================================================================
// ZaiCodingProvider — GLM Coding Plan subscription endpoint
// ============================================================================
// Same API shape as the general Z.ai provider, but uses the dedicated
// Coding Plan endpoint: https://api.z.ai/api/coding/paas/v4
//
// Image generation is NOT available through this provider — the Coding Plan
// covers chat completions only. Use the general `zai` provider for image gen.
// ============================================================================

class ZaiCodingProvider : public ZaiProvider {
public:
  explicit ZaiCodingProvider(const LLMProviderConfig& config)
      : ZaiProvider(config) {}

  std::string ProviderId() const override { return "zai-coder"; }
};

} // namespace animus::kernel::llm

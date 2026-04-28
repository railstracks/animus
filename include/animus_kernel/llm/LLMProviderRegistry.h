#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "animus_kernel/llm/ILLMProvider.h"
#include "animus_kernel/llm/LLMProviderConfig.h"

namespace animus::kernel::llm {

// ============================================================================
// LLMProviderRegistry — factory for creating provider instances
// ============================================================================

class LLMProviderRegistry {
public:
  using Factory =
      std::function<std::unique_ptr<ILLMProvider>(const LLMProviderConfig&)>;

  /// Register a factory function for a provider id.
  void Register(const std::string& id, Factory factory);

  /// Create a provider instance from the given config.
  /// Returns nullptr if the provider_id has no registered factory.
  std::unique_ptr<ILLMProvider> Create(
      const LLMProviderConfig& config) const;

  /// List all registered provider ids.
  std::vector<std::string> Available() const;

  /// Check if a provider id is registered.
  bool Has(const std::string& id) const;

private:
  std::unordered_map<std::string, Factory> m_factories;
};

} // namespace animus::kernel::llm

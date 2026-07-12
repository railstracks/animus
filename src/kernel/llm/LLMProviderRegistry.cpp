#include "animus_kernel/llm/LLMProviderRegistry.h"

namespace animus::kernel::llm {

void LLMProviderRegistry::Register(const std::string& id, Factory factory) {
  m_factories[id] = std::move(factory);
}

std::unique_ptr<ILLMProvider> LLMProviderRegistry::Create(
    const LLMProviderConfig& config) const {
  auto it = m_factories.find(config.provider_id);
  if (it == m_factories.end()) {
    return nullptr;
  }
  return it->second(config);
}

std::vector<std::string> LLMProviderRegistry::Available() const {
  std::vector<std::string> ids;
  ids.reserve(m_factories.size());
  for (const auto& [id, _] : m_factories) {
    ids.push_back(id);
  }
  return ids;
}

bool LLMProviderRegistry::Has(const std::string& id) const {
  return m_factories.count(id) > 0;
}

} // namespace animus::kernel::llm

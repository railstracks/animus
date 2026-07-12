#include "animus_kernel/tools/ToolRegistry.h"

#include <algorithm>

namespace animus::kernel {

void ToolRegistry::Register(std::unique_ptr<IToolHandler> handler) {
    if (!handler) return;
    std::string name = handler->GetDefinition().name;
    if (name.empty()) return;
    m_order.push_back(name);
    m_handlers[name] = std::move(handler);
}

IToolHandler* ToolRegistry::Find(const std::string& name) const {
    auto it = m_handlers.find(name);
    return it != m_handlers.end() ? it->second.get() : nullptr;
}

std::vector<ToolDefinition> ToolRegistry::GetAllDefinitions() const {
    std::vector<ToolDefinition> defs;
    defs.reserve(m_order.size());
    for (const auto& name : m_order) {
        auto it = m_handlers.find(name);
        if (it == m_handlers.end()) continue;
        // Skip disabled tools
        auto dit = m_disabled.find(name);
        if (dit != m_disabled.end() && dit->second) continue;
        defs.push_back(it->second->GetDefinition());
    }
    return defs;
}

std::vector<ToolDefinition> ToolRegistry::GetAllDefinitionsIncludingDisabled() const {
    std::vector<ToolDefinition> defs;
    defs.reserve(m_order.size());
    for (const auto& name : m_order) {
        auto it = m_handlers.find(name);
        if (it != m_handlers.end()) {
            defs.push_back(it->second->GetDefinition());
        }
    }
    return defs;
}

bool ToolRegistry::Has(const std::string& name) const {
    return m_handlers.find(name) != m_handlers.end();
}

bool ToolRegistry::Unregister(const std::string& name) {
    auto it = m_handlers.find(name);
    if (it == m_handlers.end()) {
        return false;
    }
    m_handlers.erase(it);
    m_order.erase(std::remove(m_order.begin(), m_order.end(), name), m_order.end());
    m_disabled.erase(name);
    return true;
}

std::size_t ToolRegistry::EnabledCount() const {
    std::size_t count = 0;
    for (const auto& name : m_order) {
        auto dit = m_disabled.find(name);
        if (dit == m_disabled.end() || !dit->second) count++;
    }
    return count;
}

bool ToolRegistry::SetEnabled(const std::string& name, bool enabled) {
    if (!Has(name)) return false;
    m_disabled[name] = !enabled;
    return true;
}

bool ToolRegistry::IsEnabled(const std::string& name) const {
    auto dit = m_disabled.find(name);
    return (dit == m_disabled.end() || !dit->second);
}

} // namespace animus::kernel
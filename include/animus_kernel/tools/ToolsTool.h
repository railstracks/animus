#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/AgentStore.h"

namespace Json { class Value; }

namespace animus::kernel {

// ============================================================================
// ToolsTool — agent self-inspection of available tools
//
// Actions: list, info, enable, disable, enabled
// Ticket 099.
// ============================================================================

class ToolsTool : public IToolHandler {
public:
    explicit ToolsTool(ToolRegistry& registry, AgentStore* agentStore = nullptr);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    ToolRegistry& m_registry;
    AgentStore* m_agentStore;

    // Returns the enabled_tools whitelist for the calling agent, or empty
    // vector if all tools are enabled (no whitelist configured).
    std::vector<std::string> GetAgentToolWhitelist(const Json::Value& args) const;
};

} // namespace animus::kernel
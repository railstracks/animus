#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/tools/ToolTypes.h"
#include "animus_kernel/AgentStore.h"

#include <json/json.h>

namespace animus::kernel {

// ============================================================================
// AgentTool — agent self-management tool
//
// Actions:
//   view   — returns the agent's own settings
//   update — updates avatar, description, and/or identity
//
// Agent ID is always from __agent_id (ChainRunner-injected).
// An agent can only view and modify its own configuration.
//
// Identity edits are gated by allowSelfIdentityEdit config flag.
// ============================================================================

class AgentTool : public IToolHandler {
public:
    AgentTool(AgentStore* agentStore, bool allowIdentityEdit);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    ToolResult HandleView(const std::string& agentId);
    ToolResult HandleUpdate(const std::string& agentId, const Json::Value& args);

    AgentStore* m_agentStore;
    bool m_allowIdentityEdit;
};

} // namespace animus::kernel

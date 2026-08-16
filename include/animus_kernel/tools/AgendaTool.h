#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/AgendaStore.h"

namespace animus::kernel {

// ============================================================================
// AgendaTool — agent-facing CRUD for calendar/agenda events
//
// Actions: create, list, update, delete, complete, view
// Events are private to the agent (scoped by agent_id).
// ============================================================================

class AgendaTool : public IToolHandler {
public:
    explicit AgendaTool(AgendaStore* store);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    AgendaStore* m_store;

    ToolResult DoCreate(const Json::Value& args, const std::string& agentId);
    ToolResult DoList(const Json::Value& args, const std::string& agentId);
    ToolResult DoUpdate(const Json::Value& args, const std::string& agentId);
    ToolResult DoDelete(const Json::Value& args, const std::string& agentId);
    ToolResult DoComplete(const Json::Value& args, const std::string& agentId);
    ToolResult DoView(const Json::Value& args, const std::string& agentId);

    std::string EventToJson(const AgendaEvent& ev) const;
};

} // namespace animus::kernel
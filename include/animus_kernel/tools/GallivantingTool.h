#pragma once

#include "animus_kernel/tools/ToolRegistry.h"

namespace animus::kernel {

class GallivantingStore;

// ============================================================================
// GallivantingTool — agent-facing composite gallivanting management tool
//
// Actions: list, read, create, update, delete, record, history
//
// All operations scoped by agent_id (injected via __agent_id by ChainRunner).
// ============================================================================

class GallivantingTool : public IToolHandler {
public:
    explicit GallivantingTool(GallivantingStore* store);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    ToolResult HandleList(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleRead(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleCreate(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleUpdate(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleDelete(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleRecord(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleHistory(const std::string& agentId, const std::string& rawArgs);

    GallivantingStore* m_store;
};

} // namespace animus::kernel

#pragma once

#include "animus_kernel/tools/ToolRegistry.h"

#include <string>

namespace animus::kernel {

class NodeManager;

// ============================================================================
// NodeTool — agent-facing tool for discovering external nodes
//
// Actions: list, info
//
// list — list connected nodes available to this agent
// info — show details for a specific node (tools, hostname, OS, uptime)
//
// Actual tool execution on nodes happens via the __node parameter in
// regular tool calls, NOT through this tool. This tool is discovery only.
// ============================================================================

class NodeTool : public IToolHandler {
public:
    explicit NodeTool(NodeManager* nodeManager);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    ToolResult HandleList(const std::string& agentId);
    ToolResult HandleInfo(const std::string& agentId, const std::string& rawArgs);

    NodeManager* m_nodeManager;
};

} // namespace animus::kernel

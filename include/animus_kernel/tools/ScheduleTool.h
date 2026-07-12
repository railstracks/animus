#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/scheduler/Scheduler.h"

namespace animus::kernel {

// ============================================================================
// ScheduleTool — agent-facing 'schedule' tool
// ============================================================================

class ScheduleTool : public IToolHandler {
public:
    explicit ScheduleTool(Scheduler* scheduler);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    ToolResult HandleSet(const std::string& callId,
                          const std::string& agentId,
                          const std::string& argsJson);
    ToolResult HandleList(const std::string& callId,
                           const std::string& agentId,
                           const std::string& argsJson);
    ToolResult HandleCancel(const std::string& callId,
                             const std::string& agentId,
                             const std::string& argsJson);

    Scheduler* m_scheduler;
};

} // namespace animus::kernel

#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/admin/DiaryManager.h"

namespace animus::kernel {

// ============================================================================
// DiaryTool — agent-facing diary tool
//
// Operations: write, read, search, delete
// The diary is agent-private. This tool is scoped to the calling agent.
// ============================================================================

class DiaryTool : public IToolHandler {
public:
    explicit DiaryTool(DiaryManager* diaryManager);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    DiaryManager::OperationResult HandleList(const std::string& agentId, const Json::Value& args);
    DiaryManager::OperationResult HandleWrite(const std::string& agentId, const Json::Value& args);
    DiaryManager::OperationResult HandleRead(const std::string& agentId, const Json::Value& args);
    DiaryManager::OperationResult HandleSearch(const std::string& agentId, const Json::Value& args);
    DiaryManager::OperationResult HandleDelete(const std::string& agentId, const Json::Value& args);

    DiaryManager* m_diaryManager;
};

} // namespace animus::kernel
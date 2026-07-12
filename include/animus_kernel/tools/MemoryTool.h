#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/MemoryFileStore.h"

namespace animus::kernel {

namespace memory { class MemorySearch; class MemoryStore; }
class SessionManager;
class AgentStore;

// ============================================================================
// MemoryTool — agent-facing composite memory tool
//
// Actions: search, recall, pin, inspect, file_list, file_read, file_write, file_delete
//
// search      — FTS5 across all memory domains (episodic, semantic, files, diary, sessions)
// recall      — weighted retrieval from episodic memory layers
// pin         — annotate an observation for consolidation attention
// inspect     — view memory layer state and perspectives
// file_list   — list memory files for this agent
// file_read   — read a memory file's content
// file_write  — create or update a memory file
// file_delete — delete a memory file (owner only)
//
// All operations are scoped by agent_id (injected via __agent_id by ChainRunner).
// ============================================================================

class MemoryTool : public IToolHandler {
public:
    MemoryTool(memory::MemorySearch* search,
               memory::MemoryStore* store,
               SessionManager* sessions,
               memory::MemoryFileStore* fileStore = nullptr,
               AgentStore* agentStore = nullptr);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    ToolResult HandleSearch(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleRecall(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandlePin(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleInspect(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleFileList(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleFileBrowse(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleFileRead(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleFileSearch(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleFileHeadings(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleFileWrite(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleFileDelete(const std::string& agentId, const std::string& rawArgs);

    int64_t AgentIdToNumeric(const std::string& agentId) const;

    memory::MemorySearch* m_search;
    memory::MemoryStore* m_store;
    SessionManager* m_sessions;
    memory::MemoryFileStore* m_fileStore;
    AgentStore* m_agentStore;
};

} // namespace animus::kernel

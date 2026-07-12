#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/SessionManager.h"
#include "animus_kernel/SessionNotesStore.h"
#include "animus_kernel/SessionTagsStore.h"

namespace animus::kernel {

class CompactionService;

// ============================================================================
// SessionsTool — agent-facing session layer access
//
// Actions: list, search, history, reply, notes:list/add/update/remove/reorder,
//          compactions, compactions:latest, compact
// All operations are scoped to the calling agent.
// ============================================================================

class SessionsTool : public IToolHandler {
public:
    SessionsTool(SessionManager* sessions,
                 SessionNotesStore* notesStore,
                 SessionTagsStore* tagsStore,
                 CompactionService* compactionService = nullptr);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    // Parse __session_key from tool call arguments (injected by chain runner).
    std::string GetCurrentSessionKey(const std::string& rawArgs) const;

    ToolResult HandleList(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleSearch(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleHistory(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleReply(const std::string& agentId, const std::string& rawArgs);

    // Notes sub-actions
    ToolResult HandleNotesList(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleNotesAdd(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleNotesUpdate(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleNotesRemove(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleNotesReorder(const std::string& agentId, const std::string& rawArgs);

    // Tags sub-actions
    ToolResult HandleTagsList(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleTagsSet(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleTagsRemove(const std::string& agentId, const std::string& rawArgs);

    // Compaction sub-actions
    ToolResult HandleCompactions(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleCompactionsLatest(const std::string& agentId, const std::string& rawArgs);
    ToolResult HandleCompact(const std::string& agentId, const std::string& rawArgs);

    // Resolve a session by its string key (conversation ID).
    std::shared_ptr<Session> ResolveSession(const std::string& sessionKey) const;

    SessionManager* m_sessions;
    SessionNotesStore* m_notesStore;
    SessionTagsStore* m_tagsStore;
    CompactionService* m_compactionService;
};

} // namespace animus::kernel
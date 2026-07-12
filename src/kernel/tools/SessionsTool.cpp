#include "animus_kernel/tools/SessionsTool.h"
#include "animus_kernel/CompactionService.h"

#include <algorithm>
#include <chrono>
#include <json/json.h>
#include <json/writer.h>
#include <sstream>

namespace animus::kernel {

namespace {

Json::Value ParseArgs(const std::string& args) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(args);
    std::string errors;
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        return {};
    }
    return root;
}

std::string GetStringField(const Json::Value& args, const std::string& key,
                             const std::string& defaultVal = "") {
    if (args.isMember(key) && args[key].isString()) {
        return args[key].asString();
    }
    return defaultVal;
}

int GetIntField(const Json::Value& args, const std::string& key, int defaultVal = 0) {
    if (args.isMember(key) && args[key].isInt()) {
        return args[key].asInt();
    }
    return defaultVal;
}

std::string ResultToJsonString(const Json::Value& body) {
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, body);
}

std::string ErrorJson(const std::string& error) {
    Json::Value root(Json::objectValue);
    root["error"] = error;
    return ResultToJsonString(root);
}

std::vector<int64_t> GetInt64Array(const Json::Value& args, const std::string& key) {
    std::vector<int64_t> result;
    if (args.isMember(key) && args[key].isArray()) {
        for (const auto& item : args[key]) {
            if (item.isInt64() || item.isInt()) {
                result.push_back(item.asInt64());
            }
        }
    }
    return result;
}

std::string Truncate(const std::string& s, size_t maxLen) {
    if (s.size() <= maxLen) return s;
    return s.substr(0, maxLen) + "...";
}

} // anonymous namespace

// ============================================================================
// Construction
// ============================================================================

SessionsTool::SessionsTool(SessionManager* sessions,
                             SessionNotesStore* notesStore,
                             SessionTagsStore* tagsStore,
                             CompactionService* compactionService)
    : m_sessions(sessions)
    , m_notesStore(notesStore)
    , m_tagsStore(tagsStore)
    , m_compactionService(compactionService) {}

// ============================================================================
// Tool definition
// ============================================================================

ToolDefinition SessionsTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "sessions";
    def.description =
        "Access your session layer — list conversations, search history, "
        "reply to other sessions, and maintain working notes for the current "
        "session. Session notes are persistent bullet points that survive "
        "across turns — use them for reminders, decisions, and context that "
        "matters for this specific conversation.";
    def.resultMode = ToolResultMode::deliver_to_model;

    def.parameters.push_back({
        "action", "string",
        "Session operation to perform",
        true, "", {
            "list", "search", "history", "reply",
            "notes:list", "notes:add", "notes:update",
            "notes:remove", "notes:reorder",
            "tags:list", "tags:set", "tags:remove",
            "compactions", "compactions:latest", "compact"
        }
    });

    // list parameters
    def.parameters.push_back({
        "status", "string",
        "Filter sessions: active, recent, all (default: active)",
        false, "", {"active", "recent", "all"}
    });

    // search parameters
    def.parameters.push_back({
        "query", "string",
        "Search query for session history (required for search)",
        false
    });
    def.parameters.push_back({
        "limit", "integer",
        "Max search results (default 10, max 50)",
        false
    });

    // history parameters
    def.parameters.push_back({
        "session_key", "string",
        "Session key to retrieve history from (required for history)",
        false
    });
    def.parameters.push_back({
        "limit", "integer",
        "Max messages to return (default 10, max 50)",
        false
    });

    // reply parameters
    def.parameters.push_back({
        "message", "string",
        "Message to send (required for reply)",
        false
    });

    // notes:add parameters
    def.parameters.push_back({
        "bullet", "string",
        "Note text (required for notes:add)",
        false
    });
    def.parameters.push_back({
        "position", "string",
        "Where to add the note: top or bottom (default: bottom)",
        false, "", {"top", "bottom"}
    });

    // notes:update parameters
    def.parameters.push_back({
        "note_id", "integer",
        "ID of the note to update (required for notes:update, notes:remove)",
        false
    });

    // notes:reorder parameters
    def.parameters.push_back({
        "note_ids", "array",
        "Ordered array of note IDs for notes:reorder",
        false
    });

    // tags:set parameters
    def.parameters.push_back({
        "tag", "string",
        "Tag to set or remove (required for tags:set, tags:remove)",
        false
    });

    return def;
}

// ============================================================================
// Execute
// ============================================================================

ToolResult SessionsTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    if (args.isNull()) {
        result.success = false;
        result.error = "failed to parse arguments";
        return result;
    }

    std::string agentId = GetStringField(args, "__agent_id", "");
    std::string action = GetStringField(args, "action", "");

    if (action.empty()) {
        result.success = false;
        result.error = "action is required";
        return result;
    }

    if (action == "list") {
        return HandleList(agentId, call.arguments);
    } else if (action == "search") {
        return HandleSearch(agentId, call.arguments);
    } else if (action == "history") {
        return HandleHistory(agentId, call.arguments);
    } else if (action == "reply") {
        return HandleReply(agentId, call.arguments);
    } else if (action == "notes:list") {
        return HandleNotesList(agentId, call.arguments);
    } else if (action == "notes:add") {
        return HandleNotesAdd(agentId, call.arguments);
    } else if (action == "notes:update") {
        return HandleNotesUpdate(agentId, call.arguments);
    } else if (action == "notes:remove") {
        return HandleNotesRemove(agentId, call.arguments);
    } else if (action == "notes:reorder") {
        return HandleNotesReorder(agentId, call.arguments);
    } else if (action == "tags:list") {
        return HandleTagsList(agentId, call.arguments);
    } else if (action == "tags:set") {
        return HandleTagsSet(agentId, call.arguments);
    } else if (action == "tags:remove") {
        return HandleTagsRemove(agentId, call.arguments);
    } else if (action == "compactions") {
        return HandleCompactions(agentId, call.arguments);
    } else if (action == "compactions:latest") {
        return HandleCompactionsLatest(agentId, call.arguments);
    } else if (action == "compact") {
        return HandleCompact(agentId, call.arguments);
    } else {
        result.success = false;
        result.error = "unknown action: " + action;
        return result;
    }
}

std::string SessionsTool::GetCurrentSessionKey(const std::string& rawArgs) const {
    auto args = ParseArgs(rawArgs);
    return GetStringField(args, "__session_key", "");
}

std::shared_ptr<Session> SessionsTool::ResolveSession(const std::string& sessionKey) const {
    if (!m_sessions || sessionKey.empty()) return nullptr;
    for (const auto& s : m_sessions->ListSessions()) {
        if (s && s->Key().ToString() == sessionKey) {
            return s;
        }
    }
    return nullptr;
}

// ============================================================================
// list — show active sessions
// ============================================================================

ToolResult SessionsTool::HandleList(const std::string& agentId,
                                     const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    if (!m_sessions) {
        result.success = false;
        result.error = "session manager not available";
        return result;
    }

    auto args = ParseArgs(rawArgs);
    std::string status = GetStringField(args, "status", "active");

    auto allSessions = m_sessions->ListSessions();

    Json::Value items(Json::arrayValue);
    for (const auto& session : allSessions) {
        if (!session) continue;
        if (session->AgentId() != agentId) continue;

        // Filter by status
        if (status == "active" && session->IsTerminated()) continue;
        // "recent" and "all" pass through

        Json::Value item(Json::objectValue);
        item["session_key"] = session->Key().ToString();
        item["connector"] = session->Key().connector;
        item["turn_count"] = static_cast<Json::Int>(session->Turns().size());
        item["last_active_unix_ms"] = static_cast<Json::Int64>(session->LastActiveUnixMs());
        item["terminated"] = session->IsTerminated();

        // Last message preview
        const auto& turns = session->Turns();
        if (!turns.empty()) {
            std::string preview;
            for (auto it = turns.rbegin(); it != turns.rend(); ++it) {
                if (it->role == "user" || it->role == "assistant") {
                    preview = Truncate(it->content, 120);
                    break;
                }
            }
            item["last_message_preview"] = preview;
        }

        items.append(item);
    }

    Json::Value body(Json::objectValue);
    body["sessions"] = items;
    body["count"] = static_cast<Json::Int>(items.size());

    result.success = true;
    result.output = ResultToJsonString(body);
    return result;
}

// ============================================================================
// search — search across session turns
// ============================================================================

ToolResult SessionsTool::HandleSearch(const std::string& agentId,
                                       const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    std::string query = GetStringField(args, "query", "");
    if (query.empty()) {
        result.success = false;
        result.error = "query is required for search";
        return result;
    }

    int limit = GetIntField(args, "limit", 10);
    if (limit < 1) limit = 10;
    if (limit > 50) limit = 50;

    if (!m_sessions) {
        result.success = false;
        result.error = "session manager not available";
        return result;
    }

    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

    auto allSessions = m_sessions->ListSessions();
    Json::Value items(Json::arrayValue);
    int emitted = 0;

    for (const auto& session : allSessions) {
        if (!session) continue;
        if (session->AgentId() != agentId) continue;
        if (emitted >= limit) break;

        for (const auto& turn : session->Turns()) {
            if (emitted >= limit) break;
            if (turn.content.empty()) continue;

            std::string lowerContent = turn.content;
            std::transform(lowerContent.begin(), lowerContent.end(),
                           lowerContent.begin(), ::tolower);

            if (lowerContent.find(lowerQuery) != std::string::npos) {
                Json::Value item(Json::objectValue);
                item["session_key"] = session->Key().ToString();
                item["role"] = turn.role;
                item["content"] = Truncate(turn.content, 280);
                item["unix_ms"] = static_cast<Json::Int64>(turn.unix_ms);
                items.append(item);
                emitted++;
            }
        }
    }

    Json::Value body(Json::objectValue);
    body["results"] = items;
    body["count"] = static_cast<Json::Int>(items.size());

    result.success = true;
    result.output = ResultToJsonString(body);
    return result;
}

// ============================================================================
// history — retrieve messages from a specific session
// ============================================================================

ToolResult SessionsTool::HandleHistory(const std::string& agentId,
                                        const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    std::string sessionKey = GetStringField(args, "session_key", "");
    int limit = GetIntField(args, "limit", 10);
    if (limit < 1) limit = 10;
    if (limit > 50) limit = 50;

    if (sessionKey.empty()) {
        result.success = false;
        result.error = "session_key is required for history";
        return result;
    }

    if (!m_sessions) {
        result.success = false;
        result.error = "session manager not available";
        return result;
    }

    // Find the session
    auto allSessions = m_sessions->ListSessions();
    std::shared_ptr<Session> target;
    for (const auto& s : allSessions) {
        if (s && s->Key().ToString() == sessionKey) {
            target = s;
            break;
        }
    }

    if (!target) {
        result.success = false;
        result.error = "session not found: " + sessionKey;
        return result;
    }

    if (target->AgentId() != agentId) {
        result.success = false;
        result.error = "session does not belong to this agent";
        return result;
    }

    const auto& turns = target->Turns();
    Json::Value items(Json::arrayValue);

    // Take the last N turns
    int start = std::max(0, static_cast<int>(turns.size()) - limit);
    for (int i = start; i < static_cast<int>(turns.size()); ++i) {
        const auto& turn = turns[i];
        if (turn.role == "tool") continue; // skip tool results in history view

        Json::Value item(Json::objectValue);
        item["role"] = turn.role;
        item["content"] = Truncate(turn.content, 500);
        item["unix_ms"] = static_cast<Json::Int64>(turn.unix_ms);
        items.append(item);
    }

    Json::Value body(Json::objectValue);
    body["session_key"] = sessionKey;
    body["messages"] = items;
    body["count"] = static_cast<Json::Int>(items.size());

    result.success = true;
    result.output = ResultToJsonString(body);
    return result;
}

// ============================================================================
// reply — send a message to another session
// ============================================================================

ToolResult SessionsTool::HandleReply(const std::string& agentId,
                                      const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    std::string sessionKey = GetStringField(args, "session_key", "");
    std::string message = GetStringField(args, "message", "");

    if (sessionKey.empty()) {
        result.success = false;
        result.error = "session_key is required for reply";
        return result;
    }
    if (message.empty()) {
        result.success = false;
        result.error = "message is required for reply";
        return result;
    }

    if (!m_sessions) {
        result.success = false;
        result.error = "session manager not available";
        return result;
    }

    // Find the target session
    auto allSessions = m_sessions->ListSessions();
    std::shared_ptr<Session> target;
    for (const auto& s : allSessions) {
        if (s && s->Key().ToString() == sessionKey) {
            target = s;
            break;
        }
    }

    if (!target) {
        result.success = false;
        result.error = "session not found: " + sessionKey;
        return result;
    }

    if (target->AgentId() != agentId) {
        result.success = false;
        result.error = "session does not belong to this agent";
        return result;
    }

    if (target->IsTerminated()) {
        result.success = false;
        result.error = "session is terminated";
        return result;
    }

    // Add assistant turn to the target session
    SessionTurn assistantTurn;
    assistantTurn.role = "assistant";
    assistantTurn.content = message;
    assistantTurn.unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    target->AddTurn(std::move(assistantTurn));

    // Flush the session so it persists
    m_sessions->FlushSession(target->Id());

    Json::Value body(Json::objectValue);
    body["success"] = true;
    body["session_key"] = sessionKey;
    body["message"] = Truncate(message, 200);

    // Note: actual delivery to the interface (Signal, WhatsApp, etc.) is
    // handled by the session pipeline — the turn is stored and will be
    // delivered on the next flush/routing cycle.

    result.success = true;
    result.output = ResultToJsonString(body);
    return result;
}

// ============================================================================
// notes:list
// ============================================================================

ToolResult SessionsTool::HandleNotesList(const std::string& agentId,
                                          const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    if (!m_notesStore) {
        result.success = false;
        result.error = "session notes not available";
        return result;
    }

    std::string sessionKey = GetCurrentSessionKey(rawArgs);
    if (sessionKey.empty()) {
        result.success = false;
        result.error = "no active session context";
        return result;
    }

    auto notes = m_notesStore->ListForSession(sessionKey, agentId);

    Json::Value items(Json::arrayValue);
    for (const auto& note : notes) {
        Json::Value item(Json::objectValue);
        item["id"] = static_cast<Json::Int64>(note.id);
        item["bullet"] = note.bullet;
        item["sort_order"] = note.sort_order;
        items.append(item);
    }

    Json::Value body(Json::objectValue);
    body["notes"] = items;
    body["count"] = static_cast<Json::Int>(items.size());

    result.success = true;
    result.output = ResultToJsonString(body);
    return result;
}

// ============================================================================
// notes:add
// ============================================================================

ToolResult SessionsTool::HandleNotesAdd(const std::string& agentId,
                                         const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    std::string bullet = GetStringField(args, "bullet", "");

    if (bullet.empty()) {
        result.success = false;
        result.error = "bullet text is required for notes:add";
        return result;
    }

    if (!m_notesStore) {
        result.success = false;
        result.error = "session notes not available";
        return result;
    }

    std::string sessionKey = GetCurrentSessionKey(rawArgs);
    if (sessionKey.empty()) {
        result.success = false;
        result.error = "no active session context";
        return result;
    }

    // Check note limit
    auto count = m_notesStore->CountForSession(sessionKey, agentId);
    if (count >= SessionNotesStore::kMaxNotesPerSession) {
        result.success = false;
        result.error = "session note limit reached (" +
            std::to_string(SessionNotesStore::kMaxNotesPerSession) +
            "). Remove some notes first.";
        return result;
    }

    std::string position = GetStringField(args, "position", "bottom");
    int sortOrder;
    if (position == "top") {
        // Insert at position 0 — shift all existing notes up
        sortOrder = 0;
        auto existing = m_notesStore->ListForSession(sessionKey, agentId);
        std::vector<int64_t> ids;
        // The new note gets sort_order 0, existing notes shift by +1
        // We reorder all existing notes after insert
        for (const auto& n : existing) {
            ids.push_back(n.id);
        }
        // Create the new note at sort_order 0
        auto note = m_notesStore->Create(sessionKey, agentId, bullet, 0);
        // Reorder existing notes: new note first, then all existing
        std::vector<int64_t> allIds;
        allIds.push_back(note.id);
        for (auto id : ids) {
            allIds.push_back(id);
        }
        m_notesStore->Reorder(allIds, agentId);
    } else {
        sortOrder = m_notesStore->NextSortOrder(sessionKey, agentId);
        m_notesStore->Create(sessionKey, agentId, bullet, sortOrder);
    }

    // Return updated list
    return HandleNotesList(agentId, rawArgs);
}

// ============================================================================
// notes:update
// ============================================================================

ToolResult SessionsTool::HandleNotesUpdate(const std::string& agentId,
                                            const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    int64_t noteId = static_cast<int64_t>(GetIntField(args, "note_id", 0));
    std::string bullet = GetStringField(args, "bullet", "");

    if (noteId <= 0) {
        result.success = false;
        result.error = "note_id is required for notes:update";
        return result;
    }
    if (bullet.empty()) {
        result.success = false;
        result.error = "bullet text is required for notes:update";
        return result;
    }

    if (!m_notesStore) {
        result.success = false;
        result.error = "session notes not available";
        return result;
    }

    if (!m_notesStore->Update(noteId, agentId, bullet)) {
        result.success = false;
        result.error = "note not found or update failed";
        return result;
    }

    return HandleNotesList(agentId, rawArgs);
}

// ============================================================================
// notes:remove
// ============================================================================

ToolResult SessionsTool::HandleNotesRemove(const std::string& agentId,
                                            const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    int64_t noteId = static_cast<int64_t>(GetIntField(args, "note_id", 0));

    if (noteId <= 0) {
        result.success = false;
        result.error = "note_id is required for notes:remove";
        return result;
    }

    if (!m_notesStore) {
        result.success = false;
        result.error = "session notes not available";
        return result;
    }

    if (!m_notesStore->Delete(noteId, agentId)) {
        result.success = false;
        result.error = "note not found or delete failed";
        return result;
    }

    return HandleNotesList(agentId, rawArgs);
}

// ============================================================================
// notes:reorder
// ============================================================================

ToolResult SessionsTool::HandleNotesReorder(const std::string& agentId,
                                             const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    auto noteIds = GetInt64Array(args, "note_ids");

    if (noteIds.empty()) {
        result.success = false;
        result.error = "note_ids array is required for notes:reorder";
        return result;
    }

    if (!m_notesStore) {
        result.success = false;
        result.error = "session notes not available";
        return result;
    }

    if (!m_notesStore->Reorder(noteIds, agentId)) {
        result.success = false;
        result.error = "reorder failed";
        return result;
    }

    return HandleNotesList(agentId, rawArgs);
}

// ============================================================================
// tags:list
// ============================================================================

ToolResult SessionsTool::HandleTagsList(const std::string& agentId,
                                         const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    if (!m_tagsStore) {
        result.success = false;
        result.error = "session tags not available";
        return result;
    }

    std::string sessionKey = GetCurrentSessionKey(rawArgs);
    if (sessionKey.empty()) {
        result.success = false;
        result.error = "no active session context";
        return result;
    }

    auto tags = m_tagsStore->ListForSession(sessionKey);

    Json::Value items(Json::arrayValue);
    for (const auto& t : tags) {
        Json::Value item(Json::objectValue);
        item["tag"] = t.tag;
        item["source"] = t.source;
        items.append(item);
    }

    Json::Value body(Json::objectValue);
    body["tags"] = items;
    body["count"] = static_cast<Json::Int>(items.size());

    result.success = true;
    result.output = ResultToJsonString(body);
    return result;
}

// ============================================================================
// tags:set
// ============================================================================

ToolResult SessionsTool::HandleTagsSet(const std::string& agentId,
                                        const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    std::string tag = GetStringField(args, "tag", "");

    if (tag.empty()) {
        result.success = false;
        result.error = "tag is required for tags:set";
        return result;
    }

    if (!m_tagsStore) {
        result.success = false;
        result.error = "session tags not available";
        return result;
    }

    std::string sessionKey = GetCurrentSessionKey(rawArgs);
    if (sessionKey.empty()) {
        result.success = false;
        result.error = "no active session context";
        return result;
    }

    m_tagsStore->Add(sessionKey, agentId, tag, "agent");

    return HandleTagsList(agentId, rawArgs);
}

// ============================================================================
// tags:remove
// ============================================================================

ToolResult SessionsTool::HandleTagsRemove(const std::string& agentId,
                                           const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    std::string tag = GetStringField(args, "tag", "");

    if (tag.empty()) {
        result.success = false;
        result.error = "tag is required for tags:remove";
        return result;
    }

    if (!m_tagsStore) {
        result.success = false;
        result.error = "session tags not available";
        return result;
    }

    std::string sessionKey = GetCurrentSessionKey(rawArgs);
    if (sessionKey.empty()) {
        result.success = false;
        result.error = "no active session context";
        return result;
    }

    if (!m_tagsStore->Remove(sessionKey, agentId, tag)) {
        result.success = false;
        result.error = "tag not found";
        return result;
    }

    return HandleTagsList(agentId, rawArgs);
}

// ============================================================================
// Compaction actions
// ============================================================================

ToolResult SessionsTool::HandleCompactions(const std::string& agentId,
                                              const std::string& rawArgs) {
    ToolResult result;
    result.success = true;

    if (!m_compactionService) {
        result.success = false;
        result.error = "compaction service not available";
        return result;
    }

    const Json::Value args = ParseArgs(rawArgs);
    const std::string sessionKey = GetStringField(args, "session_key", "");

    // Resolve session
    SessionId sessionId = 0;
    if (!sessionKey.empty()) {
        auto session = ResolveSession(sessionKey);
        if (session) sessionId = session->Id();
    }
    if (sessionId == 0) {
        result.success = false;
        result.error = "session not found";
        return result;
    }

    auto compactions = m_compactionService->GetCompactions(sessionId);
    Json::Value arr(Json::arrayValue);
    for (const auto& c : compactions) {
        Json::Value entry;
        entry["id"] = static_cast<Json::UInt64>(c.id);
        entry["message_id"] = static_cast<Json::UInt64>(c.message_id);
        entry["token_count"] = static_cast<Json::UInt64>(c.token_count);
        entry["model"] = c.model;
        entry["created_at_unix_ms"] = static_cast<Json::UInt64>(c.created_at_unix_ms);
        // Summary is abbreviated for listing
        entry["summary_preview"] = c.summary.substr(0, 200) + (c.summary.size() > 200 ? "..." : "");
        arr.append(entry);
    }

    Json::Value output;
    output["compactions"] = arr;
    output["total"] = static_cast<Json::UInt>(compactions.size());

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.output = Json::writeString(wb, output);
    return result;
}

ToolResult SessionsTool::HandleCompactionsLatest(const std::string& agentId,
                                                    const std::string& rawArgs) {
    ToolResult result;
    result.success = true;

    if (!m_compactionService) {
        result.success = false;
        result.error = "compaction service not available";
        return result;
    }

    const Json::Value args = ParseArgs(rawArgs);
    const std::string sessionKey = GetStringField(args, "session_key", "");

    SessionId sessionId = 0;
    if (!sessionKey.empty()) {
        auto session = ResolveSession(sessionKey);
        if (session) sessionId = session->Id();
    }
    if (sessionId == 0) {
        result.success = false;
        result.error = "session not found";
        return result;
    }

    auto latest = m_compactionService->GetLatestCompaction(sessionId);
    if (latest) {
        Json::Value output;
        output["id"] = static_cast<Json::UInt64>(latest->id);
        output["message_id"] = static_cast<Json::UInt64>(latest->message_id);
        output["summary"] = latest->summary;
        output["token_count"] = static_cast<Json::UInt64>(latest->token_count);
        output["model"] = latest->model;
        output["created_at_unix_ms"] = static_cast<Json::UInt64>(latest->created_at_unix_ms);

        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        result.output = Json::writeString(wb, output);
    } else {
        result.output = "{\"compaction_summary\":null}";
    }
    return result;
}

ToolResult SessionsTool::HandleCompact(const std::string& agentId,
                                         const std::string& rawArgs) {
    ToolResult result;
    result.success = true;

    if (!m_compactionService) {
        result.success = false;
        result.error = "compaction service not available";
        return result;
    }

    const Json::Value args = ParseArgs(rawArgs);
    const std::string sessionKey = GetStringField(args, "session_key", "");

    SessionId sessionId = 0;
    if (!sessionKey.empty()) {
        auto session = ResolveSession(sessionKey);
        if (session) sessionId = session->Id();
    }
    if (sessionId == 0) {
        result.success = false;
        result.error = "session not found";
        return result;
    }

    // Force compaction regardless of threshold
    auto compactResult = m_compactionService->CompactNow(
        sessionId, "", "", "", 128000);

    if (compactResult.success) {
        Json::Value output;
        output["compacted"] = true;
        output["turns_compacted"] = static_cast<Json::UInt64>(compactResult.turns_compacted);
        output["summary_tokens"] = static_cast<Json::UInt64>(compactResult.entry.token_count);

        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        result.output = Json::writeString(wb, output);
    } else {
        result.success = false;
        result.error = compactResult.error;
    }
    return result;
}

} // namespace animus::kernel

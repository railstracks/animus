#include "animus_kernel/tools/GallivantingTool.h"

#include "animus_kernel/GallivantingStore.h"

#include <chrono>
#include <json/json.h>
#include <json/writer.h>
#include <sstream>

namespace animus::kernel {

namespace {

int64_t AgentIdToNumeric(const std::string& agentId) {
    if (agentId.empty()) return 0;
    try { return std::stoll(agentId); }
    catch (...) { return 0; }
}

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

std::string GetString(const Json::Value& args, const std::string& key,
                       const std::string& def = "") {
    if (args.isMember(key) && args[key].isString()) return args[key].asString();
    return def;
}

int64_t GetInt(const Json::Value& args, const std::string& key, int64_t def = 0) {
    if (args.isMember(key) && (args[key].isInt() || args[key].isInt64()))
        return args[key].asInt64();
    return def;
}

double GetDouble(const Json::Value& args, const std::string& key, double def = 0.0) {
    if (args.isMember(key) && args[key].isDouble()) return args[key].asDouble();
    return def;
}

bool GetBool(const Json::Value& args, const std::string& key, bool def = false) {
    if (args.isMember(key) && args[key].isBool()) return args[key].asBool();
    return def;
}

std::string ToJson(const Json::Value& val) {
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, val);
}

std::string ErrorResult(const std::string& error) {
    Json::Value r(Json::objectValue);
    r["error"] = error;
    return ToJson(r);
}

Json::Value ThreadToJson(const GallivantingThread& t) {
    Json::Value v(Json::objectValue);
    v["id"] = static_cast<Json::Int64>(t.id);
    v["name"] = t.name;
    v["description"] = t.description;
    // Parse sdt_tags_json into object
    Json::CharReaderBuilder builder;
    std::istringstream stream(t.sdt_tags_json);
    Json::Value tags;
    std::string errors;
    if (Json::parseFromStream(builder, stream, &tags, &errors) && (tags.isObject() || tags.isArray())) {
        v["sdt_tags"] = tags;
    } else {
        v["sdt_tags"] = Json::objectValue;
    }
    v["prompt_template"] = t.prompt_template;
    v["enabled"] = t.enabled;
    v["created_at_unix_ms"] = static_cast<Json::Int64>(t.created_at_unix_ms);
    v["updated_at_unix_ms"] = static_cast<Json::Int64>(t.updated_at_unix_ms);
    return v;
}

Json::Value SessionToJson(const GallivantingSession& s) {
    Json::Value v(Json::objectValue);
    v["id"] = static_cast<Json::Int64>(s.id);
    v["thread_id"] = static_cast<Json::Int64>(s.thread_id);
    v["started_at_unix_ms"] = static_cast<Json::Int64>(s.started_at_unix_ms);
    v["duration_ms"] = static_cast<Json::Int64>(s.duration_ms);
    v["summary"] = s.summary;
    v["outcome"] = s.outcome;

    Json::CharReaderBuilder builder;
    std::istringstream scoreStream(s.sdt_scores_json);
    Json::Value scores;
    std::string errors;
    if (Json::parseFromStream(builder, scoreStream, &scores, &errors) && (scores.isObject() || scores.isArray())) {
        v["sdt_scores"] = scores;
    } else {
        v["sdt_scores"] = Json::objectValue;
    }

    std::istringstream toolsStream(s.tools_used_json);
    Json::Value tools;
    if (Json::parseFromStream(builder, toolsStream, &tools, &errors) && tools.isArray()) {
        v["tools_used"] = tools;
    } else {
        v["tools_used"] = Json::arrayValue;
    }

    v["created_at_unix_ms"] = static_cast<Json::Int64>(s.created_at_unix_ms);
    return v;
}

} // anonymous namespace

// ============================================================================
// Construction
// ============================================================================

GallivantingTool::GallivantingTool(GallivantingStore* store)
    : m_store(store) {}

// ============================================================================
// Tool definition
// ============================================================================

ToolDefinition GallivantingTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "gallivanting";
    def.description =
        "Manage your gallivanting threads — self-directed exploration activities. "
        "List available threads, read their state, create new threads, update descriptions, "
        "record session results, and view session history. Threads track what you've been "
        "exploring and help maintain balance across different needs.";
    def.resultMode = ToolResultMode::deliver_to_model;

    def.parameters.push_back({
        "action", "string",
        "Gallivanting operation",
        true, "", {"list", "read", "create", "update", "delete", "record", "history"}
    });

    // read/delete/history
    def.parameters.push_back({
        "thread_id", "string",
        "Thread ID (required for read, update, delete, record, history)",
        false
    });

    // create/update
    def.parameters.push_back({
        "name", "string",
        "Thread name (required for create, optional for update)",
        false
    });
    def.parameters.push_back({
        "description", "string",
        "Thread state description (optional for create/update)",
        false
    });
    def.parameters.push_back({
        "sdt_tags", "object",
        "SDT need tags: {autonomy, competence, relatedness, personal_development, relaxation, meaning} with 0.0-1.0 values",
        false
    });
    def.parameters.push_back({
        "prompt_template", "string",
        "Custom prompt override for this thread (empty = use default gallivanting prompt)",
        false
    });
    def.parameters.push_back({
        "enabled", "boolean",
        "Whether this thread is active (default: true)",
        false
    });

    // record
    def.parameters.push_back({
        "summary", "string",
        "What you did during this session (required for record)",
        false
    });
    def.parameters.push_back({
        "outcome", "string",
        "What came of this session — insights, progress, plans",
        false
    });
    def.parameters.push_back({
        "duration_ms", "integer",
        "Session duration in milliseconds",
        false
    });
    def.parameters.push_back({
        "sdt_scores", "object",
        "Actual SDT scores for this session {autonomy: 0.7, ...}",
        false
    });
    def.parameters.push_back({
        "tools_used", "array",
        "List of tools used during this session",
        false
    });

    // history
    def.parameters.push_back({
        "limit", "integer",
        "Max results for history (default: 20)",
        false
    });

    return def;
}

// ============================================================================
// Execute
// ============================================================================

ToolResult GallivantingTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    if (args.isNull()) {
        result.success = false;
        result.error = "failed to parse arguments";
        return result;
    }

    const std::string agentId = GetString(args, "__agent_id", "");
    const std::string action = GetString(args, "action");

    if (action.empty()) {
        result.success = false;
        result.error = "action is required";
        return result;
    }

    if (action == "list") return HandleList(agentId, call.arguments);
    if (action == "read") return HandleRead(agentId, call.arguments);
    if (action == "create") return HandleCreate(agentId, call.arguments);
    if (action == "update") return HandleUpdate(agentId, call.arguments);
    if (action == "delete") return HandleDelete(agentId, call.arguments);
    if (action == "record") return HandleRecord(agentId, call.arguments);
    if (action == "history") return HandleHistory(agentId, call.arguments);

    result.success = false;
    result.error = "unknown action: " + action;
    return result;
}

// ============================================================================
// list
// ============================================================================

ToolResult GallivantingTool::HandleList(const std::string& agentId, const std::string&) {
    ToolResult result;
    result.call_id = "";

    if (!m_store) {
        result.success = false;
        result.error = "gallivanting store not available";
        return result;
    }

    auto threads = m_store->ListThreads(agentId);
    Json::Value items(Json::arrayValue);
    for (const auto& t : threads) {
        items.append(ThreadToJson(t));
    }

    Json::Value body(Json::objectValue);
    body["threads"] = items;
    body["count"] = static_cast<Json::Int>(threads.size());

    result.success = true;
    result.output = ToJson(body);
    return result;
}

// ============================================================================
// read
// ============================================================================

ToolResult GallivantingTool::HandleRead(const std::string& agentId, const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    int64_t threadId = GetInt(args, "thread_id", 0);
    if (threadId <= 0) {
        result.success = false;
        result.error = "thread_id is required";
        return result;
    }

    if (!m_store) {
        result.success = false;
        result.error = "gallivanting store not available";
        return result;
    }

    auto thread = m_store->GetThread(threadId);
    if (!thread) {
        result.success = false;
        result.error = "thread not found";
        return result;
    }

    if (thread->agent_id != agentId) {
        result.success = false;
        result.error = "thread does not belong to this agent";
        return result;
    }

    auto sessions = m_store->ListSessionsForThread(threadId, 5);

    Json::Value body = ThreadToJson(*thread);
    Json::Value recentSessions(Json::arrayValue);
    for (const auto& s : sessions) {
        recentSessions.append(SessionToJson(s));
    }
    body["recent_sessions"] = recentSessions;

    result.success = true;
    result.output = ToJson(body);
    return result;
}

// ============================================================================
// create
// ============================================================================

ToolResult GallivantingTool::HandleCreate(const std::string& agentId, const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    std::string name = GetString(args, "name");
    if (name.empty()) {
        result.success = false;
        result.error = "name is required";
        return result;
    }

    if (!m_store) {
        result.success = false;
        result.error = "gallivanting store not available";
        return result;
    }

    GallivantingThread thread;
    thread.agent_id = agentId;
    thread.name = name;
    thread.description = GetString(args, "description");
    thread.prompt_template = GetString(args, "prompt_template");
    thread.enabled = GetBool(args, "enabled", true);

    // Serialize sdt_tags
    if (args.isMember("sdt_tags") && (args["sdt_tags"].isObject() || args["sdt_tags"].isArray())) {
        thread.sdt_tags_json = ToJson(args["sdt_tags"]);
    } else {
        thread.sdt_tags_json = "[]";
    }

    auto created = m_store->CreateThread(thread);

    result.success = true;
    result.output = ToJson(ThreadToJson(created));
    return result;
}

// ============================================================================
// update
// ============================================================================

ToolResult GallivantingTool::HandleUpdate(const std::string& agentId, const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    int64_t threadId = GetInt(args, "thread_id", 0);
    if (threadId <= 0) {
        result.success = false;
        result.error = "thread_id is required";
        return result;
    }

    if (!m_store) {
        result.success = false;
        result.error = "gallivanting store not available";
        return result;
    }

    auto thread = m_store->GetThread(threadId);
    if (!thread) {
        result.success = false;
        result.error = "thread not found";
        return result;
    }
    if (thread->agent_id != agentId) {
        result.success = false;
        result.error = "thread does not belong to this agent";
        return result;
    }

    // Apply updates
    if (args.isMember("name") && args["name"].isString()) {
        thread->name = args["name"].asString();
    }
    if (args.isMember("description") && args["description"].isString()) {
        thread->description = args["description"].asString();
    }
    if (args.isMember("prompt_template") && args["prompt_template"].isString()) {
        thread->prompt_template = args["prompt_template"].asString();
    }
    if (args.isMember("enabled") && args["enabled"].isBool()) {
        thread->enabled = args["enabled"].asBool();
    }
    if (args.isMember("sdt_tags") && (args["sdt_tags"].isObject() || args["sdt_tags"].isArray())) {
        thread->sdt_tags_json = ToJson(args["sdt_tags"]);
    }

    m_store->UpdateThread(*thread);

    auto updated = m_store->GetThread(threadId);
    result.success = true;
    result.output = ToJson(ThreadToJson(updated.value_or(*thread)));
    return result;
}

// ============================================================================
// delete
// ============================================================================

ToolResult GallivantingTool::HandleDelete(const std::string& agentId, const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    int64_t threadId = GetInt(args, "thread_id", 0);
    if (threadId <= 0) {
        result.success = false;
        result.error = "thread_id is required";
        return result;
    }

    if (!m_store) {
        result.success = false;
        result.error = "gallivanting store not available";
        return result;
    }

    auto thread = m_store->GetThread(threadId);
    if (!thread) {
        result.success = false;
        result.error = "thread not found";
        return result;
    }
    if (thread->agent_id != agentId) {
        result.success = false;
        result.error = "thread does not belong to this agent";
        return result;
    }

    m_store->DeleteThread(threadId);

    Json::Value body(Json::objectValue);
    body["success"] = true;
    body["deleted_thread_id"] = static_cast<Json::Int64>(threadId);

    result.success = true;
    result.output = ToJson(body);
    return result;
}

// ============================================================================
// record
// ============================================================================

ToolResult GallivantingTool::HandleRecord(const std::string& agentId, const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    int64_t threadId = GetInt(args, "thread_id", 0);
    std::string summary = GetString(args, "summary");

    if (threadId <= 0) {
        result.success = false;
        result.error = "thread_id is required for record";
        return result;
    }
    if (summary.empty()) {
        result.success = false;
        result.error = "summary is required for record";
        return result;
    }

    if (!m_store) {
        result.success = false;
        result.error = "gallivanting store not available";
        return result;
    }

    auto thread = m_store->GetThread(threadId);
    if (!thread) {
        result.success = false;
        result.error = "thread not found";
        return result;
    }
    if (thread->agent_id != agentId) {
        result.success = false;
        result.error = "thread does not belong to this agent";
        return result;
    }

    GallivantingSession session;
    session.thread_id = threadId;
    session.agent_id = agentId;
    session.started_at_unix_ms = GetInt(args, "started_at_ms", 0);
    session.duration_ms = GetInt(args, "duration_ms", 0);
    session.summary = summary;
    session.outcome = GetString(args, "outcome");

    if (args.isMember("sdt_scores") && (args["sdt_scores"].isObject() || args["sdt_scores"].isArray())) {
        session.sdt_scores_json = ToJson(args["sdt_scores"]);
    } else {
        session.sdt_scores_json = "{}";
    }

    if (args.isMember("tools_used") && args["tools_used"].isArray()) {
        session.tools_used_json = ToJson(args["tools_used"]);
    } else {
        session.tools_used_json = "[]";
    }

    auto created = m_store->CreateSession(session);

    // Update thread description if provided
    std::string newDescription = GetString(args, "description");
    if (!newDescription.empty()) {
        thread->description = newDescription;
        m_store->UpdateThread(*thread);
    }

    result.success = true;
    result.output = ToJson(SessionToJson(created));
    return result;
}

// ============================================================================
// history
// ============================================================================

ToolResult GallivantingTool::HandleHistory(const std::string& agentId, const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    int64_t threadId = GetInt(args, "thread_id", 0);
    int64_t limit = GetInt(args, "limit", 20);
    if (limit < 1) limit = 20;
    if (limit > 100) limit = 100;

    if (!m_store) {
        result.success = false;
        result.error = "gallivanting store not available";
        return result;
    }

    std::vector<GallivantingSession> sessions;
    if (threadId > 0) {
        auto thread = m_store->GetThread(threadId);
        if (!thread) {
            result.success = false;
            result.error = "thread not found";
            return result;
        }
        if (thread->agent_id != agentId) {
            result.success = false;
            result.error = "thread does not belong to this agent";
            return result;
        }
        sessions = m_store->ListSessionsForThread(threadId, limit);
    } else {
        sessions = m_store->ListRecentSessions(agentId, limit);
    }

    Json::Value items(Json::arrayValue);
    for (const auto& s : sessions) {
        items.append(SessionToJson(s));
    }

    Json::Value body(Json::objectValue);
    body["sessions"] = items;
    body["count"] = static_cast<Json::Int>(sessions.size());

    result.success = true;
    result.output = ToJson(body);
    return result;
}

} // namespace animus::kernel

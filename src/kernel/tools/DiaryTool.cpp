#include "animus_kernel/tools/DiaryTool.h"

#include <json/json.h>
#include <json/writer.h>

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

std::string GetAction(const Json::Value& args) {
    if (args.isMember("action") && args["action"].isString()) {
        return args["action"].asString();
    }
    return "";
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

std::string ResultToJsonString(int httpStatus, const Json::Value& body) {
    Json::Value result(Json::objectValue);
    result["status"] = httpStatus;
    result["data"] = body;
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, result);
}

std::string ErrorToJsonString(int httpStatus, const std::string& error) {
    Json::Value result(Json::objectValue);
    result["status"] = httpStatus;
    result["error"] = error;
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, result);
}
} // anonymous namespace

// ============================================================================
// Construction
// ============================================================================

DiaryTool::DiaryTool(DiaryManager* diaryManager)
    : m_diaryManager(diaryManager) {}

// ============================================================================
// Tool definition
// ============================================================================

ToolDefinition DiaryTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "diary";
    def.description =
        "Your private diary. Write reflections, observations, and intentions. "
        "List, read, or search past entries, or delete entries. "
        "Diary entries are private to you — no other agent or admin user can read them.";
    def.resultMode = ToolResultMode::deliver_to_model;

    def.parameters.push_back({
        "action", "string",
        "The diary operation to perform: write, list, read, search, delete",
        true, "", {"write", "list", "read", "search", "delete"}
    });

    // write parameters
    def.parameters.push_back({
        "content", "string",
        "Entry content (required for write)",
        false
    });
    def.parameters.push_back({
        "layer", "string",
        "Entry layer: reflection, observation, or intention (optional, default: reflection)",
        false, "", {"reflection", "observation", "intention"}
    });
    def.parameters.push_back({
        "tags", "array",
        "Optional tags for the entry (array of strings)",
        false
    });
    def.parameters.push_back({
        "session_id", "string",
        "Optional session context for the entry",
        false
    });

    // list parameters
    def.parameters.push_back({
        "page", "integer",
        "Page number for list/search results (default 1)",
        false
    });
    def.parameters.push_back({
        "limit", "integer",
        "Entries per page for list/search (default 20, max 100)",
        false
    });

    // read parameters
    def.parameters.push_back({
        "layer", "string",
        "Filter by layer (optional, for read)",
        false, "", {"reflection", "observation", "intention"}
    });
    def.parameters.push_back({
        "since", "integer",
        "Read entries since this Unix ms timestamp (optional, for read)",
        false
    });
    def.parameters.push_back({
        "until", "integer",
        "Read entries until this Unix ms timestamp (optional, for read)",
        false
    });
    def.parameters.push_back({
        "limit", "integer",
        "Maximum number of entries to return (default 100, for read/search)",
        false
    });

    // search parameters
    def.parameters.push_back({
        "query", "string",
        "Search query for full-text search (required for search)",
        false
    });

    // delete parameters
    def.parameters.push_back({
        "entry_id", "string",
        "ID of the entry to delete (required for delete)",
        false
    });

    return def;
}

// ============================================================================
// Execute
// ============================================================================

ToolResult DiaryTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    if (args.isNull()) {
        result.success = false;
        result.error = "failed to parse arguments";
        return result;
    }

    // Determine agent_id from call arguments.
    // The chain runner injects __agent_id into tool calls for agent-scoped tools.
    std::string agentId = GetStringField(args, "__agent_id", "");

    std::string action = GetAction(args);
    if (action.empty()) {
        result.success = false;
        result.error = "action is required (write, list, read, search, delete)";
        return result;
    }

    DiaryManager::OperationResult opResult;

    if (action == "write") {
        opResult = HandleWrite(agentId, args);
    } else if (action == "list") {
        opResult = HandleList(agentId, args);
    } else if (action == "read") {
        opResult = HandleRead(agentId, args);
    } else if (action == "search") {
        opResult = HandleSearch(agentId, args);
    } else if (action == "delete") {
        opResult = HandleDelete(agentId, args);
    } else {
        result.success = false;
        result.error = "unknown action: " + action;
        return result;
    }

    result.success = (opResult.httpStatusCode < 400);
    if (result.success) {
        result.output = ResultToJsonString(opResult.httpStatusCode, opResult.body);
    } else {
        result.error = opResult.body.isMember("error")
            ? opResult.body["error"].asString()
            : "unknown error";
    }
    return result;
}

// ============================================================================
// Action handlers
// ============================================================================

DiaryManager::OperationResult DiaryTool::HandleList(
        const std::string& agentId, const Json::Value& args) {
    std::string layer = GetStringField(args, "layer", "");
    int page = GetIntField(args, "page", 1);
    int limit = GetIntField(args, "limit", 20);

    return m_diaryManager->ListEntries(agentId, layer, page, limit);
}

DiaryManager::OperationResult DiaryTool::HandleWrite(
        const std::string& agentId, const Json::Value& args) {
    Json::Value body(Json::objectValue);
    if (args.isMember("content") && args["content"].isString()) {
        body["content"] = args["content"];
    }
    if (args.isMember("layer") && args["layer"].isString()) {
        body["layer"] = args["layer"];
    }
    if (args.isMember("tags") && args["tags"].isArray()) {
        body["tags"] = args["tags"];
    }
    if (args.isMember("session_id") && args["session_id"].isString()) {
        body["session_id"] = args["session_id"];
    }

    return m_diaryManager->WriteEntry(agentId, body);
}

DiaryManager::OperationResult DiaryTool::HandleRead(
        const std::string& agentId, const Json::Value& args) {
    std::string layer = GetStringField(args, "layer", "");
    int64_t since = static_cast<int64_t>(GetIntField(args, "since", 0));
    int64_t until = static_cast<int64_t>(GetIntField(args, "until", 0));
    int limit = GetIntField(args, "limit", 100);

    return m_diaryManager->ReadEntries(agentId, layer, since, until, limit);
}

DiaryManager::OperationResult DiaryTool::HandleSearch(
        const std::string& agentId, const Json::Value& args) {
    std::string query = GetStringField(args, "query", "");
    int page = GetIntField(args, "page", 1);
    int limit = GetIntField(args, "limit", 20);

    return m_diaryManager->SearchEntries(agentId, query, page, limit);
}

DiaryManager::OperationResult DiaryTool::HandleDelete(
        const std::string& agentId, const Json::Value& args) {
    std::string entryId = GetStringField(args, "entry_id", "");

    if (entryId.empty()) {
        DiaryManager::OperationResult result;
        result.httpStatusCode = 400;
        result.body["error"] = "entry_id is required for delete";
        return result;
    }

    return m_diaryManager->DeleteEntry(agentId, entryId);
}

} // namespace animus::kernel
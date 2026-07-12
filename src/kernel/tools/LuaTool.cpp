#include "animus_kernel/tools/LuaTool.h"
#include "animus_kernel/lua/ScriptStore.h"

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

LuaTool::LuaTool(std::unordered_map<std::string, std::unique_ptr<LuaState>>& agentStates,
                 ToolRegistry& tools,
                 HttpClient& http,
                 ScriptStore& scriptStore,
                 AgentConfigStore* configStore)
    : m_agentStates(agentStates)
    , m_tools(tools)
    , m_http(http)
    , m_scriptStore(scriptStore)
    , m_configStore(configStore)
{}

// ============================================================================
// Tool definition
// ============================================================================

ToolDefinition LuaTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "lua";
    def.description =
        "Execute, manage, and store Lua scripts for tool composition and automation. "
        "Eval runs a script string. Run executes a stored script by name. "
        "Store saves a script. List shows available scripts. Delete removes a script.";
    def.resultMode = ToolResultMode::deliver_to_model;

    def.parameters.push_back({
        "action", "string",
        "The operation to perform: eval, run, store, list, delete",
        true, "", {"eval", "run", "store", "list", "delete"}
    });

    // eval parameters
    def.parameters.push_back({
        "script", "string",
        "Lua script source code to evaluate (required for eval)",
        false
    });

    // run parameters
    def.parameters.push_back({
        "name", "string",
        "Name of stored script to run (required for run, delete)",
        false
    });

    // store parameters
    def.parameters.push_back({
        "source", "string",
        "Lua script source to store (required for store)",
        false
    });

    // store description (optional)
    def.parameters.push_back({
        "description", "string",
        "Optional description for the stored script",
        false
    });

    return def;
}

// ============================================================================
// Execute
// ============================================================================

ToolResult LuaTool::Execute(const ToolCall& call) {
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
        result.error = "action is required (eval, run, store, list, delete)";
        return result;
    }

    if (action == "eval") {
        return HandleEval(agentId, args);
    } else if (action == "run") {
        return HandleRun(agentId, args);
    } else if (action == "store") {
        return HandleStore(agentId, args);
    } else if (action == "list") {
        return HandleList(agentId, args);
    } else if (action == "delete") {
        return HandleDelete(agentId, args);
    } else {
        result.success = false;
        result.error = "unknown action: " + action;
        return result;
    }
}

// ============================================================================
// Action handlers
// ============================================================================

ToolResult LuaTool::HandleEval(const std::string& agentId, const Json::Value& args) {
    ToolResult result;
    std::string script = GetStringField(args, "script", "");
    if (script.empty()) {
        result.success = false;
        result.error = "script is required for eval action";
        return result;
    }

    try {
        LuaState& state = GetOrCreateState(agentId);
        std::string output = state.Eval(script, agentId);
        result.success = true;
        result.output = output;
    } catch (const LuaException& e) {
        result.success = false;
        result.error = std::string("Lua error: ") + e.what();
    }
    return result;
}

ToolResult LuaTool::HandleRun(const std::string& agentId, const Json::Value& args) {
    ToolResult result;
    std::string name = GetStringField(args, "name", "");
    if (name.empty()) {
        result.success = false;
        result.error = "name is required for run action";
        return result;
    }

    // Look up script from persistent store
    auto stored = m_scriptStore.GetByName(agentId, name);
    if (!stored) {
        result.success = false;
        result.error = "script not found: " + name;
        return result;
    }

    if (!stored->enabled) {
        result.success = false;
        result.error = "script is disabled: " + name;
        return result;
    }

    try {
        LuaState& state = GetOrCreateState(agentId);
        std::string output = state.Eval(stored->source, agentId);
        result.success = true;
        result.output = output;
    } catch (const LuaException& e) {
        result.success = false;
        result.error = std::string("Lua error: ") + e.what();
    }
    return result;
}

ToolResult LuaTool::HandleStore(const std::string& agentId, const Json::Value& args) {
    ToolResult result;
    std::string name = GetStringField(args, "name", "");
    std::string source = GetStringField(args, "source", "");
    std::string description = GetStringField(args, "description", "");
    if (name.empty()) {
        result.success = false;
        result.error = "name is required for store action";
        return result;
    }
    if (source.empty()) {
        result.success = false;
        result.error = "source is required for store action";
        return result;
    }

    // Check if script already exists (upsert semantics)
    auto existing = m_scriptStore.GetByName(agentId, name);
    if (existing) {
        // Update existing script
        existing->source = source;
        existing->description = description.empty() ? existing->description : description;
        std::string error;
        if (!m_scriptStore.Update(*existing, &error)) {
            result.success = false;
            result.error = "failed to update script: " + error;
            return result;
        }
        result.success = true;
        result.output = ResultToJsonString(200, Json::Value("updated"));
    } else {
        // Create new script
        ScriptDescriptor desc;
        desc.agent_id = agentId;
        desc.name = name;
        desc.source = source;
        desc.description = description;
        desc.enabled = true;
        std::string error;
        std::string id = m_scriptStore.CreateAndReturnId(desc, &error);
        if (id.empty()) {
            result.success = false;
            result.error = "failed to store script: " + error;
            return result;
        }
        result.success = true;
        result.output = ResultToJsonString(200, Json::Value("stored"));
    }
    return result;
}

ToolResult LuaTool::HandleList(const std::string& agentId, const Json::Value& /*args*/) {
    ToolResult result;
    auto scripts = m_scriptStore.List(agentId);

    Json::Value arr(Json::arrayValue);
    for (const auto& s : scripts) {
        Json::Value entry(Json::objectValue);
        entry["id"] = s.id;
        entry["name"] = s.name;
        entry["description"] = s.description;
        entry["enabled"] = s.enabled;
        entry["created_at"] = s.created_at;
        entry["updated_at"] = s.updated_at;
        arr.append(entry);
    }

    result.success = true;
    result.output = ResultToJsonString(200, arr);
    return result;
}

ToolResult LuaTool::HandleDelete(const std::string& agentId, const Json::Value& args) {
    ToolResult result;
    std::string name = GetStringField(args, "name", "");
    if (name.empty()) {
        result.success = false;
        result.error = "name is required for delete action";
        return result;
    }

    auto stored = m_scriptStore.GetByName(agentId, name);
    if (!stored) {
        result.success = false;
        result.error = "script not found: " + name;
        return result;
    }

    std::string error;
    if (!m_scriptStore.Delete(stored->id, &error)) {
        result.success = false;
        result.error = "failed to delete script: " + error;
        return result;
    }

    result.success = true;
    result.output = ResultToJsonString(200, Json::Value("deleted"));
    return result;
}

// ============================================================================
// State management
// ============================================================================

LuaState& LuaTool::GetOrCreateState(const std::string& agentId) {
    auto it = m_agentStates.find(agentId);
    if (it == m_agentStates.end()) {
        LuaState::Config config;
        config.agentId = agentId;
        config.configStore = m_configStore;
        auto state = std::make_unique<LuaState>(config, m_tools, m_http);
        it = m_agentStates.emplace(agentId, std::move(state)).first;
    }
    return *it->second;
}

} // namespace animus::kernel
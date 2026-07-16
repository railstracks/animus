#include "animus_kernel/tools/AgentTool.h"

#include <json/json.h>
#include <set>
#include <vector>

namespace animus::kernel {

AgentTool::AgentTool(AgentStore* agentStore, bool allowIdentityEdit)
    : m_agentStore(agentStore)
    , m_allowIdentityEdit(allowIdentityEdit) {}

ToolDefinition AgentTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "agent";
    def.description =
        "View and update your own agent settings. "
        "You can modify your avatar (icon), description, and identity (system prompt). "
        "Use action \"view\" to see your current settings, "
        "action \"update\" to change them.";
    def.resultMode = ToolResultMode::deliver_to_model;

    def.parameters.push_back({
        "action", "string",
        "The action to perform: view or update",
        true, "", {"view", "update"}
    });
    def.parameters.push_back({
        "avatar", "string",
        "New avatar (emoji or short text). For action=update.",
        false
    });
    def.parameters.push_back({
        "description", "string",
        "New description text. For action=update.",
        false
    });
    def.parameters.push_back({
        "identity", "string",
        "New identity / system prompt text. For action=update. May be restricted by operator configuration.",
        false
    });

    return def;
}

ToolResult AgentTool::Execute(const ToolCall& call) {
    ToolResult result;

    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    auto* reader = builder.newCharReader();
    reader->parse(call.arguments.c_str(), call.arguments.c_str() + call.arguments.size(), &args, &parseErr);
    delete reader;

    if (!parseErr.empty()) {
        result.success = false;
        result.error = "Failed to parse arguments: " + parseErr;
        return result;
    }

    const std::string agentId = args.get("__agent_id", "").asString();
    if (agentId.empty()) {
        result.success = false;
        result.error = "Agent ID is required (injected by ChainRunner)";
        return result;
    }

    const std::string action = args.get("action", "").asString();

    if (action == "view") {
        return HandleView(agentId);
    } else if (action == "update") {
        return HandleUpdate(agentId, args);
    } else {
        result.success = false;
        result.error = "Unknown action: " + action + ". Valid actions: view, update.";
        return result;
    }
}

ToolResult AgentTool::HandleView(const std::string& agentId) {
    ToolResult result;

    if (!m_agentStore) {
        result.success = false;
        result.error = "Agent store not available";
        return result;
    }

    auto agent = m_agentStore->GetById(agentId);
    if (!agent) {
        result.success = false;
        result.error = "Agent not found: " + agentId;
        return result;
    }

    Json::Value out(Json::objectValue);
    out["name"] = agent->name;
    out["avatar"] = agent->avatar;
    out["description"] = agent->description;
    out["identity"] = agent->identity;
    out["model"] = agent->default_provider + "/" + agent->default_model;
    out["created_at_unix_ms"] = static_cast<Json::Int64>(agent->created_at_unix_ms);

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.success = true;
    result.output = Json::writeString(wb, out);
    return result;
}

ToolResult AgentTool::HandleUpdate(const std::string& agentId, const Json::Value& args) {
    ToolResult result;

    if (!m_agentStore) {
        result.success = false;
        result.error = "Agent store not available";
        return result;
    }

    auto agent = m_agentStore->GetById(agentId);
    if (!agent) {
        result.success = false;
        result.error = "Agent not found: " + agentId;
        return result;
    }

    // Track what changed
    std::vector<std::string> changed;
    std::vector<std::string> rejected;

    // Allowed fields
    if (args.isMember("avatar")) {
        agent->avatar = args["avatar"].asString();
        changed.push_back("avatar");
    }
    if (args.isMember("description")) {
        agent->description = args["description"].asString();
        changed.push_back("description");
    }
    if (args.isMember("identity")) {
        if (!m_allowIdentityEdit) {
            rejected.push_back("identity (disabled by operator)");
        } else {
            agent->identity = args["identity"].asString();
            changed.push_back("identity");
        }
    }

    // Reject any other fields
    static const std::set<std::string> allowedParams = {
        "action", "avatar", "description", "identity", "__agent_id", "__session_key"
    };
    for (const auto& key : args.getMemberNames()) {
        if (allowedParams.find(key) == allowedParams.end()) {
            rejected.push_back(key + " (not self-editable)");
        }
    }

    if (changed.empty()) {
        result.success = false;
        result.error = "No updatable fields provided.";
        if (!rejected.empty()) {
            std::string rejStr;
            for (size_t i = 0; i < rejected.size(); ++i) {
                if (i > 0) rejStr += ", ";
                rejStr += rejected[i];
            }
            result.error += " Rejected: " + rejStr;
        }
        return result;
    }

    bool ok = m_agentStore->Update(*agent);
    result.success = ok;

    Json::Value out(Json::objectValue);
    out["updated"] = Json::Value(Json::arrayValue);
    for (const auto& field : changed) {
        out["updated"].append(field);
    }
    if (!rejected.empty()) {
        out["rejected"] = Json::Value(Json::arrayValue);
        for (const auto& field : rejected) {
            out["rejected"].append(field);
        }
    }

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.output = Json::writeString(wb, out);

    if (!ok) {
        result.error = "Database update failed";
    }
    return result;
}

} // namespace animus::kernel

#include "animus_kernel/tools/ToolsTool.h"

#include <json/json.h>
#include <json/writer.h>
#include <sstream>
#include <algorithm>

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

std::string ToJson(const Json::Value& v) {
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, v);
}

std::string GetAction(const Json::Value& args) {
    if (args.isMember("action") && args["action"].isString())
        return args["action"].asString();
    return "";
}

std::string GetString(const Json::Value& args, const std::string& key) {
    if (args.isMember(key) && args[key].isString())
        return args[key].asString();
    return "";
}
} // anonymous namespace

// ============================================================================
// Construction
// ============================================================================

ToolsTool::ToolsTool(ToolRegistry& registry, AgentStore* agentStore)
    : m_registry(registry)
    , m_agentStore(agentStore) {}

// ============================================================================
// Tool definition
// ============================================================================

ToolDefinition ToolsTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "tools";
    def.description =
        "Inspect and manage your available tools. List all tools, get detailed "
        "schema for a specific tool, or enable/disable tools at runtime. "
        "Disabling unused tools reduces prompt token usage.";
    def.resultMode = ToolResultMode::deliver_to_model;

    def.parameters.push_back({
        "action", "string",
        "Operation: list (all tools), enabled (enabled only), info (tool schema), "
        "enable, disable",
        true, "", {"list", "enabled", "info", "enable", "disable"}
    });
    def.parameters.push_back({
        "tool", "string",
        "Tool name (required for info, enable, disable)",
        false
    });

    return def;
}

// ============================================================================
// Helper: get agent tool whitelist
// ============================================================================

std::vector<std::string> ToolsTool::GetAgentToolWhitelist(const Json::Value& args) const {
    if (!m_agentStore) return {};

    std::string agentId;
    if (args.isMember("__agent_id") && args["__agent_id"].isString()) {
        agentId = args["__agent_id"].asString();
    }
    if (agentId.empty()) return {};

    auto agent = m_agentStore->GetById(agentId);
    if (!agent || agent->enabled_tools.empty()) return {};

    return agent->enabled_tools;
}

// ============================================================================
// Execute
// ============================================================================

ToolResult ToolsTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    if (args.isNull()) {
        result.success = false;
        result.error = "failed to parse arguments";
        return result;
    }

    std::string action = GetAction(args);
    if (action.empty()) {
        result.success = false;
        result.error = "action is required (list, enabled, info, enable, disable)";
        return result;
    }

    if (action == "list") {
        auto allDefs = m_registry.GetAllDefinitionsIncludingDisabled();
        auto whitelist = GetAgentToolWhitelist(args);
        bool useWhitelist = !whitelist.empty();

        Json::Value toolsArray(Json::arrayValue);
        int enabledCount = 0;

        for (const auto& def : allDefs) {
            // If agent has a whitelist, tools not in it are not available at all
            bool agentEnabled = true;
            if (useWhitelist) {
                agentEnabled = std::find(whitelist.begin(), whitelist.end(), def.name)
                               != whitelist.end();
            }

            Json::Value entry(Json::objectValue);
            entry["name"] = def.name;
            entry["description"] = def.description;
            entry["enabled"] = agentEnabled && m_registry.IsEnabled(def.name);
            entry["actions"] = static_cast<int>(def.parameters.size());
            if (!agentEnabled && useWhitelist) {
                entry["available"] = false;
            }
            if (entry["enabled"].asBool()) enabledCount++;
            toolsArray.append(entry);
        }

        Json::Value output(Json::objectValue);
        output["tools"] = toolsArray;
        output["total"] = static_cast<int>(allDefs.size());
        output["enabled"] = enabledCount;

        result.success = true;
        result.output = ToJson(output);
        return result;
    }

    if (action == "enabled") {
        auto whitelist = GetAgentToolWhitelist(args);
        bool useWhitelist = !whitelist.empty();

        auto allDefs = m_registry.GetAllDefinitions();
        Json::Value toolsArray(Json::arrayValue);

        for (const auto& def : allDefs) {
            // Filter by agent whitelist if configured
            if (useWhitelist) {
                bool inWhitelist = std::find(whitelist.begin(), whitelist.end(), def.name)
                                   != whitelist.end();
                if (!inWhitelist) continue;
            }

            Json::Value entry(Json::objectValue);
            entry["name"] = def.name;
            entry["description"] = def.description;
            toolsArray.append(entry);
        }

        Json::Value output(Json::objectValue);
        output["tools"] = toolsArray;
        output["enabled"] = static_cast<int>(toolsArray.size());

        result.success = true;
        result.output = ToJson(output);
        return result;
    }

    if (action == "info") {
        std::string toolName = GetString(args, "tool");
        if (toolName.empty()) {
            result.success = false;
            result.error = "tool name is required for info action";
            return result;
        }

        auto* handler = m_registry.Find(toolName);
        if (!handler) {
            result.success = false;
            result.error = "tool not found: " + toolName;
            return result;
        }

        auto def = handler->GetDefinition();
        Json::Value output(Json::objectValue);
        output["name"] = def.name;
        output["description"] = def.description;
        output["enabled"] = m_registry.IsEnabled(def.name);

        Json::Value paramsArray(Json::arrayValue);
        for (const auto& p : def.parameters) {
            Json::Value param(Json::objectValue);
            param["name"] = p.name;
            param["type"] = p.type;
            param["description"] = p.description;
            param["required"] = p.required;
            if (!p.enum_values.empty()) {
                Json::Value enums(Json::arrayValue);
                for (const auto& e : p.enum_values) enums.append(e);
                param["enum_values"] = enums;
            }
            paramsArray.append(param);
        }
        output["parameters"] = paramsArray;

        result.success = true;
        result.output = ToJson(output);
        return result;
    }

    if (action == "enable") {
        std::string toolName = GetString(args, "tool");
        if (toolName.empty()) {
            result.success = false;
            result.error = "tool name is required for enable action";
            return result;
        }

        // Prevent disabling the tools tool itself
        if (toolName == "tools") {
            result.success = false;
            result.error = "cannot modify the tools tool";
            return result;
        }

        if (!m_registry.Has(toolName)) {
            result.success = false;
            result.error = "tool not found: " + toolName;
            return result;
        }

        m_registry.SetEnabled(toolName, true);

        Json::Value output(Json::objectValue);
        output["tool"] = toolName;
        output["enabled"] = true;
        result.success = true;
        result.output = ToJson(output);
        return result;
    }

    if (action == "disable") {
        std::string toolName = GetString(args, "tool");
        if (toolName.empty()) {
            result.success = false;
            result.error = "tool name is required for disable action";
            return result;
        }

        // Prevent disabling the tools tool itself
        if (toolName == "tools") {
            result.success = false;
            result.error = "cannot disable the tools tool (would lock you out)";
            return result;
        }

        if (!m_registry.Has(toolName)) {
            result.success = false;
            result.error = "tool not found: " + toolName;
            return result;
        }

        m_registry.SetEnabled(toolName, false);

        Json::Value output(Json::objectValue);
        output["tool"] = toolName;
        output["enabled"] = false;
        result.success = true;
        result.output = ToJson(output);
        return result;
    }

    result.success = false;
    result.error = "unknown action: " + action + " (supported: list, enabled, info, enable, disable)";
    return result;
}

} // namespace animus::kernel
#include "animus_kernel/tools/CompositeTool.h"

#include <json/json.h>
#include <json/writer.h>

#include <sstream>
#include <set>

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

CompositeTool::CompositeTool(const std::string& toolName,
                             const std::string& description)
    : m_toolName(toolName)
    , m_description(description)
{}

// ============================================================================
// Plugin registration (Phase 1)
// ============================================================================

void CompositeTool::RegisterPlugin(const CompositePlugin& plugin) {
    m_plugins[plugin.id] = plugin;
    // Invalidate schema — it needs re-finalization
    m_schemaFinalized = false;
}

bool CompositeTool::UnregisterPlugin(const std::string& id) {
    auto it = m_plugins.find(id);
    if (it == m_plugins.end()) return false;
    m_plugins.erase(it);
    m_schemaFinalized = false;
    return true;
}

bool CompositeTool::HasPlugin(const std::string& id) const {
    return m_plugins.find(id) != m_plugins.end();
}

std::vector<std::string> CompositeTool::GetPluginIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_plugins.size());
    for (const auto& [id, _] : m_plugins) {
        ids.push_back(id);
    }
    return ids;
}

// ============================================================================
// Schema finalization (Phase 2)
// ============================================================================

void CompositeTool::FinalizeSchema() {
    m_mergedDefinition = BuildMergedDefinition();
    m_schemaFinalized = true;
}

ToolDefinition CompositeTool::BuildMergedDefinition() const {
    ToolDefinition def;
    def.name = m_toolName;
    def.description = m_description;
    def.resultMode = ToolResultMode::deliver_to_model;

    // Build a rich description that tells the agent which actions
    // are available for each registered adapter.
    //
    // Instead of flattening all parameters (which confuses the agent
    // when platforms have different capabilities), we:
    // 1. Enumerate per-adapter actions in the description
    // 2. Collect all unique actions as enum values
    // 3. Add a "describe" action that returns per-platform schema

    // --- Build per-adapter description ---
    std::ostringstream desc;
    desc << m_description << "\n\n";

    // Collect all known actions across all plugins for the enum
    std::set<std::string> allActions;

    for (const auto& [pluginId, plugin] : m_plugins) {
        desc << "## " << plugin.name << " (" << pluginId << ")\n";
        desc << "Capabilities: ";
        for (size_t i = 0; i < plugin.capabilities.size(); ++i) {
            if (i > 0) desc << ", ";
            desc << plugin.capabilities[i];
        }
        desc << "\n";

        desc << "Actions: ";
        bool first = true;
        for (const auto& [action, params] : plugin.actionSchemas) {
            if (!first) desc << ", ";
            first = false;
            desc << action;
            allActions.insert(action);
        }
        desc << "\n";

        // Per-action parameter summary
        for (const auto& [action, params] : plugin.actionSchemas) {
            if (params.empty()) continue;
            desc << "  " << action << "(";
            bool pfirst = true;
            for (const auto& p : params) {
                if (!pfirst) desc << ", ";
                pfirst = false;
                desc << p.name;
                if (p.required) desc << "*";
            }
            desc << ")\n";
        }
        desc << "\n";
    }

    desc << "Use 'describe' with a platform_id to get the full parameter schema for a specific adapter.\n";
    desc << "Use 'list' to see configured instances.";

    def.description = desc.str();

    // --- Core parameters ---
    // action with enum of all known actions plus composite actions
    std::vector<std::string> actionEnum;
    actionEnum.push_back("list");
    actionEnum.push_back("describe");
    for (const auto& a : allActions) {
        actionEnum.push_back(a);
    }

    def.parameters.push_back({
        "action", "string",
        "The operation to perform. Available actions depend on the adapter type "
        "of the platform_id you specify. Use 'list' to see instances, 'describe' "
        "to get per-platform schema.",
        true,
        "", // default
        actionEnum // enum_values
    });

    def.parameters.push_back({
        "platform_id", "string",
        "Instance to use (e.g. 'bluesky:personal', 'vk:community'). "
        "Required for all actions except 'list'.",
        false
    });

    // Add composite-level parameters from subclass
    auto compositeParams = GetCompositeParameters();
    for (auto& p : compositeParams) {
        def.parameters.push_back(std::move(p));
    }

    // Add all unique parameters from all plugins (de-duplicated by name)
    // These serve as the "available parameters" the agent can use.
    // The per-action schema is available via the 'describe' action.
    std::set<std::string> seenParams;
    for (const auto& [pluginId, plugin] : m_plugins) {
        for (const auto& [action, params] : plugin.actionSchemas) {
            for (const auto& p : params) {
                if (seenParams.count(p.name)) continue;
                seenParams.insert(p.name);
                def.parameters.push_back(p);
            }
        }
    }

    return def;
}

// ============================================================================
// IToolHandler interface
// ============================================================================

ToolDefinition CompositeTool::GetDefinition() const {
    // Return the finalized schema if available, otherwise a basic one
    if (m_schemaFinalized) {
        return m_mergedDefinition;
    }

    // Unfinalized — return minimal definition
    ToolDefinition def;
    def.name = m_toolName;
    def.description = m_description + " (no plugins loaded)";
    def.resultMode = ToolResultMode::deliver_to_model;
    def.parameters.push_back({
        "action", "string",
        "Available actions depend on configured plugins",
        true
    });
    return def;
}

ToolResult CompositeTool::Execute(const ToolCall& call) {
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
        result.error = "action is required";
        return result;
    }

    // Handle composite-level actions (list, describe)
    if (action == "list" || action == "describe") {
        return HandleCompositeAction(call, action);
    }

    // Route to the appropriate plugin
    std::string pluginId = GetPluginIdFromCall(call);
    if (pluginId.empty()) {
        // Try to get from arguments
        pluginId = GetStringField(args, "platform_id", "");
    }

    if (pluginId.empty()) {
        result.success = false;
        result.error = "platform_id is required for action: " + action;
        return result;
    }

    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end()) {
        result.success = false;
        result.error = "unknown platform: " + pluginId;
        return result;
    }

    // Check if the adapter supports this action
    const auto& plugin = it->second;
    if (plugin.actionSchemas.find(action) == plugin.actionSchemas.end()) {
        // Build a helpful error with available actions
        std::string available;
        bool first = true;
        for (const auto& [act, _] : plugin.actionSchemas) {
            if (!first) available += ", ";
            first = false;
            available += act;
        }
        result.success = false;
        result.error = "adapter '" + pluginId + "' does not support action '" +
                       action + "'. Available actions: " + available;
        return result;
    }

    // Delegate to the plugin's handler
    try {
        return it->second.handler(call);
    } catch (const std::exception& e) {
        result.success = false;
        result.error = std::string("plugin error: ") + e.what();
        return result;
    }
}

} // namespace animus::kernel

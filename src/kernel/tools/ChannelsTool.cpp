#include "animus_kernel/tools/ChannelsTool.h"
#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/ChannelManager.h"
#include "animus_kernel/ChannelState.h"

#include <json/json.h>
#include <json/writer.h>

#include <sstream>
#include <algorithm>
#include <set>
#include <map>

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

/// Extract adapter type from platform_id prefix.
std::string ExtractAdapterType(const std::string& platformId) {
    auto pos = platformId.find(':');
    if (pos == std::string::npos) return platformId;
    return platformId.substr(0, pos);
}

std::string ResultToJsonString(const Json::Value& body) {
    Json::Value result(Json::objectValue);
    result["status"] = 200;
    result["data"] = body;
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, result);
}

/// Convert ToolParameter vector to JSON array.
Json::Value ParamsToJson(const std::vector<ToolParameter>& params) {
    Json::Value arr(Json::arrayValue);
    for (const auto& p : params) {
        Json::Value obj(Json::objectValue);
        obj["name"] = p.name;
        obj["type"] = p.type;
        obj["description"] = p.description;
        obj["required"] = p.required;
        arr.append(obj);
    }
    return arr;
}

} // anonymous namespace

// ============================================================================
// Construction
// ============================================================================

ChannelsTool::ChannelsTool()
    : CompositeTool("channels",
        "Publish, browse, and interact with connected channels. "
        "Use 'list' to see available instances. Use 'describe' to see "
        "which actions a specific adapter supports. Platform availability "
        "depends on configuration.")
{}

ToolDefinition ChannelsTool::GetDefinition() const {
    // If we have an agent filter set, build a fresh filtered definition.
    // Otherwise use the finalized schema from FinalizeSchema().
    if (!m_currentAgentId.empty()) {
        auto def = BuildMergedDefinition();
        def.session_types = {"consolidation", "default"};
        return def;
    }
    auto def = CompositeTool::GetDefinition();
    // Social tool is excluded from auto-reply sessions (chat/wall) —
    // the agent should produce plain text replies, and the plumbing
    // handles delivery via SendAutoReply.
    def.session_types = {"consolidation", "default"};
    return def;
}

// ============================================================================
// Execute override — intercepts post results for session routing
// ============================================================================

ToolResult ChannelsTool::Execute(const ToolCall& call) {
    // Parse args early so we can intercept after delegation
    auto preArgs = ParseArgs(call.arguments);
    std::string platformId = GetStringField(preArgs, "platform_id");
    std::string action = GetStringField(preArgs, "action");
    std::string adapterType = ExtractAdapterType(platformId);

    // C++ bridge: IRC actions that need ChannelManager access
    if (adapterType == "irc" && m_channelManager) {
        ToolResult result;
        result.call_id = call.id;
        if (action == "post") {
            std::string target = GetStringField(preArgs, "channel");
            if (target.empty()) target = GetStringField(preArgs, "target");
            std::string content = GetStringField(preArgs, "content");
            if (target.empty() || content.empty()) {
                result.success = false;
                result.error = "IRC post requires 'channel' and 'content' parameters";
                result.call_id = call.id;
                return result;
            }
            ChannelManager::ReplyTarget rt;
            rt.channel_name = ExtractInstanceName(platformId);
            rt.channel_type = "irc";
            rt.irc_target = target;
            m_channelManager->SendReply(rt, content);
            result.success = true;
            result.output = "Message sent to " + target + " on IRC";
            result.call_id = call.id;
            return result;
        }
        if (action == "list_channels") {
            auto chState = m_channelManager->GetChannel(ExtractInstanceName(platformId));
            if (chState) {
                Json::Value channels(Json::arrayValue);
                if (chState->config.isMember("channels") && chState->config["channels"].isArray()) {
                    for (const auto& ch : chState->config["channels"]) {
                        channels.append(ch["name"].asString());
                    }
                }
                std::string nick = chState->config.get("nick", "?").asString();
                std::string host = chState->config.get("host", "?").asString();
                Json::Value out(Json::objectValue);
                out["channels"] = channels;
                out["nick"] = nick;
                out["server"] = host;
                result.success = true;
                result.output = ResultToJsonString(out);
            } else {
                result.success = false;
                result.error = "IRC channel not found: " + ExtractInstanceName(platformId);
            }
            result.call_id = call.id;
            return result;
        }

        if (action == "get_channel") {
            auto chState = m_channelManager->GetChannel(ExtractInstanceName(platformId));
            if (chState) {
                std::string channel = GetStringField(preArgs, "channel");
                Json::Value out(Json::objectValue);
                out["channel"] = channel;
                out["nick"] = chState->config.get("nick", "?").asString();
                out["server"] = chState->config.get("host", "?").asString();
                out["connected"] = chState->connected;
                out["enabled"] = chState->enabled;
                // Check if this channel is in the joined channels list
                bool inChannel = false;
                if (chState->config.isMember("channels") && chState->config["channels"].isArray()) {
                    for (const auto& ch : chState->config["channels"]) {
                        if (ch["name"].asString() == channel) {
                            inChannel = true;
                            if (ch.isMember("key") && !ch["key"].asString().empty()) {
                                out["has_key"] = true;
                            }
                            break;
                        }
                    }
                }
                out["joined"] = inChannel;
                // Include agent_id and DM settings
                if (chState->config.isMember("agent_id")) {
                    out["agent_id"] = chState->config["agent_id"].asString();
                }
                if (chState->config.isMember("allowed_dm_users")) {
                    Json::Value dmUsers(Json::arrayValue);
                    for (const auto& u : chState->config["allowed_dm_users"]) {
                        dmUsers.append(u.asString());
                    }
                    out["allowed_dm_users"] = dmUsers;
                }
                result.success = true;
                result.output = ResultToJsonString(out);
            } else {
                result.success = false;
                result.error = "IRC channel not found: " + ExtractInstanceName(platformId);
            }
            result.call_id = call.id;
            return result;
        }
    }

    // Delegate to CompositeTool::Execute for the actual work
    auto result = CompositeTool::Execute(call);

    // If the agent just created a post, register the post_id for session routing
    if (result.success && m_postCreatedCb &&
        (action == "post" || action == "reply") &&
        !platformId.empty()) {
        // Try to extract post_id from the result
        auto resultJson = ParseArgs(result.output);
        std::string postId;

        // Navigate: {"status":200,"data":{"post_id":"..."}} or nested output
        if (resultJson.isMember("data")) {
            auto& data = resultJson["data"];
            if (data.isMember("post_id")) {
                postId = data["post_id"].asString();
            } else if (data.isMember("id")) {
                postId = data["id"].asString();
            } else if (data.isMember("output")) {
                // The Lua adapter returns {"success":true,"output":"{\"post_id\":...}"}
                auto inner = ParseArgs(data["output"].asString());
                if (inner.isMember("post_id")) postId = inner["post_id"].asString();
                else if (inner.isMember("id")) postId = inner["id"].asString();
            }
        }

        if (!postId.empty()) {
            // The ChainRunner injects __session_key and __agent_id into tool calls
            std::string sessionKey = GetStringField(preArgs, "__session_key", "");
            std::string agentId = GetStringField(preArgs, "__agent_id", "");

            try {
                m_postCreatedCb(platformId, postId, sessionKey, agentId);
            } catch (...) {
                // Don't fail the tool call if routing registration fails
            }
        }
    }

    return result;
}

// ============================================================================
// Routing helpers
// ============================================================================

std::string ChannelsTool::GetActionFromCall(const ToolCall& call) const {
    auto args = ParseArgs(call.arguments);
    if (args.isNull()) return "";
    return GetStringField(args, "action");
}

std::string ChannelsTool::GetPluginIdFromCall(const ToolCall& call) const {
    auto args = ParseArgs(call.arguments);
    if (args.isNull()) return "";
    std::string platformId = GetStringField(args, "platform_id");
    if (platformId.empty()) return "";
    return ExtractAdapterType(platformId);
}

std::string ChannelsTool::ExtractInstanceName(const std::string& platformId) const {
    auto colonPos = platformId.find(':');
    if (colonPos != std::string::npos) {
        return platformId.substr(colonPos + 1);
    }
    return platformId;
}

std::vector<ToolParameter> ChannelsTool::GetCompositeParameters() const {
    return {
        {"extra", "object",
         "Platform-specific parameters passed through to the adapter.",
         false},
    };
}

// ============================================================================
// Agent-aware schema filtering
// ============================================================================

std::set<std::string> ChannelsTool::GetAdapterTypesForAgent(const std::string& agentId) const {
    std::set<std::string> types;

    if (agentId.empty()) {
        // No agent filter — return all registered adapter types
        for (const auto& [id, _] : GetAllPlugins()) {
            types.insert(id);
        }
        return types;
    }

    // 1. Check ChannelManager (C++ connectors: IRC, Telegram, Discord, Slack, etc.)
    if (m_channelManager) {
        for (const auto& ch : m_channelManager->ListChannels()) {
            // Check if this channel is assigned to this agent
            std::string chAgentId = ch.config.get("agent_id", "").asString();
            if (chAgentId == agentId) {
                types.insert(ch.type);
            }
        }
    }

    // 2. Check AgentConfigStore (Lua adapters: Bluesky, VK, Mastodon, etc.)
    if (m_configStore) {
        m_configStore->WarmCache(agentId);
        auto keys = m_configStore->ListKeys(agentId);
        for (const auto& key : keys) {
            // Support both channels.X and legacy social.X prefixes
            std::string rest;
            if (key.size() >= 9 && key.substr(0, 9) == "channels.") {
                rest = key.substr(9);
            } else if (key.size() >= 7 && key.substr(0, 7) == "social.") {
                rest = key.substr(7);
            } else {
                continue;
            }
            auto dotPos = rest.find('.');
            if (dotPos == std::string::npos) continue;
            std::string platformId = rest.substr(0, dotPos);
            std::string adapterType = ExtractAdapterType(platformId);
            types.insert(adapterType);
        }
    }

    return types;
}

ToolDefinition ChannelsTool::BuildMergedDefinition() const {
    // Determine which adapter types to include
    std::set<std::string> includeTypes;
    if (!m_currentAgentId.empty()) {
        // Use const_cast to call the non-const helper — it's logically const
        // (only reads data, doesn't modify state)
        includeTypes = const_cast<ChannelsTool*>(this)->GetAdapterTypesForAgent(m_currentAgentId);
    }

    // If no types found and no agent filter, fall back to base behavior
    if (includeTypes.empty() && m_currentAgentId.empty()) {
        return CompositeTool::BuildMergedDefinition();
    }

    // Build filtered definition
    ToolDefinition def;
    def.name = "channels";
    def.resultMode = ToolResultMode::deliver_to_model;

    std::ostringstream desc;
    desc << "Publish, browse, and interact with connected channels. "
         << "Use 'list' to see available instances. Use 'describe' to see "
         << "which actions a specific adapter supports. Platform availability "
         << "depends on configuration.\n\n";

    std::set<std::string> allActions;

    for (const auto& [pluginId, plugin] : GetAllPlugins()) {
        // Skip adapters not in the include set
        if (!includeTypes.empty() && !includeTypes.count(pluginId)) continue;

        desc << "## " << plugin.name << " (" << pluginId << ")\n";
        desc << "Capabilities: ";
        for (size_t i = 0; i < plugin.capabilities.size(); ++i) {
            if (i > 0) desc << ", ";
            desc << plugin.capabilities[i];
        }
        desc << "\n";

        desc << "Actions: ";
        bool first = true;
        for (const auto& [action, _] : plugin.actionSchemas) {
            if (!first) desc << ", ";
            first = false;
            desc << action;
            allActions.insert(action);
        }
        desc << "\n";

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

    if (allActions.empty()) {
        // Agent has no channels configured
        desc << "No channels are currently configured for this agent. "
             << "Use 'list' to check for available instances.\n";
    }

    desc << "Use 'describe' with a platform_id to get the full parameter schema for a specific adapter.\n";
    desc << "Use 'list' to see configured instances.";

    def.description = desc.str();

    // Build action enum
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
        true, "", actionEnum
    });

    def.parameters.push_back({
        "platform_id", "string",
        "Instance to use (e.g. 'bluesky:personal', 'vk:community'). "
        "Required for all actions except 'list'.",
        false
    });

    // Add composite-level parameters
    auto compositeParams = GetCompositeParameters();
    for (auto& p : compositeParams) {
        def.parameters.push_back(std::move(p));
    }

    // Add parameters only from included adapters
    std::set<std::string> seenParams;
    for (const auto& [pluginId, plugin] : GetAllPlugins()) {
        if (!includeTypes.empty() && !includeTypes.count(pluginId)) continue;
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
// Composite actions (list, describe)
// ============================================================================

ToolResult ChannelsTool::HandleCompositeAction(const ToolCall& call,
                                              const std::string& action) {
    ToolResult result;
    result.call_id = call.id;

    // --- list: enumerate configured instances ---
    if (action == "list") {
        Json::Value platforms(Json::arrayValue);

        // Get agent_id from call arguments for scoped channel listing
        auto callArgs = ParseArgs(call.arguments);
        std::string callAgentId = GetStringField(callArgs, "__agent_id", "");

        if (m_configStore) {
            // Use agent_id from call args if available, otherwise list all
            std::vector<std::string> agentIds;
            if (!callAgentId.empty()) {
                agentIds.push_back(callAgentId);
            }
            std::set<std::string> seen;

            for (const auto& id : agentIds) {
                if (seen.count(id)) continue;
                seen.insert(id);

                auto keys = m_configStore->ListKeys(id);
                std::map<std::string, Json::Value> instanceMap;

                for (const auto& key : keys) {
                    // Support both channels.X and legacy social.X prefixes
                    std::string rest;
                    if (key.size() >= 9 && key.substr(0, 9) == "channels.") {
                        rest = key.substr(9);
                    } else if (key.size() >= 7 && key.substr(0, 7) == "social.") {
                        rest = key.substr(7);
                    } else {
                        continue;
                    }
                    auto dotPos = rest.find('.');
                    if (dotPos == std::string::npos) continue;

                    std::string platformId = rest.substr(0, dotPos);
                    std::string field = rest.substr(dotPos + 1);

                    // Skip sensitive fields
                    if (field == "access_jwt" || field == "app_password" ||
                        field == "access_token") continue;

                    if (instanceMap.find(platformId) == instanceMap.end()) {
                        instanceMap[platformId] = Json::objectValue;
                        instanceMap[platformId]["platform_id"] = platformId;
                        auto colonPos = platformId.find(':');
                        std::string adapterType = (colonPos != std::string::npos)
                            ? platformId.substr(0, colonPos) : platformId;
                        instanceMap[platformId]["type"] = adapterType;
                        instanceMap[platformId]["adapter_registered"] = HasPlugin(adapterType);

                        // Include available actions for this adapter
                        const auto* plugin = GetPlugin(adapterType);
                        if (plugin) {
                            Json::Value actions(Json::arrayValue);
                            for (const auto& [actName, _] : plugin->actionSchemas) {
                                actions.append(actName);
                            }
                            instanceMap[platformId]["actions"] = actions;
                            Json::Value caps(Json::arrayValue);
                            for (const auto& cap : plugin->capabilities) {
                                caps.append(cap);
                            }
                            instanceMap[platformId]["capabilities"] = caps;
                        }
                    }
                    instanceMap[platformId][field] = m_configStore->Get(id, key);
                }

                for (auto& [pid, entry] : instanceMap) {
                    platforms.append(entry);
                }
            }
        }

        // Also include channels from ChannelManager (C++ connectors)
        // These are channels like discord, slack, whatsapp, irc that are
        // configured through the admin UI rather than agent config keys.
        if (m_channelManager) {
            std::set<std::string> seenPlatformIds;
            std::set<std::string> seenAdapterTypes;
            // Collect platform_ids AND adapter types from config store
            for (const auto& item : platforms) {
                if (item.isMember("platform_id")) {
                    seenPlatformIds.insert(item["platform_id"].asString());
                }
                if (item.isMember("type")) {
                    seenAdapterTypes.insert(item["type"].asString());
                }
            }

            auto channels = m_channelManager->ListChannels();
            for (const auto& ch : channels) {
                // Skip email — has its own dedicated tool, not a channel
                if (ch.type == "email") continue;

                // Filter by agent_id if set
                if (!callAgentId.empty()) {
                    std::string chAgentId = ch.config.get("agent_id", "").asString();
                    if (chAgentId != callAgentId) continue;
                }

                // Build platform_id: type + ":" + name (e.g. "discord:steadyfort")
                std::string platformId = ch.type + ":" + ch.name;
                // Dedup by platform_id or adapter type
                if (seenPlatformIds.count(platformId) || seenAdapterTypes.count(ch.type)) continue;

                Json::Value entry(Json::objectValue);
                entry["platform_id"] = platformId;
                entry["type"] = ch.type;
                entry["enabled"] = ch.enabled;
                entry["connected"] = m_channelManager->IsChannelConnected(ch.name);
                entry["adapter_registered"] = HasPlugin(ch.type);

                // Include adapter actions if registered
                const auto* plugin = GetPlugin(ch.type);
                if (plugin) {
                    Json::Value actions(Json::arrayValue);
                    for (const auto& [actName, _] : plugin->actionSchemas) {
                        actions.append(actName);
                    }
                    entry["actions"] = actions;
                    Json::Value caps(Json::arrayValue);
                    for (const auto& cap : plugin->capabilities) {
                        caps.append(cap);
                    }
                    entry["capabilities"] = caps;
                }

                // Merge non-sensitive config fields
                if (!ch.config.isNull() && ch.config.isObject()) {
                    for (const auto& key : ch.config.getMemberNames()) {
                        if (key == "bot_token" || key == "access_token" ||
                            key == "app_password" || key == "password") continue;
                        entry[key] = ch.config[key];
                    }
                }

                platforms.append(entry);
                seenPlatformIds.insert(platformId);
                seenPlatformIds.insert(ch.type);
            }
        }

        if (platforms.empty()) {
            for (const auto& pluginId : GetPluginIds()) {
                Json::Value entry(Json::objectValue);
                entry["type"] = pluginId;
                entry["platform_id"] = pluginId;
                const auto* plugin = GetPlugin(pluginId);
                if (plugin) {
                    Json::Value actions(Json::arrayValue);
                    for (const auto& [actName, _] : plugin->actionSchemas) {
                        actions.append(actName);
                    }
                    entry["actions"] = actions;
                }
                platforms.append(entry);
            }
        }

        result.success = true;
        result.output = ResultToJsonString(platforms);
        return result;
    }

    // --- describe: return per-adapter schema ---
    if (action == "describe") {
        auto args = ParseArgs(call.arguments);
        std::string platformId = GetStringField(args, "platform_id");

        if (platformId.empty()) {
            // Describe all adapters
            Json::Value adapters(Json::objectValue);
            for (const auto& [pluginId, plugin] : GetAllPlugins()) {
                Json::Value adapter(Json::objectValue);
                adapter["name"] = plugin.name;
                adapter["id"] = plugin.id;

                Json::Value caps(Json::arrayValue);
                for (const auto& cap : plugin.capabilities) {
                    caps.append(cap);
                }
                adapter["capabilities"] = caps;

                Json::Value actions(Json::objectValue);
                for (const auto& [actName, params] : plugin.actionSchemas) {
                    actions[actName] = ParamsToJson(params);
                }
                adapter["actions"] = actions;

                adapters[pluginId] = adapter;
            }

            result.success = true;
            result.output = ResultToJsonString(adapters);
            return result;
        }

        // Describe a specific adapter
        std::string adapterType = ExtractAdapterType(platformId);
        const auto* plugin = GetPlugin(adapterType);
        if (!plugin) {
            result.success = false;
            result.error = "no adapter registered for: " + adapterType;
            return result;
        }

        Json::Value adapter(Json::objectValue);
        adapter["name"] = plugin->name;
        adapter["id"] = plugin->id;

        Json::Value caps(Json::arrayValue);
        for (const auto& cap : plugin->capabilities) {
            caps.append(cap);
        }
        adapter["capabilities"] = caps;

        Json::Value actions(Json::objectValue);
        for (const auto& [actName, params] : plugin->actionSchemas) {
            actions[actName] = ParamsToJson(params);
        }
        adapter["actions"] = actions;

        result.success = true;
        result.output = ResultToJsonString(adapter);
        return result;
    }

    result.success = false;
    result.error = "unknown composite action: " + action;
    return result;
}

} // namespace animus::kernel

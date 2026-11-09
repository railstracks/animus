#include "animus_kernel/tools/ChannelsTool.h"
#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/ChannelManager.h"
#include "animus_kernel/ChannelState.h"
#include "animus_kernel/ChannelContextStore.h"
#include "animus_kernel/MediaVault.h"
#include "animus_kernel/DescribeService.h"
#include "animus_kernel/lua/HttpUpload.h"

#include <json/json.h>
#include <json/writer.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>
#include <map>
#include <unistd.h>

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


// ── Secret masking for channel config in tool output (#15 hardening) ──
// Live incident Aug 28: list_channels leaked the Discord bot_token — the
// config-store path's deny-list (access_jwt/app_password/access_token) had
// drifted from the ChannelManager path's (bot_token/access_token/…), and the
// token fell through the gap; the model then authed raw REST calls with it.
// One shared rule now. Substring-based so future config fields named
// *token*/*secret*/*password*/*jwt*/*api_key* are masked by default.
static bool IsSensitiveConfigKey(const std::string& key) {
    auto contains = [&](const char* needle) {
        return key.find(needle) != std::string::npos;
    };
    return contains("token") || contains("secret") ||
           contains("password") || contains("jwt") ||
           key == "api_key";
}

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

    // #209: resolve platform_id BEFORE the C++ bridge checks. Models
    // routinely omit it on minimal replies (the card names the action,
    // not the id) — without this, IRC calls skip the bridge entirely and
    // die in the Lua fallback as "unknown Lua handler error". Sources:
    //   1. this session's latest arrival (the platform being replied on);
    //   2. single-instance inference: exactly one IRC channel configured —
    //      unambiguous by definition.
    if (platformId.empty() && m_channelManager) {
        if (m_channelContextStore) {
            const std::string hoistSessionKey =
                GetStringField(preArgs, "__session_key", "");
            const std::string hoistAgentId =
                GetStringField(preArgs, "__agent_id", "");
            if (!hoistSessionKey.empty()) {
                auto latest = m_channelContextStore->LatestArrival(
                    hoistSessionKey, hoistAgentId);
                if (latest && !latest->platform_id.empty()) {
                    platformId = latest->platform_id;
                }
            }
        }
        if (platformId.empty()) {
            int ircCount = 0;
            std::string soleIrc;
            for (const auto& ch : m_channelManager->ListChannels()) {
                if (ch.type == "irc") {
                    ++ircCount;
                    soleIrc = ch.name;
                }
            }
            if (ircCount == 1) {
                platformId = "irc:" + soleIrc;
            }
        }
        if (!platformId.empty()) {
            preArgs["platform_id"] = platformId;
            adapterType = ExtractAdapterType(platformId);
        }
    }

    // C++ bridge: WhatsApp reply/send — no HTTP API; sends go through the
    // gateway outbox via ChannelManager (same path as auto-replies).
    if (adapterType == "whatsapp" && m_channelManager &&
        (action == "reply" || action == "send_message")) {
        ToolResult result;
        result.call_id = call.id;
        std::string chatId = GetStringField(preArgs, "chat_id");
        if (chatId.empty()) chatId = GetStringField(preArgs, "peer_id");
        std::string content = GetStringField(preArgs, "content");
        if (chatId.empty() || content.empty()) {
            result.success = false;
            result.error = "WhatsApp reply requires 'chat_id' (or 'peer_id') and 'content' parameters";
            return result;
        }
        ChannelManager::ReplyTarget rt;
        rt.channel_name = ExtractInstanceName(platformId);
        rt.channel_type = "whatsapp";
        rt.peer_id = chatId;
        m_channelManager->SendReply(rt, content);
        result.success = true;
        result.output = "Message sent to WhatsApp chat " + chatId;
        return result;
    }

    // C++ bridge: IRC actions that need ChannelManager access
    if (adapterType == "irc" && m_channelManager) {
        ToolResult result;
        result.call_id = call.id;
        if (action == "post" || action == "reply" || action == "send_action") {
            // Channel: explicit arg wins; the reply-target resolution above
            // fills it from the latest arrival's peer_id when omitted (the
            // IRC adapter sets peer_id to the #channel or DM nick).
            std::string target = GetStringField(preArgs, "channel");
            if (target.empty()) target = GetStringField(preArgs, "target");
            std::string content = GetStringField(preArgs, "content");
            if (target.empty() || content.empty()) {
                result.success = false;
                result.error = "IRC " + action + " requires 'channel' (or 'target') and 'content' parameters";
                result.call_id = call.id;
                return result;
            }
            ChannelManager::ReplyTarget rt;
            rt.channel_name = ExtractInstanceName(platformId);
            rt.channel_type = "irc";
            rt.irc_target = target;
            if (action == "send_action") {
                m_channelManager->SendAction(rt, content);
                result.success = true;
                result.output = "Action sent to " + target + " on IRC";
            } else {
                m_channelManager->SendReply(rt, content);
                result.success = true;
                result.output = "Message sent to " + target + " on IRC";
            }
            result.call_id = call.id;
            return result;
        }
        if (action == "join" || action == "part") {
            // #205: opt-in gate — channel roaming must be enabled per
            // instance (allow_join_part in the /channels admin form).
            const std::string instance = ExtractInstanceName(platformId);
            auto chState = m_channelManager->GetChannel(instance);
            const bool allowed = chState.has_value()
                && chState->config.get("allow_join_part", false).asBool();
            if (!allowed) {
                result.success = false;
                result.error = "IRC channel joining is disabled on this instance. "
                               "Ask the operator to enable it (allow_join_part in the /channels admin form).";
                return result;
            }
            std::string channel = GetStringField(preArgs, "channel");
            if (channel.empty()) channel = GetStringField(preArgs, "target");
            if (channel.empty()) {
                result.success = false;
                result.error = "IRC " + action + " requires a 'channel' parameter";
                return result;
            }
            ChannelManager::ReplyTarget rt;
            rt.channel_name = instance;
            rt.channel_type = "irc";
            rt.irc_target = channel;
            if (action == "join") {
                const std::string key = GetStringField(preArgs, "key");
                result.success = m_channelManager->JoinChannel(rt, key);
                result.output = result.success
                    ? "Joined " + channel + (key.empty() ? std::string() : " (with key)")
                    : "Join failed: invalid channel name or no connection";
            } else {
                const std::string farewell = GetStringField(preArgs, "farewell");
                result.success = m_channelManager->PartChannel(rt, farewell);
                result.output = result.success
                    ? "Left " + channel
                    : "Part failed: invalid channel name or no connection";
            }
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
    // ── #16: Reply-target resolution from session store ──────────
    // If this is a reply action and the caller didn't provide target IDs,
    // fill them from the ChannelContextStore's LatestArrival for this
    // session. The model no longer needs to echo IDs from message text —
    // the store holds the server-resolved values.
    //
    // Only fills MISSING fields — explicit args always win. This is the
    // trust boundary: if the model passes a post_id, it's the model's
    // choice; the store only provides defaults for omitted fields.
    ToolCall resolvedCall = call;
    if (m_channelContextStore && action == "reply") {
        std::string sessionKey = GetStringField(preArgs, "__session_key", "");
        std::string agentId = GetStringField(preArgs, "__agent_id", "");

        if (!sessionKey.empty()) {
            auto latest = m_channelContextStore->LatestArrival(sessionKey, agentId);
            if (latest) {
                Json::Value args = ParseArgs(call.arguments);
                bool modified = false;

                // Helper: fill a field only if it's absent or empty
                auto fillIfMissing = [&](const char* field,
                                         const std::string& val) {
                    if (val.empty()) return;
                    if (!args.isMember(field) ||
                        (args[field].isString() && args[field].asString().empty())) {
                        args[field] = val;
                        modified = true;
                    }
                };

                // Addressing first: platform_id routes the call to the adapter.
                // Models often omit it on minimal replies — fill from the
                // arrival so the call reaches the right platform.
                fillIfMissing("platform_id", latest->platform_id);

                // Bluesky: post_id (direct parent) + root_id (thread root);
                // chat arrivals fill the convo id instead (Aug 31 — chat
                // replies go through action=chat_send with convo_id).
                fillIfMissing("post_id", latest->post_id);
                fillIfMissing("root_id", latest->thread_root_id.empty()
                                             ? latest->post_id
                                             : latest->thread_root_id);
                if (latest->channel_type == "bluesky") {
                    fillIfMissing("convo_id", latest->peer_id);
                }

                // Discord: channel_id + message_id. Live-truth mapping (Aug 27
                // workstation verification): guild/wall messages carry the
                // channel id in post_id, DMs in peer_id. channel_name is the
                // configured instance name ("discord"), never a channel id.
                if (latest->channel_type == "discord") {
                    fillIfMissing("channel_id", latest->post_id.empty()
                                                     ? latest->peer_id
                                                     : latest->post_id);
                    fillIfMissing("message_id", latest->source_message_id);
                }

                // Email: thread_id
                fillIfMissing("thread_id", latest->email_thread_id);

                // IRC: channel target
                fillIfMissing("channel", latest->peer_id);

                // Telegram / WhatsApp: chat_id from peer_id (group arrivals
                // carry the chat jid in post_id — Wall-shaped targets)
                if (latest->channel_type == "telegram" ||
                    latest->channel_type == "whatsapp") {
                    fillIfMissing("chat_id", latest->peer_id.empty()
                                             ? latest->post_id
                                             : latest->peer_id);
                }

                // Slack: channel + thread target. thread_ts is filled ONLY
                // when the adapter declared one (in-thread arrival, or
                // threaded_replies=true threading on the source ts) — the
                // adapter's flag is authoritative, not the fill's default.
                if (latest->channel_type == "slack") {
                    fillIfMissing("channel_id", latest->peer_id.empty()
                                                 ? latest->post_id
                                                 : latest->peer_id);
                    fillIfMissing("thread_ts", latest->reply_parent_id);
                }

                // VK: wall comments answer a specific post (post_id, from
                // the generic fill above) and optionally a comment —
                // reply_to_comment from the arrival's reply_parent_id.
                if (latest->channel_type == "vk") {
                    fillIfMissing("reply_to_comment", latest->reply_parent_id);
                }

                // Moltbook: comments answer a post (post_id); nested
                // replies carry parent_id = source comment id.
                if (latest->channel_type == "moltbook") {
                    fillIfMissing("post_id", latest->post_id);
                    fillIfMissing("parent_id", latest->reply_parent_id);
                }

                if (modified) {
                    Json::StreamWriterBuilder wb;
                    wb.settings_["indentation"] = "";
                    resolvedCall.arguments = Json::writeString(wb, args);
                    preArgs = args;
                    platformId = GetStringField(preArgs, "platform_id");
                }
            }
        }
    }

    // #159 outbound media: resolve files[] into kernel-validated
    // _media_files descriptors. The model never supplies _media_files —
    // it is stripped and rebuilt here on every send_media call.
    std::vector<std::string> outboundTempFiles;
    if (action == "send_media") {
        Json::Value args = ParseArgs(resolvedCall.arguments);
        args.removeMember("_media_files");
        std::string resolveErr;
        if (!ResolveOutboundFiles(args, outboundTempFiles, resolveErr)) {
            for (const auto& t : outboundTempFiles) {
                std::error_code ec;
                std::filesystem::remove(t, ec);
            }
            ToolResult fail;
            fail.call_id = call.id;
            fail.success = false;
            fail.error = resolveErr;
            return fail;
        }
        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";
        resolvedCall.arguments = Json::writeString(wb, args);
    }

    auto result = CompositeTool::Execute(resolvedCall);

    // #158 pull-mode: vault attachments surfacing in poll digests. Push
    // lanes (Discord/Slack/Bluesky) vault at dispatch; agent-driven polls
    // (Mastodon) carry raw descriptors in their digest — rewrite them into
    // media:// refs here, before the agent reads the output.
    if (action == "poll" && result.success && m_mediaVault &&
        !result.output.empty() && result.output.front() == '{') {
        result.output = VaultPollAttachments(
            result.output, platformId,
            GetStringField(preArgs, "__agent_id", m_currentAgentId));
    }

    // #159: base64-sourced temporaries are dispatch-scoped — remove after
    // the adapter has uploaded them (or failed; nothing persists).
    if (!outboundTempFiles.empty()) {
        for (const auto& t : outboundTempFiles) {
            std::error_code ec;
            std::filesystem::remove(t, ec);
        }
    }

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

std::vector<std::string> ChannelsTool::GetPlatformIdsForAgent(const std::string& agentId) const {
    std::vector<std::string> result;

    if (agentId.empty()) return result;

    // 1. ChannelManager (C++ connectors)
    if (m_channelManager) {
        for (const auto& ch : m_channelManager->ListChannels()) {
            std::string chAgentId = ch.config.get("agent_id", "").asString();
            if (chAgentId == agentId) {
                result.push_back(ch.type + ":" + ch.name);
            }
        }
    }

    // 2. AgentConfigStore (Lua adapters)
    if (m_configStore) {
        m_configStore->WarmCache(agentId);
        auto keys = m_configStore->ListKeys(agentId);
        for (const auto& key : keys) {
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
            // Avoid duplicates
            if (std::find(result.begin(), result.end(), platformId) == result.end()) {
                result.push_back(platformId);
            }
        }
    }

    return result;
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

    // Build dynamic platform_id enum from configured channels
    auto platformIds = GetPlatformIdsForAgent(m_currentAgentId);
    std::vector<std::string> platformIdEnum;
    if (!platformIds.empty()) {
        platformIdEnum = platformIds;
    }

    std::string platformIdDesc;
    if (platformIds.empty()) {
        platformIdDesc = "Instance to use. Call 'list' to see configured instances. "
                         "Format: <type>:<name>";
    } else {
        platformIdDesc = "Instance to use. Configured for this agent: ";
        for (std::size_t i = 0; i < platformIds.size(); ++i) {
            if (i > 0) platformIdDesc += ", ";
            platformIdDesc += platformIds[i];
        }
        platformIdDesc += ". Use 'list' to see all.";
    }

    def.parameters.push_back({
        "platform_id", "string",
        platformIdDesc,
        false, "", platformIdEnum
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

std::string ChannelsTool::VaultPollAttachments(const std::string& output,
                                               const std::string& platformId,
                                               const std::string& agentId) {
    Json::Value digest;
    Json::CharReaderBuilder rb;
    std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    std::string errs;
    if (!reader->parse(output.data(), output.data() + output.size(),
                       &digest, &errs) ||
        !digest.isObject() || !digest.isMember("mentions") ||
        !digest["mentions"].isArray()) {
        return output;   // not a mention digest — pass through untouched
    }

    // Scope: the instance's own host (Mastodon media URLs are
    // instance-hosted proxies) + operator media_hosts when configured.
    std::vector<std::string> scope;
    if (m_configStore && !platformId.empty()) {
        const std::string instanceUrl = m_configStore->Get(
            agentId, "channels." + platformId + ".instance_url");
        const size_t sep = instanceUrl.find("://");
        std::string host = (sep == std::string::npos)
                               ? instanceUrl : instanceUrl.substr(sep + 3);
        const size_t slash = host.find('/');
        if (slash != std::string::npos) host = host.substr(0, slash);
        if (!host.empty()) scope.push_back(host);
    }
    if (m_channelManager) {
        auto ch = m_channelManager->GetChannel(ExtractInstanceName(platformId));
        if (ch && ch->config.isObject()) {
            const Json::Value extra =
                ch->config.get("media_hosts", Json::Value());
            if (extra.isArray()) {
                for (const auto& h : extra) {
                    const std::string s = h.asString();
                    if (!s.empty()) scope.push_back(s);
                }
            }
        }
    }

    for (auto& mention : digest["mentions"]) {
        if (!mention.isObject() || !mention.isMember("attachments") ||
            !mention["attachments"].isArray())
            continue;
        for (auto& e : mention["attachments"]) {
            if (!e.isObject()) continue;
            MediaVault::Request req;
            req.url = e.get("url", "").asString();
            if (req.url.empty()) continue;
            req.kind = e.get("kind", "file").asString();
            req.filename = e.get("filename", "").asString();
            req.mime = e.get("mime", "").asString();
            req.dedupe_key =
                e.get("dedupe_key", e.get("id", "").asString()).asString();
            req.alt_text = e.get("alt_text", "").asString();
            req.dimensions = e.get("dimensions", "").asString();
            req.duration = e.get("duration", "").asString();
            req.scope_hosts = scope;
            const MediaVault::Result r = m_mediaVault->Acquire(req);
            Json::Value o;
            if (r.ok) {
                o["ref"] = r.ref;
                o["sha256"] = r.sha256;
                o["kind"] = req.kind;
                o["mime"] = r.mime;
                o["filename"] = r.filename;
                o["size"] = static_cast<Json::UInt64>(r.size);
                if (r.deduped) o["deduped"] = true;
                if (!req.alt_text.empty()) o["alt_text"] = req.alt_text;
                if (!req.dimensions.empty()) o["dimensions"] = req.dimensions;
                if (!req.duration.empty()) o["duration"] = req.duration;
            } else {
                o["url"] = req.url;
                o["error"] = r.error;   // loud: the digest shows the refusal
            }
            e = o;
        }
    }
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, digest);
}

// Base64 decode for inline files[] payloads (local copy — the kernel
// keeps per-tool decoders; returns empty on invalid input, callers
// reject loudly).
static std::string Base64Decode(const std::string& encoded) {
    static const std::string kChars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(encoded.size() / 4 * 3);
    int val = 0, valb = -8;
    for (const char c : encoded) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        const std::size_t idx = kChars.find(c);
        if (idx == std::string::npos) return {};
        val = (val << 6) + static_cast<int>(idx);
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// ============================================================================
// #159 outbound media: send_media files[] resolution
// ============================================================================

bool ChannelsTool::ResolveOutboundFiles(Json::Value& args,
                                        std::vector<std::string>& tempFiles,
                                        std::string& error) {
    if (!args.isMember("files") || !args["files"].isArray() ||
        args["files"].empty()) {
        error = "send_media requires files[] (entries: {ref|path|data, "
                "filename?, mime?, alt_text?})";
        return false;
    }

    Json::Value descriptors(Json::arrayValue);
    for (const auto& entry : args["files"]) {
        if (!entry.isObject()) {
            error = "files[] entries must be objects";
            return false;
        }

        const std::string ref = entry.get("ref", "").asString();
        const std::string rawPath = entry.get("path", "").asString();
        const std::string data = entry.get("data", "").asString();

        const int sourceCount =
            (!ref.empty()) + (!rawPath.empty()) + (!data.empty());
        if (sourceCount == 0) {
            error = "files[] entry needs one of ref (media://...), path, or data";
            return false;
        }
        if (sourceCount > 1) {
            error = "files[] entry must use exactly one of ref|path|data";
            return false;
        }

        std::string path, filename, mime, altText;
        std::uint64_t size = 0;
        bool refImage = false;
        if (!ref.empty()) {
            // media://<sha256> — vault-resolved inbound object
            if (!m_mediaVault) {
                error = "media vault unavailable — media:// refs not resolvable";
                return false;
            }
            auto rec = m_mediaVault->Resolve(ref);
            if (!rec) {
                error = "unknown media ref: " + ref;
                return false;
            }
            path = m_mediaVault->PathFor(rec->sha256).string();
            filename = entry.get("filename", rec->filename).asString();
            mime = entry.get("mime", rec->mime).asString();
            altText = entry.get("alt_text", rec->alt_text).asString();
            size = rec->size;
            refImage = rec->kind == "image";
        } else if (!rawPath.empty()) {
            // local path — must live inside the outbound root (dataDir)
            if (m_outboundRoot.empty()) {
                error = "outbound root not configured — path sources denied";
                return false;
            }
            const std::filesystem::path p(rawPath);
            if (!PathWithinRoots(p, {m_outboundRoot})) {
                error = "path outside allowed tree: " + rawPath;
                return false;
            }
            path = rawPath;
            filename = entry.get("filename", p.filename().string()).asString();
            mime = entry.get("mime", "").asString();
            altText = entry.get("alt_text", "").asString();
            std::error_code statEc;
            const std::uint64_t pathSize =
                std::filesystem::file_size(p, statEc);
            if (statEc) {
                error = "cannot stat path: " + rawPath + " ("
                        + statEc.message() + ")";
                return false;
            }
            size = pathSize;
        } else {
            // inline base64 — decode to a dispatch-scoped temp file
            if (m_outboundRoot.empty()) {
                error = "outbound root not configured — data sources denied";
                return false;
            }
            std::string bytes = Base64Decode(data);
            if (bytes.empty()) {
                error = "invalid or empty base64 payload in files[] entry";
                return false;
            }
            if (static_cast<std::uint64_t>(bytes.size()) > kUploadByteCap) {
                error = "size cap exceeded: " + std::to_string(bytes.size()) +
                        " bytes > " + std::to_string(kUploadByteCap);
                return false;
            }
            const std::filesystem::path tmpDir =
                m_outboundRoot / "media" / "tmp";
            std::error_code ec;
            std::filesystem::create_directories(tmpDir, ec);
            char name[64];
            std::snprintf(name, sizeof(name), "out-%d-%lld",
                          static_cast<int>(::getpid()),
                          static_cast<long long>(
                              std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()));
            const std::filesystem::path tmp = tmpDir / name;
            {
                std::ofstream f(tmp, std::ios::binary);
                f.write(bytes.data(),
                        static_cast<std::streamsize>(bytes.size()));
                if (!f) {
                    error = "temp write failed: " + tmp.string();
                    return false;
                }
            }
            tempFiles.push_back(tmp.string());
            path = tmp.string();
            filename = entry.get("filename", "attachment.bin").asString();
            mime = entry.get("mime", "").asString();
            altText = entry.get("alt_text", "").asString();
            size = static_cast<std::uint64_t>(bytes.size());
        }

        Json::Value d;
        // #161: auto-alt — fill empty alt on outbound images through the
        // describe engine (local-first). Explicit alt (caller-supplied or
        // vault-recorded platform alt) always wins; never overwritten.
        // Describe failures never block the send.
        bool machineAlt = false;
        std::string altError;
        if ((refImage || mime.rfind("image/", 0) == 0) && altText.empty() &&
            m_describeService) {
            const auto r = m_describeService->DescribePath(
                path, mime, DescribeService::kAltTextPrompt);
            if (r.ok) {
                altText = r.text;
                machineAlt = true;
            } else {
                altError = r.error;
            }
        }
        d["path"] = path;
        d["filename"] = filename;
        if (!mime.empty()) d["mime"] = mime;
        if (!altText.empty()) d["alt_text"] = altText;
        if (machineAlt) d["alt_text_source"] = "machine";
        if (!altError.empty())
            d["alt_error"] = altError.substr(0, 200);
        // exact byte size — slack's getUploadURLExternal requires length
        d["size"] = static_cast<Json::UInt64>(size);
        descriptors.append(d);
    }

    args["_media_files"] = descriptors;
    return true;
}

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

                    // Skip sensitive fields (shared rule — see helper above)
                    if (IsSensitiveConfigKey(field)) continue;

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
                        if (IsSensitiveConfigKey(key)) continue;
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

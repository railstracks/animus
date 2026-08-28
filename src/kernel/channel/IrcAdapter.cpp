#include "animus_kernel/Log.h"
#include "animus_kernel/ChannelAdapters.h"
#include "animus_kernel/ChannelContext.h"
#include "animus_kernel/ChannelHelpers.h"

#include <iostream>

#include "animus_kernel/ChannelState.h"
#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/ChannelManager.h"
#include "animus_kernel/interfaces/IrcInterface.h"

#include <json/json.h>

namespace animus::kernel {

using namespace channel_detail;

// ============================================================================
// IrcAdapter
// ============================================================================

bool IrcAdapter::Start(const ChannelState& state, std::string* error) {
    const Json::Value& cfg = state.config;

    IrcInterfaceRuntime::Config ircCfg;
    ircCfg.host = GetString(cfg, "host");
    ircCfg.port = GetInt(cfg, "port", 6667);
    ircCfg.serverPassword = GetString(cfg, "server_password");
    ircCfg.nick = GetString(cfg, "nick");
    ircCfg.username = GetString(cfg, "username", "animus");
    ircCfg.realname = GetString(cfg, "realname", "Animus Agent");

    if (cfg.isMember("channels") && cfg["channels"].isArray()) {
        for (const auto& ch : cfg["channels"]) {
            IrcInterfaceRuntime::ChannelConfig entry;
            entry.name = GetString(ch, "name");
            entry.key = GetString(ch, "key");
            ircCfg.channels.push_back(entry);
        }
    }

    if (cfg.isMember("reconnect") && cfg["reconnect"].isObject()) {
        ircCfg.reconnectEnabled = cfg["reconnect"].get("enabled", true).asBool();
        ircCfg.reconnectInitialDelayMs = cfg["reconnect"].get("initial_delay_ms", 1000).asInt();
        ircCfg.reconnectMaxDelayMs = cfg["reconnect"].get("max_delay_ms", 60000).asInt();
    }

    ircCfg.useTls = cfg.get("use_tls", false).asBool();
    ircCfg.forceIpv4 = cfg.get("force_ipv4", true).asBool();

    m_channelName = state.name;
    m_botNick = ircCfg.nick;

    m_runtime = std::make_shared<IrcInterfaceRuntime>(
        state.name, ircCfg,
        [this](const std::string& nick, const std::string& target,
               const std::string& message, bool isNotice) {
            OnMessage(nick, target, message, isNotice);
        },
        [this](bool connected, const std::string&) {
            OnStatus(connected, 0);
        });

    m_runtime->Start();
    ALOG_INFO("irc", "Started: " << state.name
              << " (" << ircCfg.host << ":" << ircCfg.port << ")");
    return true;
}

void IrcAdapter::Stop() {
    std::shared_ptr<IrcInterfaceRuntime> runtime;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        runtime = std::move(m_runtime);
    }
    if (runtime) runtime->Stop();
    ALOG_INFO("irc", "Stopped: " << m_channelName);
}

void IrcAdapter::SendReply(const ChannelReplyTarget& target, const std::string& text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_runtime) {
        m_runtime->SendPrivmsg(target.irc_target, text);
    }
}

bool IrcAdapter::IsConnected() const {
    // TODO: track connected state from OnStatus callback
    return m_runtime != nullptr;
}

void IrcAdapter::OnMessage(const std::string& sourceNick,
                            const std::string& target,
                            const std::string& message,
                            bool isNotice) {
    if (sourceNick.empty() || message.empty()) return;
    if (sourceNick == m_botNick) return;

    // Config-based filtering (respond_to_channel_activity, respond_to_direct_messages, etc.)
    // TODO: re-add config filtering. The original code read from ChannelManager::m_channels.
    // Each adapter now needs to store its config locally at Start() time.
    bool respondToChannel = true;
    bool respondToDm = true;
    bool respondToNotices = false;

    if (isNotice && !respondToNotices) return;

    const bool channelMessage = !target.empty() &&
        (target[0] == '#' || target[0] == '&' || target[0] == '!' || target[0] == '+');
    if (channelMessage && !respondToChannel) return;
    if (!channelMessage && !respondToDm) return;

    std::string conversationId = channelMessage
        ? ("channel:" + target) : ("dm:" + sourceNick);

    std::string agentId; // TODO: read from stored config
    std::string sessionType = "irc";
    std::string sessionKey = sessionType + ":" + m_channelName + ":" + conversationId;

    // Inline reply hints removed (#15): delivery semantics live in the
    // Channel Context card (system message), not the user turn.
    std::string prompt;
    if (channelMessage) {
        prompt = "IRC message from " + sourceNick + " in " + target + ":\n" + message;
    } else {
        prompt = "IRC private message from " + sourceNick + ":\n" + message;
    }

    // Origin metadata (#15/#42): adapter-supplied source map. The card prints
    // present keys only ("User: …", "Channel: …"). IRC has no platform ids;
    // the channel key is omitted for DMs.
    Json::Value origin;
    origin["user"] = sourceNick;
    if (channelMessage) origin["channel"] = target;
    Json::Value dispatchMeta;
    dispatchMeta["origin"] = origin;
    // Channel-definition good form (#42): adapters state their delivery
    // semantics explicitly rather than relying on the kernel default.
    dispatchMeta["reply_instructions"] =
        "Your text replies are delivered automatically; do NOT use the "
        "channels tool to send your reply";
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    const std::string metadata = Json::writeString(wb, dispatchMeta);

    ChannelReplyTarget replyTarget;
    replyTarget.channel_name = m_channelName;
    replyTarget.channel_type = "irc";
    replyTarget.type = ChannelReplyTarget::Chat;
    replyTarget.irc_target = channelMessage ? target : sourceNick;
    replyTarget.interface_name = m_channelName;

    m_ctx.dispatch(agentId, sessionKey, prompt, sessionType, replyTarget, metadata);
}

void IrcAdapter::OnStatus(bool connected, std::uint64_t eventUnixMs) {
    ALOG_INFO("irc", "" << m_channelName << ": "
              << (connected ? "connected" : "disconnected"));
}

} // namespace animus::kernel

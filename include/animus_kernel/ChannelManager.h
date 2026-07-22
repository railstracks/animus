#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <json/value.h>

#include "animus_kernel/ChannelState.h"

namespace drogon {
class WebSocketClient;
using WebSocketClientPtr = std::shared_ptr<WebSocketClient>;
}

namespace trantor {
class EventLoop;
}

namespace animus::kernel {

class AgentConfigStore;
class HttpClient;
class SessionManager;
class ChainRunner;
class AgentStore;
struct KernelConfig;

// Forward declarations
namespace telegram {
    struct Message;
    class TelegramBotApi;
    struct Update;
}

class IrcInterfaceRuntime;

// ============================================================================
// ChannelRouter — routes inbound events to agent sessions
//
// Maintains a registry mapping conversation identifiers to session keys:
//   channel_name + routing_key → session key
//
// When the agent creates a reply, the routing info carries back through
// ReplyTarget so the connector knows where to send the response.
// ============================================================================

class ChannelRouter {
public:
    struct RoutingEntry {
        std::string session_key;
        std::chrono::steady_clock::time_point created;
        std::string agent_id;
        std::string channel_name;
    };

    ChannelRouter() = default;
    ~ChannelRouter() = default;

    void Register(const std::string& channelName,
                  const std::string& routingKey,
                  const std::string& sessionKey,
                  const std::string& agentId);

    std::optional<RoutingEntry> Lookup(const std::string& channelName,
                                        const std::string& routingKey) const;

    void PruneExpired(std::chrono::seconds ttl);

private:
    std::unordered_map<std::string, RoutingEntry> m_entries;
    mutable std::mutex m_mutex;

    static std::string MakeKey(const std::string& channelName,
                               const std::string& routingKey);
};

// ============================================================================
// ChannelManager — unified manager for all communication channels
//
// Owns connector runtimes (IRC sockets, Telegram/VK long poll threads, etc.)
// and routes inbound messages to agent sessions via a unified dispatch path.
//
// Storage: AgentConfigStore (SQLite) under channel.{name}.* keys.
// Each channel stores: type, enabled, config (JSON blob).
// ============================================================================

class ChannelManager {
public:
    // Auto-reply context — tells the dispatcher how to route the
    // assistant's text response back to the originating conversation.
    struct ReplyTarget {
        std::string channel_name;    // Channel instance name
        std::string channel_type;    // "irc", "telegram", "vk", "bluesky", etc.
        enum Type { Chat, Wall } type{Chat};
        std::string peer_id;         // Chat: peer/user/chat ID to send to
        std::string post_id;         // Wall: post ID to comment on
        std::string reply_to_comment;// Wall: comment ID for threading
        std::string group_id;        // VK wall: owner group ID
        // IRC-specific
        std::string irc_target;      // IRC: channel or nick to send to
        std::string interface_name;  // IRC: interface name for SendPrivmsg
        // Email-specific
        std::string email_thread_id;   // AgentMail thread ID for reply threading
        std::string email_inbox_id;    // Which inbox to send from
    };

    // Callback to invoke the chain runner on a (possibly new) session.
    using DispatchCallback = std::function<void(
        const std::string& agentId,
        const std::string& sessionKey,
        const std::string& message,
        const std::string& sessionType,
        const ReplyTarget& replyTarget)>;

    // Callback to log a message to session history without triggering a chain.
    using LogCallback = std::function<void(
        const std::string& agentId,
        const std::string& sessionKey,
        const std::string& message,
        const std::string& sessionType)>;

    ChannelManager(HttpClient& httpClient,
                   AgentConfigStore* configStore,
                   DispatchCallback dispatch,
                   LogCallback logCallback);
    ~ChannelManager();

    ChannelManager(const ChannelManager&) = delete;
    ChannelManager& operator=(const ChannelManager&) = delete;

    // --- Channel CRUD ---
    std::vector<ChannelState> ListChannels() const;
    std::optional<ChannelState> GetChannel(const std::string& name) const;
    bool CreateChannel(const ChannelState& state, std::string* error);
    bool UpdateChannelConfig(const std::string& name,
                             const Json::Value& config,
                             std::string* error);
    bool DeleteChannel(const std::string& name, std::string* error);
    bool SetChannelEnabled(const std::string& name, bool enabled, std::string* error);

    // --- Lifecycle ---
    // Load channels from config store, start enabled connectors
    bool Initialize();

    // Stop all connectors
    void Shutdown();

    // Restart a specific channel (after config change)
    bool RestartChannel(const std::string& name, std::string* error);

    // --- Runtime status ---
    bool IsChannelConnected(const std::string& name) const;

    // --- WhatsApp-specific ---
    std::string GetWhatsAppQrUrl(const std::string& name) const;

    // --- IRC-specific: send PRIVMSG ---
    bool SendIrcPrivmsg(const std::string& channelName,
                        const std::string& target,
                        const std::string& message);

    // --- Send auto-reply via the right connector ---
    void SendReply(const ReplyTarget& target, const std::string& text);

    // --- Migration ---
    // Migrate from legacy interfaces.json + social.* keys
    int MigrateFromLegacy();

    // --- Validation ---
    static bool ValidateConfig(const std::string& type,
                               const Json::Value& config,
                               std::string* error);

    /// Sync credential keys from a ChannelState.config JSON blob into the
    /// AgentConfigStore so Lua adapters can read them via config.get().
    /// Keys are written as channels.<platform_id>.<key>.
    void SyncChannelCredentialsToConfigStore(
        const std::string& name,
        const std::string& type,
        const Json::Value& config);

private:
    // --- Per-connector runtime state ---
    // Outbound message queue entry for WhatsApp gateway
    struct OutboundMessage {
        std::string jid;
        std::string baseJid;   // Base JID (without device) for message 'to' attr
        std::string text;
        bool is_group = false;
    };

    struct PollerState {
        std::string channel_name;
        std::string channel_type;
        Json::Value config;

        // VK Long Poll state
        std::string lp_server;
        std::string lp_key;
        std::string lp_ts;

        // Telegram Long Poll state
        int64_t last_update_id{0};

        // Agent association
        std::string agent_id{};  // Agent to dispatch to

        // VK state
        std::string group_id;        // VK: group_id (loaded from config)

        // Backoff
        int consecutive_errors{0};
        std::chrono::steady_clock::time_point next_attempt;

        // Event deduplication
        std::set<int64_t> seenEventIds;
        static constexpr size_t kMaxSeenIds = 200;

        // Email WebSocket state
        bool ws_connected{false};               // True when WS is connected
        std::set<std::string> seenEventStrIds;  // String-based dedup for WS events
        std::chrono::steady_clock::time_point last_ws_event; // Liveness tracking

        // Discord Gateway state
        std::string discord_bot_user_id;

        // WhatsApp state
        std::string whatsapp_qr_url;
        std::mutex whatsapp_qr_mutex;

        std::thread thread;
        bool active{false};
    };

    // IRC runtime wrapper
    struct IrcRuntime {
        std::string channel_name;
        std::shared_ptr<IrcInterfaceRuntime> runtime;
    };

    // --- Connector threads ---
    void VkLongPollLoop(PollerState* state);
    void TelegramLongPollLoop(PollerState* state);

    // --- VK event processing ---
    void ProcessVKEvent(PollerState* state, const std::string& eventType,
                        const std::string& objectJson);
    void ProcessVKMessageNew(PollerState* state, const std::string& objectJson);
    void ProcessVKWallPostNew(PollerState* state, const std::string& objectJson);
    void ProcessVKWallReplyNew(PollerState* state, const std::string& objectJson);

    // --- Telegram event processing ---
    void ProcessTelegramMessage(PollerState* state,
                                const struct telegram::Message& msg);
    void ProcessTelegramChatMemberUpdate(PollerState* state,
                                          const struct telegram::Update& update);

    // --- Email connector ---
    void EmailWebSocketLoop(PollerState* state);
    void EmailPollLoop(PollerState* state);  // Fallback
    void DiscordGatewayLoop(PollerState* state);
    void WhatsAppGatewayLoop(PollerState* state);
    void WhatsAppGatewayLoopInner(PollerState* state);
    void SlackPollingLoop(PollerState* state);
    void SlackSocketModeLoop(PollerState* state);
    void NextcloudTalkPollLoop(PollerState* state);
    void ProcessEmailMessage(PollerState* state,
                              const std::string& threadId,
                              const std::string& messageId,
                              const std::string& sender,
                              const std::string& subject,
                              const std::string& bodyText);
    void ProcessDiscordMessage(PollerState* state,
                               const std::string& channelId,
                               const std::string& messageId,
                               const std::string& authorId,
                               const std::string& authorUsername,
                               const std::string& content);

    // --- Session dispatch ---
    void DispatchToSession(PollerState* state,
                           const std::string& routingKey,
                           const std::string& message,
                           const std::string& sessionType);

    // Log message to session history without triggering an agent chain.
    // Used for channel tracking — messages are stored as context for later mentions.
    void LogToSession(PollerState* state,
                      const std::string& routingKey,
                      const std::string& message,
                      const std::string& sessionType);

    // --- VK helpers ---
    bool FetchVkLongPollServer(PollerState* state);
    std::string BuildVkLongPollUrl(const PollerState* state) const;
    std::unordered_map<std::string, std::string> ResolveVKUsers(
        PollerState* state, const std::vector<std::string>& userIds);
    std::string FetchVKChatHistory(PollerState* state,
                                   const std::string& peerId,
                                   int count);

    // --- IRC helpers ---
    void StartIrcChannel(const ChannelState& state);
    void StopIrcChannel(const std::string& name);
    void SyncIrcMessageCallback(
        const std::string& channelName,
        const std::string& botNick,
        const std::string& sourceNick,
        const std::string& target,
        const std::string& message,
        bool isNotice);
    void SyncIrcStatusCallback(
        const std::string& channelName,
        bool connected,
        std::uint64_t eventUnixMs);

    // --- Async restart queue ---
    // Channel restarts are enqueued and processed by a background thread
    // to avoid blocking HTTP handlers (which caused Drogon response errors).
    struct PendingRestart {
        std::string channel_name;
        ChannelState state;
    };
    std::mutex m_restartMutex;
    std::vector<PendingRestart> m_pendingRestarts;
    std::atomic<bool> m_restartThreadRunning{false};
    void EnqueueRestart(const std::string& name, const ChannelState& state);
    void ProcessPendingRestarts();

    // --- Config helpers ---
    void LoadChannelsFromConfigStore();
    void StartChannel(const ChannelState& state);
    void StopChannel(const std::string& name);
    std::string GetConfigString(const std::string& name,
                                const std::string& key,
                                const std::string& defaultVal = "") const;

    HttpClient& m_httpClient;
    AgentConfigStore* m_configStore;
    ChannelRouter m_router;
    DispatchCallback m_dispatch;
    LogCallback m_logCallback;

    // All channel states (loaded from config store)
    std::unordered_map<std::string, ChannelState> m_channels;
    mutable std::mutex m_channelsMutex;

    // Poller runtimes (VK, Telegram, etc.)
    std::unordered_map<std::string, std::unique_ptr<PollerState>> m_pollers;
    mutable std::mutex m_pollersMutex;

    // IRC runtimes
    std::unordered_map<std::string, IrcRuntime> m_ircRuntimes;
    mutable std::mutex m_ircMutex;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
};

} // namespace animus::kernel

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
#include "animus_kernel/IChannelAdapter.h"
#include "animus_kernel/ChannelAdapters.h"

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
    // ReplyTarget and dispatch callbacks are now defined in IChannelAdapter.h
    // as ChannelReplyTarget, ChannelDispatchCallback, ChannelLogCallback.
    // These aliases preserve backward compatibility for existing callers.
    using ReplyTarget = ChannelReplyTarget;
    using DispatchCallback = ChannelDispatchCallback;
    using LogCallback = ChannelLogCallback;

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

    // --- WhatsApp-specific (legacy, reads from poller state) ---
    std::string GetWhatsAppQrUrl(const std::string& name) const;

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

    // --- Legacy connector threads (Discord/WhatsApp) ---
    // These remain as ChannelManager methods until adapter migration.
    void DiscordGatewayLoop(PollerState* state);
    void WhatsAppGatewayLoop(PollerState* state);
    void WhatsAppGatewayLoopInner(PollerState* state);
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

    // Poller runtimes — legacy path for Discord/WhatsApp only
    std::unordered_map<std::string, std::unique_ptr<PollerState>> m_pollers;
    mutable std::mutex m_pollersMutex;

    // Adapter instances — for connectors migrated to IChannelAdapter
    std::unordered_map<std::string, std::unique_ptr<IChannelAdapter>> m_adapters;
    std::unordered_map<std::string, std::string> m_adapterTypes; // name → type
    mutable std::mutex m_adaptersMutex;

    // Shared context for adapters
    std::unique_ptr<ChannelContext> m_channelCtx;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
};

} // namespace animus::kernel

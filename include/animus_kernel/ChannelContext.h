#pragma once

#include <functional>
#include <mutex>
#include <string>

#include <json/value.h>

#include "animus_kernel/IChannelAdapter.h"

namespace animus::kernel {

class HttpClient;
class AgentConfigStore;
class ChannelRouter;

/// Shared infrastructure passed to all channel adapters.
/// Provides the dependencies adapters need without coupling them
/// to ChannelManager internals.
struct ChannelContext {
    HttpClient& httpClient;
    AgentConfigStore* configStore = nullptr;
    ChannelRouter& router;

    // Callbacks for routing inbound messages to agent sessions
    ChannelDispatchCallback dispatch;
    ChannelLogCallback logCallback;
};

/// Per-adapter runtime state for poller-based connectors.
/// Shared across Telegram, VK, Email, Discord, WhatsApp, Slack, Nextcloud.
struct PollerRuntime {
    std::string channel_name;
    std::string channel_type;
    Json::Value config;
    std::string agent_id;

    // Long-poll state (VK + Telegram)
    std::string lp_server;
    std::string lp_key;
    std::string lp_ts;
    int64_t last_update_id{0};

    // VK
    std::string group_id;

    // Backoff
    int consecutive_errors{0};
    std::chrono::steady_clock::time_point next_attempt;

    // Event deduplication
    std::set<int64_t> seenEventIds;
    static constexpr size_t kMaxSeenIds = 200;

    // Email WebSocket state
    bool ws_connected{false};
    std::set<std::string> seenEventStrIds;
    std::chrono::steady_clock::time_point last_ws_event;

    // Discord Gateway state
    std::string discord_bot_user_id;

    // WhatsApp state
    std::string whatsapp_qr_url;
    std::mutex whatsapp_qr_mutex;

    std::thread thread;
    std::atomic<bool> active{false};

    /// Check and record an event ID. Returns true if this is a new (unseen) event.
    bool RememberEvent(int64_t id) {
        if (seenEventIds.count(id)) return false;
        if (seenEventIds.size() >= kMaxSeenIds) {
            // Drop oldest ~half to avoid unbounded growth
            size_t toRemove = seenEventIds.size() / 2;
            auto it = seenEventIds.begin();
            for (size_t i = 0; i < toRemove; ++i) { it = seenEventIds.erase(it); }
        }
        seenEventIds.insert(id);
        return true;
    }

    /// Check and record a string event ID (for WS-based connectors).
    bool RememberEventStr(const std::string& id) {
        if (seenEventStrIds.count(id)) return false;
        seenEventStrIds.insert(id);
        return true;
    }
};

/// Dispatch helpers available to all adapters.
struct ChannelDispatch {
    /// Route an inbound message to an agent session (triggers chain).
    static void Dispatch(ChannelContext& ctx,
                         PollerRuntime* state,
                         const std::string& routingKey,
                         const std::string& message,
                         const std::string& sessionType);

    /// Log a message to session history without triggering a chain.
    static void Log(ChannelContext& ctx,
                    PollerRuntime* state,
                    const std::string& routingKey,
                    const std::string& message,
                    const std::string& sessionType);
};

} // namespace animus::kernel

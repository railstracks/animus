#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace animus::kernel {

// Forward declarations for Telegram types (full defs in TelegramTypes.h / TelegramBotApi.h)
namespace telegram {
    struct Message;
    class TelegramBotApi;
    struct Update;
}

class AgentConfigStore;
class HttpClient;
class SessionManager;
class ChainRunner;
class AgentStore;
struct KernelConfig;

// ============================================================================
// SocialEventRouter — routes inbound social events to agent sessions
//
// Maintains a registry mapping conversation identifiers to session keys:
//   peer_id  → session key  (for chat/DM conversations)
//   post_id  → session key  (for wall post threads)
//
// When the agent creates a post, SocialTool registers the post_id here
// so inbound replies route back to the originating session.
// ============================================================================

class SocialEventRouter {
public:
    struct RoutingEntry {
        std::string session_key;       // connector:conversation_id:thread_id
        std::chrono::steady_clock::time_point created;
        std::string agent_id;
        std::string platform_id;       // e.g. "vk:community"
    };

    SocialEventRouter(AgentConfigStore* configStore);
    ~SocialEventRouter() = default;

    // --- Session registry ---
    // Register a routing key → session mapping
    void RegisterPeer(const std::string& platformId, const std::string& peerId,
                      const std::string& sessionKey, const std::string& agentId);
    void RegisterPost(const std::string& platformId, const std::string& postId,
                      const std::string& sessionKey, const std::string& agentId);

    // Look up routing entries
    std::optional<RoutingEntry> LookupPeer(const std::string& platformId,
                                           const std::string& peerId) const;
    std::optional<RoutingEntry> LookupPost(const std::string& platformId,
                                           const std::string& postId) const;

    // Remove expired entries (called periodically)
    void PruneExpired(std::chrono::seconds ttl);

    // Persist to / load from config store
    void SaveState();
    void LoadState();

private:
    AgentConfigStore* m_configStore;

    // In-memory routing tables
    // Key: "platformId:peerId" or "platformId:postId"
    std::unordered_map<std::string, RoutingEntry> m_peerMap;
    std::unordered_map<std::string, RoutingEntry> m_postMap;
    mutable std::mutex m_mutex;

    static std::string MakePeerKey(const std::string& platformId,
                                   const std::string& peerId);
    static std::string MakePostKey(const std::string& platformId,
                                   const std::string& postId);
};

// ============================================================================
// SocialEventPoller — manages Long Poll / REST polling for social instances
//
// Each configured social instance with polling.enabled=true gets a poller.
// VK instances use Long Poll (groups.getLongPollServer → hold → re-poll).
// Future: Bluesky uses REST polling for notifications.
//
// Inbound events are routed through SocialEventRouter to agent sessions.
// The agent session receives a formatted prompt with resolved user names.
// ============================================================================

class SocialEventPoller {
public:
    struct PollerConfig {
        std::string platform_id;       // e.g. "vk:community"
        std::string adapter_type;      // e.g. "vk"
        std::string method;            // "long_poll" or "rest"
        int wait_seconds{25};          // Long Poll hold time
        int interval_seconds{900};     // REST poll interval
        std::chrono::seconds session_ttl{86400};  // Session expiry
        std::vector<std::string> events;  // Event types to handle
    };

    // Auto-reply context — tells the dispatcher how to route the
    // assistant's text response back to the originating conversation.
    struct ReplyTarget {
        std::string platform_id;   // e.g. "vk:personal"
        std::string adapter_type;  // e.g. "vk"
        enum Type { Chat, Wall } type{Chat};
        std::string peer_id;       // For chat: peer_id to send to
        std::string post_id;       // For wall: post_id to comment on
        std::string reply_to_comment; // For wall: comment_id to reply to (threaded)
        std::string group_id;      // For wall: needed for owner_id
    };

    // Callback to invoke the chain runner on a (possibly new) session.
    // The ReplyTarget tells the dispatcher how to auto-reply.
    using DispatchCallback = std::function<void(
        const std::string& agentId,
        const std::string& sessionKey,
        const std::string& message,
        const std::string& sessionType,
        const ReplyTarget& replyTarget)>;

    SocialEventPoller(
        HttpClient& httpClient,
        AgentConfigStore* configStore,
        SocialEventRouter& router,
        DispatchCallback dispatch);
    ~SocialEventPoller();

    SocialEventPoller(const SocialEventPoller&) = delete;
    SocialEventPoller& operator=(const SocialEventPoller&) = delete;

    // Configure a poller for a specific instance
    void AddInstance(const PollerConfig& config);

    // Start all configured pollers
    bool Start();

    // Stop all pollers
    void Stop();

    bool IsRunning() const { return m_running; }

private:
    // Per-instance poller state
    struct InstanceState {
        PollerConfig config;
        std::string access_token;
        std::string group_id;

        // Long Poll state (VK)
        std::string lp_server;
        std::string lp_key;
        std::string lp_ts;

        // Backoff
        int consecutive_errors{0};
        std::chrono::steady_clock::time_point next_attempt;

        // Event deduplication — track recently seen message/comment IDs
        std::set<int64_t> seenEventIds;
        static constexpr size_t kMaxSeenIds = 200;  // keep bounded

        std::thread thread;
        bool active{false};
    };

    void LongPollLoop(InstanceState* state);
    void RestPollLoop(InstanceState* state);

    // Telegram Long Poll
    void TelegramLongPollLoop(InstanceState* state);
    void ProcessTelegramMessage(InstanceState* state,
                                const struct telegram::Message& msg,
                                class telegram::TelegramBotApi& api);
    void ProcessTelegramChatMemberUpdate(InstanceState* state,
                                          const struct telegram::Update& update);

    // VK Long Poll helpers
    bool FetchLongPollServer(InstanceState* state);
    std::string BuildLongPollUrl(const InstanceState* state) const;

    // Event processing
    void ProcessVKEvent(InstanceState* state, const std::string& eventType,
                        const std::string& objectJson);
    void ProcessVKMessageNew(InstanceState* state, const std::string& objectJson);
    void ProcessVKWallPostNew(InstanceState* state, const std::string& objectJson);
    void ProcessVKWallReplyNew(InstanceState* state, const std::string& objectJson);

    // User resolution — batch resolve user IDs to display names
    std::unordered_map<std::string, std::string> ResolveVKUsers(
        InstanceState* state,
        const std::vector<std::string>& userIds);

    // Session dispatch
    void DispatchToSession(InstanceState* state,
                           const std::string& routingKey,
                           const std::string& message,
                           const std::string& sessionType);

    // Fetch conversation history for context
    std::string FetchVKChatHistory(InstanceState* state,
                                   const std::string& peerId,
                                   int count);

    HttpClient& m_httpClient;
    AgentConfigStore* m_configStore;
    SocialEventRouter& m_router;
    DispatchCallback m_dispatch;

    std::unordered_map<std::string, std::unique_ptr<InstanceState>> m_instances;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
};

} // namespace animus::kernel

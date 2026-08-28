#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "animus_kernel/IDataStore.h"

namespace animus::kernel {

// ============================================================================
// ChannelArrival — one trusted channel-context record for a session
//
// Written by AgentKernel::ExecuteChannelDispatch (covers both the direct
// dispatch path and the message-queue flush path, which funnels into it)
// BEFORE the chain runs; read by the ChannelContextProvider (#15) during
// system-prompt assembly to render the trusted context card.
//
// Everything here is server-side truth built from routing state — never from
// message text. Author names/handles are attacker-influenced strings stored
// as DATA: the provider interpolates them into quoted positions only.
//
// source_message_id is the #32 prerequisite (edit/delete events address by
// platform message id; inbound history entries become addressable).
// ============================================================================

struct ChannelArrival {
    int64_t id{0};
    std::string session_key;   // e.g. "channel:chat:discord:123456"
    std::string agent_id;

    // --- Origin -------------------------------------------------------------
    std::string channel_type;        // e.g. "discord", "bluesky"
    std::string channel_name;        // configured instance name
    std::string platform_id;         // "type:name" — channels-tool addressing
    std::string message_type;        // "dm" | "mention" | "wall_reply" | "tracked" | ...

    // --- Author (data, never instruction position) ---------------------------
    std::string author_id;           // platform-stable id (snowflake / DID / uid)
    std::string author_handle;       // display handle/name — attacker-influenced
    std::string origin;              // adapter-supplied origin map (compact JSON:
                                     // {"user":…,"channel":…,"trust":…}); card
                                     // prints present keys only. Trust attaches
                                     // to the channel (operator-configured),
                                     // never to content self-description.

    // --- Targeting (server-resolved ReplyTarget snapshot) ---------------------
    std::string delivery;            // "auto" (Chat: text reply auto-sent) | "tool" (must use channels tool)
    std::string peer_id;             // Chat targets
    std::string post_id;             // Wall targets (post/comment being replied to)
    std::string group_id;            // group contexts
    std::string email_thread_id;     // email threads

    // --- IDs (for #32 edit/delete + #16 reply resolution) ---------------------
    std::string source_message_id;   // the platform's id for THIS arriving message
    std::string reply_parent_id;     // parent post/message URI (replies)
    std::string thread_root_id;      // thread root URI (threaded platforms)

    int64_t created_at_unix_ms{0};
    bool consumed{false};            // provider rendered it / chain consumed it
};

// ============================================================================
// ChannelContextStore — per-session trusted channel context side-store
//
// Follows the SessionNotesStore pattern. Also carries the seen-URI watermark
// set (#20) so thread hydration can dedup across arrivals.
// ============================================================================

class ChannelContextStore {
public:
    explicit ChannelContextStore(IDataStore* store);

    void EnsureSchema();

    // Record an arrival. Returns the stored record with id + timestamps set.
    ChannelArrival AddArrival(const ChannelArrival& arrival);

    // Unconsumed arrivals for a session, oldest first (provider renders one
    // card per arrival; message-queue flushes concatenate several).
    std::vector<ChannelArrival> PendingArrivals(const std::string& sessionKey,
                                                 const std::string& agentId) const;

    // Most recent arrival regardless of consumed state — the default reply
    // target for #16 resolution ("reply to what's on the channel now").
    std::optional<ChannelArrival> LatestArrival(const std::string& sessionKey,
                                                 const std::string& agentId) const;

    // Recent arrivals (admin visibility / debugging), newest first.
    std::vector<ChannelArrival> RecentArrivals(const std::string& sessionKey,
                                                const std::string& agentId,
                                                int limit) const;

    // Mark one arrival consumed. Returns true on success.
    bool MarkConsumed(int64_t arrivalId);

    // Mark all pending arrivals of a session consumed (called when the chain
    // that saw them completes). Returns number marked.
    int MarkAllConsumed(const std::string& sessionKey, const std::string& agentId);

    // Prune a session's arrivals to the most recent `keepLast`. Returns removed count.
    int Prune(const std::string& sessionKey, const std::string& agentId, int keepLast = 20);

    // --- Seen-URI watermark (#20 thread-hydration dedup) -----------------------
    // Returns true if the URI was newly added; false if it was already seen.
    bool AddSeenUri(const std::string& sessionKey,
                    const std::string& agentId,
                    const std::string& uri);
    bool HasSeenUri(const std::string& sessionKey,
                    const std::string& agentId,
                    const std::string& uri) const;

    static constexpr int kDefaultKeepArrivals = 20;
    static constexpr int kMaxArrivalsPerSession = 100;  // hard cap (Prune enforces)

    // Canonicalize session keys across their two producer forms: raw dispatch
    // keys ("channel:chat:discord:123") and SessionKey::ToString() output with
    // trailing empty pipe components ("channel:chat:discord:123||"). Applied
    // at every store boundary so lookups hit regardless of producer.
    static std::string NormalizeSessionKey(const std::string& key);

private:
    static int64_t NowUnixMs();
    IDataStore* m_store;
};

} // namespace animus::kernel

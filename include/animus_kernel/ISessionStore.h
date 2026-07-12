#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "animus_kernel/Session.h"

namespace animus::kernel {

// ISessionStore abstracts where Sessions live.
//
// Default: in-memory. Future: Redis-backed for cross-node synchronization.
class ISessionStore {
public:
    virtual ~ISessionStore() = default;

    virtual std::shared_ptr<Session> GetOrCreate(const SessionKey& key) = 0;
    virtual std::shared_ptr<Session> GetById(SessionId id) = 0;
    virtual std::vector<std::shared_ptr<Session>> List() = 0;

    // Paginated list with optional content search.
    // Default implementation: in-memory slicing of List() (no search).
    // SqliteSessionStore overrides with SQL LIMIT/OFFSET + LIKE.
    struct ListPage {
        std::vector<std::shared_ptr<Session>> sessions;
        std::size_t total{0};
    };
    virtual ListPage ListPaginated(std::size_t offset, std::size_t limit,
                                   const std::string& search = "") {
        // Default: in-memory pagination (no search support).
        auto all = List();
        ListPage page;
        page.total = all.size();
        if (offset < all.size()) {
            auto end = std::min(offset + limit, all.size());
            page.sessions.assign(all.begin() + offset, all.begin() + end);
        }
        return page;
    }

    virtual bool DeleteById(SessionId id) = 0;

    // Persist a single session's state to backing store.
    // Called after mutations (e.g. turns added) to ensure durability.
    // No-op for in-memory stores.
    virtual void FlushSession(SessionId id) = 0;

    // Query unprocessed session turns directly from DB (bypasses in-memory cache).
    // Returns {session_id, turn_id, role, content} tuples for turns where
    // intake_processed = 0 and the session's agent_id matches (or agentId is empty).
    struct UnprocessedTurn {
        SessionId session_id{0};
        SessionTurnId turn_id{0};
        std::string role;
        std::string content;
    };
    virtual std::vector<UnprocessedTurn> GetUnprocessedTurns(
        const std::string& agentId, int limit) = 0;

    // Mark specific turns as processed by turn_id
    virtual void MarkTurnsProcessed(const std::vector<SessionTurnId>& turnIds) = 0;
};

} // namespace animus::kernel

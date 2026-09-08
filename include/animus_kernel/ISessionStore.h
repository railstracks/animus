#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

    // Paginated turn fetch WITHOUT hydrating the full Session object.
    // For chat history browsing: open cost scales with page size, not
    // session length. Items are newest-first (turn_id DESC).
    // Default: hydrate + slice (correct for in-memory stores).
    struct SessionTurnPage {
        bool found{false};
        SessionKey key{};
        std::uint64_t total{0};
        std::vector<SessionTurn> items;
    };
    virtual SessionTurnPage GetSessionTurnsPage(SessionId id,
                                                std::size_t page,
                                                std::size_t limit) {
        SessionTurnPage out;
        auto session = GetById(id);
        if (!session) {
            return out;
        }
        out.found = true;
        out.key = session->Key();
        const auto& turns = session->Turns();
        out.total = static_cast<std::uint64_t>(turns.size());
        if (page == 0) {
            page = 1;
        }
        const std::size_t offset = (page - 1) * limit;
        if (offset < turns.size()) {
            const std::size_t count = std::min(limit, turns.size() - offset);
            for (std::size_t i = 0; i < count; ++i) {
                out.items.push_back(turns[turns.size() - 1 - (offset + i)]);
            }
        }
        return out;
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
        std::size_t token_count{0};
        int64_t unix_ms{0};
    };
    virtual std::vector<UnprocessedTurn> GetUnprocessedTurns(
        const std::string& agentId, int limit) = 0;

    // Retrieve turns for session reporting. Unlike GetUnprocessedTurns, this
    // ignores the intake_processed flag and instead filters by timestamp:
    // only turns newer than `sinceUnixMs` for the given session are returned.
    // Used by session reporting to get turns that arrived since the last report.
    virtual std::vector<UnprocessedTurn> GetTurnsForSessionReport(
        SessionId sessionId,
        int64_t sinceUnixMs,
        int limit) {
        // Default: no-op (stores that don't implement this return empty)
        return {};
    }

    // Mark specific turns as processed by turn_id
    virtual void MarkTurnsProcessed(const std::vector<SessionTurnId>& turnIds) = 0;

    // Query metadata values for a session matching a JSON key path.
    // Returns all non-null values found for the given key in the metadata JSON column.
    // Used by channel pollers to check which external message IDs are already stored.
    // Set metadata on the last user turn of a session
    virtual void SetLastUserTurnMetadata(
        SessionId sessionId,
        const std::string& metadata) {
        // Default: no-op (only SQLite store supports this)
    }

    // Query metadata values for a session matching a JSON key path.
    // Returns all non-null values found for the given key in the metadata JSON column.
    // Used by channel pollers to check which external message IDs are already stored.
    virtual std::vector<std::string> GetMetadataValues(
        SessionId sessionId,
        const std::string& jsonKey) {
        return {};  // Default: no metadata support
    }
};

} // namespace animus::kernel

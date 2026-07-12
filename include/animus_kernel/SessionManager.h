#pragma once

#include <memory>
#include <string>
#include <vector>

#include "animus_kernel/IncomingEvent.h"
#include "animus_kernel/ISessionRouter.h"
#include "animus_kernel/ISessionStore.h"

namespace animus::kernel {

struct SessionContextSet {
    SessionAccess primary;
    std::vector<SessionAccess> context;
};

class SessionManager {
public:
    SessionManager(
        std::unique_ptr<ISessionStore> store,
        std::unique_ptr<ISessionRouter> router);

    SessionContextSet Resolve(const IncomingEvent& event);
    std::shared_ptr<Session> GetOrCreate(const SessionKey& key);
    std::shared_ptr<Session> GetById(SessionId id);
    std::vector<std::shared_ptr<Session>> ListSessions();

    // Paginated session list with optional content search.
    ISessionStore::ListPage ListSessionsPaginated(
        std::size_t offset, std::size_t limit, const std::string& search = "");

    bool DeleteById(SessionId id);

    // Persist a single session's state to backing store.
    void FlushSession(SessionId id);

    // Persist all sessions (legacy API, no-op for immediate-write stores)
    void FlushStore();

    // DB-level queries for consolidation (delegates to store)
    std::vector<ISessionStore::UnprocessedTurn> GetUnprocessedTurns(
        const std::string& agentId, int limit);
    void MarkTurnsProcessed(const std::vector<SessionTurnId>& turnIds);

    // Access underlying store for compaction and other services
    ISessionStore& GetStore() { return *m_store; }

private:
    std::unique_ptr<ISessionStore> m_store;
    std::unique_ptr<ISessionRouter> m_router;
};

} // namespace animus::kernel

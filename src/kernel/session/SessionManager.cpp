#include "animus_kernel/SessionManager.h"

#include <unordered_set>
#include <utility>

namespace animus::kernel {

SessionManager::SessionManager(
    std::unique_ptr<ISessionStore> store,
    std::unique_ptr<ISessionRouter> router)
    : m_store(std::move(store)), m_router(std::move(router)) {
}

SessionContextSet SessionManager::Resolve(const IncomingEvent& event) {
    SessionContextSet out{};

    const SessionRoutingResult routing = m_router->Route(event);
    out.primary = SessionAccess(m_store->GetOrCreate(routing.primary),
                                SessionAccessMode::ReadWrite);

    std::unordered_set<std::string> loadedContextKeys;
    for (const auto& k : routing.context) {
        const std::string serialized = k.ToString();
        if (!loadedContextKeys.insert(serialized).second) {
            continue;
        }

        out.context.emplace_back(m_store->GetOrCreate(k),
                                 SessionAccessMode::ReadOnly);
    }

    return out;
}

std::shared_ptr<Session> SessionManager::GetOrCreate(const SessionKey& key) {
    return m_store->GetOrCreate(key);
}

std::shared_ptr<Session> SessionManager::GetById(SessionId id) {
    return m_store->GetById(id);
}

std::vector<std::shared_ptr<Session>> SessionManager::ListSessions() {
    return m_store->List();
}

ISessionStore::ListPage SessionManager::ListSessionsPaginated(
        std::size_t offset, std::size_t limit, const std::string& search) {
    return m_store->ListPaginated(offset, limit, search);
}

bool SessionManager::DeleteById(SessionId id) {
    return m_store->DeleteById(id);
}

void SessionManager::FlushSession(SessionId id) {
    m_store->FlushSession(id);
}

void SessionManager::FlushStore() {
    // No-op: SqliteSessionStore persists immediately.
    // Kept for API compatibility.
}


std::vector<ISessionStore::UnprocessedTurn> SessionManager::GetUnprocessedTurns(
        const std::string& agentId, int limit) {
    return m_store->GetUnprocessedTurns(agentId, limit);
}

void SessionManager::MarkTurnsProcessed(const std::vector<SessionTurnId>& turnIds) {
    m_store->MarkTurnsProcessed(turnIds);
}

} // namespace animus::kernel

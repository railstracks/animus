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

std::vector<std::string> SessionManager::GetMetadataValues(
    const std::string& sessionKeyStr,
    const std::string& jsonKey) {
    // Parse session key string: format is "connector|conversation_id|thread_id"
    // Channel sessions use "channel:<key>||"
    std::string connector, convoId, threadId;
    auto first = sessionKeyStr.find('|');
    if (first != std::string::npos) {
        connector = sessionKeyStr.substr(0, first);
        auto second = sessionKeyStr.find('|', first + 1);
        if (second != std::string::npos) {
            convoId = sessionKeyStr.substr(first + 1, second - first - 1);
            threadId = sessionKeyStr.substr(second + 1);
        }
    } else {
        connector = sessionKeyStr;
    }

    SessionKey key{connector, convoId, threadId};
    auto session = m_store->GetOrCreate(key);
    if (!session) return {};

    return m_store->GetMetadataValues(session->Id(), jsonKey);
}

void SessionManager::SetLastUserTurnMetadata(
    const std::string& sessionKeyStr,
    const std::string& metadata) {
    // Parse session key string: format is "connector|conversation_id|thread_id"
    std::string connector, convoId, threadId;
    auto first = sessionKeyStr.find('|');
    if (first != std::string::npos) {
        connector = sessionKeyStr.substr(0, first);
        auto second = sessionKeyStr.find('|', first + 1);
        if (second != std::string::npos) {
            convoId = sessionKeyStr.substr(first + 1, second - first - 1);
            threadId = sessionKeyStr.substr(second + 1);
        }
    } else {
        connector = sessionKeyStr;
    }

    SessionKey key{connector, convoId, threadId};
    auto session = m_store->GetOrCreate(key);
    if (!session) return;

    m_store->SetLastUserTurnMetadata(session->Id(), metadata);
}

} // namespace animus::kernel

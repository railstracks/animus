#include "animus_kernel/InMemorySessionStore.h"

namespace animus::kernel {

InMemorySessionStore::InMemorySessionStore() = default;

std::shared_ptr<Session> InMemorySessionStore::GetOrCreate(const SessionKey& key) {
    const std::string mapKey = key.ToString();

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_byKey.find(mapKey);
    if (it != m_byKey.end()) {
        return it->second;
    }

    const SessionId id = m_nextId.fetch_add(1);
    auto session = std::make_shared<Session>(id, key);

    m_byKey.emplace(mapKey, session);
    m_byId.emplace(id, session);

    return session;
}

std::shared_ptr<Session> InMemorySessionStore::GetById(SessionId id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_byId.find(id);
    if (it == m_byId.end()) {
        return nullptr;
    }
    return it->second;
}

} // namespace animus::kernel

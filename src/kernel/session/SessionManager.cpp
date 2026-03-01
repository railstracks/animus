#include "animus_kernel/SessionManager.h"

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

    for (const auto& k : routing.context) {
        out.context.emplace_back(m_store->GetOrCreate(k),
                                 SessionAccessMode::ReadOnly);
    }

    return out;
}

std::shared_ptr<Session> SessionManager::GetOrCreate(const SessionKey& key) {
    return m_store->GetOrCreate(key);
}

} // namespace animus::kernel

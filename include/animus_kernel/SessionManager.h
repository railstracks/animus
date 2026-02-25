#pragma once

#include <memory>
#include <string>
#include <vector>

#include "animus_kernel/IncomingEvent.h"
#include "animus_kernel/ISessionRouter.h"
#include "animus_kernel/ISessionStore.h"

namespace animus::kernel {

struct SessionContextSet {
    std::shared_ptr<Session> primary;
    std::vector<std::shared_ptr<Session>> context;
};

class SessionManager {
public:
    SessionManager(
        std::unique_ptr<ISessionStore> store,
        std::unique_ptr<ISessionRouter> router);

    SessionContextSet Resolve(const IncomingEvent& event);
    std::shared_ptr<Session> GetOrCreate(const SessionKey& key);

private:
    std::unique_ptr<ISessionStore> m_store;
    std::unique_ptr<ISessionRouter> m_router;
};

} // namespace animus::kernel

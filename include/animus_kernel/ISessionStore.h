#pragma once

#include <memory>

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
};

} // namespace animus::kernel

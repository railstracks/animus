#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "animus_kernel/ISessionStore.h"

namespace animus::kernel {

class InMemorySessionStore final : public ISessionStore {
public:
    InMemorySessionStore();

    std::shared_ptr<Session> GetOrCreate(const SessionKey& key) override;
    std::shared_ptr<Session> GetById(SessionId id) override;

private:
    std::mutex m_mutex;
    std::atomic<SessionId> m_nextId{1};
    std::unordered_map<std::string, std::shared_ptr<Session>> m_byKey;
    std::unordered_map<SessionId, std::shared_ptr<Session>> m_byId;
};

} // namespace animus::kernel

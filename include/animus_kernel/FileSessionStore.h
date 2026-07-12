#pragma once

#include "animus_kernel/ISessionStore.h"
#include "animus_kernel/Session.h"

#include <filesystem>
#include <mutex>
#include <string>

namespace animus::kernel {

// Persists sessions to a JSON file on disk.
// Wraps in-memory state; flushes on every write, loads on startup.
class FileSessionStore : public ISessionStore {
public:
    explicit FileSessionStore(const std::string& filePath);

    std::shared_ptr<Session> GetOrCreate(const SessionKey& key) override;
    std::shared_ptr<Session> GetById(SessionId id) override;
    std::vector<std::shared_ptr<Session>> List() override;
    bool DeleteById(SessionId id) override;
    void FlushSession(SessionId id) override;

public:
    // Call after mutations to persist to disk
    void FlushToDisk() const { SaveToDisk(); }

private:
    void LoadFromDisk();
    void SaveToDisk() const;

    std::filesystem::path m_filePath;
    mutable std::mutex m_mutex;

    SessionId m_nextId{1};

    struct SessionEntry {
        std::shared_ptr<Session> session;
    };
    std::vector<SessionEntry> m_entries;
};

} // namespace animus::kernel

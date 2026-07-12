#pragma once

#include "animus_kernel/ISessionStore.h"
#include "animus_kernel/Session.h"

#include <mutex>
#include <string>

namespace animus::kernel {

class IDataStore;

// SqliteSessionStore persists sessions in SQLite (same database as MemoryStore).
// Implements ISessionStore — drop-in replacement for FileSessionStore.
//
// On construction:
//   1. Creates session tables if they don't exist
//   2. If sessions table is empty and sessions.json exists, imports legacy data
//   3. Loads all sessions into memory for fast lookup
//
// Writes are persisted immediately to SQLite after each mutation.
class SqliteSessionStore : public ISessionStore {
public:
    // Takes a shared IDataStore (does not assume ownership).
    // legacyJsonPath: path to old sessions.json for one-time migration (empty = skip).
    explicit SqliteSessionStore(IDataStore* dataStore,
                                const std::string& legacyJsonPath = "state/sessions.json");

    std::shared_ptr<Session> GetOrCreate(const SessionKey& key) override;
    std::shared_ptr<Session> GetById(SessionId id) override;
    std::vector<std::shared_ptr<Session>> List() override;

    // Paginated list with optional content search.
    // search: if non-empty, filters to sessions containing the phrase in any turn content.
    // Returns matching sessions for the page + total match count.
    ISessionStore::ListPage ListPaginated(std::size_t offset, std::size_t limit,
                                           const std::string& search = "") override;

    bool DeleteById(SessionId id) override;
    void FlushSession(SessionId id) override;

    std::vector<UnprocessedTurn> GetUnprocessedTurns(
        const std::string& agentId, int limit) override;
    void MarkTurnsProcessed(const std::vector<SessionTurnId>& turnIds) override;

private:
    void EnsureSchema();
    void LoadFromDb();
    void MigrateFromJson(const std::string& jsonPath);
    void PersistSession(const Session& session);
    void DeleteSessionFromDb(SessionId id);

    IDataStore* m_store;
    std::mutex m_mutex;

    SessionId m_nextId{1};

    struct Entry {
        std::shared_ptr<Session> session;
    };
    std::vector<Entry> m_entries;
};

} // namespace animus::kernel

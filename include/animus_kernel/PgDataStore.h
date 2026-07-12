#pragma once

#include "animus_kernel/IDataStore.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

struct pg_conn;
typedef pg_conn PGconn;

namespace animus::kernel {

// ============================================================================
// PgDataStore — PostgreSQL implementation of IDataStore with connection pooling
//
// Manages a pool of PGconn* connections. Each Prepare() call acquires a
// connection and returns it to the pool when the statement is destroyed.
// Exec() acquires, executes, and returns immediately.
//
// LastInsertRowId() tracks the last insert per-connection via thread-local
// storage set by the statement that performed the INSERT.
// ============================================================================

class PgDataStore : public IDataStore {
public:
    struct Config {
        std::string host{"localhost"};
        int port{5432};
        std::string database{"animus"};
        std::string username{"animus"};
        std::string password;
        int pool_size{10};
    };

    explicit PgDataStore(const Config& config);
    ~PgDataStore() override;

    PgDataStore(const PgDataStore&) = delete;
    PgDataStore& operator=(const PgDataStore&) = delete;

    // IDataStore
    bool Exec(const std::string& sql) override;
    std::unique_ptr<IStatement> Prepare(const std::string& sql) override;
    int64_t LastInsertRowId() override;
    std::string ErrMsg() override;
    DataStoreDialect Dialect() const override { return DataStoreDialect::PostgreSQL; }
    bool SupportsVectorSearch() const override { return m_hasPgvector; }
    bool IsOpen() const override;

    int64_t Changes() override { return m_lastChanges.load(); }
    void SetLastChanges(int64_t count) { m_lastChanges.store(count); }
    void SetLastError(const std::string& err) { m_lastError = err; }

    bool TryReconnect();

    // Connection pool internals (used by PgStatement)
    struct PooledConnection {
        PGconn* conn{nullptr};
        int id{0};
        bool healthy{true};
    };

    // Acquire a connection from the pool (blocks until one is available)
    PooledConnection* Acquire();
    // Return a connection to the pool
    void Release(PooledConnection* pc);
    // Set the last insert row ID for a specific connection (thread-local)
    void SetConnLastInsertId(PooledConnection* pc, int64_t id);

private:
    static std::string TranslatePlaceholders(const std::string& sql);
    bool DetectPgvector();
    std::unique_ptr<PooledConnection> CreateConnection();
    bool CheckConnectionHealth(PooledConnection* pc);

    std::string m_connInfo;
    std::vector<std::unique_ptr<PooledConnection>> m_allConnections;
    std::deque<PooledConnection*> m_available;
    mutable std::mutex m_poolMutex;
    std::condition_variable m_poolCv;
    int m_poolSize{10};

    bool m_hasPgvector{false};
    std::atomic<int64_t> m_lastChanges{0};
    std::string m_lastError;

    // Per-connection last insert tracking (for LastInsertRowId)
    struct ConnState {
        int64_t lastInsertId{0};
    };
    std::mutex m_stateMutex;
    std::vector<std::unique_ptr<ConnState>> m_connStates;
};

} // namespace animus::kernel

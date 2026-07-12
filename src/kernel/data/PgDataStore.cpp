#include "animus_kernel/PgDataStore.h"

#include <atomic>
#include <cstring>
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

#include <libpq-fe.h>

namespace animus::kernel {

// ============================================================================
// PgStatement — IStatement wrapper around PGresult* with pooled connection
// ============================================================================

class PgStatement final : public IStatement {
public:
    PgStatement(PgDataStore* owner, PgDataStore::PooledConnection* pc,
                const std::string& sql)
        : m_owner(owner), m_pc(pc), m_sql(sql),
          m_currentRow(0), m_result(nullptr), m_hasResult(false), m_done(false) {}

    ~PgStatement() override {
        Clear();
        if (m_pc) m_owner->Release(m_pc);
    }

    bool BindInt(int idx, int val) override {
        m_params.emplace_back(std::to_string(val));
        m_paramFormats.push_back(0);
        return true;
    }
    bool BindInt64(int idx, int64_t val) override {
        m_params.emplace_back(std::to_string(val));
        m_paramFormats.push_back(0);
        return true;
    }
    bool BindDouble(int idx, double val) override {
        m_params.emplace_back(std::to_string(val));
        m_paramFormats.push_back(0);
        return true;
    }
    bool BindText(int idx, const std::string& val) override {
        m_params.emplace_back(val);
        m_paramFormats.push_back(0);
        return true;
    }
    bool BindBlob(int idx, const void* data, size_t size) override {
        static const char hexChars[] = "0123456789abcdef";
        std::string hex;
        hex.reserve(size * 2 + 2);
        hex += "\\x";
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            hex += hexChars[bytes[i] >> 4];
            hex += hexChars[bytes[i] & 0x0F];
        }
        m_params.emplace_back(std::move(hex));
        m_paramFormats.push_back(0);
        return true;
    }
    bool BindNull(int idx) override {
        m_params.emplace_back(std::string());
        m_nullFlags.push_back(static_cast<int>(m_params.size()) - 1);
        m_paramFormats.push_back(0);
        return true;
    }

    bool Step() override {
        if (!m_hasResult) {
            if (!Execute()) {
                m_done = true;
                return false;
            }
            m_hasResult = true;
            m_currentRow = 0;
            // SELECT queries (PGRES_TUPLES_OK): return true only if rows exist.
            // DML statements (PGRES_COMMAND_OK): return true once so callers can
            // check RowsAffected(), then false on subsequent calls.
            if (m_result && PQntuples(m_result) > 0) return true;
            // COMMAND_OK with 0 tuples (e.g. empty UPDATE): first Step() returns
            // true (success) so caller can check RowsAffected().
            ExecStatusType status = PQresultStatus(m_result);
            if (status == PGRES_COMMAND_OK && !m_done) {
                m_done = true;
                return true;
            }
            // SELECT with 0 rows: no rows to iterate.
            m_done = true;
            return false;
        }
        if (!m_result) { m_done = true; return false; }
        m_currentRow++;
        if (m_currentRow < PQntuples(m_result)) return true;
        m_done = true;
        return false;
    }

    int64_t ColumnInt64(int col) override {
        if (!m_result || m_currentRow >= PQntuples(m_result) || col >= PQnfields(m_result)) return 0;
        if (PQgetisnull(m_result, m_currentRow, col)) return 0;
        return std::atoll(PQgetvalue(m_result, m_currentRow, col));
    }
    double ColumnDouble(int col) override {
        if (!m_result || m_currentRow >= PQntuples(m_result) || col >= PQnfields(m_result)) return 0.0;
        if (PQgetisnull(m_result, m_currentRow, col)) return 0.0;
        return std::stod(PQgetvalue(m_result, m_currentRow, col));
    }
    std::string ColumnText(int col) override {
        if (!m_result || m_currentRow >= PQntuples(m_result) || col >= PQnfields(m_result)) return {};
        if (PQgetisnull(m_result, m_currentRow, col)) return {};
        return std::string(PQgetvalue(m_result, m_currentRow, col),
                          PQgetlength(m_result, m_currentRow, col));
    }
    std::vector<uint8_t> ColumnBlob(int col) override {
        if (!m_result || m_currentRow >= PQntuples(m_result) || col >= PQnfields(m_result)) return {};
        if (PQgetisnull(m_result, m_currentRow, col)) return {};
        const char* val = PQgetvalue(m_result, m_currentRow, col);
        int len = PQgetlength(m_result, m_currentRow, col);
        if (!val || len <= 0) return {};
        if (len >= 2 && val[0] == '\\' && val[1] == 'x') {
            const char* hex = val + 2;
            int hexLen = len - 2;
            std::vector<uint8_t> result;
            result.reserve(hexLen / 2);
            for (int i = 0; i + 1 < hexLen; i += 2) {
                auto hexVal = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return 0;
                };
                result.push_back(static_cast<uint8_t>((hexVal(hex[i]) << 4) | hexVal(hex[i + 1])));
            }
            return result;
        }
        const auto* bytes = reinterpret_cast<const uint8_t*>(val);
        return std::vector<uint8_t>(bytes, bytes + len);
    }

    ColumnType GetColumnType(int col) override {
        if (!m_result || col >= PQnfields(m_result)) return ColumnType::Unknown;
        if (PQgetisnull(m_result, m_currentRow, col)) return ColumnType::Null;
        Oid typeid_ = PQftype(m_result, col);
        if (typeid_ == 23 || typeid_ == 20 || typeid_ == 21) return ColumnType::Integer;
        if (typeid_ == 700 || typeid_ == 701 || typeid_ == 1700) return ColumnType::Float;
        if (typeid_ == 25 || typeid_ == 1043 || typeid_ == 1042) return ColumnType::Text;
        return ColumnType::Unknown;
    }
    bool IsColumnNull(int col) override {
        if (!m_result || m_currentRow >= PQntuples(m_result) || col >= PQnfields(m_result)) return true;
        return PQgetisnull(m_result, m_currentRow, col) != 0;
    }
    void Reset() override {
        Clear();
        m_params.clear();
        m_paramFormats.clear();
        m_nullFlags.clear();
    }
    void Finalize() override { Clear(); }

private:
    bool Execute() {
        if (!m_pc || !m_pc->conn) return false;

        std::vector<const char*> paramValues;
        std::vector<int> paramLengths;
        for (size_t i = 0; i < m_params.size(); ++i) {
            bool isNull = false;
            for (int nullIdx : m_nullFlags) {
                if (nullIdx == static_cast<int>(i)) { isNull = true; break; }
            }
            if (isNull) {
                paramValues.push_back(nullptr);
                paramLengths.push_back(0);
            } else {
                paramValues.push_back(m_params[i].c_str());
                paramLengths.push_back(static_cast<int>(m_params[i].size()));
            }
        }

        m_result = PQexecParams(
            m_pc->conn, m_sql.c_str(),
            static_cast<int>(m_params.size()), nullptr,
            paramValues.data(), paramLengths.data(),
            m_paramFormats.data(), 0);

        if (!m_result) {
            m_lastError = PQerrorMessage(m_pc->conn);
            m_owner->SetLastError(m_lastError);
            m_pc->healthy = false;
            return false;
        }

        ExecStatusType status = PQresultStatus(m_result);
        if (status == PGRES_TUPLES_OK || status == PGRES_SINGLE_TUPLE) {
            m_owner->SetLastChanges(0);
            return true;
        }
        if (status == PGRES_COMMAND_OK) {
            char* tuples = PQcmdTuples(m_result);
            int64_t changes = tuples ? std::atoll(tuples) : 0;
            m_owner->SetLastChanges(changes);

            // Track last insert ID for this connection via SELECT lastval()
            // Only meaningful after an actual INSERT (not ON CONFLICT DO UPDATE).
            // lastval() returns the most recent sequence value for this session.
            // Guard against lastval() failing (e.g. after ON CONFLICT DO UPDATE
            // where no sequence was consumed) — the error is harmless and should
            // not pollute the connection state.
            if (changes > 0) {
                PGresult* idResult = PQexec(m_pc->conn, "SELECT lastval()");
                if (idResult) {
                    if (PQresultStatus(idResult) == PGRES_TUPLES_OK && PQntuples(idResult) > 0) {
                        int64_t id = std::atoll(PQgetvalue(idResult, 0, 0));
                        m_owner->SetConnLastInsertId(m_pc, id);
                    }
                    PQclear(idResult);
                }
                // Clear any error from lastval() failing (ON CONFLICT path)
                // Consume the error by running a trivial no-op that succeeds.
                PGresult* clearResult = PQexec(m_pc->conn, "SELECT 1");
                if (clearResult) PQclear(clearResult);
            }
            return true;
        }

        m_lastError = PQresultErrorMessage(m_result);
        m_owner->SetLastError(m_lastError);
        m_owner->SetLastChanges(0);
        return false;
    }

    void Clear() {
        if (m_result) { PQclear(m_result); m_result = nullptr; }
        m_hasResult = false;
        m_done = false;
        m_currentRow = 0;
    }

    PgDataStore* m_owner;
    PgDataStore::PooledConnection* m_pc;
    std::string m_sql;
    std::vector<std::string> m_params;
    std::vector<int> m_paramFormats;
    std::vector<int> m_nullFlags;
    int m_currentRow{0};
    PGresult* m_result{nullptr};
    bool m_hasResult{false};
    bool m_done{false};
    std::string m_lastError;
};

// ============================================================================
// PgDataStore — connection pool implementation
// ============================================================================

static std::unique_ptr<PgDataStore::PooledConnection> CreatePooledConnection(const std::string& connInfo, int id) {
    PGconn* conn = PQconnectdb(connInfo.c_str());
    if (!conn || PQstatus(conn) != CONNECTION_OK) {
        std::cerr << "[datastore] Connection " << id << " failed: "
                  << (conn ? PQerrorMessage(conn) : "null") << std::endl;
        if (conn) PQfinish(conn);
        return nullptr;
    }
    auto pc = std::make_unique<PgDataStore::PooledConnection>();
    pc->conn = conn;
    pc->id = id;
    pc->healthy = true;
    return pc;
}

PgDataStore::PgDataStore(const Config& config) {
    std::ostringstream conninfo;
    conninfo << "host=" << config.host
             << " port=" << config.port
             << " dbname=" << config.database
             << " user=" << config.username;
    if (!config.password.empty()) {
        conninfo << " password=" << config.password;
    }
    m_connInfo = conninfo.str();
    m_poolSize = std::max(1, config.pool_size);

    // Create pool connections
    for (int i = 0; i < m_poolSize; ++i) {
        auto pc = CreatePooledConnection(m_connInfo, i);
        if (pc) {
            m_available.push_back(pc.get());
            m_allConnections.push_back(std::move(pc));
        }
    }

    if (m_allConnections.empty()) {
        std::cerr << "[datastore] FATAL: no PostgreSQL connections established" << std::endl;
        return;
    }

    // Detect pgvector on first connection
    m_hasPgvector = DetectPgvector();

    // Initialize per-connection state
    for (size_t i = 0; i < m_allConnections.size(); ++i) {
        m_connStates.push_back(std::make_unique<ConnState>());
    }

    std::cerr << "[datastore] PostgreSQL pool: " << m_allConnections.size()
              << "/" << m_poolSize << " connections to "
              << config.host << ":" << config.port
              << "/" << config.database
              << " (pgvector: " << (m_hasPgvector ? "yes" : "no") << ")"
              << std::endl;
}

PgDataStore::~PgDataStore() {
    // All statements should have returned their connections by now
    for (auto& pc : m_allConnections) {
        if (pc->conn) { PQfinish(pc->conn); pc->conn = nullptr; }
    }
}

bool PgDataStore::IsOpen() const {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    for (const auto& pc : m_allConnections) {
        if (pc->conn && pc->healthy) return true;
    }
    return !m_allConnections.empty();
}

std::unique_ptr<PgDataStore::PooledConnection> PgDataStore::CreateConnection() {
    return nullptr; // unreachable — kept for API compatibility
}

// Actual creation used in constructor
PgDataStore::PooledConnection* PgDataStore::Acquire() {
    std::unique_lock<std::mutex> lock(m_poolMutex);
    m_poolCv.wait(lock, [this]() { return !m_available.empty(); });
    auto* pc = m_available.front();
    m_available.pop_front();

    // Health check
    if (!CheckConnectionHealth(pc)) {
        // Try to reconnect this connection
        if (pc->conn) { PQfinish(pc->conn); }
        pc->conn = PQconnectdb(m_connInfo.c_str());
        if (!pc->conn || PQstatus(pc->conn) != CONNECTION_OK) {
            pc->healthy = false;
            std::cerr << "[datastore] Connection " << pc->id << " reconnection failed" << std::endl;
        } else {
            pc->healthy = true;
        }
    }

    return pc;
}

void PgDataStore::Release(PooledConnection* pc) {
    if (!pc) return;
    std::lock_guard<std::mutex> lock(m_poolMutex);
    m_available.push_back(pc);
    m_poolCv.notify_one();
}

bool PgDataStore::CheckConnectionHealth(PooledConnection* pc) {
    if (!pc || !pc->conn) return false;
    if (PQstatus(pc->conn) != CONNECTION_OK) return false;
    // Ping with a trivial query
    PGresult* result = PQexec(pc->conn, "SELECT 1");
    bool ok = (result && PQresultStatus(result) == PGRES_TUPLES_OK);
    if (result) PQclear(result);
    return ok;
}

void PgDataStore::SetConnLastInsertId(PooledConnection* pc, int64_t id) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (pc && pc->id >= 0 && pc->id < static_cast<int>(m_connStates.size())) {
        m_connStates[pc->id]->lastInsertId = id;
    }
}

bool PgDataStore::Exec(const std::string& sql) {
    auto* pc = Acquire();
    if (!pc || !pc->conn) {
        if (pc) Release(pc);
        return false;
    }

    m_lastChanges.store(0);
    PGresult* result = PQexec(pc->conn, sql.c_str());
    if (!result) {
        m_lastError = PQerrorMessage(pc->conn);
        pc->healthy = false;
        Release(pc);
        return false;
    }

    ExecStatusType status = PQresultStatus(result);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        m_lastError = PQresultErrorMessage(result);
        PQclear(result);
        Release(pc);
        return false;
    }

    if (status == PGRES_COMMAND_OK) {
        char* tuples = PQcmdTuples(result);
        m_lastChanges.store(tuples ? std::atoll(tuples) : 0);
    }

    PQclear(result);
    Release(pc);
    return true;
}

std::unique_ptr<IStatement> PgDataStore::Prepare(const std::string& sql) {
    auto* pc = Acquire();
    if (!pc || !pc->conn) {
        if (pc) Release(pc);
        std::cerr << "[datastore] Prepare: no available connection" << std::endl;
        return nullptr;
    }

    std::string translated = TranslatePlaceholders(sql);
    return std::make_unique<PgStatement>(this, pc, translated);
}

int64_t PgDataStore::LastInsertRowId() {
    // The last INSERT was on the connection that most recently called SetConnLastInsertId.
    // Since statements are typically destroyed before LastInsertRowId is called,
    // we return the most recently set value across all connections.
    std::lock_guard<std::mutex> lock(m_stateMutex);
    int64_t maxId = 0;
    for (const auto& state : m_connStates) {
        if (state->lastInsertId > maxId) maxId = state->lastInsertId;
    }
    return maxId;
}

std::string PgDataStore::ErrMsg() {
    return m_lastError.empty() ? "database error" : m_lastError;
}

// static
std::string PgDataStore::TranslatePlaceholders(const std::string& sql) {
    std::string result;
    result.reserve(sql.size() + 32);
    int param_idx = 1;
    for (size_t i = 0; i < sql.size(); ++i) {
        if (sql[i] == '?') {
            result += "$";
            result += std::to_string(param_idx++);
        } else {
            result += sql[i];
        }
    }
    return result;
}

bool PgDataStore::DetectPgvector() {
    auto* pc = Acquire();
    if (!pc || !pc->conn) { if (pc) Release(pc); return false; }

    bool found = false;
    PGresult* result = PQexec(pc->conn,
        "SELECT EXISTS(SELECT 1 FROM pg_extension WHERE extname = 'vector')");
    if (result && PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) > 0) {
        found = (PQgetvalue(result, 0, 0)[0] == 't');
    }
    if (result) PQclear(result);

    if (!found) {
        PGresult* createResult = PQexec(pc->conn, "CREATE EXTENSION IF NOT EXISTS vector");
        if (createResult) {
            if (PQresultStatus(createResult) == PGRES_COMMAND_OK) {
                found = true;
                std::cerr << "[datastore] pgvector extension created" << std::endl;
            }
            PQclear(createResult);
        }
    }

    Release(pc);
    return found;
}

bool PgDataStore::TryReconnect() {
    // With a pool, individual connections reconnect on Acquire()
    // This method checks if any connection is alive
    std::lock_guard<std::mutex> lock(m_poolMutex);
    for (const auto& pc : m_allConnections) {
        if (pc->conn && PQstatus(pc->conn) == CONNECTION_OK) return true;
    }
    return false;
}

} // namespace animus::kernel

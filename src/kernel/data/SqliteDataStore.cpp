#include "animus_kernel/SqliteDataStore.h"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

#include <sqlite3.h>

namespace animus::kernel {

// ============================================================================
// SqliteStatement — IStatement wrapper around sqlite3_stmt*
// ============================================================================

class SqliteStatement final : public IStatement {
public:
    explicit SqliteStatement(sqlite3_stmt* stmt) : m_stmt(stmt) {}
    ~SqliteStatement() override { Finalize(); }

    bool BindInt(int idx, int val) override {
        return sqlite3_bind_int(m_stmt, idx, val) == SQLITE_OK;
    }
    bool BindInt64(int idx, int64_t val) override {
        return sqlite3_bind_int64(m_stmt, idx, val) == SQLITE_OK;
    }
    bool BindDouble(int idx, double val) override {
        return sqlite3_bind_double(m_stmt, idx, val) == SQLITE_OK;
    }
    bool BindText(int idx, const std::string& val) override {
        return sqlite3_bind_text(m_stmt, idx, val.c_str(),
                                 static_cast<int>(val.size()), SQLITE_TRANSIENT) == SQLITE_OK;
    }
    bool BindBlob(int idx, const void* data, size_t size) override {
        return sqlite3_bind_blob(m_stmt, idx, data, static_cast<int>(size), SQLITE_TRANSIENT) == SQLITE_OK;
    }
    bool BindNull(int idx) override {
        return sqlite3_bind_null(m_stmt, idx) == SQLITE_OK;
    }

    bool Step() override {
        int rc = sqlite3_step(m_stmt);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        // Error — caller can check the data store's ErrMsg
        return false;
    }

    int64_t ColumnInt64(int col) override {
        return sqlite3_column_int64(m_stmt, col);
    }
    double ColumnDouble(int col) override {
        return sqlite3_column_double(m_stmt, col);
    }
    std::string ColumnText(int col) override {
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(m_stmt, col));
        return text ? std::string(text) : std::string();
    }
    std::vector<uint8_t> ColumnBlob(int col) override {
        const void* blob = sqlite3_column_blob(m_stmt, col);
        int size = sqlite3_column_bytes(m_stmt, col);
        if (!blob || size <= 0) return {};
        const auto* bytes = static_cast<const uint8_t*>(blob);
        return std::vector<uint8_t>(bytes, bytes + size);
    }
    ColumnType GetColumnType(int col) override {
        switch (sqlite3_column_type(m_stmt, col)) {
            case SQLITE_INTEGER: return ColumnType::Integer;
            case SQLITE_FLOAT:   return ColumnType::Float;
            case SQLITE_TEXT:    return ColumnType::Text;
            case SQLITE_NULL:    return ColumnType::Null;
            default:             return ColumnType::Unknown;
        }
    }
    bool IsColumnNull(int col) override {
        return sqlite3_column_type(m_stmt, col) == SQLITE_NULL;
    }

    void Reset() override {
        sqlite3_reset(m_stmt);
        sqlite3_clear_bindings(m_stmt);
    }
    void Finalize() override {
        if (m_stmt) {
            sqlite3_finalize(m_stmt);
            m_stmt = nullptr;
        }
    }

private:
    sqlite3_stmt* m_stmt;
};

// ============================================================================
// SqliteDataStore
// ============================================================================

SqliteDataStore::SqliteDataStore(const std::string& dbPath) {
    // Ensure parent directory exists
    std::filesystem::path p(dbPath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    int rc = sqlite3_open(dbPath.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        std::cerr << "[datastore] failed to open " << dbPath
                  << ": " << (m_db ? sqlite3_errmsg(m_db) : "unknown error") << std::endl;
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        return;
    }

    // Enable WAL for concurrent reads and foreign keys
    Exec("PRAGMA journal_mode=WAL;");
    Exec("PRAGMA foreign_keys=ON;");

    std::cerr << "[datastore] opened " << dbPath << std::endl;
}

SqliteDataStore::~SqliteDataStore() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool SqliteDataStore::Exec(const std::string& sql) {
    if (!m_db) return false;
    char* err = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::cerr << "[datastore] SQL error: " << (err ? err : "unknown") << std::endl;
        sqlite3_free(err);
        return false;
    }
    return true;
}

std::unique_ptr<IStatement> SqliteDataStore::Prepare(const std::string& sql) {
    if (!m_db) return nullptr;
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[datastore] prepare error: " << sqlite3_errmsg(m_db) << std::endl;
        return nullptr;
    }
    return std::make_unique<SqliteStatement>(stmt);
}

int64_t SqliteDataStore::LastInsertRowId() {
    if (!m_db) return 0;
    return sqlite3_last_insert_rowid(m_db);
}

std::string SqliteDataStore::ErrMsg() {
    if (!m_db) return "database not open";
    return sqlite3_errmsg(m_db);
}

int64_t SqliteDataStore::Changes() {
    if (!m_db) return 0;
    return sqlite3_changes(m_db);
}

} // namespace animus::kernel

#pragma once

#include "animus_kernel/IDataStore.h"

#include <string>

struct sqlite3;

namespace animus::kernel {

// SqliteDataStore wraps a sqlite3* connection and implements IDataStore.
// Owns the connection lifetime.
class SqliteDataStore : public IDataStore {
public:
    // Opens (or creates) the database at dbPath.
    // Enables WAL mode and foreign keys.
    explicit SqliteDataStore(const std::string& dbPath);
    ~SqliteDataStore() override;

    SqliteDataStore(const SqliteDataStore&) = delete;
    SqliteDataStore& operator=(const SqliteDataStore&) = delete;

    bool Exec(const std::string& sql) override;
    std::unique_ptr<IStatement> Prepare(const std::string& sql) override;
    int64_t LastInsertRowId() override;
    std::string ErrMsg() override;
    int64_t Changes() override;

    // Direct access for migration code that needs raw sqlite3*.
    sqlite3* RawHandle() const { return m_db; }

    // Returns true if the database was opened successfully.
    bool IsOpen() const override { return m_db != nullptr; }

    // IDataStore — backend identification
    DataStoreDialect Dialect() const override { return DataStoreDialect::SQLite; }
    bool SupportsVectorSearch() const override { return false; }

private:
    sqlite3* m_db{nullptr};
};

} // namespace animus::kernel

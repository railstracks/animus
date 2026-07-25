#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace animus::kernel {

// ============================================================================
// IStatement — abstraction over a prepared statement handle
// ============================================================================

enum class ColumnType {
    Integer,
    Float,
    Text,
    Null,
    Unknown
};

// ============================================================================
// DataStoreDialect — identifies which backend is in use
// ============================================================================

enum class DataStoreDialect {
    SQLite,
    PostgreSQL
};

class IStatement {
public:
    virtual ~IStatement() = default;

    // Binding
    virtual bool BindInt(int idx, int val) = 0;
    virtual bool BindInt64(int idx, int64_t val) = 0;
    virtual bool BindDouble(int idx, double val) = 0;
    virtual bool BindText(int idx, const std::string& val) = 0;
    virtual bool BindBlob(int idx, const void* data, size_t size) = 0;
    virtual bool BindNull(int idx) = 0;

    // Execution

    // Step() — advance the statement one row.
    // Returns true if a row is available (use Column* to read it).
    // Returns false if done or error.
    // WARNING: For DML (INSERT/UPDATE/DELETE), Step() return values differ
    // between backends. SQLite returns false on success (SQLITE_DONE).
    // PostgreSQL returns true on success (PGRES_COMMAND_OK).
    // For DML, use ExecDML() instead.
    virtual bool Step() = 0;

    // ExecDML() — execute a DML statement (INSERT/UPDATE/DELETE).
    // Returns true on success, false on error (check IDataStore::ErrMsg()).
    // This is backend-agnostic and should be used for all write operations
    // instead of Step().
    virtual bool ExecDML() = 0;

    // Column access (only valid after Step() returned true)
    virtual int64_t ColumnInt64(int col) = 0;
    virtual double ColumnDouble(int col) = 0;
    virtual std::string ColumnText(int col) = 0;
    virtual std::vector<uint8_t> ColumnBlob(int col) = 0;
    virtual ColumnType GetColumnType(int col) = 0;
    virtual bool IsColumnNull(int col) = 0;

    // Lifecycle
    virtual void Reset() = 0;
    virtual void Finalize() = 0;
};

// ============================================================================
// IDataStore — abstraction over a database connection
// ============================================================================

class IDataStore {
public:
    virtual ~IDataStore() = default;

    // Execute a raw SQL statement (no result rows).
    virtual bool Exec(const std::string& sql) = 0;

    // Prepare a parameterized statement.
    virtual std::unique_ptr<IStatement> Prepare(const std::string& sql) = 0;

    // Last inserted row ID (after an INSERT).
    virtual int64_t LastInsertRowId() = 0;

    // Human-readable error message from the last failed operation.
    virtual std::string ErrMsg() = 0;

    // Backend identification — allows Stores to branch on SQL dialect.
    virtual DataStoreDialect Dialect() const = 0;

    // Whether this backend supports native vector search (pgvector).
    virtual bool SupportsVectorSearch() const = 0;

    // Whether the connection is established and usable.
    virtual bool IsOpen() const = 0;

    // Number of rows affected by the last INSERT/UPDATE/DELETE.
    // SQLite: returns sqlite3_changes().
    // PostgreSQL: returns PQcmdTuples() from last execution.
    virtual int64_t Changes() = 0;

    // Transaction support.
    virtual bool BeginTransaction() { return Exec("BEGIN"); }
    virtual bool Commit() { return Exec("COMMIT"); }
    virtual bool Rollback() { return Exec("ROLLBACK"); }
};

} // namespace animus::kernel

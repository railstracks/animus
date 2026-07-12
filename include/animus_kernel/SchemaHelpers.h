#pragma once

#include <string>

namespace animus::kernel {

class IDataStore;

// ============================================================================
// SchemaHelpers — dialect-aware SQL utilities for schema creation
// ============================================================================
//
// Key differences between SQLite and PostgreSQL DDL:
//   - SQLite: INTEGER PRIMARY KEY AUTOINCREMENT → PostgreSQL: SERIAL PRIMARY KEY
//   - SQLite: PRAGMA table_info → PostgreSQL: information_schema.columns
//   - SQLite: PRAGMA foreign_keys=OFF/ON → PostgreSQL: not needed (DDL is transactional)
//   - SQLite: ALTER TABLE ... RENAME TO → PostgreSQL: same syntax
//   - SQLite: CREATE VIRTUAL TABLE (FTS5) → PostgreSQL: skip (use pgvector/text search)
//
// For CRUD operations, PgDataStore translates ? → $1, $2... placeholders.
// Only schema-creation code needs dialect branching.

namespace schema {

// Returns true if a column exists in the given table.
// Uses PRAGMA table_info on SQLite, information_schema on PostgreSQL.
bool ColumnExists(IDataStore* store, const std::string& table, const std::string& column);

// Returns true if a table exists.
bool TableExists(IDataStore* store, const std::string& table);

// Returns true if an index exists.
bool IndexExists(IDataStore* store, const std::string& indexName);

// Execute CREATE TABLE with dialect-appropriate syntax.
// Replaces "INTEGER PRIMARY KEY AUTOINCREMENT" with "SERIAL PRIMARY KEY"
// when the dialect is PostgreSQL.
void CreateTable(IDataStore* store, const std::string& sql);

// Run a PRAGMA command on SQLite only (no-op on PostgreSQL).
void Pragma(IDataStore* store, const std::string& sql);

// Returns dialect-appropriate upsert SQL.
// SQLite: INSERT OR REPLACE INTO table (cols) VALUES (...)
// PostgreSQL: INSERT INTO table (cols) VALUES (...) ON CONFLICT (conflict_cols) DO UPDATE SET updates
std::string UpsertSql(IDataStore* store,
                     const std::string& table,
                     const std::string& columns,
                     const std::string& conflict_cols,
                     const std::string& update_set);

// Returns dialect-appropriate insert-ignore SQL.
// SQLite: INSERT OR IGNORE INTO table (cols) VALUES (...)
// PostgreSQL: INSERT INTO table (cols) VALUES (...) ON CONFLICT (conflict_cols) DO NOTHING
std::string InsertIgnoreSql(IDataStore* store,
                            const std::string& table,
                            const std::string& columns,
                            const std::string& conflict_cols);

// Returns the number of rows affected by the last INSERT/UPDATE/DELETE.
// SQLite: SELECT changes()
// PostgreSQL: uses PQcmdTuples on the last result
int64_t RowsAffected(IDataStore* store);

// Returns true if the last INSERT/UPDATE/DELETE affected at least one row.
bool DidAffectRows(IDataStore* store);

} // namespace schema

} // namespace animus::kernel
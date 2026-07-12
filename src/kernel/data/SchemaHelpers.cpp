#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/IDataStore.h"

#include <algorithm>
#include <regex>
#include <sstream>

namespace animus::kernel {

namespace schema {

bool ColumnExists(IDataStore* store, const std::string& table, const std::string& column) {
    if (!store) return false;

    if (store->Dialect() == DataStoreDialect::PostgreSQL) {
        auto stmt = store->Prepare(
            "SELECT COUNT(*) FROM information_schema.columns "
            "WHERE table_name = ? AND column_name = ?");
        if (!stmt) return false;
        stmt->BindText(1, table);
        stmt->BindText(2, column);
        if (stmt->Step()) {
            return stmt->ColumnInt64(0) > 0;
        }
        return false;
    }

    // SQLite
    auto stmt = store->Prepare("SELECT COUNT(*) FROM pragma_table_info('" + table + "') WHERE name=?");
    if (!stmt) return false;
    stmt->BindText(1, column);
    if (stmt->Step()) {
        return stmt->ColumnInt64(0) > 0;
    }
    return false;
}

bool TableExists(IDataStore* store, const std::string& table) {
    if (!store) return false;

    if (store->Dialect() == DataStoreDialect::PostgreSQL) {
        auto stmt = store->Prepare(
            "SELECT COUNT(*) FROM information_schema.tables WHERE table_name = ?");
        if (!stmt) return false;
        stmt->BindText(1, table);
        if (stmt->Step()) {
            return stmt->ColumnInt64(0) > 0;
        }
        return false;
    }

    // SQLite
    auto stmt = store->Prepare("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?");
    if (!stmt) return false;
    stmt->BindText(1, table);
    if (stmt->Step()) {
        return stmt->ColumnInt64(0) > 0;
    }
    return false;
}

bool IndexExists(IDataStore* store, const std::string& indexName) {
    if (!store) return false;

    if (store->Dialect() == DataStoreDialect::PostgreSQL) {
        auto stmt = store->Prepare(
            "SELECT COUNT(*) FROM pg_indexes WHERE indexname = ?");
        if (!stmt) return false;
        stmt->BindText(1, indexName);
        if (stmt->Step()) {
            return stmt->ColumnInt64(0) > 0;
        }
        return false;
    }

    // SQLite
    auto stmt = store->Prepare("SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name=?");
    if (!stmt) return false;
    stmt->BindText(1, indexName);
    if (stmt->Step()) {
        return stmt->ColumnInt64(0) > 0;
    }
    return false;
}

// Generate a comma-separated list of placeholders for the given number of columns.
// SQLite: ?,?,?,...  PostgreSQL: $1,$2,$3,...
static std::string PlaceholderList(IDataStore* store, const std::string& columns) {
    // Count commas + 1 to get column count
    int count = 1;
    for (char c : columns) {
        if (c == ',') count++;
    }

    std::string result;
    if (store->Dialect() == DataStoreDialect::PostgreSQL) {
        for (int i = 1; i <= count; i++) {
            if (i > 1) result += ",";
            result += "$" + std::to_string(i);
        }
    } else {
        for (int i = 0; i < count; i++) {
            if (i > 0) result += ",";
            result += "?";
        }
    }
    return result;
}

void CreateTable(IDataStore* store, const std::string& sql) {
    if (!store) return;

    if (store->Dialect() == DataStoreDialect::PostgreSQL) {
        // Transform SQLite DDL to PostgreSQL:
        // 1. INTEGER PRIMARY KEY AUTOINCREMENT → SERIAL PRIMARY KEY
        // 2. All other INTEGER columns → BIGINT (SQLite INTEGER is 64-bit variable;
        //    PostgreSQL INTEGER is 32-bit, which overflows with Unix millisecond timestamps)
        std::string pgSql = sql;

        // Replace "INTEGER PRIMARY KEY AUTOINCREMENT" with "SERIAL PRIMARY KEY"
        std::regex autoIncComma(
            R"(\bINTEGER\s+PRIMARY\s+KEY\s+AUTOINCREMENT\s*,)",
            std::regex::icase);
        pgSql = std::regex_replace(pgSql, autoIncComma, "SERIAL PRIMARY KEY,");

        // Also handle the case where it's the last column (no trailing comma)
        std::regex autoIncEnd(
            R"(\bINTEGER\s+PRIMARY\s+KEY\s+AUTOINCREMENT\s*\))",
            std::regex::icase);
        pgSql = std::regex_replace(pgSql, autoIncEnd, "SERIAL PRIMARY KEY)");

        // Replace remaining INTEGER with BIGINT
        // (after AUTOINCREMENT is already replaced)
        pgSql = std::regex_replace(pgSql, std::regex("\\bINTEGER\\b"), "BIGINT");

        // Replace REAL with DOUBLE PRECISION (PostgreSQL REAL is 4 bytes)
        pgSql = std::regex_replace(pgSql, std::regex("\\bREAL\\b"), "DOUBLE PRECISION");

        // Replace BLOB with BYTEA
        pgSql = std::regex_replace(pgSql, std::regex("\\bBLOB\\b", std::regex::icase), "BYTEA");

        store->Exec(pgSql);
        return;
    }

    store->Exec(sql);
}

void Pragma(IDataStore* store, const std::string& sql) {
    if (!store) return;
    if (store->Dialect() == DataStoreDialect::PostgreSQL) {
        // PRAGMAs are SQLite-only, no-op on PostgreSQL
        return;
    }
    store->Exec(sql);
}

std::string UpsertSql(IDataStore* store,
                     const std::string& table,
                     const std::string& columns,
                     const std::string& conflict_cols,
                     const std::string& update_set) {
    if (store->Dialect() == DataStoreDialect::PostgreSQL) {
        return "INSERT INTO " + table + " (" + columns + ") VALUES (" 
            + PlaceholderList(store, columns) + ") "
            + "ON CONFLICT (" + conflict_cols + ") DO UPDATE SET " + update_set;
    }
    return "INSERT OR REPLACE INTO " + table + " (" + columns + ") VALUES ("
        + PlaceholderList(store, columns) + ")";
}

std::string InsertIgnoreSql(IDataStore* store,
                            const std::string& table,
                            const std::string& columns,
                            const std::string& conflict_cols) {
    if (store->Dialect() == DataStoreDialect::PostgreSQL) {
        return "INSERT INTO " + table + " (" + columns + ") VALUES ("
            + PlaceholderList(store, columns) + ") "
            + "ON CONFLICT (" + conflict_cols + ") DO NOTHING";
    }
    return "INSERT OR IGNORE INTO " + table + " (" + columns + ") VALUES ("
        + PlaceholderList(store, columns) + ")";
}

bool DidAffectRows(IDataStore* store) {
    return RowsAffected(store) > 0;
}

int64_t RowsAffected(IDataStore* store) {
    if (!store) return 0;
    return store->Changes();
}

} // namespace schema

} // namespace animus::kernel
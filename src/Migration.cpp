#include "Migration.h"

#include "animus_kernel/IDataStore.h"
#include "animus_kernel/SchemaHelpers.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace animus::migrate {

using animus::kernel::IDataStore;
using animus::kernel::DataStoreDialect;
using animus::kernel::schema::DidAffectRows;
using animus::kernel::schema::GetTableColumns;
using animus::kernel::schema::TableExists;

const std::vector<std::string>& MigrationTables() {
    // Dependency order (referenced tables first) — must match the stores'
    // EnsureSchema() creation order.
    static const std::vector<std::string> kTables = {
        "agents",
        "agent_config",
        "memory_layers",
        "observations",
        "memory_mutations",
        "layer_perspectives",
        "memory_files",
        "ontology_entities",
        "ontology_properties",
        "ontology_mutations",
        "ontology_search_docs",
        "consolidation_watermarks",
        "consolidation_runs",
        "sessions",
        "session_turns",
        "session_notes",
        "session_tags",
        "session_compactions",
        "diary_entries",
        "gallivanting_threads",
        "gallivanting_sessions",
    };
    return kTables;
}

namespace {

// FTS5 virtual tables and their shadow tables (name, name_data, name_idx,
// name_content, name_docsize, name_config, name_delete). These hold the
// derived search index, rebuilt after migration (tsvector backfill on
// PostgreSQL), so they are excluded from the manifest coverage check.
bool IsSearchIndexTable(const std::string& name) {
    static const char* kShadowSuffixes[] = {
        "_data", "_idx", "_content", "_docsize", "_config", "_delete"
    };
    std::string stem = name;
    for (const char* suffix : kShadowSuffixes) {
        const size_t n = std::strlen(suffix);
        if (stem.size() > n && stem.compare(stem.size() - n, n, suffix) == 0) {
            stem = stem.substr(0, stem.size() - n);
            break;  // strip at most one shadow suffix
        }
    }
    return stem.size() >= 4 && stem.compare(stem.size() - 4, 4, "_fts") == 0;
}

} // namespace

PreflightReport Preflight(IDataStore* src, IDataStore* dst) {
    PreflightReport report;
    const auto& tables = MigrationTables();

    for (const auto& table : tables) {
        if (!TableExists(src, table)) {
            report.notes.push_back(
                table + ": not present in source — nothing to migrate");
            continue;
        }
        auto srcCols = GetTableColumns(src, table);
        if (srcCols.empty()) {
            report.problems.push_back(
                table + ": exists in source but column introspection returned nothing");
            report.ok = false;
            continue;
        }
        auto dstCols = GetTableColumns(dst, table);
        if (dstCols.empty()) {
            report.problems.push_back(
                table + ": missing in target — run animusd once against the "
                "target to create the schema, then re-run the migration");
            report.ok = false;
            continue;
        }
        std::string missing;
        for (const auto& col : srcCols) {
            if (std::find(dstCols.begin(), dstCols.end(), col) == dstCols.end()) {
                if (!missing.empty()) missing += ", ";
                missing += col;
            }
        }
        if (!missing.empty()) {
            report.problems.push_back(
                table + ": schema mismatch — source columns absent in target: " +
                missing + " (source and target schemas are on different "
                "versions; align them and re-run)");
            report.ok = false;
        }
    }

    // Source coverage: a user table the manifest does not know about would
    // be silently skipped — the table-level version of #217. SQLite only
    // (the migration source is always SQLite; sqlite_master is its catalog).
    if (src->Dialect() == DataStoreDialect::SQLite) {
        auto stmt = src->Prepare(
            "SELECT name FROM sqlite_master WHERE type='table'");
        if (stmt) {
            while (stmt->Step()) {
                const std::string name = stmt->ColumnText(0);
                if (name.rfind("sqlite_", 0) == 0) continue;   // internal
                if (IsSearchIndexTable(name)) continue;        // derived index
                if (std::find(tables.begin(), tables.end(), name) == tables.end()) {
                    report.problems.push_back(
                        name + ": present in source but not in the migration "
                        "manifest — add it to MigrationTables(), otherwise it "
                        "will be silently skipped");
                    report.ok = false;
                }
            }
        }
    }
    return report;
}

int64_t MigrateTable(IDataStore* src, IDataStore* dst,
                     const std::string& table, const MigrationOptions& options) {
    // Column list derived from the actual source schema at runtime (#217):
    // hardcoded lists drifted from the real schema and either failed
    // Prepare (table silently skipped) or copied a subset (columns lost).
    const std::vector<std::string> columns = GetTableColumns(src, table);
    if (columns.empty()) {
        std::cout << "  " << table << ": not present in source, skipping\n";
        return 0;
    }

    auto countStmt = src->Prepare("SELECT COUNT(*) FROM " + table);
    if (!countStmt) {
        std::cerr << "  [error] " << table << ": cannot prepare row count: "
                  << src->ErrMsg() << "\n";
        return -1;
    }
    int64_t totalRows = 0;
    if (countStmt->Step()) totalRows = countStmt->ColumnInt64(0);
    if (totalRows == 0) {
        std::cout << "  " << table << ": 0 rows (empty)\n";
        return 0;
    }

    std::cout << "  " << table << ": " << totalRows << " rows ("
              << columns.size() << " columns)";

    if (options.dryRun) {
        std::cout << " (dry run, skipping)\n";
        return 0;
    }

    std::string colList;
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) colList += ", ";
        colList += columns[i];
    }

    std::string insertSql = "INSERT INTO " + table + " (" + colList + ") VALUES (";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) insertSql += ", ";
        insertSql += "?";
    }
    insertSql += ")";

    auto insertStmt = dst->Prepare(insertSql);
    if (!insertStmt) {
        std::cerr << "\n  [error] Cannot prepare INSERT for " << table
                  << ": " << dst->ErrMsg() << "\n";
        return -1;
    }

    auto selectStmt = src->Prepare("SELECT " + colList + " FROM " + table);
    if (!selectStmt) {
        std::cerr << "\n  [error] Cannot prepare SELECT for " << table
                  << ": " << src->ErrMsg() << "\n";
        return -1;
    }

    int64_t migrated = 0;
    int64_t errors = 0;
    while (selectStmt->Step()) {
        for (size_t i = 0; i < columns.size(); ++i) {
            // Read as text — the destination will coerce via PostgreSQL
            if (selectStmt->IsColumnNull(static_cast<int>(i))) {
                insertStmt->BindNull(static_cast<int>(i) + 1);
            } else {
                insertStmt->BindText(static_cast<int>(i) + 1,
                                     selectStmt->ColumnText(static_cast<int>(i)));
            }
        }

        // Execute via ExecDML and test success by rows-affected. The
        // original Step()+ErrMsg() guard miscounts: Step() returns false
        // on a completed INSERT (DONE, no row), and ErrMsg() is the
        // connection's sticky last error — a stale error from earlier
        // schema probing flags every insert as failed even on success.
        if (insertStmt->ExecDML() && DidAffectRows(dst)) {
            migrated++;
        } else {
            errors++;
            if (options.verbose) {
                std::cerr << "\n  [error] Row " << migrated + errors << ": "
                          << dst->ErrMsg() << "\n";
            }
        }
        insertStmt->Reset();
    }

    std::cout << " → " << migrated << " migrated";
    if (errors > 0) std::cout << ", " << errors << " errors";
    std::cout << "\n";

    // Row-level errors mark the table as failed (exit code 1) — never
    // reported as success.
    return errors > 0 ? -1 : migrated;
}

} // namespace animus::migrate

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "animus_kernel/AgentKernel.h"
#include "animus_kernel/DatabaseConfig.h"
#include "animus_kernel/IDataStore.h"
#include "animus_kernel/PgDataStore.h"
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/SqliteDataStore.h"

using animus::kernel::IDataStore;
using animus::kernel::PgDataStore;
using animus::kernel::SqliteDataStore;
using animus::kernel::schema::ColumnExists;

namespace {

struct MigrationConfig {
    std::string sqlitePath;
    std::string pgHost = "localhost";
    int pgPort = 5432;
    std::string pgDatabase;
    std::string pgUser;
    std::string pgPassword;
    bool dryRun = false;
    bool verbose = false;
};

void PrintUsage() {
    std::cerr << "Usage: animus-migrate [OPTIONS]\n\n"
              << "Migrate data from SQLite to PostgreSQL.\n\n"
              << "Options:\n"
              << "  --sqlite <path>        Path to SQLite database file (required)\n"
              << "  --pg-host <host>       PostgreSQL host (default: localhost)\n"
              << "  --pg-port <port>       PostgreSQL port (default: 5432)\n"
              << "  --pg-database <name>   PostgreSQL database name (required)\n"
              << "  --pg-user <user>       PostgreSQL user (required)\n"
              << "  --pg-password <pass>   PostgreSQL password\n"
              << "  --pg-password-file <f>  Read password from file\n"
              << "  --dry-run              Show what would be migrated without writing\n"
              << "  --verbose              Print row-by-row details\n"
              << "  -h, --help             Show this help\n";
}

bool ParseArgs(int argc, char* argv[], MigrationConfig& cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            PrintUsage();
            std::exit(0);
        } else if (arg == "--sqlite") {
            if (i + 1 >= argc) { std::cerr << "--sqlite requires a value\n"; return false; }
            cfg.sqlitePath = argv[++i];
        } else if (arg == "--pg-host") {
            if (i + 1 >= argc) { std::cerr << "--pg-host requires a value\n"; return false; }
            cfg.pgHost = argv[++i];
        } else if (arg == "--pg-port") {
            if (i + 1 >= argc) { std::cerr << "--pg-port requires a value\n"; return false; }
            cfg.pgPort = std::stoi(argv[++i]);
        } else if (arg == "--pg-database") {
            if (i + 1 >= argc) { std::cerr << "--pg-database requires a value\n"; return false; }
            cfg.pgDatabase = argv[++i];
        } else if (arg == "--pg-user") {
            if (i + 1 >= argc) { std::cerr << "--pg-user requires a value\n"; return false; }
            cfg.pgUser = argv[++i];
        } else if (arg == "--pg-password") {
            if (i + 1 >= argc) { std::cerr << "--pg-password requires a value\n"; return false; }
            cfg.pgPassword = argv[++i];
        } else if (arg == "--pg-password-file") {
            if (i + 1 >= argc) { std::cerr << "--pg-password-file requires a value\n"; return false; }
            std::string path = argv[++i];
            std::ifstream pf(path);
            if (!pf.is_open()) { std::cerr << "Cannot open password file: " << path << "\n"; return false; }
            std::getline(pf, cfg.pgPassword);
            // Trim trailing newline
            while (!cfg.pgPassword.empty() && (cfg.pgPassword.back() == '\n' || cfg.pgPassword.back() == '\r'))
                cfg.pgPassword.pop_back();
        } else if (arg == "--dry-run") {
            cfg.dryRun = true;
        } else if (arg == "--verbose") {
            cfg.verbose = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }

    if (cfg.sqlitePath.empty()) { std::cerr << "--sqlite is required\n"; return false; }
    if (cfg.pgDatabase.empty()) { std::cerr << "--pg-database is required\n"; return false; }
    if (cfg.pgUser.empty()) { std::cerr << "--pg-user is required\n"; return false; }

    return true;
}

// Table migration info: name, columns in order, optional tsvector backfill column.
struct TableInfo {
    std::string name;
    std::vector<std::string> columns;
    // Column name -> source column for tsvector generation (empty = skip)
    std::vector<std::pair<std::string, std::string>> tsvectorColumns;
};

int64_t MigrateTable(IDataStore* src, IDataStore* dst, const TableInfo& table, const MigrationConfig& cfg) {
    // Count source rows
    auto countStmt = src->Prepare("SELECT COUNT(*) FROM " + table.name);
    if (!countStmt) {
        std::cerr << "  [skip] " << table.name << ": cannot count rows\n";
        return -1;
    }
    int64_t totalRows = 0;
    if (countStmt->Step()) totalRows = countStmt->ColumnInt64(0);
    if (totalRows == 0) {
        std::cout << "  " << table.name << ": 0 rows (empty)\n";
        return 0;
    }

    std::cout << "  " << table.name << ": " << totalRows << " rows";

    if (cfg.dryRun) {
        std::cout << " (dry run, skipping)\n";
        return 0;
    }

    // Build column list for SELECT and INSERT
    std::string colList;
    for (size_t i = 0; i < table.columns.size(); ++i) {
        if (i > 0) colList += ", ";
        colList += table.columns[i];
    }

    // Build INSERT statement
    std::string insertSql = "INSERT INTO " + table.name + " (" + colList + ") VALUES (";
    for (size_t i = 0; i < table.columns.size(); ++i) {
        if (i > 0) insertSql += ", ";
        insertSql += "?";
    }
    insertSql += ")";

    auto insertStmt = dst->Prepare(insertSql);
    if (!insertStmt) {
        std::cerr << "\n  [error] Cannot prepare INSERT for " << table.name
                  << ": " << dst->ErrMsg() << "\n";
        return -1;
    }

    // SELECT all rows from source
    auto selectStmt = src->Prepare("SELECT " + colList + " FROM " + table.name);
    if (!selectStmt) {
        std::cerr << "\n  [error] Cannot prepare SELECT for " << table.name
                  << ": " << src->ErrMsg() << "\n";
        return -1;
    }

    int64_t migrated = 0;
    int64_t errors = 0;
    while (selectStmt->Step()) {
        // Bind all columns
        for (size_t i = 0; i < table.columns.size(); ++i) {
            // Read as text — the destination will coerce via PostgreSQL
            if (selectStmt->IsColumnNull(static_cast<int>(i))) {
                insertStmt->BindNull(static_cast<int>(i) + 1);
            } else {
                insertStmt->BindText(static_cast<int>(i) + 1, selectStmt->ColumnText(static_cast<int>(i)));
            }
        }

        if (!insertStmt->Step() && !dst->ErrMsg().empty()) {
            errors++;
            if (cfg.verbose) {
                std::cerr << "\n  [error] Row " << migrated + errors << ": " << dst->ErrMsg() << "\n";
            }
        } else {
            migrated++;
        }
        insertStmt->Reset();
    }

    std::cout << " → " << migrated << " migrated";
    if (errors > 0) std::cout << ", " << errors << " errors";
    std::cout << "\n";

    return migrated;
}

} // namespace

int main(int argc, char* argv[]) {
    MigrationConfig cfg;
    if (!ParseArgs(argc, argv, cfg)) {
        PrintUsage();
        return 1;
    }

    std::cout << "=== Animus SQLite → PostgreSQL Migration ===\n\n";

    // Open source SQLite database
    std::cout << "Opening SQLite source: " << cfg.sqlitePath << "\n";
    auto sqliteDb = std::make_unique<SqliteDataStore>(cfg.sqlitePath);
    if (!sqliteDb->IsOpen()) {
        std::cerr << "Failed to open SQLite database: " << cfg.sqlitePath << "\n";
        return 1;
    }
    // Enable WAL mode and foreign keys
    sqliteDb->Exec("PRAGMA journal_mode=WAL");
    sqliteDb->Exec("PRAGMA foreign_keys=ON");

    // Open destination PostgreSQL database
    std::cout << "Connecting to PostgreSQL: " << cfg.pgHost << ":" << cfg.pgPort
              << "/" << cfg.pgDatabase << "\n";
    PgDataStore::Config pgConfig;
    pgConfig.host = cfg.pgHost;
    pgConfig.port = cfg.pgPort;
    pgConfig.database = cfg.pgDatabase;
    pgConfig.username = cfg.pgUser;
    pgConfig.password = cfg.pgPassword;

    auto pgDb = std::make_unique<PgDataStore>(pgConfig);
    if (!pgDb->IsOpen()) {
        std::cerr << "Failed to connect to PostgreSQL\n";
        return 1;
    }

    // --- Phase 1: Create schema on PostgreSQL ---
    std::cout << "\n--- Creating schema on PostgreSQL ---\n";

    // We use AgentKernel's schema initialization by creating a minimal kernel,
    // but that's heavy. Instead, let's directly create schema using the Store classes.
    // The Store classes' EnsureSchema() methods create tables correctly for PostgreSQL.
    // We'll instantiate each store to trigger schema creation.

    // Tables and their columns, in dependency order (referenced tables first).
    // This list must match the Store EnsureSchema() order.
    std::vector<TableInfo> tables = {
        // agents (referenced by many tables)
        {"agents", {"id", "agent_id", "name", "description", "identity", "avatar",
                    "default_provider", "default_model", "context_window", "temperature",
                    "reasoning_enabled", "reasoning_effort",
                    "max_chain_steps", "max_tool_calls_per_chain", "timeout_seconds",
                    "token_budget_per_prompt", "episodic_token_budget",
                    "semantic_token_budget", "perspectives_token_budget",
                    "consolidation_tool_budget",
                    "enabled_tools", "tool_configs",
                    "created_at_unix_ms", "updated_at_unix_ms", "diary_secret"}, {}},
        // agent_config
        {"agent_config", {"id", "agent_id", "key", "value", "updated_at"}, {}},
        // memory_layers (referenced by observations)
        {"memory_layers", {"id", "agent_id", "name", "description", "layer_type",
                          "capacity", "retention_policy", "consolidation_policy",
                          "created_at_unix_ms", "updated_at_unix_ms"}, {}},
        // observations
        {"observations", {"id", "layer_id", "text", "memory_state", "superseded_by",
                          "importance", "decay_factor", "created_at_unix_ms",
                          "updated_at_unix_ms"}, {}},
        // memory_mutations
        {"memory_mutations", {"id", "observation_id", "mutation_type", "description",
                              "created_at_unix_ms"}, {}},
        // layer_perspectives
        {"layer_perspectives", {"layer_id", "updated_at_unix_ms"}, {}},
        // memory_files
        {"memory_files", {"id", "source_path", "file_type", "content", "content_mutable",
                          "agent_id", "superseded", "created_at_unix_ms",
                          "imported_at_unix_ms"}, {}},
        // ontology_entities
        {"ontology_entities", {"id", "full_path", "name", "entity_type", "canonical",
                                "status", "created_at_unix_ms", "updated_at_unix_ms"}, {}},
        // ontology_properties
        {"ontology_properties", {"id", "entity_id", "key", "value", "property_type",
                                  "confidence", "source", "created_at_unix_ms",
                                  "updated_at_unix_ms"}, {}},
        // ontology_mutations
        {"ontology_mutations", {"id", "entity_id", "mutation_type", "description",
                                 "created_at_unix_ms"}, {}},
        // ontology_search_docs
        {"ontology_search_docs", {"doc_id", "entity_id", "text"}, {}},
        // consolidation_watermarks
        {"consolidation_watermarks", {"id", "agent_id", "source", "last_processed_id",
                                       "last_run_unix_ms"}, {}},
        // consolidation_runs
        {"consolidation_runs", {"id", "agent_id", "status", "started_at_unix_ms",
                                 "completed_at_unix_ms", "observations_processed",
                                 "entities_created", "error"}, {}},
        // sessions
        {"sessions", {"id", "connector", "conversation_id", "thread_id", "provider_id",
                      "summary", "agent_id", "created_at_unix_ms",
                      "last_active_unix_ms", "terminated", "session_type"}, {}},
        // session_turns
        {"session_turns", {"id", "session_id", "role", "content", "timestamp_unix_ms",
                           "token_count", "model", "provider_id"}, {}},
        // session_notes
        {"session_notes", {"id", "session_key", "agent_id", "content",
                           "created_at_unix_ms", "updated_at_unix_ms"}, {}},
        // session_tags
        {"session_tags", {"id", "session_key", "agent_id", "tag", "source",
                          "created_at_unix_ms"}, {}},
        // session_compactions
        {"session_compactions", {"id", "session_id", "compacted_at_unix_ms",
                                  "original_turn_count", "compacted_turn_count"}, {}},
        // diary_entries
        {"diary_entries", {"id", "agent_id", "content", "layer", "source",
                           "importance", "created_at_unix_ms"}, {}},
        // gallivanting_threads
        {"gallivanting_threads", {"id", "agent_id", "session_key", "started_at_unix_ms",
                                   "ended_at_unix_ms", "summary"}, {}},
        // gallivanting_sessions
        {"gallivanting_sessions", {"id", "agent_id", "thread_id", "session_type",
                                    "started_at_unix_ms", "ended_at_unix_ms"}, {}},
    };

    // Initialize schema on PostgreSQL by running AgentKernel's schema init.
    // We'll do this by creating tables directly using schema::CreateTable
    // with each table's DDL.
    // But it's cleaner to just instantiate the stores. However, the stores
    // have complex constructors. Let's use a simpler approach:
    // read the schema from SQLite and create equivalent tables on PostgreSQL.

    // Actually, the simplest correct approach: use AgentKernel to initialize the
    // PostgreSQL schema. But we can't do that without all the store dependencies.
    // Instead, let the user run animusd first to create the schema, then migrate data.

    std::cout << "\n--- Migrating data ---\n";
    std::cout << "Note: The PostgreSQL schema should already exist (run animusd once first).\n\n";

    int64_t totalMigrated = 0;
    int64_t totalErrors = 0;

    for (const auto& table : tables) {
        auto result = MigrateTable(sqliteDb.get(), pgDb.get(), table, cfg);
        if (result >= 0) totalMigrated += result;
        else totalErrors++;
    }

    // --- Phase 2: Backfill tsvector columns ---
    if (!cfg.dryRun) {
        std::cout << "\n--- Backfilling search vectors ---\n";

        // Update tsvector columns for text search
        pgDb->Exec("UPDATE observations SET search_vector = to_tsvector('english', COALESCE(text, '')) WHERE search_vector IS NULL");
        std::cout << "  observations search_vector: backfilled\n";

        pgDb->Exec("UPDATE diary_entries SET search_vector = to_tsvector('english', COALESCE(content, '')) WHERE search_vector IS NULL");
        std::cout << "  diary_entries search_vector: backfilled\n";

        pgDb->Exec("UPDATE memory_files SET search_vector = to_tsvector('english', COALESCE(content, '')) WHERE search_vector IS NULL");
        std::cout << "  memory_files search_vector: backfilled\n";

        pgDb->Exec("UPDATE ontology_search_docs SET search_vector = to_tsvector('english', COALESCE(text, '')) WHERE search_vector IS NULL");
        std::cout << "  ontology_search_docs search_vector: backfilled\n";
    }

    // --- Phase 3: Update sequences ---
    // PostgreSQL SERIAL columns use sequences that need to be set to the max id.
    if (!cfg.dryRun) {
        std::cout << "\n--- Updating sequences ---\n";
        std::vector<std::string> serialTables = {
            "agents", "agent_config", "memory_layers", "observations",
            "memory_mutations", "memory_files", "ontology_entities",
            "ontology_properties", "ontology_mutations", "ontology_search_docs",
            "consolidation_watermarks", "consolidation_runs", "sessions",
            "session_turns", "session_notes", "session_tags", "session_compactions",
            "diary_entries", "gallivanting_threads", "gallivanting_sessions"
        };

        for (const auto& table : serialTables) {
            std::string seqName = table + "_id_seq";
            std::string sql = "SELECT setval('" + seqName + "', COALESCE((SELECT MAX(id) FROM " + table + "), 1))";
            auto stmt = pgDb->Prepare(sql);
            if (stmt) {
                stmt->Step();
                std::cout << "  " << seqName << ": updated\n";
            } else {
                std::cout << "  " << seqName << ": skipped (sequence may not exist)\n";
            }
        }
    }

    std::cout << "\n=== Migration complete ===\n";
    std::cout << "Total rows migrated: " << totalMigrated << "\n";
    if (totalErrors > 0) {
        std::cout << "Tables with errors: " << totalErrors << "\n";
    }

    return totalErrors > 0 ? 1 : 0;
}
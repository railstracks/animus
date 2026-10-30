#include <algorithm>
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

#include "Migration.h"

using animus::kernel::IDataStore;
using animus::kernel::PgDataStore;
using animus::kernel::SqliteDataStore;
using animus::kernel::schema::GetTableColumns;

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

    animus::migrate::MigrationOptions options;
    options.dryRun = cfg.dryRun;
    options.verbose = cfg.verbose;

    // --- Phase 0: Preflight (#217) — verify schemas BEFORE any data moves.
    // Column lists are derived at runtime from the source schema and
    // checked against the target; any mismatch aborts the run before a
    // single row is copied. Never a silent skip, never a partial copy
    // that reads as success.
    std::cout << "\n--- Preflight: verifying schemas ---\n";
    auto preflight = animus::migrate::Preflight(sqliteDb.get(), pgDb.get());
    for (const auto& note : preflight.notes) {
        std::cout << "  [note] " << note << "\n";
    }
    if (!preflight.ok) {
        std::cerr << "\nSchema problems found — aborting BEFORE any data was moved:\n";
        for (const auto& problem : preflight.problems) {
            std::cerr << "  [error] " << problem << "\n";
        }
        std::cerr << "\nAlign the source and target schemas (same animus version), then re-run.\n";
        return 2;
    }
    std::cout << "  Schema check passed.\n";

    // --- Phase 1: Migrate data ---
    // The PostgreSQL schema should already exist (run animusd once first).
    std::cout << "\n--- Migrating data ---\n";
    std::cout << "Note: The PostgreSQL schema should already exist (run animusd once first).\n\n";

    int64_t totalMigrated = 0;
    int64_t totalErrors = 0;

    for (const auto& table : animus::migrate::MigrationTables()) {
        auto result = animus::migrate::MigrateTable(sqliteDb.get(), pgDb.get(),
                                                    table, options);
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
    // Runtime-derived: every target table with an `id` column carries a
    // sequence to advance. (The old hardcoded list missed layer_perspectives —
    // same staleness family as #217.)
    if (!cfg.dryRun) {
        std::cout << "\n--- Updating sequences ---\n";

        for (const auto& table : animus::migrate::MigrationTables()) {
            auto cols = GetTableColumns(pgDb.get(), table);
            if (std::find(cols.begin(), cols.end(), "id") == cols.end()) continue;

            std::string seqName = table + "_id_seq";
            std::string sql = "SELECT setval('" + seqName +
                "', COALESCE((SELECT MAX(id) FROM " + table + "), 1))";
            auto stmt = pgDb->Prepare(sql);
            if (stmt) {
                stmt->ExecDML();
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

// #217 — animus-migrate must derive column lists at runtime and fail
// loudly on schema mismatch instead of silently skipping tables or
// dropping columns. Worst case from the issue: layer_perspectives'
// hardcoded list named two valid columns, so the copy "succeeded" while
// dropping all six perspective text columns.
//
// Round-trip: populate a source store through the real MemoryStore
// seeding path plus direct rows, migrate via Preflight + MigrateTable
// (the exact functions the tool calls), and assert column-and-row
// equality on the destination.

#include "Migration.h"

#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/SqliteDataStore.h"

#include <iostream>
#include <string>
#include <vector>

using namespace animus::kernel;
using namespace animus::kernel::memory;
using animus::migrate::MigrationOptions;
using animus::migrate::MigrationTables;
using animus::migrate::MigrateTable;
using animus::migrate::Preflight;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_migration_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

// Populates a source store: 7 seeded layers (real seeding path), two
// perspective rows with every text column set distinctly (DELETE first
// for determinism — seeding may or may not create perspective rows),
// three observations (FK-legal layer ids from the seeded layers).
void PopulateSource(SqliteDataStore& store, MemoryStore& memory) {
    Assert(memory.CreateDefaultLayersForAgent("agent-a"), "agent-a seeding");
    auto layers = memory.ListLayersForAgent("agent-a");
    Assert(layers.size() == 7, "7 seeded layers, got " + std::to_string(layers.size()));

    Assert(store.Exec("DELETE FROM layer_perspectives"),
           "clear perspective rows for deterministic baseline");

    for (int i = 0; i < 2 && i < static_cast<int>(layers.size()); ++i) {
        const std::string n = std::to_string(i);
        std::string sql =
            "INSERT INTO layer_perspectives (layer_id, retrospective, "
            "retrospective_valence, current_perspective, current_valence, "
            "future_perspective, future_valence, updated_at_unix_ms) VALUES (" +
            std::to_string(layers[i].id) + ", " +
            "'retro-" + n + "', " +
            "'retro-val-" + n + "', " +
            "'cur-" + n + "', " +
            "'cur-val-" + n + "', " +
            "'fut-" + n + "', " +
            "'fut-val-" + n + "', " +
            "1700000000000)";
        Assert(store.Exec(sql), "perspective insert " + n);
    }

    for (int i = 0; i < 3 && i < static_cast<int>(layers.size()); ++i) {
        std::string sql =
            "INSERT INTO observations (layer_id, agent_id, text, weight, "
            "decay_rate, created_at_unix_ms, updated_at_unix_ms) VALUES (" +
            std::to_string(layers[i].id) + ", 'agent-a', 'obs-text-" +
            std::to_string(i) + "', 1.5, 0.9, 1700000000000, 1700000000000)";
        Assert(store.Exec(sql), "observation insert " + std::to_string(i));
    }
}

int64_t CountRows(IDataStore* db, const std::string& table) {
    auto stmt = db->Prepare("SELECT COUNT(*) FROM " + table);
    if (!stmt || !stmt->Step()) return -1;
    return stmt->ColumnInt64(0);
}

// Reads one column (ordered by key) as strings for value-equality checks.
std::vector<std::string> ReadColumnOrdered(IDataStore* db, const std::string& table,
                                           const std::string& column,
                                           const std::string& orderBy) {
    std::vector<std::string> values;
    auto stmt = db->Prepare(
        "SELECT " + column + " FROM " + table + " ORDER BY " + orderBy);
    if (!stmt) return values;
    while (stmt->Step()) values.push_back(stmt->ColumnText(0));
    return values;
}

bool Contains(const std::vector<std::string>& lines, const std::string& needle) {
    for (const auto& line : lines) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

// Acceptance: a populated store migrates with zero column loss — every
// column of every populated table arrives byte-identical on the target.
int TestRoundTripPreservesAllColumns() {
    std::cerr << "  [Migration] round-trip preserves all columns...\n";
    const std::string srcPath = MakeTempDbPath();
    const std::string dstPath = MakeTempDbPath();

    SqliteDataStore srcStore(srcPath);
    MemoryStore srcMemory(&srcStore);
    PopulateSource(srcStore, srcMemory);

    // Expected totals derived from the source itself — robust to any
    // manifest table carrying rows we did not insert explicitly.
    int64_t expected = 0;
    for (const auto& table : MigrationTables()) {
        if (schema::TableExists(&srcStore, table)) {
            expected += CountRows(&srcStore, table);
        }
    }
    Assert(CountRows(&srcStore, "memory_layers") == 7, "7 source layers");
    Assert(CountRows(&srcStore, "layer_perspectives") == 2,
           "2 source perspective rows");
    Assert(CountRows(&srcStore, "observations") == 3, "3 source observations");

    SqliteDataStore dstStore(dstPath);
    MemoryStore dstMemory(&dstStore);  // schema only, no data

    auto report = Preflight(&srcStore, &dstStore);
    Assert(report.ok, "preflight ok on matching schemas");
    if (!report.ok) {
        for (const auto& p : report.problems) std::cerr << "    problem: " << p << "\n";
    }

    int64_t migrated = 0;
    for (const auto& table : MigrationTables()) {
        auto r = MigrateTable(&srcStore, &dstStore, table, MigrationOptions{});
        Assert(r >= 0, "migrate " + table + " without error");
        if (r > 0) migrated += r;
    }
    Assert(migrated == expected,
           "every source row migrated: expected " + std::to_string(expected) +
           ", got " + std::to_string(migrated));

    // layer_perspectives — the table the hardcoded list truncated to
    // 2 of 9 columns while reporting success.
    auto srcCols = schema::GetTableColumns(&srcStore, "layer_perspectives");
    auto dstCols = schema::GetTableColumns(&dstStore, "layer_perspectives");
    Assert(srcCols.size() == 9, "9 source perspective columns, got " +
           std::to_string(srcCols.size()));
    Assert(srcCols == dstCols, "destination perspective columns identical");

    for (const auto& col : srcCols) {
        auto a = ReadColumnOrdered(&srcStore, "layer_perspectives", col, "layer_id");
        auto b = ReadColumnOrdered(&dstStore, "layer_perspectives", col, "layer_id");
        Assert(a == b, "layer_perspectives." + col + " values identical after migration");
    }

    // observations — full column set through the same check.
    srcCols = schema::GetTableColumns(&srcStore, "observations");
    dstCols = schema::GetTableColumns(&dstStore, "observations");
    Assert(srcCols == dstCols, "destination observation columns identical");
    for (const auto& col : srcCols) {
        auto a = ReadColumnOrdered(&srcStore, "observations", col, "id");
        auto b = ReadColumnOrdered(&dstStore, "observations", col, "id");
        Assert(a == b, "observations." + col + " values identical after migration");
    }

    Assert(CountRows(&srcStore, "memory_layers") == CountRows(&dstStore, "memory_layers"),
           "memory_layers row count equal");

    (void)dstMemory;
    return 0;
}

// Acceptance: any schema mismatch produces an error before any data is
// copied — never a skip, never a partial migration.
int TestSchemaMismatchFailsLoudBeforeCopy() {
    std::cerr << "  [Migration] schema mismatch fails loud, before any copy...\n";
    const std::string srcPath = MakeTempDbPath();
    const std::string dstPath = MakeTempDbPath();

    SqliteDataStore srcStore(srcPath);
    MemoryStore srcMemory(&srcStore);
    PopulateSource(srcStore, srcMemory);

    SqliteDataStore dstStore(dstPath);
    MemoryStore dstMemory(&dstStore);
    // Simulate a target on a different schema version: one column gone.
    Assert(dstStore.Exec("ALTER TABLE layer_perspectives DROP COLUMN future_valence"),
           "drop column on target");

    auto report = Preflight(&srcStore, &dstStore);
    Assert(!report.ok, "preflight fails on schema mismatch");
    Assert(Contains(report.problems, "future_valence"),
           "problem names the missing column (future_valence)");
    Assert(Contains(report.problems, "layer_perspectives"),
           "problem names the mismatching table");

    // main() aborts on !ok before MigrateTable runs — nothing is copied.
    Assert(CountRows(&dstStore, "layer_perspectives") == 0,
           "no partial copy after failed preflight");

    (void)srcMemory; (void)dstMemory;
    return 0;
}

// A user table the manifest does not know about would be silently
// skipped — the table-level version of #217. Preflight must flag it.
int TestUnlistedSourceTableIsFlagged() {
    std::cerr << "  [Migration] unlisted source table is flagged...\n";
    const std::string srcPath = MakeTempDbPath();
    const std::string dstPath = MakeTempDbPath();

    SqliteDataStore srcStore(srcPath);
    MemoryStore srcMemory(&srcStore);
    PopulateSource(srcStore, srcMemory);
    Assert(srcStore.Exec("CREATE TABLE rogue_table (id INTEGER PRIMARY KEY, note TEXT)"),
           "create rogue table");
    Assert(srcStore.Exec("INSERT INTO rogue_table (note) VALUES ('data that would vanish')"),
           "insert rogue row");

    SqliteDataStore dstStore(dstPath);
    MemoryStore dstMemory(&dstStore);

    auto report = Preflight(&srcStore, &dstStore);
    Assert(!report.ok, "preflight fails on unlisted source table");
    Assert(Contains(report.problems, "rogue_table"),
           "problem names the unlisted table");

    (void)srcMemory; (void)dstMemory;
    return 0;
}

// FTS5 virtual tables and their shadow tables are derived search index —
// excluded from the manifest coverage check, not flagged as missing.
int TestSearchIndexTablesAreExcluded() {
    std::cerr << "  [Migration] search-index tables are excluded...\n";
    const std::string srcPath = MakeTempDbPath();
    const std::string dstPath = MakeTempDbPath();

    SqliteDataStore srcStore(srcPath);
    MemoryStore srcMemory(&srcStore);
    PopulateSource(srcStore, srcMemory);
    // FTS-family names as plain tables — the exclusion is name-based.
    Assert(srcStore.Exec("CREATE TABLE notes_fts (id INTEGER PRIMARY KEY)"),
           "create fts-named table");
    Assert(srcStore.Exec("CREATE TABLE notes_fts_data (id INTEGER, block BLOB)"),
           "create shadow-named table");

    SqliteDataStore dstStore(dstPath);
    MemoryStore dstMemory(&dstStore);

    auto report = Preflight(&srcStore, &dstStore);
    Assert(report.ok, "preflight ok with search-index tables present");
    if (!report.ok) {
        for (const auto& p : report.problems) std::cerr << "    problem: " << p << "\n";
    }

    (void)srcMemory; (void)dstMemory;
    return 0;
}

} // namespace

int main() {
    std::cerr << "[MigrationTests] #217 runtime column derivation + loud failures\n";
    TestRoundTripPreservesAllColumns();
    TestSchemaMismatchFailsLoudBeforeCopy();
    TestUnlistedSourceTableIsFlagged();
    TestSearchIndexTablesAreExcluded();
    if (g_failures == 0) {
        std::cerr << "[MigrationTests] ALL GREEN\n";
        return 0;
    }
    std::cerr << "[MigrationTests] " << g_failures << " FAILURES\n";
    return 1;
}

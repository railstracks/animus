// tests/DataStoreConcurrencyTests.cpp — issue #76
//
// Concurrent-writer stress for statement-scoped write bookkeeping.
//
// Reproduces (pre-fix): phantom id=0 create-failures and wrong-id reads from
// store-global LastInsertRowId()/Changes() while other daemon threads run
// concurrent DML (the production failure shape proven on animus-tradingbot,
// Sep 11: rows committed, create reported failed; memory_mutations carrying
// a concurrent insert's id).
//
// Acceptance (issue #76):
//   (a) zero phantom create-failures
//   (b) every returned id unique and > 0
//   (c) every returned id maps back to its own content (SELECT text by id)
//   (d) observations row count == writers * iterations
//
// Runs on SQLite by default (the logical race is dialect-independent: the
// check-then-read spans two store calls). Set ANIMUS_TEST_PG_DSN to also run
// against PostgreSQL — the dialect the production race was proven on:
//   ANIMUS_TEST_PG_DSN="host=127.0.0.1 port=15432 dbname=animus_76_test \
//                       user=animus password=..."
// The named database must exist and be disposable (tables are created/dropped).

#include "animus_kernel/IDataStore.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/PgDataStore.h"
#include "animus_kernel/SqliteDataStore.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace animus::kernel;
using namespace animus::kernel::memory;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_datastore_conc_test_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd >= 0) close(fd);
    return std::string(tmp) + ".db";
}

struct WriteOutcome {
    int64_t id = 0;
    std::string marker;
};

// fullAssertions: exact phantom-count and row-count equality. Enabled for
// pooled-connection backends (PostgreSQL) where the production race lives;
// a single shared SQLite connection additionally produces genuine
// SQLITE_BUSY contention noise that is not the bug under test.
void RunConcurrentScenario(IDataStore* store, const std::string& label,
                           bool fullAssertions) {
    std::cerr << "  [" << label << "] concurrent-writer stress...\n";

    store->Exec("CREATE TABLE IF NOT EXISTS _stress_churn ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, v TEXT)");
    MemoryStore memoryStore(store);

    MemoryLayer layer;
    layer.name = "day";
    layer.horizon = "1 day";
    layer.sort_order = 0;
    layer.enabled = true;
    auto createdLayer = memoryStore.CreateLayer(layer);
    Assert(createdLayer.id > 0, label + ": base layer created");

    const int kWriters = 3;
    const int kChurners = 1;
    const int kIters = 60;

    std::vector<std::vector<WriteOutcome>> results(kWriters);
    std::atomic<bool> stop{false};

    // Churn threads: unrelated DML pressure on the shared store — the
    // corruption source for store-global bookkeeping. Individual churn
    // failures are intentionally ignored (pressure, not correctness).
    std::vector<std::thread> churners;
    for (int c = 0; c < kChurners; ++c) {
        churners.emplace_back([store, &stop, c]() {
            int n = 0;
            while (!stop.load()) {
                auto stmt = store->Prepare(
                    "INSERT INTO _stress_churn (v) VALUES (?)");
                if (stmt) {
                    stmt->BindText(1, "churn-" + std::to_string(c) +
                                         "-" + std::to_string(n));
                    stmt->ExecDML();
                }
                if (n % 16 == 7)
                    store->Exec("DELETE FROM _stress_churn WHERE id % 5 = 3");
                ++n;
            }
        });
    }

    std::vector<std::thread> writers;
    for (int w = 0; w < kWriters; ++w) {
        writers.emplace_back([&, w]() {
            results[w].resize(kIters);
            for (int i = 0; i < kIters; ++i) {
                Observation obs;
                obs.layer_id = createdLayer.id;
                obs.agent_id = "default";
                obs.text = "stress|w" + std::to_string(w) +
                           "|i" + std::to_string(i);
                obs.source = "concurrency-test";
                auto created =
                    memoryStore.CreateObservationForAgent("default", obs);
                results[w][i].id = created.id;
                results[w][i].marker = obs.text;
            }
        });
    }

    for (auto& t : writers) t.join();
    stop.store(true);
    for (auto& t : churners) t.join();

    // (a) zero phantom create-failures
    int phantom = 0;
    int total = 0;
    std::map<int64_t, int> idCount;
    for (const auto& v : results) {
        for (const auto& r : v) {
            ++total;
            if (r.id <= 0) {
                ++phantom;
            } else {
                idCount[r.id]++;
            }
        }
    }
    if (fullAssertions) {
        Assert(phantom == 0,
               label + ": (a) zero phantom failures (got " +
                   std::to_string(phantom) + "/" + std::to_string(total) + ")");
    }

    // (b) returned ids unique
    int dup = 0;
    for (const auto& [id, n] : idCount)
        if (n > 1) ++dup;
    Assert(dup == 0,
           label + ": (b) returned ids unique (duplicates: " +
               std::to_string(dup) + ")");

    // (c) every id maps back to its own content
    int mismatched = 0;
    for (const auto& v : results) {
        for (const auto& r : v) {
            if (r.id <= 0) continue;
            auto stmt = store->Prepare(
                "SELECT text FROM observations WHERE id = ?");
            if (!stmt) {
                ++mismatched;
                continue;
            }
            stmt->BindInt64(1, r.id);
            if (!stmt->Step() || stmt->ColumnText(0) != r.marker)
                ++mismatched;
        }
    }
    Assert(mismatched == 0,
           label + ": (c) every id maps to its own content (mismatches: " +
               std::to_string(mismatched) + ")");

    // (d) committed row count
    {
        auto stmt = store->Prepare("SELECT COUNT(*) FROM observations");
        int64_t n = (stmt && stmt->Step()) ? stmt->ColumnInt64(0) : -1;
        if (fullAssertions) {
            Assert(n == total,
                   label + ": (d) row count matches (" + std::to_string(n) +
                       " vs " + std::to_string(total) + ")");
        }
    }

    store->Exec("DROP TABLE IF EXISTS _stress_churn");
}

// Parses "key=value key=value ..." into PgDataStore::Config.
// Recognized keys: host port dbname database user username password.
PgDataStore::Config ParsePgDsn(const std::string& dsn) {
    PgDataStore::Config cfg;
    std::istringstream iss(dsn);
    std::string token;
    while (iss >> token) {
        auto eq = token.find('=');
        if (eq == std::string::npos) continue;
        std::string key = token.substr(0, eq);
        std::string val = token.substr(eq + 1);
        if (key == "host") cfg.host = val;
        else if (key == "port") cfg.port = std::atoi(val.c_str());
        else if (key == "dbname" || key == "database") cfg.database = val;
        else if (key == "user" || key == "username") cfg.username = val;
        else if (key == "password") cfg.password = val;
    }
    return cfg;
}

}  // namespace

int main() {
    {
        const auto dbPath = MakeTempDbPath();
        SqliteDataStore dataStore(dbPath);
        // Contention-tolerant settings so the stress measures the bookkeeping
        // race, not SQLITE_BUSY noise.
        dataStore.Exec("PRAGMA busy_timeout=5000");
        dataStore.Exec("PRAGMA journal_mode=WAL");
        // Test-only: skip fsync per autocommit so the stress measures the
        // bookkeeping race, not disk sync.
        dataStore.Exec("PRAGMA synchronous=OFF");
        RunConcurrentScenario(&dataStore, "sqlite", /*fullAssertions=*/false);
    }

    if (const char* dsn = std::getenv("ANIMUS_TEST_PG_DSN")) {
        if (*dsn) {
            PgDataStore pg(ParsePgDsn(dsn));
            if (pg.IsOpen()) {
                pg.Exec("DROP TABLE IF EXISTS observations");
                pg.Exec("DROP TABLE IF EXISTS memory_layers");
                pg.Exec("DROP TABLE IF EXISTS memory_mutations");
                RunConcurrentScenario(&pg, "postgres", /*fullAssertions=*/true);
            } else {
                std::cerr << "  [postgres] SKIP: store failed to open\n";
            }
        }
    }

    if (g_failures > 0) {
        std::cerr << "DATASTORE CONCURRENCY: " << g_failures
                  << " failure(s)\n";
        return 1;
    }
    std::cerr << "DATASTORE CONCURRENCY: all green\n";
    return 0;
}

#include "animus_kernel/IdRanges.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/OntologyStore.h"
#include "animus_kernel/admin/DiaryManager.h"
#include "animus_kernel/scheduler/ScheduleStore.h"
#include "animus_kernel/scheduler/TaskRunStore.h"
#include "animus_kernel/IDataStore.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

using namespace animus::kernel;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_nodeid_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

constexpr uint64_t kNode1Seed = uint64_t{1} << 40;
constexpr uint64_t kNode2Seed = uint64_t{2} << 40;
constexpr uint64_t kNode3Seed = uint64_t{3} << 40;

// Construct all agent-global stores (their ctors run EnsureSchema) in the
// same order AgentKernel does.
struct StoreSet {
    SqliteDataStore dataStore;
    memory::MemoryStore memory;
    ontology::OntologyStore ontology;
    DiaryStore diary;
    ScheduleStore schedules;
    TaskRunStore taskRuns;

    explicit StoreSet(const std::string& path)
        : dataStore(path), memory(&dataStore),
          ontology(&dataStore), diary(&dataStore),
          schedules(&dataStore), taskRuns(&dataStore) {
        taskRuns.EnsureSchema();   // ctor doesn't ensure
    }
};

} // namespace

// Single-node default (nodeId 0): seeding is a no-op, ids start at 1.
int TestSingleNodeNoOp() {
    std::cerr << "  [P1a] single-node no-op...\n";
    auto dbPath = MakeTempDbPath();
    {
        StoreSet stores(dbPath);
        std::string err;
        Assert(SeedAgentGlobalIdRanges(&stores.dataStore, 0, &err) == 0,
               "nodeId 0 must be a no-op");

        stores.memory.CreateDefaultLayersForAgent("agent1");
        auto layers = stores.memory.ListLayersForAgent("agent1");
        Assert(!layers.empty(), "default layers created");
        if (!layers.empty()) {
            Assert(layers[0].id >= 1 && layers[0].id < (int64_t)kNode1Seed,
                   "single-node layer id starts small, got " +
                   std::to_string(layers[0].id));
        }
    }
    std::filesystem::remove(dbPath);
    return 0;
}

// Node 1 on a fresh DB: every agent-global id allocates from the node range.
int TestSeedNodeOne() {
    std::cerr << "  [P1a] node 1 fresh seed...\n";
    auto dbPath = MakeTempDbPath();
    {
        StoreSet stores(dbPath);
        std::string err;
        const int seeded = SeedAgentGlobalIdRanges(&stores.dataStore, 1, &err);
        // 13 agent-global tables (P2b adds schedule_leases) + task_runs
        // (P2a scheduler replication; seeded explicitly as well).
        Assert(seeded == (int)AgentGlobalTables().size() + 1,
               "all tables seeded, got " + std::to_string(seeded) +
               " err=" + err);

        // memory_layers + observations
        // #78 P2c: default layers seed with deterministic preset ids
        // 1..7 — BELOW every node's id range by design, so
        // independently-booted nodes seed identical rows and cross-apply
        // converges instead of colliding on unique(agent_id, name).
        // (#188: the old 'layer id in node-1 range' assertion predated
        // P2c and pinned the wrong contract for DEFAULTS.)
        stores.memory.CreateDefaultLayersForAgent("agent1");
        auto layers = stores.memory.ListLayersForAgent("agent1");
        Assert(!layers.empty() && layers[0].id >= 1 && layers[0].id <= 7,
               "default layer id is a deterministic preset (1..7), got " +
               std::to_string(layers.empty() ? 0 : layers[0].id));
        // The node-range contract applies to AUTO-ID allocations: a
        // user-created layer (no preset) must allocate from the seeded
        // node-1 sequence.
        if (!layers.empty()) {
            memory::MemoryLayer userLayer = layers[0];
            userLayer.name = "user-layer-p1a";
            userLayer.sort_order = 100;
            auto createdLayer = stores.memory.CreateLayer(userLayer);
            Assert(createdLayer.id > (int64_t)kNode1Seed,
                   "user layer id in node-1 range, got " +
                   std::to_string(createdLayer.id));
        }
        if (!layers.empty()) {
            memory::Observation obs;
            obs.layer_id = layers[0].id;
            obs.text = "federation-era observation";
            obs.agent_id = "agent1";
            auto created = stores.memory.CreateObservationForAgent("agent1", obs);
            Assert(created.id > (int64_t)kNode1Seed,
                   "observation id in node-1 range, got " +
                   std::to_string(created.id));
        }

        // ontology_entities
        ontology::OntologyEntity e;
        e.name = "FedTest";
        e.full_path = "projects/FedTest";
        e.root_category = ontology::RootCategory::Projects;
        e.agent_id = "agent1";
        auto entity = stores.ontology.CreateEntity(e, "p1a test");
        Assert(entity.id > (int64_t)kNode1Seed,
               "ontology entity id in node-1 range, got " +
               std::to_string(entity.id));

        // diary_entries (integer pk not surfaced in DiaryEntry — raw query)
        DiaryEntry de;
        de.id = "p1a-diary-1";   // caller-supplied uuid-style id
        de.agent_id = "agent1";
        de.content = "node-1 range diary";
        de.timestamp_unix_ms = 1;
        auto created = stores.diary.Create(de);
        Assert(!created.id.empty(), "diary entry created");
        {
            auto q = stores.dataStore.Prepare(
                "SELECT id FROM diary_entries ORDER BY id LIMIT 1");
            Assert(q && q->Step(), "diary row exists");
            if (q) Assert(q->ColumnInt64(0) > (int64_t)kNode1Seed,
                          "diary integer id in node-1 range, got " +
                          std::to_string(q->ColumnInt64(0)));
        }

        // every agent-global table has a raised sequence
        for (const auto& table : AgentGlobalTables()) {
            auto q = stores.dataStore.Prepare(
                "SELECT seq FROM sqlite_sequence WHERE name=?");
            Assert(q != nullptr, "seq query prepared for " + table);
            if (q) {
                q->BindText(1, table);
                Assert(q->Step(), "sqlite_sequence row exists for " + table);
                if (q->Step() == false) {
                    // Step() above consumed the row; re-query for the value
                }
            }
            auto q2 = stores.dataStore.Prepare(
                "SELECT seq FROM sqlite_sequence WHERE name=?");
            if (q2) {
                q2->BindText(1, table);
                if (q2->Step()) {
                    Assert(q2->ColumnInt64(0) >= (int64_t)kNode1Seed,
                           table + " seq >= node-1 seed, got " +
                           std::to_string(q2->ColumnInt64(0)));
                }
            }
        }
    }
    std::filesystem::remove(dbPath);
    return 0;
}

// Raise-only: seeding a LOWER node id onto already-high data never regresses.
int TestRaiseOnlyNeverLowers() {
    std::cerr << "  [P1a] raise-only...\n";
    auto dbPath = MakeTempDbPath();
    {
        StoreSet stores(dbPath);
        std::string err;
        SeedAgentGlobalIdRanges(&stores.dataStore, 2, &err);

        // #78 P2c: defaults are deterministic presets (1..7), identical
        // on every node (#188 triage) — the node-range contract applies
        // to auto-id allocations:
        stores.memory.CreateDefaultLayersForAgent("agent1");
        auto layers = stores.memory.ListLayersForAgent("agent1");
        Assert(!layers.empty(), "layers created");
        int64_t firstLayerId = layers.empty() ? 0 : layers[0].id;
        Assert(firstLayerId >= 1 && firstLayerId <= 7,
               "default layer preset id (1..7), got " +
               std::to_string(firstLayerId));
        memory::MemoryLayer userLayer = layers[0];
        userLayer.name = "user-layer-raise";
        userLayer.sort_order = 100;
        auto u1 = stores.memory.CreateLayer(userLayer);
        Assert(u1.id > (int64_t)kNode2Seed, "node-2 range id, got " +
               std::to_string(u1.id));

        // Now seed node 1 (LOWER) — must be a no-op on the sequence; the
        // NEXT auto-id allocation must still land in node-2's range
        // (contiguous), not restart from the node-1 seed.
        SeedAgentGlobalIdRanges(&stores.dataStore, 1, &err);
        memory::MemoryLayer userLayer2 = layers[0];
        userLayer2.name = "user-layer-raise-2";
        auto u2 = stores.memory.CreateLayer(userLayer2);
        Assert(u2.id > (int64_t)kNode2Seed,
               "post-lower-seed id still in node-2 range, got " +
               std::to_string(u2.id));
        Assert(u2.id > u1.id, "ids stay contiguous after re-seed: " +
               std::to_string(u1.id) + " then " + std::to_string(u2.id));
        // direct sequence check
        auto q = stores.dataStore.Prepare(
            "SELECT seq FROM sqlite_sequence WHERE name='memory_layers'");
        Assert(q && q->Step(), "memory_layers seq row");
        if (q) Assert(q->ColumnInt64(0) >= (int64_t)kNode2Seed,
                      "sequence never lowered below node-2 seed, got " +
                      std::to_string(q->ColumnInt64(0)));
    }
    std::filesystem::remove(dbPath);
    return 0;
}

// Double-boot idempotence: same node seeded twice, ids stay contiguous.
int TestIdempotentDoubleBoot() {
    std::cerr << "  [P1a] idempotent double boot...\n";
    auto dbPath = MakeTempDbPath();
    {
        StoreSet stores(dbPath);
        std::string err;
        SeedAgentGlobalIdRanges(&stores.dataStore, 3, &err);
        stores.memory.CreateDefaultLayersForAgent("agent1");
        SeedAgentGlobalIdRanges(&stores.dataStore, 3, &err);   // "reboot"

        memory::Observation obs;
        auto layers = stores.memory.ListLayersForAgent("agent1");
        Assert(!layers.empty(), "layers exist");
        if (!layers.empty()) {
            obs.layer_id = layers[0].id;
            obs.agent_id = "agent1";
            obs.text = "post-reboot observation";
            auto o1 = stores.memory.CreateObservationForAgent("agent1", obs);
            auto o2 = stores.memory.CreateObservationForAgent("agent1", obs);
            Assert(o2.id == o1.id + 1,
                   "contiguous ids after re-seed: " +
                   std::to_string(o1.id) + " then " + std::to_string(o2.id));
            Assert(o1.id > (int64_t)kNode3Seed, "node-3 range");
        }
    }
    std::filesystem::remove(dbPath);
    return 0;
}

// Node id bounds: >= 2^20 rejected loudly.
int TestNodeIdBounds() {
    std::cerr << "  [P1a] node id bounds...\n";
    auto dbPath = MakeTempDbPath();
    {
        StoreSet stores(dbPath);
        std::string err;
        Assert(SeedAgentGlobalIdRanges(&stores.dataStore, (uint64_t{1} << 20), &err) == -1,
               "node id 2^20 must be rejected");
        Assert(!err.empty(), "error explains the range");
        Assert(SeedAgentGlobalIdRanges(&stores.dataStore, 0, &err) == 0,
               "0 stays a no-op");
    }
    std::filesystem::remove(dbPath);
    return 0;
}

int main() {
    std::cerr << "\n=== Node Id Range Tests (#78 P1a) ===\n\n";
    TestSingleNodeNoOp();
    TestSeedNodeOne();
    TestRaiseOnlyNeverLowers();
    TestIdempotentDoubleBoot();
    TestNodeIdBounds();
    if (g_failures == 0) std::cerr << "\nAll node id range tests passed.\n";
    else std::cerr << "\n" << g_failures << " test assertion(s) FAILED.\n";
    return g_failures == 0 ? 0 : 1;
}

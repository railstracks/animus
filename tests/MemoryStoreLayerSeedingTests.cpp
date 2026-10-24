// #213 — memory-layer preset seeding must be agent-qualified.
//
// Bug: MemoryStore::CreateLayer's by-id preset lookup
// (SELECT id FROM memory_layers WHERE id = ?) had no agent predicate, so
// once the first agent's defaults held ids 1..7 every later agent's
// seeding returned those rows and inserted nothing — a second agent ended
// up with ZERO memory layers.
//
// Decided semantics (issue #213 fix-direction paragraph):
//   * A preset id counts as taken only if the row at that id belongs to
//     the seeding agent (replicated-copy / reseed case the pinning was
//     built for).
//   * A row at the preset id held by a DIFFERENT agent is not adopted —
//     CreateLayer falls through to normal auto-id allocation;
//     unique(agent_id, name) remains the dedup for later-seeded agents.
//   * A genuinely absent preset id stays claimable (fresh store, or a
//     deleted preset slot), which keeps first-agent seeding pinned to
//     ids 1..7 and single-agent installs byte-identical.

#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/SqliteDataStore.h"

#include <iostream>
#include <string>
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
    char tmp[] = "/tmp/animus_layer_seed_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

bool ContainsName(const std::vector<MemoryLayer>& layers, const std::string& name) {
    for (const auto& layer : layers) {
        if (layer.name == name) return true;
    }
    return false;
}

// Acceptance: two agents against one store — both end with their own
// 7 default layers (agent 2 must not silently adopt agent 1's rows).
int TestTwoAgentsBothGetSevenLayers() {
    std::cerr << "  [LayerSeeding] two agents, one store...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memoryStore(&dataStore);

    Assert(memoryStore.CreateDefaultLayersForAgent("agent-a"), "agent-a seeding");
    Assert(memoryStore.CreateDefaultLayersForAgent("agent-b"), "agent-b seeding");

    auto a = memoryStore.ListLayersForAgent("agent-a");
    auto b = memoryStore.ListLayersForAgent("agent-b");
    Assert(a.size() == 7, "agent-a has 7 layers, got " + std::to_string(a.size()));
    Assert(b.size() == 7, "agent-b has 7 layers, got " + std::to_string(b.size()));

    for (const char* name : {"day", "week", "month", "year",
                             "decade", "century", "millennium"}) {
        Assert(ContainsName(a, name), std::string("agent-a has layer ") + name);
        Assert(ContainsName(b, name), std::string("agent-b has layer ") + name);
    }

    // The two agents' rows must be distinct rows (no id overlap).
    for (const auto& la : a) {
        for (const auto& lb : b) {
            Assert(la.id != lb.id,
                   "layer ids distinct across agents, shared id " +
                   std::to_string(la.id));
        }
    }

    // Agent-b's layers are auto-id allocated, not preset ids 1..7.
    for (const auto& layer : b) {
        Assert(layer.id < 1 || layer.id > 7,
               "agent-b layer id outside preset range, got " +
               std::to_string(layer.id));
    }
    return 0;
}

// Acceptance: reseeding an existing agent's layers still dedupes —
// no duplicate rows, ids stable for replication.
int TestReseedDedupesStableIds() {
    std::cerr << "  [LayerSeeding] reseed dedupes, ids stable...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memoryStore(&dataStore);

    Assert(memoryStore.CreateDefaultLayersForAgent("agent-a"), "first seeding");
    auto before = memoryStore.ListLayersForAgent("agent-a");
    Assert(before.size() == 7, "7 layers after first seed");

    // Reseed is a no-op when the agent already has layers (the dedup gate
    // at the top of CreateDefaultLayersForAgent): no duplicates, ids stable.
    bool reseeded = memoryStore.CreateDefaultLayersForAgent("agent-a");
    Assert(!reseeded, "reseed is a no-op when layers already exist");
    auto after = memoryStore.ListLayersForAgent("agent-a");
    Assert(after.size() == 7, "still 7 layers after reseed, got " +
           std::to_string(after.size()));

    for (size_t i = 0; i < before.size(); ++i) {
        Assert(before[i].id == after[i].id, "layer id stable across reseed");
    }
    return 0;
}

// Acceptance: single-agent installs unchanged — first-seeded agent still
// pins preset ids 1..7 (no renumbering of existing rows).
int TestSingleAgentPresetIdsUnchanged() {
    std::cerr << "  [LayerSeeding] single agent keeps preset ids 1..7...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memoryStore(&dataStore);

    Assert(memoryStore.CreateDefaultLayersForAgent("solo"), "solo seeding");
    auto layers = memoryStore.ListLayersForAgent("solo");
    Assert(layers.size() == 7, "7 layers, got " + std::to_string(layers.size()));
    for (size_t i = 0; i < layers.size(); ++i) {
        Assert(layers[i].id == static_cast<int64_t>(i) + 1,
               "preset id " + std::to_string(i + 1) + ", got " +
               std::to_string(layers[i].id));
    }
    return 0;
}

// Decided semantics: a genuinely absent preset id stays claimable.
// After a FULL wipe (all 7 preset layers deleted), a fresh seeding of the
// SAME agent re-claims preset ids 1..7 — pinning is a property of id
// availability + agent match, not "first agent once" bookkeeping.
// (Partial deletes: seeding no-ops because the agent still has layers.)
int TestPresetIdClaimableAfterDelete() {
    std::cerr << "  [LayerSeeding] absent preset id reclaimable...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memoryStore(&dataStore);

    Assert(memoryStore.CreateDefaultLayersForAgent("agent-a"), "seeding");
    auto layers = memoryStore.ListLayersForAgent("agent-a");
    Assert(layers.size() == 7, "7 layers before delete");

    // Full wipe, then reseed — every preset slot is free again.
    for (const auto& layer : layers) {
        Assert(memoryStore.DeleteLayer(layer.id), "delete layer");
    }
    Assert(memoryStore.ListLayersForAgent("agent-a").empty(), "all layers gone");

    Assert(memoryStore.CreateDefaultLayersForAgent("agent-a"), "reseed after wipe");
    auto after = memoryStore.ListLayersForAgent("agent-a");
    Assert(after.size() == 7, "back to 7 layers, got " +
           std::to_string(after.size()));
    for (size_t i = 0; i < after.size(); ++i) {
        Assert(after[i].id == static_cast<int64_t>(i) + 1,
               "preset id " + std::to_string(i + 1) + " re-claimed, got " +
               std::to_string(after[i].id));
    }
    return 0;
}

// ── #236: perspective auto-create must key on the pinning OUTCOME ──
//
// Bug: the forced perspective branch gated on preset_id > 0 (the REQUEST)
// instead of `fixed` (the pinning outcome), so post-#213 fall-through
// layers (auto layer ids, e.g. agent B's 8..14) still forced perspective
// id = layer id into the single id-space cross-apply replicates — and
// OR IGNORE silently dropped the row on collision, leaving layers
// perspective-less with no error surfaced.

int TestFallThroughPerspectivesGetRows() {
    std::cerr << "  [LayerSeeding] #236 fall-through perspectives...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memoryStore(&dataStore);

    Assert(memoryStore.CreateDefaultLayersForAgent("agent-a"), "agent-a seeding");
    Assert(memoryStore.CreateDefaultLayersForAgent("agent-b"), "agent-b seeding");

    auto a = memoryStore.ListLayersForAgent("agent-a");
    auto b = memoryStore.ListLayersForAgent("agent-b");
    Assert(a.size() == 7, "agent-a has 7 layers");
    Assert(b.size() == 7, "agent-b has 7 layers");

    for (const auto& layer : a) {
        Assert(memoryStore.GetPerspective(layer.id).has_value(),
               "agent-a layer " + std::to_string(layer.id) + " has a perspective");
    }
    for (const auto& layer : b) {
        Assert(memoryStore.GetPerspective(layer.id).has_value(),
               "agent-b layer " + std::to_string(layer.id) + " has a perspective");
    }

    // 14 distinct perspective rows — one per layer, none swallowed.
    auto count = dataStore.Prepare(
        "SELECT COUNT(*) FROM layer_perspectives");
    Assert(count && count->Step(), "count perspectives");
    Assert(count->ColumnInt64(0) == 14,
           "14 perspective rows, got " + std::to_string(count->ColumnInt64(0)));
    return 0;
}

// Regression: a foreign perspective row squatting a preset id (dirty or
// pre-replicated store) must not leave the pinned layer perspective-less —
// the swallowed forced insert falls back to the auto path with a warning.
// FK-legal construction: the store enforces PRAGMA foreign_keys=ON, so the
// squatter must reference a REAL layer — remove layer 1 (its perspective
// cascades away), free layer 2's perspective slot, then squat id 1 at
// layer 2. A later agent's seeding pins layer 1 (absent again) but its
// forced perspective insert collides with the squatter.
int TestPresetPerspectiveSquatFallsBack() {
    std::cerr << "  [LayerSeeding] #236 preset-id squat fallback...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memoryStore(&dataStore);

    // Agent A pins layers 1..7 with perspectives 1..7.
    Assert(memoryStore.CreateDefaultLayersForAgent("agent-a"), "agent-a seeding");

    // Dirty-store construction (FK-legal).
    auto delLayer = dataStore.Prepare("DELETE FROM memory_layers WHERE id=1");
    Assert(delLayer && delLayer->ExecDML(), "remove layer 1 (perspective cascades)");
    auto delP2 = dataStore.Prepare(
        "DELETE FROM layer_perspectives WHERE layer_id=2");
    Assert(delP2 && delP2->ExecDML(), "free layer 2's perspective slot");
    auto squat = dataStore.Prepare(
        "INSERT INTO layer_perspectives (id, layer_id, updated_at_unix_ms) "
        "VALUES (1, 2, 0)");
    Assert(squat && squat->ExecDML(), "squat perspective id 1");

    // Agent B seeds: preset id 1 is ABSENT as a layer, so B pins layer 1 —
    // but the forced perspective insert (id=1) collides with the squatter.
    // The fallback must still give layer 1 a perspective row.
    Assert(memoryStore.CreateDefaultLayersForAgent("agent-b"), "agent-b seeding");

    auto b = memoryStore.ListLayersForAgent("agent-b");
    Assert(b.size() == 7, "agent-b has 7 layers, got " +
           std::to_string(b.size()));
    Assert(!b.empty() && b.front().id == 1,
           "agent-b pinned layer 1, got id " +
           std::to_string(b.empty() ? -1 : b.front().id));

    Assert(memoryStore.GetPerspective(1).has_value(),
           "layer 1 has a perspective despite id squat");
    // The squatter row is untouched: layer 2's perspective still holds id 1.
    auto squatter = dataStore.Prepare(
        "SELECT COUNT(*) FROM layer_perspectives WHERE layer_id=2 AND id=1");
    Assert(squatter && squatter->Step() && squatter->ColumnInt64(0) == 1,
           "squatter row untouched");

    // Every one of agent-b's layers ends with a perspective — none swallowed.
    for (const auto& layer : b) {
        Assert(memoryStore.GetPerspective(layer.id).has_value(),
               "agent-b layer " + std::to_string(layer.id) + " has a perspective");
    }
    return 0;
}

}  // namespace

int main() {
    std::cerr << "[MemoryStoreLayerSeedingTests]\n";
    TestTwoAgentsBothGetSevenLayers();
    TestReseedDedupesStableIds();
    TestSingleAgentPresetIdsUnchanged();
    TestPresetIdClaimableAfterDelete();
    TestFallThroughPerspectivesGetRows();
    TestPresetPerspectiveSquatFallsBack();
    if (g_failures > 0) {
        std::cerr << "[MemoryStoreLayerSeedingTests] FAILED: " << g_failures
                  << " assertion(s)\n";
        return 1;
    }
    std::cerr << "[MemoryStoreLayerSeedingTests] all green\n";
    return 0;
}
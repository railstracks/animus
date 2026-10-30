#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/OntologyStore.h"
#include "animus_kernel/SqliteDataStore.h"

#include <filesystem>
#include <iostream>
#include <string>

using namespace animus::kernel;
using namespace animus::kernel::memory;
using namespace animus::kernel::ontology;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_ontology_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

int TestEntityPathAndPropertyUpsert() {
    std::cerr << "  [Ontology] Entity path creation + property upsert...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memoryStore(&dataStore);
    OntologyStore ontologyStore(&dataStore);

    MemoryLayer layer;
    layer.name = "day";
    layer.horizon = "1 day";
    layer.sort_order = 0;
    layer.enabled = true;
    auto createdLayer = memoryStore.CreateLayer(layer);
    Assert(createdLayer.id > 0, "Layer should be created");

    Observation observation;
    observation.layer_id = createdLayer.id;
    observation.agent_id = "agent1";
    observation.text = "Jack shared his phone number.";
    observation.source = "intake:test";
    auto createdObservation = memoryStore.CreateObservationForAgent("agent1", observation);
    Assert(createdObservation.id > 0, "Observation should be created");

    auto entity = ontologyStore.EnsureEntityPath(
        RootCategory::Persons, "jack_smith", "test seed");
    Assert(entity.has_value(), "Entity should be created");
    Assert(entity->full_path == "persons/jack_smith", "Entity full path should include category");

    OntologyProperty prop;
    prop.entity_id = entity->id;
    prop.key = "phone";
    prop.value = "+1-555-0199";
    prop.value_type = PropertyType::Text;
    prop.linked_observation_id = createdObservation.id;
    auto updated = ontologyStore.SetProperty(prop, "new phone evidence");
    Assert(updated.id > 0, "Property upsert should succeed");
    Assert(updated.linked_observation_id.has_value(), "Property should link to observation");
    Assert(updated.memory_state == MemoryState::New, "Property state should inherit observation state");

    auto found = ontologyStore.GetProperty(entity->id, "phone");
    Assert(found.has_value(), "Property should be retrievable");
    Assert(found->value == "+1-555-0199", "Property value mismatch");

    auto mutations = ontologyStore.QueryMutations(0, 20);
    Assert(!mutations.empty(), "Mutation log should not be empty");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestPropertyStateSyncAndOrphaning() {
    std::cerr << "  [Ontology] Property sync + orphaning...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memoryStore(&dataStore);
    OntologyStore ontologyStore(&dataStore);

    MemoryLayer layer;
    layer.name = "day";
    layer.horizon = "1 day";
    layer.sort_order = 0;
    layer.enabled = true;
    auto createdLayer = memoryStore.CreateLayer(layer);

    Observation observation;
    observation.layer_id = createdLayer.id;
    observation.agent_id = "agent1";
    observation.text = "Project Animus uses C++.";
    observation.source = "intake:test";
    auto createdObservation = memoryStore.CreateObservationForAgent("agent1", observation);

    auto entity = ontologyStore.EnsureEntityPath(RootCategory::Projects, "animus");
    Assert(entity.has_value(), "Project entity should be created");

    OntologyProperty prop;
    prop.entity_id = entity->id;
    prop.key = "language";
    prop.value = "C++";
    prop.value_type = PropertyType::Text;
    prop.linked_observation_id = createdObservation.id;
    auto createdProperty = ontologyStore.SetProperty(prop, "seed language");
    Assert(createdProperty.id > 0, "Property should be created");

    Assert(memoryStore.SetObservationMemoryState(
               createdObservation.id, MemoryState::Current, "reviewed"),
           "Observation state update should succeed");
    ontologyStore.SyncPropertyStatesFromObservations(&memoryStore);
    auto synced = ontologyStore.GetProperty(entity->id, "language");
    Assert(synced.has_value(), "Synced property should exist");
    Assert(synced->memory_state == MemoryState::Current,
           "Property should sync to current when observation is current");

    Assert(memoryStore.DeleteObservation(createdObservation.id),
           "Observation deletion should succeed");
    // #189: the orphan contract holds AT DELETE TIME (the BEFORE DELETE
    // trigger fires while the FK link still resolves) — not only after a
    // rescue sync. Pin it pre-sync.
    {
        auto atDelete = ontologyStore.GetProperty(entity->id, "language");
        Assert(atDelete.has_value(), "Orphaned property still exists at delete time");
        Assert(!atDelete->linked_observation_id.has_value(),
               "Orphaned property clears link at delete time");
        Assert(atDelete->memory_state == MemoryState::Deprecated,
               "Orphaned property deprecated at delete time (trigger, pre-sync)");
    }
    ontologyStore.SyncPropertyStatesFromObservations(&memoryStore);
    auto orphaned = ontologyStore.GetProperty(entity->id, "language");
    Assert(orphaned.has_value(), "Orphaned property should still exist");
    Assert(!orphaned->linked_observation_id.has_value(),
           "Orphaned property should clear linked_observation_id");
    Assert(orphaned->memory_state == MemoryState::Deprecated,
           "Orphaned property should be deprecated");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestAgentScopedPathResolution() {
    std::cerr << "  [Ontology] #215: per-agent path namespace + property stamping...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memoryStore(&dataStore);
    OntologyStore ontologyStore(&dataStore);

    auto a1 = ontologyStore.EnsureEntityPath(
        RootCategory::Persons, "shared_target", "seed", "agent-a");
    auto b1 = ontologyStore.EnsureEntityPath(
        RootCategory::Persons, "shared_target", "seed", "agent-b");
    Assert(a1.has_value() && b1.has_value(), "both agents should create the same path");
    Assert(a1->id != b1->id, "#215: same path must resolve to distinct per-agent entities");
    Assert(a1->agent_id == "agent-a", "agent A entity stamped");
    Assert(b1->agent_id == "agent-b", "agent B entity stamped");

    auto fa = ontologyStore.FindByPath(RootCategory::Persons, "persons/shared_target", "agent-a");
    auto fb = ontologyStore.FindByPath(RootCategory::Persons, "persons/shared_target", "agent-b");
    Assert(fa.has_value() && fa->id == a1->id, "scoped lookup returns A's entity");
    Assert(fb.has_value() && fb->id == b1->id, "scoped lookup returns B's entity");

    auto fun = ontologyStore.FindByPath(RootCategory::Persons, "persons/shared_target");
    Assert(fun.has_value(), "unscoped lookup still resolves (admin/migration path)");

    auto a2 = ontologyStore.EnsureEntityPath(
        RootCategory::Persons, "shared_target", "seed", "agent-a");
    Assert(a2.has_value() && a2->id == a1->id,
           "re-ensure within one agent returns the same entity (unique index)");

    OntologyProperty pa;
    pa.entity_id = a1->id;
    pa.key = "phone";
    pa.value = "555-0100";
    pa.agent_id = "agent-a";
    auto spa = ontologyStore.SetProperty(pa, "test");
    Assert(spa.id > 0, "agent-stamped property should be created");
    auto gpa = ontologyStore.GetProperty(a1->id, "phone");
    Assert(gpa.has_value() && gpa->agent_id == "agent-a",
           "property row carries the writing agent's id");

    OntologyProperty pl;
    pl.entity_id = b1->id;
    pl.key = "note";
    pl.value = "legacy write";
    auto spl = ontologyStore.SetProperty(pl, "test");
    Assert(spl.id > 0, "legacy property should be created");
    auto gpl = ontologyStore.GetProperty(b1->id, "note");
    Assert(gpl.has_value() && gpl->agent_id == "default",
           "legacy property (no agent set) defaults to 'default'");

    OntologyProperty pb;
    pb.entity_id = b1->id;
    pb.key = "phone";
    pb.value = "555-0200";
    pb.agent_id = "agent-b";
    auto spb = ontologyStore.SetProperty(pb, "test");
    Assert(spb.id > 0 && spb.id != spa.id, "same key on distinct entities is distinct rows");

    std::filesystem::remove(dbPath);
    return 0;
}

// #219: boot-sync demotion — the blanket New->Current escalator becomes the
// no-evidence exception path, scoped to legacy/'default'-sentinel rows, and
// every transition (promotion, evidence sync, orphan rescue) leaves an
// auditable ontology_mutations row.
int TestBootSyncNoEvidenceExceptionPath() {
    std::cerr << "  [#219] Boot sync: no-evidence exception path + audit rows...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memoryStore(&dataStore);
    OntologyStore ontologyStore(&dataStore);

    MemoryLayer layer;
    layer.name = "day";
    layer.horizon = "1 day";
    layer.sort_order = 0;
    layer.enabled = true;
    auto createdLayer = memoryStore.CreateLayer(layer);
    Assert(createdLayer.id > 0, "layer should be created");

    Observation observation;
    observation.layer_id = createdLayer.id;
    observation.agent_id = "agent1";
    observation.text = "Animus ships the evidence layer.";
    observation.source = "intake:test";
    auto createdObservation = memoryStore.CreateObservationForAgent("agent1", observation);
    Assert(createdObservation.id > 0, "observation should be created");

    auto entity = ontologyStore.EnsureEntityPath(
        RootCategory::Projects, "evidence", "test", "agent1");
    Assert(entity.has_value(), "entity should exist");

    // Row 1: agent-stamped (post-#215 writer shape), unlinked, New — must
    // stay New; the reconciler links evidence later.
    OntologyProperty stamped;
    stamped.entity_id = entity->id;
    stamped.key = "stamped";
    stamped.value = "agent-stamped";
    stamped.agent_id = "agent1";
    auto s1 = ontologyStore.SetProperty(stamped, "test");
    Assert(s1.id > 0 && s1.memory_state == MemoryState::New, "stamped row starts New");

    // Row 2: legacy 'default' sentinel, unlinked, New — the exception path
    // boot sync exists for.
    OntologyProperty legacy;
    legacy.entity_id = entity->id;
    legacy.key = "legacy";
    legacy.value = "no agent identity";
    auto s2 = ontologyStore.SetProperty(legacy, "test");
    Assert(s2.id > 0 && s2.agent_id == "default", "legacy row carries the default sentinel");

    // Row 3: linked to the observation (state New, inherited at write).
    OntologyProperty linked;
    linked.entity_id = entity->id;
    linked.key = "linked";
    linked.value = "evidence-backed";
    linked.agent_id = "agent1";
    linked.linked_observation_id = createdObservation.id;
    auto s3 = ontologyStore.SetProperty(linked, "test");
    Assert(s3.id > 0 && s3.memory_state == MemoryState::New,
           "linked row inherits observation state at write");

    // Row 4: dangling link — a stale link the FK would normally reject, so
    // create it in a foreign_keys=OFF window (drift the rescue sweep exists
    // for: links that went stale outside the delete-trigger path).
    OntologyProperty dangling;
    dangling.entity_id = entity->id;
    dangling.key = "dangling";
    dangling.value = "stale link";
    dangling.agent_id = "agent1";
    dangling.linked_observation_id = 424242;  // no such observation
    Assert(dataStore.Exec("PRAGMA foreign_keys=OFF;"), "FK off should succeed");
    auto s4 = ontologyStore.SetProperty(dangling, "test");
    Assert(dataStore.Exec("PRAGMA foreign_keys=ON;"), "FK on should succeed");
    Assert(s4.id > 0 && s4.memory_state == MemoryState::Deprecated,
           "dangling link writes Deprecated");

    // Evidence moves: observation promoted to Current (SQLite trigger syncs
    // row 3 immediately — the drift boot sync exists for is other paths).
    Assert(memoryStore.SetObservationMemoryState(
               createdObservation.id, MemoryState::Current, "reviewed"),
           "observation promotion should succeed");
    // Create drift on row 3 directly (simulates a path the trigger missed —
    // e.g. Postgres, where the SQLite triggers don't exist).
    Assert(dataStore.Exec(
               "UPDATE ontology_properties SET memory_state=0 WHERE id=" + std::to_string(s3.id)),
           "drift update should succeed");

    const int64_t transitionsBefore = [ &ontologyStore ] {
        int64_t n = 0;
        for (const auto& m : ontologyStore.QueryMutations(0, 1000)) {
            if (m.mutation_type == "state_transition") ++n;
        }
        return n;
    }();

    ontologyStore.SyncPropertyStatesFromObservations(&memoryStore);

    auto g1 = ontologyStore.GetProperty(entity->id, "stamped");
    auto g2 = ontologyStore.GetProperty(entity->id, "legacy");
    auto g3 = ontologyStore.GetProperty(entity->id, "linked");
    auto g4 = ontologyStore.GetProperty(entity->id, "dangling");
    Assert(g1.has_value() && g1->memory_state == MemoryState::New,
           "#219: agent-stamped unlinked row stays New (no blanket promotion)");
    Assert(g2.has_value() && g2->memory_state == MemoryState::Current,
           "#219: legacy 'default' sentinel row promoted (exception path)");
    Assert(g3.has_value() && g3->memory_state == MemoryState::Current,
           "#219: linked row corrected from evidence (drift repaired)");
    Assert(g4.has_value() && !g4->linked_observation_id.has_value()
               && g4->memory_state == MemoryState::Deprecated,
           "#219: dangling link rescued — cleared and deprecated");

    // Audit parity: each transition left a state_transition row.
    int64_t sawLegacy = 0, sawSync = 0, sawRescue = 0;
    for (const auto& m : ontologyStore.QueryMutations(0, 1000)) {
        if (m.mutation_type != "state_transition") continue;
        if (g2.has_value() && m.target_id == g2->id) ++sawLegacy;
        if (g3.has_value() && m.target_id == g3->id) ++sawSync;
        if (g4.has_value() && m.target_id == g4->id) ++sawRescue;
    }
    Assert(sawLegacy == 1, "legacy promotion must be auditable");
    Assert(sawSync == 1, "evidence sync transition must be auditable");
    Assert(sawRescue == 1, "orphan rescue must be auditable");

    // Idempotence: a second sync pass writes no new transitions.
    ontologyStore.SyncPropertyStatesFromObservations(&memoryStore);
    int64_t transitionsAfter = 0;
    for (const auto& m : ontologyStore.QueryMutations(0, 1000)) {
        if (m.mutation_type == "state_transition") ++transitionsAfter;
    }
    Assert(transitionsAfter == transitionsBefore + 3,
           "second sync pass must be transition-free (got "
               + std::to_string(transitionsAfter - transitionsBefore - 3) + " extra rows)");

    std::filesystem::remove(dbPath);
    return 0;
}

} // namespace

int main() {
    std::cerr << "\n=== Ontology Store Tests ===\n\n";

    TestEntityPathAndPropertyUpsert();
    TestPropertyStateSyncAndOrphaning();
    TestAgentScopedPathResolution();
    TestBootSyncNoEvidenceExceptionPath();

    std::cerr << "\n";
    if (g_failures == 0) {
        std::cerr << "All ontology store tests passed.\n";
    } else {
        std::cerr << g_failures << " test(s) FAILED.\n";
    }
    std::cerr << "\n";

    return g_failures;
}


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

} // namespace

int main() {
    std::cerr << "\n=== Ontology Store Tests ===\n\n";

    TestEntityPathAndPropertyUpsert();
    TestPropertyStateSyncAndOrphaning();
    TestAgentScopedPathResolution();

    std::cerr << "\n";
    if (g_failures == 0) {
        std::cerr << "All ontology store tests passed.\n";
    } else {
        std::cerr << g_failures << " test(s) FAILED.\n";
    }
    std::cerr << "\n";

    return g_failures;
}


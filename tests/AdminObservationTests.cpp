// ============================================================================
// AdminObservationTests — #223
//
// Admin observation PATCH was legacy-only: numeric (Sql-store) ids went
// unconditionally to PatchObservationFromBody, which looks the id up in
// the legacy in-process JSON map -> 404 for every real observation. The
// fix direction (agreed in review, Oct 23): route admin PATCH through
// ReviseObservation — an admin edit IS a revision and leaves the same
// supersession trail + mutation row an agent's revision does — with
// actor provenance on admin writes.
//
// Contracts under test:
//   1. SqlPatchObservation on a real id -> supersession chain + mutation
//      row carrying the admin actor; new version's source = "admin".
//   2. Admin-created observations default to distinguishable provenance
//      (source = "admin") and log an actor-tagged create mutation.
//   3. Admin deletes log an actor-tagged mutation row (previously silent).
//   4. Error shapes: non-numeric id -> 400, absent id -> 404, bad fields
//      -> 400.
// ============================================================================

#include "animus_kernel/admin/MemoryManager.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/SqliteDataStore.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

#include <json/json.h>

using namespace animus::kernel;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        ++g_failures;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_admin_obs_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

struct Fixture {
    std::string dbPath = MakeTempDbPath();
    SqliteDataStore dataStore{dbPath};
    memory::MemoryStore store{&dataStore};
    MemoryManager manager{};

    Fixture() {
        store.CreateDefaultLayersForAgent("agent-a");
    }
    ~Fixture() {
        std::error_code ec;
        std::filesystem::remove(dbPath, ec);
        (void)ec;
    }

    memory::Observation SeedObservation(const std::string& text) {
        memory::Observation obs;
        obs.layer_id = 1;
        obs.agent_id = "agent-a";
        obs.text = text;
        obs.weight = 1.0;
        obs.tags_json = "[]";
        obs.source = "seed";
        return store.CreateObservationForAgent("agent-a", obs);
    }
};

} // namespace

int main() {
    std::cerr << "Running admin observation revision tests...\n";

    // ---- 1. PATCH on a real id: revision + actor-tagged trail ----
    {
        Fixture fx;
        auto orig = fx.SeedObservation("original text");
        Assert(orig.id > 0, "seed observation created");

        Json::Value body;
        body["text"] = "revised by admin";
        body["weight"] = 2.5;

        auto result = fx.manager.SqlPatchObservation(&fx.store,
                                                     std::to_string(orig.id), &body);
        Assert(result.httpStatusCode == 200,
               "PATCH real id -> 200, got " + std::to_string(result.httpStatusCode));
        Assert(result.body.get("text", "").asString() == "revised by admin",
               "PATCH returns the revised text");
        Assert(result.body.get("source", "").asString() == "admin",
               "revised version carries admin provenance");

        // Supersession chain: original marked, history has both versions.
        auto oldRow = fx.store.GetObservation(orig.id);
        Assert(oldRow.has_value() && oldRow->superseded_by != 0,
               "original superseded (copy-on-write preserved)");
        auto history = fx.store.GetObservationHistory(orig.id);
        Assert(history.size() == 2,
               "history holds 2 versions, got " + std::to_string(history.size()));

        // Mutation row with the admin actor.
        auto mutations = fx.store.QueryMutations(0, 100);
        bool foundRevise = false;
        for (const auto& m : mutations) {
            if (m.mutation_type == "revise" && m.target_type == "observation" &&
                m.motivation.find("[actor: admin]") != std::string::npos) {
                foundRevise = true;
            }
        }
        Assert(foundRevise, "revise mutation row carries [actor: admin]");
    }

    // ---- 2. Admin-created observations: distinguishable provenance ----
    {
        Fixture fx;
        Json::Value body;
        body["text"] = "admin-authored observation";
        auto result = fx.manager.SqlCreateLayerObservation(
            &fx.store, "1", &body);
        Assert(result.httpStatusCode == 201,
               "create -> 201, got " + std::to_string(result.httpStatusCode));
        Assert(result.body.get("source", "").asString() == "admin",
               "admin create defaults source=admin");

        auto mutations = fx.store.QueryMutations(0, 100);
        bool foundCreate = false;
        for (const auto& m : mutations) {
            if (m.mutation_type == "create" && m.target_type == "observation" &&
                m.motivation.find("[actor: admin]") != std::string::npos) {
                foundCreate = true;
            }
        }
        Assert(foundCreate, "create mutation row carries [actor: admin]");
    }

    // ---- 3. Admin delete: actor-tagged mutation row + #221 full-row
    //      snapshot — the log alone reconstructs the departed row ----
    {
        Fixture fx;
        memory::Observation seed;
        seed.layer_id = 1;
        seed.agent_id = "agent-a";
        seed.text = "rich observation to be deleted";
        seed.weight = 3.25;
        seed.tags_json = "[\"alpha\",\"beta\"]";
        seed.source = "unit-test";
        auto orig = fx.store.CreateObservationForAgent("agent-a", seed);
        Assert(orig.id > 0, "rich observation created");

        // Depart in a non-default state so the snapshot proves it captured
        // the row as it stood, not a default.
        Assert(fx.store.SetObservationMemoryState(
                   orig.id, memory::MemoryState::Current, "state change en route"),
               "state change -> true");

        auto result = fx.manager.SqlDeleteObservation(
            &fx.store, std::to_string(orig.id));
        Assert(result.httpStatusCode == 200, "delete -> 200");

        auto mutations = fx.store.QueryMutations(0, 100);
        bool foundDelete = false, foundRetire = false;
        for (const auto& m : mutations) {
            if (m.target_type != "observation" || m.target_id != orig.id) continue;
            if (m.mutation_type == "delete" &&
                m.motivation.find("[actor: admin]") != std::string::npos) {
                foundDelete = true;
            }
            if (m.mutation_type == "observation_state_changed" &&
                !m.row_snapshot.empty()) {
                foundRetire = true;
            }
        }
        Assert(foundDelete, "delete mutation row carries [actor: admin]");
        Assert(foundRetire, "state-change mutation carries a row snapshot");

        // #221 acceptance: the delete row's snapshot alone reconstructs
        // text, weight, tags, memory_state, layer, source (agent too).
        bool reconstructed = false;
        for (const auto& m : mutations) {
            if (m.mutation_type != "delete" || m.target_id != orig.id) continue;
            Json::CharReaderBuilder rb;
            Json::Value snap;
            std::string errs;
            std::istringstream ss(m.row_snapshot);
            if (!Json::parseFromStream(rb, ss, &snap, &errs)) {
                Assert(false, "row_snapshot parses as JSON: " + errs);
                break;
            }
            reconstructed =
                snap["text"].asString() == "rich observation to be deleted" &&
                snap["weight"].asDouble() == 3.25 &&
                snap["tags_json"].asString() == "[\"alpha\",\"beta\"]" &&
                snap["memory_state"].asInt64() == 1 &&  // MemoryState::Current
                snap["layer_id"].asInt64() == orig.layer_id &&
                snap["source"].asString() == "unit-test" &&
                snap["agent_id"].asString() == "agent-a";
        }
        Assert(reconstructed,
               "delete row_snapshot reconstructs the departed observation");
    }

    // ---- 4. Error shapes ----
    {
        Fixture fx;
        auto orig = fx.SeedObservation("target");

        Json::Value body;
        body["text"] = "x";
        auto badId = fx.manager.SqlPatchObservation(&fx.store, "abc123", &body);
        Assert(badId.httpStatusCode == 400, "non-numeric id -> 400");

        auto absent = fx.manager.SqlPatchObservation(&fx.store, "999999", &body);
        Assert(absent.httpStatusCode == 404, "absent id -> 404");

        Json::Value badWeight;
        badWeight["weight"] = "not-a-number";
        auto badField = fx.manager.SqlPatchObservation(
            &fx.store, std::to_string(orig.id), &badWeight);
        Assert(badField.httpStatusCode == 400, "non-numeric weight -> 400");

        auto noBody = fx.manager.SqlPatchObservation(&fx.store,
                                                     std::to_string(orig.id), nullptr);
        Assert(noBody.httpStatusCode == 400, "missing body -> 400");

        // Partial PATCH: absent fields keep current values.
        Json::Value partial;
        partial["weight"] = 3.0;
        auto partialResult = fx.manager.SqlPatchObservation(
            &fx.store, std::to_string(orig.id), &partial);
        Assert(partialResult.httpStatusCode == 200, "partial PATCH -> 200");
        Assert(partialResult.body.get("text", "").asString() == "target",
               "absent text field keeps its current value");
        Assert(partialResult.body.get("weight", 0.0).asDouble() == 3.0,
               "patched weight applied");
    }

    if (g_failures > 0) {
        std::cerr << "\nFAILED: " << g_failures << " assertions failed\n";
        return 1;
    }

    std::cerr << "\nAll admin observation tests passed!\n";
    return 0;
}
#include "animus_kernel/consolidation/ConsolidationStore.h"
#include "animus_kernel/consolidation/ConsolidationPipeline.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/admin/DiaryManager.h"
#include "animus_kernel/SqliteDataStore.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <json/json.h>
#include <json/reader.h>
#include <json/writer.h>

using namespace animus::kernel;
using namespace animus::kernel::memory;

namespace {

int g_failures = 0;

int Fail(int code, const std::string& message) {
    std::cerr << "  FAIL: " << message << " (code " << code << ")\n";
    g_failures++;
    return code;
}

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_consolidation_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

// Mock LLM callback that returns a fixed response
int g_llmCallCount = 0;
std::string MockLLMCallback(const std::string& agentId,
                              const std::string& systemPrompt,
                              const std::string& userPrompt) {
    g_llmCallCount++;

    // Return observations for intake
    if (userPrompt.find("diary entries") != std::string::npos) {
        return R"([{"text": "Agent learned about memory consolidation", "tags": ["memory", "learning"], "weight": 0.8}])";
    }

    // Return promote/demote decisions for consolidation
    if (userPrompt.find("Observations:") != std::string::npos) {
        return R"([{"id": 1, "action": "promote", "reason": "significant learning"}])";
    }

    // Return perspectives
    if (userPrompt.find("perspectives") != std::string::npos ||
        systemPrompt.find("perspective") != std::string::npos) {
        return R"({"retrospective": "The agent explored new territory", "current": "Building consolidation pipeline", "future": "Will continue testing"})";
    }

    return "[]";
}

} // anonymous namespace

// ============================================================================
// ConsolidationStore tests
// ============================================================================

int TestStoreWatermarks() {
    std::cerr << "  [Store] Watermarks CRUD...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ConsolidationStore store(&dataStore);

    // No watermark initially
    auto wm = store.GetWatermark("agent1", "diary_entries");
    Assert(!wm.has_value(), "Should be no initial watermark");

    // Set watermark
    ConsolidationWatermark newWm;
    newWm.agent_id = "agent1";
    newWm.source = "diary_entries";
    newWm.last_processed_id = 42;
    newWm.last_run_unix_ms = 1000000;
    store.SetWatermark(newWm);

    // Retrieve
    auto retrieved = store.GetWatermark("agent1", "diary_entries");
    Assert(retrieved.has_value(), "Should retrieve watermark");
    Assert(retrieved->last_processed_id == 42, "last_processed_id mismatch");
    Assert(retrieved->last_run_unix_ms == 1000000, "last_run_unix_ms mismatch");

    // Update watermark
    newWm.last_processed_id = 100;
    newWm.last_run_unix_ms = 2000000;
    store.SetWatermark(newWm);

    auto updated = store.GetWatermark("agent1", "diary_entries");
    Assert(updated.has_value(), "Should retrieve updated watermark");
    Assert(updated->last_processed_id == 100, "Updated last_processed_id mismatch");

    // List watermarks
    ConsolidationWatermark wm2;
    wm2.agent_id = "agent1";
    wm2.source = "session_turns";
    wm2.last_processed_id = 10;
    wm2.last_run_unix_ms = 500000;
    store.SetWatermark(wm2);

    auto all = store.ListWatermarks("agent1");
    Assert(all.size() == 2, "Should have 2 watermarks, got " + std::to_string(all.size()));

    std::filesystem::remove(dbPath);
    return 0;
}

int TestStoreRunLog() {
    std::cerr << "  [Store] Run log CRUD...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ConsolidationStore store(&dataStore);

    // Create run
    ConsolidationRun run;
    run.agent_id = "agent1";
    run.phase = "intake";
    run.started_unix_ms = 1000000;
    run.status = "running";
    const int64_t runId = store.CreateRun(run);
    Assert(runId > 0, "Run ID should be positive");

    // Finish run
    Assert(store.FinishRun(runId, "completed", R"({"created": 5})", ""),
           "FinishRun should succeed");

    // List runs
    auto runs = store.ListRuns("agent1");
    Assert(runs.size() == 1, "Should have 1 run");
    Assert(runs[0].status == "completed", "Status should be completed");
    Assert(runs[0].finished_unix_ms > 0, "finished_unix_ms should be set");

    // Get latest run by phase
    auto latest = store.GetLatestRun("agent1", "intake");
    Assert(latest.has_value(), "Should find latest intake run");
    Assert(latest->id == runId, "Latest run ID should match");

    // No run for different phase
    auto noRun = store.GetLatestRun("agent1", "perspective_revision");
    Assert(!noRun.has_value(), "Should not find run for unknown phase");

    std::filesystem::remove(dbPath);
    return 0;
}

// ============================================================================
// Pipeline tests (with mock LLM)
// ============================================================================

int TestPipelineIntakeFromDiary() {
    std::cerr << "  [Pipeline] Intake from diary...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);
    DiaryStore diaryStore(&dataStore);

    // Seed memory layers (need at least one)
    MemoryLayer baseLayer;
    baseLayer.name = "day";
    baseLayer.horizon = "1 day";
    baseLayer.sort_order = 0;
    baseLayer.evaluation_interval_seconds = 86400;
    baseLayer.cron_expr = "0 * * * *";
    baseLayer.token_budget = 4096;
    baseLayer.enabled = true;
    baseLayer.created_at_unix_ms = 1000000;
    baseLayer.updated_at_unix_ms = 1000000;
    memStore.CreateLayer(baseLayer);

    // Seed diary entries
    DiaryEntry entry;
    entry.agent_id = "agent1";
    entry.layer = "observation";
    entry.content = "Today I learned about memory consolidation in agent systems";
    entry.tags_json = "[\"memory\", \"learning\"]";
    entry.timestamp_unix_ms = 2000000;
    diaryStore.Create(entry);

    g_llmCallCount = 0;
    ConsolidationPipeline pipeline(
        &dataStore, &memStore, nullptr, &diaryStore, nullptr, nullptr, &MockLLMCallback);

    ConsolidationPipeline::Config cfg;
    cfg.intake_enabled = true;
    pipeline.Configure(cfg);

    std::string err;
    Assert(pipeline.RunIntake("agent1", std::nullopt, &err), "Intake should succeed: " + err);

    // Verify LLM was called
    Assert(g_llmCallCount >= 1, "LLM should have been called at least once");

    // Verify observations were created
    auto layers = memStore.ListLayers();
    Assert(!layers.empty(), "Should have layers");
    auto obs = memStore.ListObservationsForAgent("agent1", layers[0].id);
    Assert(!obs.empty(), "Should have observations after intake, got " + std::to_string(obs.size()));
    Assert(obs[0].text.find("memory consolidation") != std::string::npos,
           "Observation text should contain 'memory consolidation'");
    Assert(obs[0].next_review_at_ms > obs[0].created_at_unix_ms,
           "Intake observation should receive a future next_review_at_ms");

    // Verify watermark was set
    auto& store = pipeline.Store();
    auto wm = store.GetWatermark("agent1", "diary_entries");
    Assert(wm.has_value(), "Watermark should be set after intake");
    Assert(wm->last_processed_id > 0, "Watermark should have positive last_processed_id");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestPipelineLayerConsolidation() {
    std::cerr << "  [Pipeline] Layer consolidation (promote)...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);

    // Create two layers: day (bottom) and week (top)
    MemoryLayer day;
    day.name = "day";
    day.horizon = "1 day";
    day.sort_order = 0;
    day.evaluation_interval_seconds = 3600;
    day.cron_expr = "0 * * * *";
    day.token_budget = 4096;
    day.enabled = true;
    day.created_at_unix_ms = 1000000;
    day.updated_at_unix_ms = 1000000;
    memStore.CreateLayer(day);

    MemoryLayer week;
    week.name = "week";
    week.horizon = "1 week";
    week.sort_order = 1;
    week.evaluation_interval_seconds = 86400;
    week.cron_expr = "0 * * * *";
    week.token_budget = 4096;
    week.enabled = true;
    week.created_at_unix_ms = 1000000;
    week.updated_at_unix_ms = 1000000;
    memStore.CreateLayer(week);

    // Create an observation in the day layer and force it due now.
    auto layers = memStore.ListLayers();
    Observation obs;
    obs.layer_id = layers[0].id;  // day
    obs.agent_id = "agent1";
    obs.text = "Agent learned about memory consolidation";
    obs.weight = 0.8;
    obs.tags_json = "[\"memory\"]";
    obs.source = "intake:diary";
    obs.created_at_unix_ms = 2000000;
    obs.updated_at_unix_ms = 2000000;
    obs.last_evaluated_at_ms = 0;
    obs.next_review_at_ms = 1;
    auto created = memStore.CreateObservationForAgent("agent1", obs);
    Assert(created.id > 0, "Observation should be created with positive ID");

    g_llmCallCount = 0;
    // Custom LLM callback that promotes based on the actual observation ID
    int64_t createdObsId = created.id;
    auto promoteCallback = [createdObsId](const std::string&,
                                            const std::string&,
                                            const std::string&) -> std::string {
        return "[{\"id\": " + std::to_string(createdObsId) +
               ", \"action\": \"promote\", \"reason\": \"significant\"}]";
    };

    ConsolidationPipeline pipeline(
        &dataStore, &memStore, nullptr, nullptr, nullptr, nullptr, promoteCallback);

    std::string err;
    Assert(pipeline.RunLayerConsolidation("agent1", "day", &err),
           "Layer consolidation should succeed: " + err);

    // Verify observation was promoted to week layer
    auto dayObs = memStore.ListObservationsForAgent("agent1", layers[0].id);
    Assert(dayObs.empty(), "Day layer should be empty after promotion");

    auto weekObs = memStore.ListObservationsForAgent("agent1", layers[1].id);
    Assert(!weekObs.empty(), "Week layer should have the promoted observation");

    // Verify mutation was logged
    auto mutations = memStore.QueryMutations(0);
    Assert(!mutations.empty(), "Should have logged a mutation");

    // Verify stats
    auto stats = pipeline.GetStats();
    Assert(stats.observations_promoted >= 1, "Stats should show 1+ promoted");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestPipelineDemoteToArchive() {
    std::cerr << "  [Pipeline] Demote from bottom layer → archive...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);

    // Single bottom layer
    MemoryLayer day;
    day.name = "day";
    day.horizon = "1 day";
    day.sort_order = 0;
    day.evaluation_interval_seconds = 3600;
    day.cron_expr = "0 * * * *";
    day.token_budget = 4096;
    day.enabled = true;
    day.created_at_unix_ms = 1000000;
    day.updated_at_unix_ms = 1000000;
    memStore.CreateLayer(day);

    auto layers = memStore.ListLayers();
    Observation obs;
    obs.layer_id = layers[0].id;
    obs.agent_id = "agent1";
    obs.text = "Trivial observation that should be archived";
    obs.weight = 0.1;
    obs.tags_json = "[]";
    obs.source = "intake:diary";
    obs.created_at_unix_ms = 2000000;
    obs.updated_at_unix_ms = 2000000;
    obs.last_evaluated_at_ms = 0;
    obs.next_review_at_ms = 1;
    auto created = memStore.CreateObservationForAgent("agent1", obs);

    int64_t createdObsId = created.id;
    auto archiveCallback = [createdObsId](const std::string&,
                                           const std::string&,
                                           const std::string&) -> std::string {
        return "[{\"id\": " + std::to_string(createdObsId) +
               ", \"action\": \"demote\", \"reason\": \"low relevance\"}]";
    };

    ConsolidationPipeline pipeline(
        &dataStore, &memStore, nullptr, nullptr, nullptr, nullptr, archiveCallback);

    std::string err;
    Assert(pipeline.RunLayerConsolidation("agent1", "day", &err),
           "Consolidation should succeed: " + err);

    // Verify observation was archived (deleted from active layers)
    auto remaining = memStore.ListObservationsForAgent("agent1", layers[0].id);
    Assert(remaining.empty(), "Observation should be archived (gone from active)");

    auto stats = pipeline.GetStats();
    Assert(stats.observations_archived >= 1, "Stats should show 1+ archived");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestPipelinePerspectiveRevision() {
    std::cerr << "  [Pipeline] Perspective revision...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);

    MemoryLayer layer;
    layer.name = "week";
    layer.horizon = "1 week";
    layer.sort_order = 1;
    layer.evaluation_interval_seconds = 86400;
    layer.cron_expr = "0 * * * *";
    layer.token_budget = 4096;
    layer.enabled = true;
    layer.created_at_unix_ms = 1000000;
    layer.updated_at_unix_ms = 1000000;
    memStore.CreateLayer(layer);

    auto layers = memStore.ListLayers();

    auto perspCallback = [](const std::string&,
                             const std::string&,
                             const std::string&) -> std::string {
        return R"({"retrospective": "The agent explored new territory", "current": "Building consolidation pipeline", "future": "Will continue testing"})";
    };

    ConsolidationPipeline pipeline(
        &dataStore, &memStore, nullptr, nullptr, nullptr, nullptr, perspCallback);

    std::string err;
    Assert(pipeline.RunPerspectiveRevision("agent1", "week", &err),
           "Perspective revision should succeed: " + err);

    // Verify perspective was set
    auto persp = memStore.GetPerspective(layers[0].id);
    Assert(persp.has_value(), "Should have a perspective");
    Assert(persp->retrospective.find("explored") != std::string::npos,
           "Retrospective should contain 'explored'");
    Assert(persp->current_perspective.find("consolidation") != std::string::npos,
           "Current should contain 'consolidation'");
    Assert(persp->future_perspective.find("testing") != std::string::npos,
           "Future should contain 'testing'");

    auto stats = pipeline.GetStats();
    Assert(stats.perspectives_updated >= 1, "Stats should show 1+ perspectives updated");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestPipelineMillenniumLayerSkipped() {
    std::cerr << "  [Pipeline] Millennium layer skipped...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);

    MemoryLayer ml;
    ml.name = "millennium";
    ml.horizon = "1 millennium";
    ml.sort_order = 6;
    ml.evaluation_interval_seconds = 2592000;  // 30 days
    ml.cron_expr = "0 0 * * 0";  // weekly
    ml.token_budget = 4096;
    ml.enabled = true;
    ml.created_at_unix_ms = 1000000;
    ml.updated_at_unix_ms = 1000000;
    memStore.CreateLayer(ml);

    auto layers = memStore.ListLayers();

    // Add an observation to millennium layer
    Observation obs;
    obs.layer_id = layers[0].id;
    obs.agent_id = "agent1";
    obs.text = "Core identity observation";
    obs.weight = 1.0;
    obs.created_at_unix_ms = 2000000;
    obs.updated_at_unix_ms = 2000000;
    memStore.CreateObservationForAgent("agent1", obs);

    bool llmCalled = false;
    auto noOpCallback = [&llmCalled](const std::string&,
                                       const std::string&,
                                       const std::string&) -> std::string {
        llmCalled = true;
        return "[]";
    };

    ConsolidationPipeline pipeline(
        &dataStore, &memStore, nullptr, nullptr, nullptr, nullptr, noOpCallback);

    std::string err;
    Assert(pipeline.RunLayerConsolidation("agent1", "millennium", &err),
           "Should succeed (skip millennium)");

    Assert(!llmCalled, "LLM should NOT be called for millennium layer");

    // Observation should still be there
    auto obs2 = memStore.ListObservationsForAgent("agent1", layers[0].id);
    Assert(!obs2.empty(), "Millennium observation should not be touched");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestPipelineStats() {
    std::cerr << "  [Pipeline] Stats tracking...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);

    auto noOpCallback = [](const std::string&,
                            const std::string&,
                            const std::string&) -> std::string {
        return "[]";
    };

    ConsolidationPipeline pipeline(
        &dataStore, &memStore, nullptr, nullptr, nullptr, nullptr, noOpCallback);

    auto stats = pipeline.GetStats();
    Assert(stats.observations_created == 0, "Initial created should be 0");
    Assert(stats.runs_completed == 0, "Initial runs should be 0");

    // Run intake (with no diary data, should complete but create 0 obs)
    std::string err;
    pipeline.RunIntake("agent1", std::nullopt, &err);

    stats = pipeline.GetStats();
    Assert(stats.runs_completed >= 1, "Should have 1+ completed run");

    auto statusJson = pipeline.GetStatusJson();
    Assert(statusJson.isMember("runs_completed"), "Status JSON should have runs_completed");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestPipelineRunLog() {
    std::cerr << "  [Pipeline] Run logging...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);

    auto noOpCallback = [](const std::string&,
                            const std::string&,
                            const std::string&) -> std::string {
        return "[]";
    };

    ConsolidationPipeline pipeline(
        &dataStore, &memStore, nullptr, nullptr, nullptr, nullptr, noOpCallback);

    std::string err;
    pipeline.RunIntake("agent1", std::nullopt, &err);

    auto& store = pipeline.Store();
    auto runs = store.ListRuns("agent1");
    Assert(!runs.empty(), "Should have logged a run");
    Assert(runs[0].phase == "intake", "Phase should be 'intake'");
    Assert(runs[0].status == "completed", "Status should be 'completed'");

    std::filesystem::remove(dbPath);
    return 0;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cerr << "\n=== Consolidation Pipeline Tests ===\n\n";

    // ConsolidationStore tests
    std::cerr << "-- ConsolidationStore --\n";
    TestStoreWatermarks();
    TestStoreRunLog();

    // Pipeline tests
    std::cerr << "\n-- Pipeline: Intake --\n";
    TestPipelineIntakeFromDiary();

    std::cerr << "\n-- Pipeline: Layer Consolidation --\n";
    TestPipelineLayerConsolidation();
    TestPipelineDemoteToArchive();
    TestPipelineMillenniumLayerSkipped();

    std::cerr << "\n-- Pipeline: Perspective Revision --\n";
    TestPipelinePerspectiveRevision();

    std::cerr << "\n-- Pipeline: Stats & Logging --\n";
    TestPipelineStats();
    TestPipelineRunLog();

    std::cerr << "\n";
    if (g_failures == 0) {
        std::cerr << "All consolidation pipeline tests passed.\n";
    } else {
        std::cerr << g_failures << " test(s) FAILED.\n";
    }
    std::cerr << "\n";

    return g_failures;
}

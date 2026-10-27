#include "animus_kernel/consolidation/ConsolidationStore.h"
#include "animus_kernel/consolidation/ConsolidationPipeline.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/admin/DiaryManager.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/DefaultSessionRouter.h"
#include "animus_kernel/SessionManager.h"
#include "animus_kernel/SessionReportStore.h"
#include "animus_kernel/SqliteSessionStore.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/OntologyStore.h"
#include "animus_kernel/MemoryFileStore.h"
#include "animus_kernel/scheduler/Scheduler.h"
#include "animus_kernel/tools/ConsolidationTool.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
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
    baseLayer.agent_id = "agent1";
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
    // Current intake contract: the LLM creates observations via the
    // consolidation tool DURING the callback (no return-value parsing).
    // Simulate that side effect here, as the tool path would.
    auto intakeCallback = [&memStore](const std::string&,
                                       const std::string&,
                                       const std::string& userPrompt) -> std::string {
        g_llmCallCount++;
        if (userPrompt.find("diary entries") == std::string::npos) return "[]";
        auto layers = memStore.ListLayersForAgent("agent1");
        if (layers.empty()) return "[]";
        memory::Observation obs;
        obs.agent_id = "agent1";
        obs.layer_id = layers.front().id;
        obs.text = "Agent learned about memory consolidation";
        obs.tags_json = "[\"memory\",\"learning\"]";
        obs.weight = 0.8;
        obs.created_at_unix_ms = 3000000;
        obs.updated_at_unix_ms = 3000000;
        memStore.CreateObservationForAgent("agent1", obs);
        return "[]";
    };
    ConsolidationPipeline pipeline(
        &dataStore, &memStore, nullptr, &diaryStore, nullptr, nullptr, intakeCallback);

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
// #220: the default intake prompt must instruct consolidation tool calls
// (action "create") — the only path the pipeline honors — and must not
// request a JSON array return, which IntakeFromDiary/IntakeFromSessions
// discard (no return-value parsing; see the intake contract above).
int TestDefaultIntakePromptToolContract() {
    const std::string prompt = ConsolidationPipeline::Config{}.intake_prompt;

    // Names the consolidation tool and the create action (the instructed path)
    Assert(prompt.find("consolidation") != std::string::npos,
           "default intake prompt must name the consolidation tool");
    Assert(prompt.find("action \"create\"") != std::string::npos,
           "default intake prompt must instruct action \"create\" tool calls");

    // Does not request a JSON array (the old, never-parsed contract)
    Assert(prompt.find("JSON array") == std::string::npos,
           "default intake prompt must not request a JSON array");
    Assert(prompt.find("empty array") == std::string::npos,
           "default intake prompt must not request an empty-array return");
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
    day.agent_id = "agent1";
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
    week.agent_id = "agent1";
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
    day.agent_id = "agent1";
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

    // #221: the archive mutation's row_snapshot reconstructs the departed
    // row — archive deletes from active layers, so the log is all that
    // remains.
    {
        auto muts = memStore.QueryMutations(0, 1000);
        const MemoryMutation* archiveMut = nullptr;
        for (const auto& m : muts) {
            if (m.mutation_type == "archive" && m.target_id == createdObsId) {
                archiveMut = &m;
            }
        }
        Assert(archiveMut != nullptr, "archive mutation row exists");
        if (archiveMut) {
            Json::CharReaderBuilder rb;
            Json::Value snap;
            std::string errs;
            std::istringstream ss(archiveMut->row_snapshot);
            Assert(Json::parseFromStream(rb, ss, &snap, &errs),
                   "archive row_snapshot parses as JSON: " + errs);
            Assert(snap["text"].asString() ==
                       "Trivial observation that should be archived",
                   "archive row_snapshot reconstructs text");
            Assert(snap["weight"].asDouble() == 0.1,
                   "archive row_snapshot reconstructs weight");
            Assert(snap["source"].asString() == "intake:diary",
                   "archive row_snapshot reconstructs source");
        }
    }

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
    layer.agent_id = "agent1";
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

    // Perspective revision requires active observations (drift guard skips
    // empty layers) and the newest observation must postdate the
    // auto-created perspective row (staleness guard).
    const int64_t obsTs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() + 60000;
    memory::Observation seed;
    seed.agent_id = "agent1";
    seed.layer_id = layers.front().id;
    seed.text = "Core observation for perspective revision";
    seed.weight = 1.0;
    seed.created_at_unix_ms = obsTs;
    seed.updated_at_unix_ms = obsTs;
    memStore.CreateObservationForAgent("agent1", seed);

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
    ml.agent_id = "agent1";
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
// #224: live schedule refresh — layers created/updated/enabled after Start
// must get their consolidation schedules without a restart.
// ============================================================================

static void MakeScheduleTestLayer(MemoryStore& memStore, const std::string& agentId,
                                  const std::string& name, const char* cron,
                                  bool enabled) {
    MemoryLayer layer;
    layer.agent_id = agentId;
    layer.name = name;
    layer.horizon = "test horizon";
    layer.cron_expr = cron;
    layer.evaluation_interval_seconds = 3600;
    layer.token_budget = 4096;
    layer.enabled = enabled;
    memStore.CreateLayer(layer);
}

int TestRefreshSchedulesOnLayerCreate() {
    std::cerr << "  [#224] Layer create during run registers schedule...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);
    Scheduler scheduler(&dataStore);

    ConsolidationPipeline pipeline(
        &dataStore, &memStore, nullptr, nullptr, nullptr, nullptr, MockLLMCallback);
    ConsolidationPipeline::Config cfg;
    cfg.intake_enabled = true;
    pipeline.Configure(cfg);

    // Layer that exists before Start gets its schedule at Start.
    MakeScheduleTestLayer(memStore, "agent1", "day", "0 */2 * * *", true);
    std::string err;
    Assert(pipeline.Start(&scheduler, &err), "Start should succeed: " + err);
    // Wire the layer-change hook exactly as AgentKernel does (#224).
    memStore.SetLayerChangedCallback([&pipeline]() { pipeline.RefreshSchedules(); });
    Assert(scheduler.List("agent1", "consolidation").size() == 1,
           "pre-existing enabled layer should have 1 schedule after Start");

    // Wire the same hook AgentKernel wires (ChannelManager restart-hook
    // pattern) — layer mutations now drive live refreshes.
    memStore.SetLayerChangedCallback([&pipeline]() { pipeline.RefreshSchedules(); });

    // Create an enabled layer while running — its schedule must appear
    // without a restart (the acceptance criterion).
    MakeScheduleTestLayer(memStore, "agent1", "week", "0 3 * * *", true);
    Assert(scheduler.List("agent1", "consolidation").size() == 2,
           "layer created during run should get a schedule without restart");

    memStore.SetLayerChangedCallback(nullptr);
    pipeline.Stop();
    Assert(scheduler.List("agent1", "consolidation").empty(),
           "Stop should cancel all pipeline schedules");
    std::filesystem::remove(dbPath);
    return 0;
}

int TestRefreshSchedulesOnLayerUpdate() {
    std::cerr << "  [#224] Layer update during run re-registers schedule...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);
    Scheduler scheduler(&dataStore);

    ConsolidationPipeline pipeline(
        &dataStore, &memStore, nullptr, nullptr, nullptr, nullptr, MockLLMCallback);
    ConsolidationPipeline::Config cfg;
    cfg.intake_enabled = true;
    pipeline.Configure(cfg);

    MakeScheduleTestLayer(memStore, "agent1", "day", "0 */2 * * *", true);
    std::string err;
    Assert(pipeline.Start(&scheduler, &err), "Start should succeed: " + err);
    memStore.SetLayerChangedCallback([&pipeline]() { pipeline.RefreshSchedules(); });
    const auto before = scheduler.List("agent1", "consolidation");
    Assert(before.size() == 1, "expected 1 schedule after Start");

    // Wire the same hook AgentKernel wires — layer mutations drive live refresh.
    memStore.SetLayerChangedCallback([&pipeline]() { pipeline.RefreshSchedules(); });

    // Change the layer's cron while running — the schedule set must follow.
    auto layer = memStore.ListLayersForAgent("agent1").front();
    layer.cron_expr = "15 */3 * * *";
    Assert(memStore.UpdateLayer(layer), "UpdateLayer should succeed");

    const auto after = scheduler.List("agent1", "consolidation");
    Assert(after.size() == 1, "update should not duplicate schedules");
    Assert(after[0].cron_expr == "15 */3 * * *",
           "refresh should pick up the new cron, got: " + after[0].cron_expr);

    pipeline.Stop();
    std::filesystem::remove(dbPath);
    return 0;
}

int TestRefreshSchedulesOnLayerDisable() {
    std::cerr << "  [#224] Layer disable during run removes schedule...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);
    Scheduler scheduler(&dataStore);

    ConsolidationPipeline pipeline(
        &dataStore, &memStore, nullptr, nullptr, nullptr, nullptr, MockLLMCallback);
    ConsolidationPipeline::Config cfg;
    cfg.intake_enabled = true;
    pipeline.Configure(cfg);

    MakeScheduleTestLayer(memStore, "agent1", "day", "0 */2 * * *", true);
    std::string err;
    Assert(pipeline.Start(&scheduler, &err), "Start should succeed: " + err);
    memStore.SetLayerChangedCallback([&pipeline]() { pipeline.RefreshSchedules(); });
    Assert(scheduler.List("agent1", "consolidation").size() == 1, "expected 1 schedule");

    // Disable the layer — the schedule must be gone without a restart.
    auto layer = memStore.ListLayersForAgent("agent1").front();
    layer.enabled = false;
    Assert(memStore.UpdateLayer(layer), "UpdateLayer should succeed");
    Assert(scheduler.List("agent1", "consolidation").empty(),
           "disabled layer's schedule should be removed without restart");

    // Re-enable — schedule must come back.
    layer.enabled = true;
    Assert(memStore.UpdateLayer(layer), "UpdateLayer should succeed");
    Assert(scheduler.List("agent1", "consolidation").size() == 1,
           "re-enabled layer's schedule should be restored without restart");

    pipeline.Stop();
    std::filesystem::remove(dbPath);
    return 0;
}

int TestRefreshNoDuplicateSchedules() {
    std::cerr << "  [#224] Repeated refresh does not duplicate schedules...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);
    Scheduler scheduler(&dataStore);

    ConsolidationPipeline pipeline(
        &dataStore, &memStore, nullptr, nullptr, nullptr, nullptr, MockLLMCallback);
    ConsolidationPipeline::Config cfg;
    cfg.intake_enabled = true;
    pipeline.Configure(cfg);

    MakeScheduleTestLayer(memStore, "agent1", "day", "0 */2 * * *", true);
    std::string err;
    Assert(pipeline.Start(&scheduler, &err), "Start should succeed: " + err);

    // Hammer the refresh path — every layer mutation (callback-wired, as the
    // kernel does) and explicit refreshes must leave exactly one schedule
    // per scheduled layer.
    memStore.SetLayerChangedCallback([&pipeline]() { pipeline.RefreshSchedules(); });
    for (int i = 0; i < 5; ++i) {
        MakeScheduleTestLayer(memStore, "agent1", "day", "0 */2 * * *", true);
        auto layer = memStore.ListLayersForAgent("agent1").front();
        layer.cron_expr = "0 */2 * * *";
        memStore.UpdateLayer(layer);
    }
    pipeline.RefreshSchedules();
    pipeline.RefreshSchedules();

    Assert(scheduler.List("agent1", "consolidation").size() == 1,
           "repeated refresh must not duplicate schedules");

    memStore.SetLayerChangedCallback(nullptr);
    pipeline.Stop();
    std::filesystem::remove(dbPath);
    return 0;
}

int TestSessionReportScheduleShifted() {
    std::cerr << "  [#224] Session-report cron is shifted +30 min...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);
    AgentStore agentStore(&dataStore);
    Scheduler scheduler(&dataStore);

    // Agent-level intake cron drives the session-report registration.
    Agent agent;
    agent.id = "agent1";
    agent.name = "Agent One";
    agent.intake_interval = "0 * * * *";
    auto created = agentStore.Create(agent);
    Assert(!created.id.empty(), "agent create should succeed");

    ConsolidationPipeline pipeline(
        &dataStore, &memStore, nullptr, nullptr, nullptr, &agentStore, MockLLMCallback);
    ConsolidationPipeline::Config cfg;
    cfg.intake_enabled = true;
    pipeline.Configure(cfg);

    std::string err;
    Assert(pipeline.Start(&scheduler, &err), "Start should succeed: " + err);

    // Intake schedule keeps the agent cron; session-report is shifted +30.
    auto scheds = scheduler.List("agent1", "consolidation");
    bool sawIntake = false, sawShifted = false;
    for (const auto& s : scheds) {
        if (s.message == "intake:day") {
            sawIntake = true;
            Assert(s.cron_expr == "0 * * * *", "intake cron should be unshifted");
        }
        if (s.message == "session_report") {
            sawShifted = true;
            Assert(s.cron_expr == "30 * * * *",
                   "session-report cron should be shifted +30, got: " + s.cron_expr);
        }
    }
    Assert(sawIntake, "intake schedule should exist");
    Assert(sawShifted, "session_report schedule should exist");

    pipeline.Stop();
    std::filesystem::remove(dbPath);
    return 0;
}

// ============================================================================
// Main
// ============================================================================

// ============================================================================
// #70 finding 3: perspective writes must be receipt-verified — no
// unconditional success when the upsert is not confirmed.
// ============================================================================

int TestPerspectiveReceiptHonesty() {
    std::cerr << "  [#70] Perspective receipt honesty...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);

    MemoryLayer layer;
    layer.name = "day";
    layer.agent_id = "agent1";
    layer.horizon = "1 day";
    layer.sort_order = 0;
    layer.evaluation_interval_seconds = 3600;
    layer.cron_expr = "0 * * * *";
    layer.token_budget = 4096;
    layer.enabled = true;
    layer.created_at_unix_ms = 1000000;
    layer.updated_at_unix_ms = 1000000;
    Assert(memStore.CreateLayer(layer).id > 0, "CreateLayer should succeed");
    auto layers = memStore.ListLayersForAgent("agent1");
    Assert(!layers.empty(), "layer should be visible to its agent");

    ontology::OntologyStore ontologyStore(&dataStore);
    memory::MemoryFileStore fileStore(&dataStore);
    AgentStore agentStore(&dataStore);
    SessionReportStore reportStore(&dataStore);
    SessionManager sessions(
        std::make_unique<SqliteSessionStore>(&dataStore),
        std::make_unique<DefaultSessionRouter>());
    ConsolidationTool tool(&memStore, &ontologyStore, &sessions,
                           &fileStore, &agentStore, &reportStore, nullptr);

    // Happy path: write via the tool, verify the row actually changed.
    {
        ToolCall call;
        call.id = "p70a";
        call.arguments =
            R"({"action":"perspective:generate","__agent_id":"agent1",)"
            R"("__session_key":"consolidation:intake:agent1",)"
            R"("params":{"layer":"day","pov":"current","text":"fresh current text 70a"}})";
        auto result = tool.Execute(call);
        Assert(result.success, "perspective:generate should succeed: " + result.error);
        auto persp = memStore.GetPerspective(layers[0].id);
        Assert(persp.has_value() &&
                   persp->current_perspective.find("70a") != std::string::npos,
               "perspective row should contain the written text");
    }

    // Failure path: make the write fail, the tool must NOT report success.
    {
        auto drop = dataStore.Prepare("DROP TABLE layer_perspectives");
        Assert(drop && drop->ExecDML(), "drop layer_perspectives for failure injection");

        ToolCall call;
        call.id = "p70b";
        call.arguments =
            R"({"action":"perspective:generate","__agent_id":"agent1",)"
            R"("__session_key":"consolidation:intake:agent1",)"
            R"("params":{"layer":"day","pov":"future","text":"should never land"}})";
        auto result = tool.Execute(call);
        Assert(!result.success,
               "perspective:generate must fail when the write is not confirmed");
        Assert(result.error.find("not confirmed") != std::string::npos,
               "failure error should name the unconfirmed write");
    }

    std::filesystem::remove(dbPath);
    return 0;
}

int TestPipelinePerspectiveFailurePropagates() {
    std::cerr << "  [#70] Pipeline perspective failure propagates...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);

    MemoryLayer layer;
    layer.name = "day";
    layer.agent_id = "agent1";
    layer.horizon = "1 day";
    layer.sort_order = 0;
    layer.evaluation_interval_seconds = 3600;
    layer.cron_expr = "0 * * * *";
    layer.token_budget = 4096;
    layer.enabled = true;
    layer.created_at_unix_ms = 1000000;
    layer.updated_at_unix_ms = 1000000;
    Assert(memStore.CreateLayer(layer).id > 0, "CreateLayer should succeed");
    auto layers = memStore.ListLayersForAgent("agent1");
    Assert(!layers.empty(), "layer should be visible to its agent");

    memory::Observation obs;
    obs.layer_id = layers[0].id;
    obs.agent_id = "agent1";
    obs.text = "seed observation so perspective revision is not skipped";
    memStore.CreateObservationForAgent("agent1", obs);

    auto cb = [](const std::string&, const std::string&, const std::string&) -> std::string {
        return R"({"retrospective":"r","current":"c","future":"f"})";
    };
    ConsolidationPipeline pipeline(&dataStore, &memStore, nullptr, nullptr,
                                   nullptr, nullptr, cb);

    auto drop = dataStore.Prepare("DROP TABLE layer_perspectives");
    Assert(drop && drop->ExecDML(), "drop layer_perspectives for failure injection");

    std::string err;
    Assert(!pipeline.RunPerspectiveRevision("agent1", "day", &err),
           "pipeline perspective revision must fail when write is not confirmed");
    Assert(err.find("not confirmed") != std::string::npos,
           "pipeline error should name the unconfirmed write: " + err);

    std::filesystem::remove(dbPath);
    return 0;
}

// ============================================================================
// #70 finding 5: fetch_pending byte budget — batches must stay under the
// downstream tool-result cap, per-turn truncation must be explicit with a
// pointer to the full text, unconsumed turns must remain pending.
// ============================================================================

int TestFetchPendingByteBudget() {
    std::cerr << "  [#70] fetch_pending byte budget...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);
    ontology::OntologyStore ontologyStore(&dataStore);
    memory::MemoryFileStore fileStore(&dataStore);
    AgentStore agentStore(&dataStore);
    SessionReportStore reportStore(&dataStore);
    SessionManager sessions(
        std::make_unique<SqliteSessionStore>(&dataStore),
        std::make_unique<DefaultSessionRouter>());
    ConsolidationTool tool(&memStore, &ontologyStore, &sessions,
                           &fileStore, &agentStore, &reportStore, nullptr);

    // One normal session with 30 turns of 3KB each = 90KB — far over the
    // 24KB default budget, well under the 50-turn limit.
    auto session = sessions.GetOrCreate(SessionKey{"rss", "feed:pulls", ""});
    session->SetAgentId("agent1");
    for (int i = 0; i < 30; ++i) {
        SessionTurn turn;
        turn.role = "user";
        turn.content = "payload-" + std::to_string(i) + "-" + std::string(3000, 'x');
        turn.unix_ms = static_cast<std::uint64_t>(1000 + i);
        session->AddTurn(std::move(turn));
    }
    sessions.GetStore().FlushSession(session->Id());

    ToolCall call;
    call.id = "f70";
    call.arguments =
        R"({"action":"fetch_pending","__agent_id":"agent1",)"
        R"("__session_key":"consolidation:intake:agent1","params":{}})";
    auto result = tool.Execute(call);
    Assert(result.success, "fetch_pending should succeed: " + result.error);

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::istringstream output(result.output);
    Assert(Json::parseFromStream(rb, output, &root, &errs),
           "fetch_pending output should be valid JSON: " + errs);
    Assert(root["total"].asInt() == 30, "total should report all 30 pending turns");
    Assert(root["turns"].size() > 0 && root["turns"].size() < 30,
           "byte budget should emit a strict subset of pending turns");
    Assert(root["has_more"].asBool(), "has_more should be true when turns remain");
    Assert(root["bytes"].asUInt64() <= 30000, "emitted bytes should respect the budget");
    Assert(!root["turns"][0]["turn_id"].isNull(),
           "turns must carry turn_id for truncation pointers");

    // A single oversized turn is truncated to the per-turn cap with a pointer.
    {
        auto big = sessions.GetOrCreate(SessionKey{"rss", "feed:bigone", ""});
        big->SetAgentId("agent1");
        SessionTurn turn;
        turn.role = "user";
        turn.content = std::string(10000, 'y');
        turn.unix_ms = 2000;
        big->AddTurn(std::move(turn));
        sessions.GetStore().FlushSession(big->Id());
        big->MarkTerminated();
        sessions.GetStore().FlushSession(big->Id());

        ToolCall bigCall;
        bigCall.id = "f70b";
        bigCall.arguments =
            R"({"action":"fetch_pending","__agent_id":"agent1",)"
            R"("__session_key":"consolidation:intake:agent1","params":{"limit":1}})";
        auto bigResult = tool.Execute(bigCall);
        Assert(bigResult.success, "fetch_pending (oversized single turn) should succeed");
        Json::Value bigRoot;
        std::istringstream bigOutput(bigResult.output);
        Assert(Json::parseFromStream(rb, bigOutput, &bigRoot, &errs),
               "oversized-turn output should be valid JSON");
        const auto& t0 = bigRoot["turns"][0];
        Assert(t0["truncated"].asBool(), "oversized turn should be flagged truncated");
        Assert(t0["content"].asString().size() == 2048,
               "oversized turn content should be cut to the per-turn cap");
        Assert(t0["note"].asString().find("sessions:history") != std::string::npos,
               "truncation note should point at the sessions tool for full text");
    }

    std::filesystem::remove(dbPath);
    return 0;
}

// ============================================================================
// #70 finding 1: ontology curation verbs — list/get/delete exposed at the
// tool layer (reads available everywhere; delete agent-scoped, motivation
// mandatory, mutation-logged). Plus finding 4 regression: missing
// root_category must be a clear required-error, never a silent invalid
// default.
// ============================================================================

int TestOntologyCurationVerbs() {
    std::cerr << "  [#70] Ontology curation verbs...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryStore memStore(&dataStore);
    SessionManager sessions(
        std::make_unique<SqliteSessionStore>(&dataStore),
        std::make_unique<DefaultSessionRouter>());
    ontology::OntologyStore ontologyStore(&dataStore);
    memory::MemoryFileStore fileStore(&dataStore);
    AgentStore agentStore(&dataStore);
    SessionReportStore reportStore(&dataStore);
    ConsolidationTool tool(&memStore, &ontologyStore, &sessions,
                           &fileStore, &agentStore, &reportStore, nullptr);

    const std::string intakeKey = "\"__session_key\":\"consolidation:intake:agent1\"";
    const std::string reviewKey = "\"__session_key\":\"consolidation:review:agent1\"";

    // Seed two entities with properties via the upsert verb itself.
    {
        ToolCall call;
        call.id = "o70a";
        call.arguments =
            "{\"action\":\"ontology:upsert\",\"__agent_id\":\"agent1\"," + intakeKey + ","
            "\"params\":{\"root_category\":\"persons\",\"path\":\"persons/JunkTarget\","
            "\"properties\":{\"origin\":{\"value\":\"rss-noise\"}}}}";
        auto result = tool.Execute(call);
        Assert(result.success, "upsert JunkTarget should succeed: " + result.error);
    }
    {
        ToolCall call;
        call.id = "o70b";
        call.arguments =
            "{\"action\":\"ontology:upsert\",\"__agent_id\":\"agent1\"," + intakeKey + ","
            "\"params\":{\"root_category\":\"concepts\",\"path\":\"concepts/KeepIdea\"}}";
        auto result = tool.Execute(call);
        Assert(result.success, "upsert KeepIdea should succeed: " + result.error);
    }

    // F4 regression: missing root_category names the requirement — never a
    // phantom "Invalid root_category: concept" the caller never sent.
    {
        ToolCall call;
        call.id = "o70f4";
        call.arguments =
            "{\"action\":\"ontology:upsert\",\"__agent_id\":\"agent1\"," + intakeKey + ","
            "\"params\":{\"path\":\"persons/NoCategory\"}}";
        auto result = tool.Execute(call);
        Assert(!result.success, "upsert without root_category should fail");
        Assert(result.error.find("root_category is required") != std::string::npos,
               "missing root_category should name the requirement: " + result.error);
    }

    // list: category-filtered, case-insensitive name filter.
    {
        ToolCall call;
        call.id = "o70l";
        call.arguments =
            "{\"action\":\"ontology:list\",\"__agent_id\":\"agent1\"," + intakeKey + ","
            "\"params\":{\"category\":\"persons\",\"name_contains\":\"junk\"}}";
        auto result = tool.Execute(call);
        Assert(result.success, "ontology:list should succeed: " + result.error);
        Assert(result.output.find("JunkTarget") != std::string::npos,
               "list should find JunkTarget via case-insensitive filter");
        Assert(result.output.find("KeepIdea") == std::string::npos,
               "category filter should exclude other roots");
    }

    // get by id: entity + properties.
    {
        ToolCall call;
        call.id = "o70g";
        call.arguments =
            "{\"action\":\"ontology:get\",\"__agent_id\":\"agent1\"," + intakeKey + ","
            "\"params\":{\"root_category\":\"persons\",\"path\":\"persons/JunkTarget\"}}";
        auto result = tool.Execute(call);
        Assert(result.success, "ontology:get should succeed: " + result.error);
        Assert(result.output.find("rss-noise") != std::string::npos,
               "get should return the seeded property");
    }

    // delete: motivation mandatory, agent-scoped, effective.
    {
        ToolCall noMotivation;
        noMotivation.id = "o70d1";
        noMotivation.arguments =
            "{\"action\":\"ontology:delete\",\"__agent_id\":\"agent1\"," + reviewKey + ","
            "\"params\":{\"path\":\"persons/JunkTarget\"}}";
        // id required for delete — path-only should be rejected
        auto r1 = tool.Execute(noMotivation);
        Assert(!r1.success && r1.error.find("id is required") != std::string::npos,
               "delete without id should name the requirement");

        // find the id via list
        ToolCall listCall;
        listCall.id = "o70l2";
        listCall.arguments =
            "{\"action\":\"ontology:list\",\"__agent_id\":\"agent1\"," + intakeKey + ","
            "\"params\":{\"name_contains\":\"JunkTarget\"}}";
        auto listResult = tool.Execute(listCall);
        Assert(listResult.success && listResult.output.find("\"id\":") != std::string::npos,
               "list for delete should expose ids");

        // extract id from the JSON (first entity)
        Json::Value root;
        Json::CharReaderBuilder rb;
        std::string errs;
        std::istringstream ls(listResult.output);
        Assert(Json::parseFromStream(rb, ls, &root, &errs), "list output valid JSON");
        const int64_t junkId = root["entities"][0]["id"].asInt64();

        ToolCall foreign;
        foreign.id = "o70d2";
        foreign.arguments =
            "{\"action\":\"ontology:delete\",\"__agent_id\":\"sable\"," + reviewKey + ","
            "\"params\":{\"id\":" + std::to_string(junkId) + ",\"motivation\":\"sweep\"}}";
        auto r2 = tool.Execute(foreign);
        Assert(!r2.success && r2.error.find("does not belong") != std::string::npos,
               "foreign agent must not delete another agent's entity");

        ToolCall unmotivated;
        unmotivated.id = "o70d3";
        unmotivated.arguments =
            "{\"action\":\"ontology:delete\",\"__agent_id\":\"agent1\"," + reviewKey + ","
            "\"params\":{\"id\":" + std::to_string(junkId) + "}}";
        auto r3 = tool.Execute(unmotivated);
        Assert(!r3.success && r3.error.find("motivation is required") != std::string::npos,
               "delete without motivation must be rejected");

        ToolCall good;
        good.id = "o70d4";
        good.arguments =
            "{\"action\":\"ontology:delete\",\"__agent_id\":\"agent1\"," + reviewKey + ","
            "\"params\":{\"id\":" + std::to_string(junkId) +
            ",\"motivation\":\"rss-noise cleanup (#70)\"}}";
        auto r4 = tool.Execute(good);
        Assert(r4.success, "motivated delete by owning agent should succeed: " + r4.error);

        // gone from listing
        auto afterList = tool.Execute(listCall);
        Assert(afterList.success &&
               afterList.output.find("JunkTarget") == std::string::npos,
               "deleted entity must vanish from listings");
    }

    std::filesystem::remove(dbPath);
    return 0;
}

int main() {
    std::cerr << "\n=== Consolidation Pipeline Tests ===\n\n";
    // #70 tests run first: the pre-existing intake-test crash below kills the
    // process before later tests can run (see ctest baseline).
    std::cerr << "-- #70: Receipt honesty & fetch budget --\n";
    TestPerspectiveReceiptHonesty();
    TestOntologyCurationVerbs();
    TestPipelinePerspectiveFailurePropagates();
    TestFetchPendingByteBudget();

    // ConsolidationStore tests
    std::cerr << "-- ConsolidationStore --\n";
    TestStoreWatermarks();
    TestStoreRunLog();

    // Pipeline tests
    std::cerr << "\n-- Pipeline: Intake --\n";
    TestPipelineIntakeFromDiary();
    TestDefaultIntakePromptToolContract();

    std::cerr << "\n-- Pipeline: Layer Consolidation --\n";
    TestPipelineLayerConsolidation();
    TestPipelineDemoteToArchive();
    TestPipelineMillenniumLayerSkipped();

    std::cerr << "\n-- Pipeline: Perspective Revision --\n";
    TestPipelinePerspectiveRevision();

    std::cerr << "\n-- Pipeline: Stats & Logging --\n";
    TestPipelineStats();
    TestPipelineRunLog();

    std::cerr << "\n-- #224: Live schedule refresh --\n";
    TestRefreshSchedulesOnLayerCreate();
    TestRefreshSchedulesOnLayerUpdate();
    TestRefreshSchedulesOnLayerDisable();
    TestRefreshNoDuplicateSchedules();
    TestSessionReportScheduleShifted();

    std::cerr << "\n";
    if (g_failures == 0) {
        std::cerr << "All consolidation pipeline tests passed.\n";
    } else {
        std::cerr << g_failures << " test(s) FAILED.\n";
    }
    std::cerr << "\n";

    return g_failures;
}

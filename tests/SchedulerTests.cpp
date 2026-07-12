#include "animus_kernel/scheduler/ScheduleStore.h"
#include "animus_kernel/scheduler/Scheduler.h"
#include "animus_kernel/tools/ScheduleTool.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/tools/ToolRegistry.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <json/json.h>
#include <json/reader.h>
#include <json/writer.h>

using namespace animus::kernel;

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
    char tmp[] = "/tmp/animus_scheduler_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

// Helper to create a one-shot schedule descriptor
ScheduleDescriptor MakeOneShot(
    const std::string& agentId,
    const std::string& nextFire,
    const std::string& message = "test reminder",
    const std::string& tag = "") {
    ScheduleDescriptor s;
    s.agent_id = agentId;
    s.tag = tag;
    s.type = ScheduleType::OneShot;
    s.next_fire = nextFire;
    s.message = message;
    s.enabled = true;
    s.max_fires = 1;
    return s;
}

// ============================================================================
// ScheduleStore tests
// ============================================================================

int TestStoreCreateAndGet() {
    std::cerr << "  [Store] Create and Get...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ScheduleStore store(&dataStore);

    auto sched = MakeOneShot("agent1", "2026-05-10T09:00:00Z", "wake up");
    std::string err;
    Assert(store.Create(sched, &err), "Create failed: " + err);

    // Verify ID was generated
    Assert(!sched.id.empty(), "ID should be auto-generated");

    // Retrieve
    auto retrieved = store.Get(sched.id);
    Assert(retrieved.has_value(), "Get should return the schedule");
    Assert(retrieved->agent_id == "agent1", "agent_id mismatch");
    Assert(retrieved->next_fire == "2026-05-10T09:00:00Z", "next_fire mismatch");
    Assert(retrieved->message == "wake up", "message mismatch");
    Assert(retrieved->type == ScheduleType::OneShot, "type mismatch");
    Assert(retrieved->enabled, "should be enabled");
    Assert(retrieved->fire_count == 0, "fire_count should be 0");
    Assert(retrieved->max_fires == 1, "max_fires should be 1");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestStoreListByAgent() {
    std::cerr << "  [Store] List by agent...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ScheduleStore store(&dataStore);

    std::string err;
    store.Create(MakeOneShot("agent1", "2026-05-10T09:00:00Z", "a1 morning", "reminders"), &err);
    store.Create(MakeOneShot("agent1", "2026-05-10T12:00:00Z", "a1 noon", "reminders"), &err);
    store.Create(MakeOneShot("agent2", "2026-05-10T09:00:00Z", "a2 morning"), &err);

    auto a1All = store.List("agent1");
    Assert(a1All.size() == 2, "agent1 should have 2 schedules, got " + std::to_string(a1All.size()));

    auto a1Tagged = store.List("agent1", "reminders");
    Assert(a1Tagged.size() == 2, "agent1 'reminders' tag should have 2");

    auto a2All = store.List("agent2");
    Assert(a2All.size() == 1, "agent2 should have 1 schedule");

    auto a3All = store.List("agent3");
    Assert(a3All.empty(), "agent3 should have 0 schedules");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestStoreDelete() {
    std::cerr << "  [Store] Delete...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ScheduleStore store(&dataStore);

    auto sched = MakeOneShot("agent1", "2026-05-10T09:00:00Z");
    std::string err;
    store.Create(sched, &err);

    Assert(store.Delete(sched.id, &err), "Delete should succeed");
    Assert(!store.Get(sched.id).has_value(), "Get after delete should return nullopt");
    Assert(!store.Delete("nonexistent", &err), "Delete nonexistent should fail");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestStoreUpdate() {
    std::cerr << "  [Store] Update...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ScheduleStore store(&dataStore);

    auto sched = MakeOneShot("agent1", "2026-05-10T09:00:00Z", "original");
    std::string err;
    store.Create(sched, &err);

    // Update: disable it and change message
    sched.message = "updated";
    sched.enabled = false;
    sched.fire_count = 1;
    sched.last_fire = "2026-05-10T09:00:05Z";
    Assert(store.Update(sched, &err), "Update should succeed");

    auto retrieved = store.Get(sched.id);
    Assert(retrieved.has_value(), "Should still exist after update");
    Assert(retrieved->message == "updated", "Message should be updated");
    Assert(!retrieved->enabled, "Should be disabled");
    Assert(retrieved->fire_count == 1, "fire_count should be 1");
    Assert(retrieved->last_fire == "2026-05-10T09:00:05Z", "last_fire mismatch");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestStoreGetDueSchedules() {
    std::cerr << "  [Store] GetDueSchedules...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ScheduleStore store(&dataStore);

    std::string err;
    auto past = MakeOneShot("agent1", "2026-05-09T09:00:00Z", "past due");
    store.Create(past, &err);

    auto now = MakeOneShot("agent1", "2026-05-10T09:00:00Z", "exactly now");
    store.Create(now, &err);

    auto future = MakeOneShot("agent1", "2026-05-11T09:00:00Z", "not yet");
    store.Create(future, &err);

    // Disabled schedule should not appear
    auto disabled = MakeOneShot("agent1", "2026-05-09T08:00:00Z", "disabled");
    disabled.enabled = false;
    store.Create(disabled, &err);

    auto due = store.GetDueSchedules("2026-05-10T09:00:00Z");
    Assert(due.size() == 2, "Should have 2 due schedules (past + now), got " + std::to_string(due.size()));

    // Both should be in chronological order
    Assert(due[0].next_fire <= due[1].next_fire, "Results should be ordered by next_fire");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestStoreCountByAgent() {
    std::cerr << "  [Store] CountByAgent...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ScheduleStore store(&dataStore);

    std::string err;
    store.Create(MakeOneShot("agent1", "2026-05-10T09:00:00Z"), &err);
    store.Create(MakeOneShot("agent1", "2026-05-10T10:00:00Z"), &err);
    store.Create(MakeOneShot("agent1", "2026-05-10T11:00:00Z"), &err);

    // Disabled should not count
    auto disabled = MakeOneShot("agent1", "2026-05-10T12:00:00Z");
    disabled.enabled = false;
    store.Create(disabled, &err);

    Assert(store.CountByAgent("agent1") == 3, "agent1 should have 3 enabled schedules");
    Assert(store.CountByAgent("agent2") == 0, "agent2 should have 0 schedules");

    std::filesystem::remove(dbPath);
    return 0;
}

// ============================================================================
// Scheduler tests (no network, no thread — test logic directly)
// ============================================================================

int TestSchedulerCreateOneShot() {
    std::cerr << "  [Scheduler] Create one-shot...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    Scheduler scheduler(&dataStore);

    std::string err;
    auto id = scheduler.Create(
        MakeOneShot("agent1", "2026-05-10T09:00:00Z", "test reminder"), &err);
    Assert(!id.empty(), "Create should return an ID: " + err);

    auto retrieved = scheduler.Get(id);
    Assert(retrieved.has_value(), "Get should find the schedule");
    Assert(retrieved->type == ScheduleType::OneShot, "Should be one-shot");
    Assert(retrieved->next_fire == "2026-05-10T09:00:00Z", "next_fire should be preserved");
    Assert(retrieved->max_fires == 1, "One-shot should have max_fires=1");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestSchedulerCreateFromCron() {
    std::cerr << "  [Scheduler] Create from cron expression...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    Scheduler scheduler(&dataStore);

    ScheduleDescriptor sched;
    sched.agent_id = "agent1";
    sched.tag = "healthcheck";
    sched.next_fire = "0 9 * * 1";  // Every Monday at 9am UTC
    sched.message = "weekly healthcheck";
    sched.type = ScheduleType::Recurring;

    std::string err;
    auto id = scheduler.Create(sched, &err);
    Assert(!id.empty(), "Create from cron should succeed: " + err);

    auto retrieved = scheduler.Get(id);
    Assert(retrieved.has_value(), "Should be retrievable");
    Assert(retrieved->type == ScheduleType::Recurring, "Should be recurring");
    Assert(!retrieved->cron_expr.empty(), "cron_expr should be set");
    Assert(!retrieved->next_fire.empty(), "next_fire should be computed");
    Assert(retrieved->next_fire != "0 9 * * 1", "next_fire should be resolved ISO, not the cron expr");

    std::cerr << "    cron '0 9 * * 1' → next_fire: " << retrieved->next_fire << "\n";

    std::filesystem::remove(dbPath);
    return 0;
}

int TestSchedulerCreateFromRelativeTime() {
    std::cerr << "  [Scheduler] Create from relative time...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    Scheduler scheduler(&dataStore);

    ScheduleDescriptor sched;
    sched.agent_id = "agent1";
    sched.next_fire = "in 30 minutes";
    sched.message = "short reminder";

    std::string err;
    auto id = scheduler.Create(sched, &err);
    Assert(!id.empty(), "Create from 'in 30 minutes' should succeed: " + err);

    auto retrieved = scheduler.Get(id);
    Assert(retrieved.has_value(), "Should be retrievable");
    Assert(retrieved->next_fire != "in 30 minutes", "next_fire should be resolved ISO");
    Assert(retrieved->next_fire.find("T") != std::string::npos, "Should contain ISO date separator");

    std::cerr << "    'in 30 minutes' → next_fire: " << retrieved->next_fire << "\n";

    std::filesystem::remove(dbPath);
    return 0;
}

int TestSchedulerCancel() {
    std::cerr << "  [Scheduler] Cancel...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    Scheduler scheduler(&dataStore);

    std::string err;
    auto id = scheduler.Create(
        MakeOneShot("agent1", "2026-05-10T09:00:00Z"), &err);

    Assert(scheduler.Cancel(id, &err), "Cancel should succeed");
    Assert(!scheduler.Get(id).has_value(), "Should be gone after cancel");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestSchedulerList() {
    std::cerr << "  [Scheduler] List...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    Scheduler scheduler(&dataStore);

    std::string err;
    scheduler.Create(MakeOneShot("agent1", "2026-05-10T09:00:00Z", "morning", "reminders"), &err);
    scheduler.Create(MakeOneShot("agent1", "2026-05-10T12:00:00Z", "noon", "reminders"), &err);
    scheduler.Create(MakeOneShot("agent2", "2026-05-10T09:00:00Z", "agent2 stuff"), &err);

    auto a1 = scheduler.List("agent1");
    Assert(a1.size() == 2, "agent1 should have 2 schedules");

    auto a1Reminders = scheduler.List("agent1", "reminders");
    Assert(a1Reminders.size() == 2, "agent1 reminders should have 2");

    auto a2 = scheduler.List("agent2");
    Assert(a2.size() == 1, "agent2 should have 1 schedule");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestSchedulerLimitEnforcement() {
    std::cerr << "  [Scheduler] Schedule limit enforcement...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    Scheduler scheduler(&dataStore);
    scheduler.SetMaxSchedulesPerAgent(3);

    std::string err;
    Assert(!scheduler.Create(MakeOneShot("agent1", "2026-05-10T09:00:00Z", "1"), &err).empty(), "1st should work");
    Assert(!scheduler.Create(MakeOneShot("agent1", "2026-05-10T10:00:00Z", "2"), &err).empty(), "2nd should work");
    Assert(!scheduler.Create(MakeOneShot("agent1", "2026-05-10T11:00:00Z", "3"), &err).empty(), "3rd should work");

    auto id = scheduler.Create(MakeOneShot("agent1", "2026-05-10T12:00:00Z", "4"), &err);
    Assert(id.empty(), "4th should fail (limit reached)");
    Assert(!err.empty(), "Error message should be set");

    std::cerr << "    limit error: " << err << "\n";

    std::filesystem::remove(dbPath);
    return 0;
}

// ============================================================================
// Fire callback test (polling-based)
// ============================================================================

int TestSchedulerFireCallback() {
    std::cerr << "  [Scheduler] Fire callback (poll-based)...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    Scheduler scheduler(&dataStore);
    scheduler.SetPollIntervalMs(500);  // Fast polling for test

    std::atomic<int> fireCount{0};
    std::string firedMessage;
    std::mutex mtx;

    scheduler.SetFireCallback([&](const IncomingEvent& event) {
        std::lock_guard<std::mutex> lock(mtx);
        fireCount++;
        if (event.metadata.count("message")) {
            firedMessage = event.metadata.at("message");
        }
    });

    // Create a schedule that fires immediately (past timestamp)
    ScheduleDescriptor past;
    past.agent_id = "agent1";
    past.next_fire = "2020-01-01T00:00:00Z";  // Already past
    past.message = "fired from the past";
    past.type = ScheduleType::OneShot;

    std::string err;
    auto id = scheduler.Create(past, &err);
    Assert(!id.empty(), "Create past schedule: " + err);

    // Start the scheduler
    Assert(scheduler.Start(&err), "Start should succeed: " + err);

    // Wait for it to fire (up to 2 seconds)
    for (int i = 0; i < 20 && fireCount.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    scheduler.Stop();

    Assert(fireCount.load() >= 1, "Fire callback should have been called at least once, got " + std::to_string(fireCount.load()));
    Assert(firedMessage == "fired from the past", "Fired message should match");

    // One-shot should be disabled after firing
    auto retrieved = scheduler.Get(id);
    Assert(retrieved.has_value(), "Schedule should still exist");
    Assert(!retrieved->enabled, "One-shot should be disabled after fire");
    Assert(retrieved->fire_count >= 1, "fire_count should be >= 1");

    std::filesystem::remove(dbPath);
    return 0;
}

// ============================================================================
// ScheduleTool tests (tool interface)
// ============================================================================

int TestScheduleToolDefinition() {
    std::cerr << "  [Tool] Definition...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    Scheduler scheduler(&dataStore);
    ScheduleTool tool(&scheduler);

    auto def = tool.GetDefinition();
    Assert(def.name == "schedule", "Tool name should be 'schedule'");
    Assert(def.resultMode == ToolResultMode::deliver_to_model, "Should be deliver_to_model");
    Assert(!def.parameters.empty(), "Should have parameters");

    // Find the 'action' parameter
    bool hasAction = false;
    for (const auto& p : def.parameters) {
        if (p.name == "action") {
            hasAction = true;
            Assert(p.required, "action should be required");
            Assert(!p.enum_values.empty(), "action should have enum values");
        }
    }
    Assert(hasAction, "Should have 'action' parameter");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestScheduleToolSetAndList() {
    std::cerr << "  [Tool] Set and List actions...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    Scheduler scheduler(&dataStore);
    ScheduleTool tool(&scheduler);

    // Set a schedule
    Json::Value setArgs;
    setArgs["action"] = "set";
    setArgs["when"] = "2026-05-10T09:00:00Z";
    setArgs["message"] = "morning reminder";
    setArgs["tag"] = "test";
    setArgs["__agent_id"] = "agent1";

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";

    ToolCall setCall;
    setCall.id = "call_set_1";
    setCall.arguments = Json::writeString(wb, setArgs);

    auto setResult = tool.Execute(setCall);
    Assert(setResult.success, "Set should succeed: " + setResult.error);
    Assert(!setResult.output.empty(), "Set should return output");

    // Parse the set result to get the ID
    Json::Value setOut;
    Json::CharReaderBuilder rb;
    std::istringstream setStream(setResult.output);
    Json::parseFromStream(rb, setStream, &setOut, nullptr);
    std::string scheduleId = setOut["id"].asString();
    Assert(!scheduleId.empty(), "Set result should contain an ID");

    // List schedules
    Json::Value listArgs;
    listArgs["action"] = "list";
    listArgs["__agent_id"] = "agent1";

    ToolCall listCall;
    listCall.id = "call_list_1";
    listCall.arguments = Json::writeString(wb, listArgs);

    auto listResult = tool.Execute(listCall);
    Assert(listResult.success, "List should succeed: " + listResult.error);

    Json::Value listOut;
    std::istringstream listStream(listResult.output);
    Json::parseFromStream(rb, listStream, &listOut, nullptr);
    Assert(listOut["count"].asInt() == 1, "Should have 1 schedule");

    // Cancel
    Json::Value cancelArgs;
    cancelArgs["action"] = "cancel";
    cancelArgs["id"] = scheduleId;

    ToolCall cancelCall;
    cancelCall.id = "call_cancel_1";
    cancelCall.arguments = Json::writeString(wb, cancelArgs);

    auto cancelResult = tool.Execute(cancelCall);
    Assert(cancelResult.success, "Cancel should succeed: " + cancelResult.error);

    // Verify it's gone
    ToolCall listCall2;
    listCall2.id = "call_list_2";
    listCall2.arguments = Json::writeString(wb, listArgs);

    auto listResult2 = tool.Execute(listCall2);
    Json::Value listOut2;
    std::istringstream listStream2(listResult2.output);
    Json::parseFromStream(rb, listStream2, &listOut2, nullptr);
    Assert(listOut2["count"].asInt() == 0, "Should have 0 schedules after cancel");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestScheduleToolErrors() {
    std::cerr << "  [Tool] Error handling...\n";
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    Scheduler scheduler(&dataStore);
    ScheduleTool tool(&scheduler);

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";

    // Missing action
    ToolCall noAction;
    noAction.id = "call_noaction";
    noAction.arguments = "{}";
    auto r1 = tool.Execute(noAction);
    Assert(!r1.success, "Missing action should fail");

    // Unknown action
    Json::Value badAction;
    badAction["action"] = "explode";
    ToolCall badCall;
    badCall.id = "call_bad";
    badCall.arguments = Json::writeString(wb, badAction);
    auto r2 = tool.Execute(badCall);
    Assert(!r2.success, "Unknown action should fail");

    // Cancel nonexistent
    Json::Value cancelArgs;
    cancelArgs["action"] = "cancel";
    cancelArgs["id"] = "does_not_exist";
    ToolCall cancelCall;
    cancelCall.id = "call_cancel_fake";
    cancelCall.arguments = Json::writeString(wb, cancelArgs);
    auto r3 = tool.Execute(cancelCall);
    Assert(!r3.success, "Cancel nonexistent should fail");

    // Set without 'when'
    Json::Value noWhen;
    noWhen["action"] = "set";
    noWhen["message"] = "no time";
    ToolCall noWhenCall;
    noWhenCall.id = "call_nowhen";
    noWhenCall.arguments = Json::writeString(wb, noWhen);
    auto r4 = tool.Execute(noWhenCall);
    Assert(!r4.success, "Set without 'when' should fail");

    std::filesystem::remove(dbPath);
    return 0;
}

} // anonymous namespace

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cerr << "\n=== Scheduler Tests ===\n\n";

    // Store tests
    std::cerr << "-- ScheduleStore --\n";
    TestStoreCreateAndGet();
    TestStoreListByAgent();
    TestStoreDelete();
    TestStoreUpdate();
    TestStoreGetDueSchedules();
    TestStoreCountByAgent();

    // Scheduler logic tests (no threading)
    std::cerr << "\n-- Scheduler --\n";
    TestSchedulerCreateOneShot();
    TestSchedulerCreateFromCron();
    TestSchedulerCreateFromRelativeTime();
    TestSchedulerCancel();
    TestSchedulerList();
    TestSchedulerLimitEnforcement();

    // Fire callback test (uses thread)
    std::cerr << "\n-- Scheduler (fire callback) --\n";
    TestSchedulerFireCallback();

    // Tool tests
    std::cerr << "\n-- ScheduleTool --\n";
    TestScheduleToolDefinition();
    TestScheduleToolSetAndList();
    TestScheduleToolErrors();

    std::cerr << "\n";
    if (g_failures == 0) {
        std::cerr << "All scheduler tests passed.\n";
    } else {
        std::cerr << g_failures << " test(s) FAILED.\n";
    }
    std::cerr << "\n";

    return g_failures;
}

// #307 tests: malformed tool-call arguments must never brick a session.
//
// Incident (2026-11-14, sessions 203/208): glm-5.3-flash emitted a tool call
// whose arguments JSON was truncated mid-escape; ChainRunner persisted it
// verbatim; every later request replayed it and the provider rejected the
// whole request with HTTP 400 invalid_request_error "invalid tool call
// arguments". Permanent brick — three failed resumes, manual compaction
// sweep to recover.
//
// Three coverage groups:
//   1. SanitizeToolCallArgsString / SanitizeToolCallArgs — the pure seam
//      (valid object passes byte-identical; malformed/non-object/empty
//      becomes "{}" with per-call markers).
//   2. PromptAssembler::Build replay repair — a poisoned assistant turn
//      heals on the wire: assembled request carries "{}" for the bad call,
//      clean calls pass through untouched.
//   3. CompactionService::CompactNow keep_recent override — 0 sweeps
//      everything (the poisoned-tail tool), default keeps the last 10.
#include "animus_kernel/ToolCallSanitizer.h"
#include "animus_kernel/PromptAssembler.h"
#include "animus_kernel/CompactionService.h"
#include "animus_kernel/Session.h"
#include "animus_kernel/SessionManager.h"
#include "animus_kernel/DefaultSessionRouter.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/SqliteSessionStore.h"
#include "animus_kernel/llm/LLMTypes.h"

#include <json/json.h>
#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace animus::kernel;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

Json::Value Parse(const std::string& s) {
    Json::Value v;
    std::istringstream ss(s);
    Json::CharReaderBuilder rb;
    std::string errs;
    Json::parseFromStream(rb, ss, &v, &errs);
    return v;
}

// The incident's payload shape: escaped C++ inside a JSON string, truncated
// before the string closes — exactly what the model emitted at 14:36/16:24.
const std::string kPoison =
    R"poison({"action":"edit","new_text":"        outInboxId = GetString(config, \"inbox_id\")poison";
const std::string kCleanObject =
    R"clean({"action":"read","path":"/etc/hostname"})clean";

// ---------------------------------------------------------------------------
// Group 1: the pure seam
// ---------------------------------------------------------------------------

void TestValidObjectPassesByteIdentical() {
    ToolCallSanitizeStats st;
    const std::string before = kCleanObject;
    std::string args = kCleanObject;
    const bool replaced = SanitizeToolCallArgsString(args, 0, st);
    Assert(!replaced, "valid object must not be replaced");
    Assert(args == before, "valid object args must be byte-identical");
    Assert(st.sanitized_count == 0, "no sanitize count for valid object");
    Assert(st.notes.empty(), "no notes for valid object");
}

void TestMalformedReplacedWithMarkers() {
    ToolCallSanitizeStats st;
    std::string args = kPoison;
    const std::size_t originalLen = args.size();
    const bool replaced = SanitizeToolCallArgsString(args, 2, st);
    Assert(replaced, "malformed arguments must be replaced");
    Assert(args == "{}", "malformed arguments become {}");
    Assert(st.sanitized_count == 1, "sanitized_count == 1");
    Assert(st.markers["calls"].size() == 1, "one marker recorded");
    Assert(st.markers["calls"][0]["index"].asInt() == 2,
           "marker carries the call index");
    Assert(st.markers["calls"][0]["reason"].asString() ==
               "invalid_json_or_non_object",
           "marker reason");
    Assert(st.markers["calls"][0]["original_len"].asUInt64() == originalLen,
           "marker records original length, not payload");
    Assert(!st.notes.empty(), "a WARN note is produced");
}

void TestNonObjectAndEmptyReplaced() {
    {
        ToolCallSanitizeStats st;
        std::string args = "[1,2,3]";  // valid JSON, wrong shape
        Assert(SanitizeToolCallArgsString(args, 0, st),
               "non-object JSON is replaced");
        Assert(args == "{}", "non-object becomes {}");
    }
    {
        ToolCallSanitizeStats st;
        std::string args;  // empty
        Assert(SanitizeToolCallArgsString(args, 0, st), "empty is replaced");
        Assert(args == "{}", "empty becomes {}");
    }
    {
        ToolCallSanitizeStats st;
        std::string args = "null";  // valid JSON literal, not an object
        Assert(SanitizeToolCallArgsString(args, 0, st), "null is replaced");
        Assert(args == "{}", "null becomes {}");
    }
}

void TestTemplateOverKernelToolCalls() {
    std::vector<ToolCall> calls;
    calls.push_back({"call_a", "file", kCleanObject});
    calls.push_back({"call_b", "file", kPoison});
    calls.push_back({"call_c", "shell_exec", kCleanObject});
    calls.push_back({"call_d", "edit", "{\"action\":\"edit\",\"new_text\":\"x"});

    ToolCallSanitizeStats st;
    const bool any = SanitizeToolCallArgs(calls, st);
    Assert(any, "vector with poison reports sanitization");
    Assert(calls[0].arguments == kCleanObject, "clean call 0 untouched");
    Assert(calls[1].arguments == "{}", "poisoned call 1 replaced");
    Assert(calls[2].arguments == kCleanObject, "clean call 2 untouched");
    Assert(calls[3].arguments == "{}", "poisoned call 3 replaced");
    Assert(st.sanitized_count == 2, "two calls sanitized");
    Assert(st.markers["calls"].size() == 2, "two markers");
    Assert(st.markers["calls"][0]["index"].asInt() == 1, "first marker idx 1");
    Assert(st.markers["calls"][1]["index"].asInt() == 3, "second marker idx 3");
}

void TestTemplateOverLLMToolCalls() {
    std::vector<llm::LLMToolCall> calls;
    calls.push_back({"call_x", "file", kPoison});
    ToolCallSanitizeStats st;
    const bool any = SanitizeToolCallArgs(calls, st);
    Assert(any, "LLMToolCall shape is served by the template");
    Assert(calls[0].arguments == "{}", "LLMToolCall poison replaced");
}

void TestMetadataMerge() {
    ToolCallSanitizeStats st;
    std::string args = kPoison;
    SanitizeToolCallArgsString(args, 0, st);

    // Empty metadata -> valid JSON containing the marker block.
    const std::string empty = MergeSanitizeMarkersIntoMetadata("", st);
    Json::Value v = Parse(empty);
    Assert(v.isObject(), "merged metadata parses as object");
    Assert(v["sanitized_tool_args"]["calls"].size() == 1,
           "marker block present in merged metadata");

    // Existing metadata keys are preserved.
    const std::string existing =
        MergeSanitizeMarkersIntoMetadata(R"({"author":"devex"})", st);
    Json::Value v2 = Parse(existing);
    Assert(v2["author"].asString() == "devex", "existing metadata preserved");
    Assert(v2["sanitized_tool_args"]["calls"].size() == 1,
           "marker block appended");

    // Garbage metadata falls back to input unchanged — metadata must never
    // become its own poison vector.
    const std::string garbage = "not json {";
    Assert(MergeSanitizeMarkersIntoMetadata(garbage, st) == garbage,
           "garbage metadata returned unchanged");
}

// ---------------------------------------------------------------------------
// Group 2: replay repair (PromptAssembler::Build)
// ---------------------------------------------------------------------------

void TestPromptAssemblerRepairsPoisonedTurn() {
    std::vector<SessionTurn> turns;
    SessionTurn user;
    user.role = "user";
    user.content = "read the config for me";
    user.unix_ms = 1000;
    turns.push_back(user);

    SessionTurn poisoned;  // the exact shape persisted by the incident
    poisoned.role = "assistant";
    poisoned.content = "";
    poisoned.unix_ms = 1001;
    poisoned.tool_calls.push_back({"call_1", "file", kPoison});
    poisoned.tool_calls.push_back({"call_2", "file", kCleanObject});
    turns.push_back(poisoned);

    PromptAssembler assembler;
    const PromptAssemblyResult result = assembler.Build(
        turns, nullptr, "next user message", "system prompt", "test-model",
        200000);

    const llm::LLMMessage* assistantMsg = nullptr;
    for (const auto& msg : result.request.messages) {
        if (msg.role == "assistant" && !msg.tool_calls.empty()) {
            assistantMsg = &msg;
            break;
        }
    }
    Assert(assistantMsg != nullptr,
           "assistant tool-call turn is rendered into the request");
    if (assistantMsg) {
        Assert(assistantMsg->tool_calls.size() == 2,
               "both tool calls rendered");
        Assert(assistantMsg->tool_calls[0].arguments == "{}",
               "poisoned arguments repaired to {} on the wire");
        Assert(assistantMsg->tool_calls[1].arguments == kCleanObject,
               "clean arguments pass through byte-identical");
    }
}

// ---------------------------------------------------------------------------
// Group 3: compaction keep_recent override
// ---------------------------------------------------------------------------

SessionId MakeSessionWithTurns(SessionManager& manager, const std::string& key,
                               int turnCount) {
    auto session =
        manager.GetOrCreate(SessionKey{"test", key, ""});
    const SessionId sid = session->Id();
    for (int i = 0; i < turnCount; ++i) {
        SessionTurn t;
        t.role = (i % 2 == 0) ? "user" : "assistant";
        t.content = "turn " + std::to_string(i);
        t.unix_ms = static_cast<std::uint64_t>(1000 + i);
        session->AddTurn(std::move(t));
    }
    manager.FlushSession(sid);
    return sid;
}

void TestCompactKeepRecentOverride() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto dbPath = fs::temp_directory_path() /
                        ("animus_307_compact_" + std::to_string(::getpid()) +
                         ".db");
    fs::remove(dbPath, ec);

    {
        SqliteDataStore dataStore(dbPath.string());
        SessionManager manager(std::make_unique<SqliteSessionStore>(&dataStore),
                               std::make_unique<DefaultSessionRouter>());
        CompactionService::CompactionConfig cfg;
        CompactionService svc(&dataStore, &manager, cfg);

        const std::string suffix = std::to_string(::getpid());

        // Override 0: compacts EVERYTHING — the poisoned-tail operator move.
        const SessionId sweepAll =
            MakeSessionWithTurns(manager, "307_sweep:" + suffix, 14);
        const CompactionResult r0 =
            svc.CompactNow(sweepAll, "", "", "", 128000, nullptr, 0);
        Assert(r0.success, "sweep-all compaction succeeds");
        Assert(r0.turns_compacted == 14,
               "keep_recent=0 compacts all 14 turns");
        Assert(r0.entry.message_id > 0, "entry records the last swept turn");

        // Default (no override): keeps the last 10, compacts the first 4.
        const SessionId defaultKeep =
            MakeSessionWithTurns(manager, "307_default:" + suffix, 14);
        const CompactionResult rd =
            svc.CompactNow(defaultKeep, "", "", "", 128000);
        Assert(rd.success, "default compaction succeeds");
        Assert(rd.turns_compacted == 4,
               "default keepRecent=10 compacts only the first 4 of 14");
    }

    fs::remove(dbPath, ec);
}

}  // namespace

int main() {
    std::cout << "[ToolCallSanitizerTests] group 1: pure seam\n";
    TestValidObjectPassesByteIdentical();
    TestMalformedReplacedWithMarkers();
    TestNonObjectAndEmptyReplaced();
    TestTemplateOverKernelToolCalls();
    TestTemplateOverLLMToolCalls();
    TestMetadataMerge();

    std::cout << "[ToolCallSanitizerTests] group 2: replay repair\n";
    TestPromptAssemblerRepairsPoisonedTurn();

    std::cout << "[ToolCallSanitizerTests] group 3: keep_recent override\n";
    TestCompactKeepRecentOverride();

    if (g_failures == 0) {
        std::cout << "[ToolCallSanitizerTests] ALL PASS\n";
    } else {
        std::cout << "[ToolCallSanitizerTests] " << g_failures
                  << " FAILURE(S)\n";
    }
    return g_failures == 0 ? 0 : 1;
}

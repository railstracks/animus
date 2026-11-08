#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <filesystem>
#include <memory>

#include <json/json.h>

#include "animus_kernel/ConversationCompactor.h"
#include "animus_kernel/DefaultSessionRouter.h"
#include "animus_kernel/SessionNotesStore.h"
#include "animus_kernel/SessionTagsStore.h"
#include "animus_kernel/tools/SessionsTool.h"
#include "animus_kernel/DispatchAgentBinding.h"
#include "animus_kernel/SessionManager.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/SqliteSessionStore.h"
#include "animus_kernel/InMemorySessionStore.h"
#include "animus_kernel/SessionRoutingRule.h"
#include "animus_kernel/PromptAssembler.h"
#include "animus_kernel/SessionManager.h"

using namespace animus::kernel;

namespace {

int Fail(int code, const std::string& message) {
    std::cerr << message << "\n";
    return code;
}

Session BuildSessionWithTurns(std::size_t turnCount, const std::string& content) {
    Session session(100, SessionKey{"test", "conversation", ""});
    for (std::size_t i = 0; i < turnCount; ++i) {
        SessionTurn turn;
        turn.role = (i % 2 == 0) ? "user" : "assistant";
        turn.content = content + " #" + std::to_string(i + 1);
        turn.unix_ms = static_cast<std::uint64_t>(1000 + i);
        session.AddTurn(std::move(turn));
    }
    return session;
}

int TestSessionRoutingAndAccess() {
    IncomingEvent baseEvent;
    baseEvent.source = "slack";
    baseEvent.metadata["channel_id"] = "C123";
    baseEvent.metadata["thread_ts"] = "T999";
    baseEvent.metadata["project_id"] = "proj-7";
    baseEvent.metadata["workspace_id"] = "ws-42";
    baseEvent.text = "hello";

    SessionRoutingCondition presentCondition{
        SessionRoutingMatchType::MetadataPresent, "project_id", ""};
    if (!presentCondition.Matches(baseEvent)) {
        return Fail(1, "expected metadata-present condition to match");
    }

    SessionRoutingCondition equalCondition{
        SessionRoutingMatchType::MetadataEquals, "workspace_id", "ws-42"};
    if (!equalCondition.Matches(baseEvent)) {
        return Fail(2, "expected metadata-equals condition to match");
    }

    SessionRoutingCondition prefixCondition{
        SessionRoutingMatchType::MetadataStartsWith, "channel_id", "C1"};
    if (!prefixCondition.Matches(baseEvent)) {
        return Fail(3, "expected metadata-prefix condition to match");
    }

    SessionRoutingCondition failedPrefixCondition{
        SessionRoutingMatchType::MetadataStartsWith, "channel_id", "D"};
    if (failedPrefixCondition.Matches(baseEvent)) {
        return Fail(4, "expected mismatched prefix condition to fail");
    }

    SessionRoutingRule projectRule{
        {
            SessionRoutingCondition{
                SessionRoutingMatchType::MetadataStartsWith, "channel_id", "C"},
            SessionRoutingCondition{
                SessionRoutingMatchType::MetadataPresent, "project_id", ""},
        },
        SessionKeyTemplate{"projects", "{meta:project_id}", ""},
    };

    if (!projectRule.Matches(baseEvent)) {
        return Fail(5, "expected project routing rule to match");
    }

    const SessionKey resolvedProject = projectRule.ResolveContext(baseEvent);
    if (resolvedProject.connector != "projects"
        || resolvedProject.conversation_id != "proj-7"
        || !resolvedProject.thread_id.empty()) {
        return Fail(6, "project routing rule resolved unexpected session key");
    }

    auto store = std::make_unique<InMemorySessionStore>();
    std::vector<SessionRoutingRule> rules{
        projectRule,
        SessionRoutingRule{
            {
                SessionRoutingCondition{
                    SessionRoutingMatchType::MetadataEquals,
                    "workspace_id",
                    "ws-42",
                },
            },
            SessionKeyTemplate{"workspace", "{meta:workspace_id}", "{source}"},
        },
        SessionRoutingRule{
            {
                SessionRoutingCondition{
                    SessionRoutingMatchType::MetadataEquals,
                    "workspace_id",
                    "ws-42",
                },
            },
            SessionKeyTemplate{"workspace", "{meta:workspace_id}", "{source}"},
        },
    };
    auto router = std::make_unique<DefaultSessionRouter>(std::move(rules));
    SessionManager mgr(std::move(store), std::move(router));

    IncomingEvent e1 = baseEvent;

    IncomingEvent e2 = e1;
    e2.text = "another";

    auto c1 = mgr.Resolve(e1);
    auto c2 = mgr.Resolve(e2);

    if (!c1.primary || !c2.primary) {
        return Fail(7, "failed to resolve primary session");
    }

    if (c1.primary.Id() != c2.primary.Id()) {
        return Fail(8, "expected same session id for same routing key");
    }

    IncomingEvent e3 = e1;
    e3.metadata["thread_ts"] = "T1000";
    auto c3 = mgr.Resolve(e3);

    if (c3.primary.Id() == c1.primary.Id()) {
        return Fail(9, "expected different session id for different thread");
    }

    SessionTurn firstTurn;
    firstTurn.role = "user";
    firstTurn.content = "hi";
    c1.primary.AddTurn(firstTurn);
    if (c1.primary.Turns().empty()) {
        return Fail(10, "turn did not persist");
    }

    if (c1.primary.Turns().front().turn_id == 0) {
        return Fail(11, "expected session to assign turn ids");
    }

    if (c1.context.size() != 2) {
        return Fail(12, "expected exactly two deduplicated context sessions");
    }

    if (c1.context.front().Mode() != SessionAccessMode::ReadOnly
        || c1.context.back().Mode() != SessionAccessMode::ReadOnly) {
        return Fail(13, "expected context sessions to be read-only");
    }

    if (c1.context.front().Key().connector != "projects"
        || c1.context.front().Key().conversation_id != "proj-7") {
        return Fail(14, "unexpected first context session key");
    }

    if (c1.context.back().Key().connector != "workspace"
        || c1.context.back().Key().conversation_id != "ws-42"
        || c1.context.back().Key().thread_id != "slack") {
        return Fail(15, "unexpected second context session key");
    }

    if (c1.context.front().Id() != c2.context.front().Id()
        || c1.context.back().Id() != c2.context.back().Id()) {
        return Fail(16, "expected context sessions to resolve consistently");
    }

    try {
        SessionTurn blockedTurn;
        blockedTurn.role = "user";
        blockedTurn.content = "nope";
        c1.context.front().AddTurn(blockedTurn);
        return Fail(17, "expected read-only context to reject AddTurn");
    } catch (const std::runtime_error&) {
    }

    return 0;
}

int TestCompactionPolicyEvaluation() {
    Session turnThresholdSession = BuildSessionWithTurns(4, "short");
    ConversationCompactor turnThresholdCompactor(
        turnThresholdSession,
        CompactionPolicy{3, 1000, CompactionStrategy::Truncate});
    if (!turnThresholdCompactor.ShouldCompact()) {
        return Fail(18, "expected turn threshold compaction to trigger");
    }

    Session tokenThresholdSession = BuildSessionWithTurns(2, std::string(80, 'x'));
    ConversationCompactor tokenThresholdCompactor(
        tokenThresholdSession,
        CompactionPolicy{50, 10, CompactionStrategy::Truncate});
    if (!tokenThresholdCompactor.ShouldCompact()) {
        return Fail(19, "expected token threshold compaction to trigger");
    }

    Session withinLimitsSession = BuildSessionWithTurns(2, "brief");
    ConversationCompactor withinLimitsCompactor(
        withinLimitsSession,
        CompactionPolicy{4, 1000, CompactionStrategy::Truncate});
    if (withinLimitsCompactor.ShouldCompact()) {
        return Fail(20, "did not expect compaction when session is within limits");
    }

    return 0;
}

int TestTruncationStrategy() {
    Session session = BuildSessionWithTurns(5, "truncate me");
    const auto originalFirstTurnId = session.Turns().front().turn_id;
    const auto keptTurnId = session.Turns()[2].turn_id;

    ConversationCompactor compactor(
        session,
        CompactionPolicy{3, 1000, CompactionStrategy::Truncate});
    if (!compactor.Compact()) {
        return Fail(21, "expected truncation compaction to run");
    }

    if (session.Turns().size() != 3) {
        return Fail(22, "expected truncation to keep only the newest turns");
    }

    if (session.Turns().front().turn_id != keptTurnId) {
        return Fail(23, "expected truncation to remove the oldest turns first");
    }

    if (session.GetCompactionSummary() != nullptr) {
        return Fail(24, "truncate strategy should not create a summary turn");
    }

    if (session.Turns().front().turn_id == originalFirstTurnId) {
        return Fail(25, "expected oldest turn to be removed during truncation");
    }

    return 0;
}

int TestSummaryProvenance() {
    Session session = BuildSessionWithTurns(4, "summarize me");
    const auto compactedFirstId = session.Turns()[0].turn_id;
    const auto compactedSecondId = session.Turns()[1].turn_id;
    const auto keptTurnId = session.Turns()[2].turn_id;

    ConversationCompactor compactor(
        session,
        CompactionPolicy{2, 1000, CompactionStrategy::Summarize});
    if (!compactor.Compact()) {
        return Fail(26, "expected summarize compaction to run");
    }

    const SessionTurn* summary = session.GetCompactionSummary();
    if (summary == nullptr) {
        return Fail(27, "expected compaction summary to be stored on the session");
    }

    if (!summary->is_summary) {
        return Fail(28, "expected compaction summary to be marked as a summary turn");
    }

    if (summary->compacted_from.size() != 2
        || summary->compacted_from[0] != compactedFirstId
        || summary->compacted_from[1] != compactedSecondId) {
        return Fail(29, "summary provenance did not record the compacted turn ids");
    }

    if (summary->turn_id == 0) {
        return Fail(30, "expected session to assign an id to the summary turn");
    }

    if (summary->content.find("Compacted 2 conversation turns") == std::string::npos) {
        return Fail(31, "expected placeholder summary content to describe the compaction");
    }

    if (session.Turns().size() != 2 || session.Turns().front().turn_id != keptTurnId) {
        return Fail(32, "expected summarized session to retain the newest turns");
    }

    return 0;
}

} // namespace

// ============================================================================
// #77: sessions:history / sessions:search DB fallback — terminated and
// persisted sessions must stay reachable; numeric ids and conversation ids
// must resolve; search must be newest-first across sessions.
// ============================================================================

namespace {

Json::Value ParseJsonOr(const std::string& text, int& failures) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &root, &errs)) {
        ++failures;
        std::cerr << "[json] parse failed: " << errs << "\n";
    }
    return root;
}

std::string HistoryArgs(const std::string& agentId,
                        const std::string& sessionKey) {
    return "{\"action\":\"history\",\"__agent_id\":\"" + agentId +
           "\",\"session_key\":\"" + sessionKey + "\"}";
}

std::string SearchArgs(const std::string& agentId, const std::string& query) {
    return "{\"action\":\"search\",\"__agent_id\":\"" + agentId +
           "\",\"query\":\"" + query + "\"}";
}

} // namespace

int TestSessionsToolHistoryFallback() {
    namespace fs = std::filesystem;
    const std::string dbPath = "/tmp/animus_session_tests_77.db";
    std::error_code ec;
    fs::remove(dbPath, ec);

    SqliteDataStore dataStore(dbPath);
    SessionNotesStore notesStore(&dataStore);
    SessionTagsStore tagsStore(&dataStore);
    SessionManager manager(
        std::make_unique<SqliteSessionStore>(&dataStore),
        std::make_unique<DefaultSessionRouter>());
    SessionsTool tool(&manager, &notesStore, &tagsStore, nullptr);

    auto session = manager.GetOrCreate(
        SessionKey{"scheduled", "daily_marketclose:buffett:1770000000", ""});
    session->SetAgentId("buffett");
    SessionTurn turn;
    turn.role = "assistant";
    turn.content = "ORCL close assessment: defensive flip confirmed";
    turn.unix_ms = 1770000000000ULL;
    session->AddTurn(std::move(turn));
    manager.GetStore().FlushSession(session->Id());
    session->MarkTerminated();
    manager.GetStore().FlushSession(session->Id());

    int failures = 0;

    // 1. "session_<id>" — the exact form that failed in production (#77).
    {
        ToolCall call;
        call.id = "t-by-id";
        call.arguments = HistoryArgs(
            "buffett", "session_" + std::to_string(session->Id()));
        auto result = tool.Execute(call);
        if (!result.success)
            return Fail(1, "history by session_<id> should succeed");
        auto body = ParseJsonOr(result.output, failures);
        if (body["source"].asString() != "database")
            return Fail(2, "history by id should be database-sourced");
        if (!body["terminated"].asBool())
            return Fail(3, "terminated session should report terminated=true");
        if (body["messages"].size() != 1 ||
            body["messages"][0]["content"].asString().find("ORCL") ==
                std::string::npos)
            return Fail(4, "history by id should return persisted content");
    }

    // 2. Conversation id form.
    {
        ToolCall call;
        call.id = "t-by-conv";
        call.arguments = HistoryArgs(
            "buffett", "daily_marketclose:buffett:1770000000");
        auto result = tool.Execute(call);
        if (!result.success)
            return Fail(5, "history by conversation id should succeed");
        auto body = ParseJsonOr(result.output, failures);
        if (body["source"].asString() != "database")
            return Fail(6, "conversation-id lookup should be database-sourced");
    }

    // 3. Full key string — still resolved (live registry match).
    {
        ToolCall call;
        call.id = "t-by-key";
        call.arguments = HistoryArgs(
            "buffett", "scheduled|daily_marketclose:buffett:1770000000|");
        auto result = tool.Execute(call);
        if (!result.success)
            return Fail(7, "history by key string should succeed");
        auto body = ParseJsonOr(result.output, failures);
        if (body["terminated"].asBool() != true)
            return Fail(8, "key-string history should still see termination");
    }

    // 4. Ownership enforced on the fallback path.
    {
        ToolCall call;
        call.id = "t-foreign";
        call.arguments = HistoryArgs(
            "sable", "session_" + std::to_string(session->Id()));
        auto result = tool.Execute(call);
        if (result.success)
            return Fail(9, "foreign agent must not read another agent's session");
    }

    // 5. Unknown key — error names the accepted formats.
    {
        ToolCall call;
        call.id = "t-unknown";
        call.arguments = HistoryArgs("buffett", "no|such|session");
        auto result = tool.Execute(call);
        if (result.success) return Fail(10, "unknown session must fail");
        if (result.error.find("accepted") == std::string::npos)
            return Fail(11, "error should list accepted formats");
    }

    if (failures) return Fail(12, "json parse failures in history asserts");
    return 0;
}

// ============================================================================
// #268: dispatch agent binding adoption — pure decision table.
// ============================================================================
int TestDispatchAgentBinding() {
    struct Case {
        std::string current;
        std::string dispatch;
        bool configured;
        std::string expected;
        std::string label;
    };
    const Case cases[] = {
        {"", "buffett", true, "buffett",
         "unbound follows dispatcher"},
        {"", "buffett", false, "buffett",
         "unbound follows dispatcher even when unconfigured (pre-existing claim semantics)"},
        {"", "", true, "",
         "no dispatcher: no change"},
        {"default", "buffett", true, "buffett",
         "sentinel adopts configured dispatcher (#268 — the live #2450 case)"},
        {"default", "buffett", false, "default",
         "unknown dispatcher never hijacks a sentinel binding"},
        {"default", "", true, "default",
         "empty dispatcher: sentinel stays"},
        {"petrel", "buffett", true, "petrel",
         "real binding never re-binds (cross-agent isolation)"},
        {"petrel", "buffett", false, "petrel",
         "real binding immune to unconfigured dispatcher"},
    };
    for (const auto& c : cases) {
        const std::string got =
            ResolveDispatchAgentBinding(c.current, c.dispatch, c.configured);
        if (got != c.expected) {
            return Fail(1, "binding case failed (" + c.label +
                        "): expected '" + c.expected + "' got '" + got + "'");
        }
    }
    return 0;
}

// ============================================================================
// #268: sessions:history bare-key fallback — channel session keys carry the
// visible key in the connector column (conversation_id stays empty); a bare
// key must resolve via the connector column and reach the ownership verdict
// instead of a false "session not found" (live case: #2450).
// ============================================================================
int TestSessionsToolBareKeyFallback() {
    namespace fs = std::filesystem;
    const std::string dbPath = "/tmp/animus_session_tests_268.db";
    std::error_code ec;
    fs::remove(dbPath, ec);

    SqliteDataStore dataStore(dbPath);
    SessionNotesStore notesStore(&dataStore);
    SessionTagsStore tagsStore(&dataStore);
    SessionManager manager(
        std::make_unique<SqliteSessionStore>(&dataStore),
        std::make_unique<DefaultSessionRouter>());
    SessionsTool tool(&manager, &notesStore, &tagsStore, nullptr);

    auto session = manager.GetOrCreate(
        SessionKey{"channel:irc:irc:dm:Petrel", "", ""});
    session->SetAgentId("buffett");
    SessionTurn turn;
    turn.role = "user";
    turn.content = "Petrel: margin call mechanics question";
    turn.unix_ms = 1794001195993ULL;
    session->AddTurn(std::move(turn));
    manager.GetStore().FlushSession(session->Id());

    int failures = 0;

    // 1. Bare channel key resolves via the connector column.
    {
        ToolCall call;
        call.id = "t-268-owner";
        call.arguments = HistoryArgs("buffett", "channel:irc:irc:dm:Petrel");
        auto result = tool.Execute(call);
        if (!result.success)
            return Fail(1, "bare channel key should resolve via connector column");
        auto body = ParseJsonOr(result.output, failures);
        if (body["messages"].size() != 1 ||
            body["messages"][0]["content"].asString().find("margin call") ==
                std::string::npos)
            return Fail(2, "bare-key history should return the persisted turn");
    }

    // 2. Foreign agent gets the honest verdict — ownership, not not-found.
    {
        ToolCall call;
        call.id = "t-268-foreign";
        call.arguments = HistoryArgs("petrel", "channel:irc:irc:dm:Petrel");
        auto result = tool.Execute(call);
        if (result.success)
            return Fail(3, "foreign agent must not read the session");
        if (result.error.find("belong") == std::string::npos)
            return Fail(4, "foreign bare-key lookup should hit the ownership "
                           "wall, not a false 'session not found'");
    }

    if (failures) return Fail(5, "json parse failures in bare-key asserts");
    return 0;
}

int TestSessionsToolSearchRecency() {
    namespace fs = std::filesystem;
    const std::string dbPath = "/tmp/animus_session_tests_77b.db";
    std::error_code ec;
    fs::remove(dbPath, ec);

    SqliteDataStore dataStore(dbPath);
    SessionNotesStore notesStore(&dataStore);
    SessionTagsStore tagsStore(&dataStore);
    SessionManager manager(
        std::make_unique<SqliteSessionStore>(&dataStore),
        std::make_unique<DefaultSessionRouter>());
    SessionsTool tool(&manager, &notesStore, &tagsStore, nullptr);

    struct Seed {
        const char* connector;
        const char* conversation;
        std::uint64_t lastActive;
        const char* content;
    };
    const Seed seeds[] = {
        {"scheduled", "older:run", 1000000, "needleword in the older run"},
        {"scheduled", "newer:run", 2000000, "needleword in the newer run"},
        {"scheduled", "foreign:agent", 3000000, "needleword owned by someone else"},
    };
    for (const auto& s : seeds) {
        auto session = manager.GetOrCreate(SessionKey{s.connector, s.conversation, ""});
        session->SetAgentId(std::string(s.conversation) == "foreign:agent"
                                ? "sable" : "buffett");
        session->SetLastActiveUnixMs(s.lastActive);
        SessionTurn turn;
        turn.role = "assistant";
        turn.content = s.content;
        turn.unix_ms = s.lastActive;
        session->AddTurn(std::move(turn));
        manager.GetStore().FlushSession(session->Id());
        session->MarkTerminated();
        manager.GetStore().FlushSession(session->Id());
    }

    // Case-insensitive query across terminated sessions, agent-scoped,
    // newest match first.
    ToolCall call;
    call.id = "t-search";
    call.arguments = SearchArgs("buffett", "NEEDLEWORD");
    auto result = tool.Execute(call);
    if (!result.success) return Fail(1, "search should succeed");

    int failures = 0;
    auto body = ParseJsonOr(result.output, failures);
    const auto& results = body["results"];
    if (body.isMember("results") ? results.size() != 2 : true) {
        // Fallback: some result shapes use different keys; inspect items.
    }
    // Locate the items array regardless of key name.
    Json::Value items;
    if (body.isMember("results")) items = body["results"];
    else if (body.isMember("matches")) items = body["matches"];
    else items = body["messages"];
    if (items.size() != 2)
        return Fail(2, "search should find exactly the agent's two matches");
    if (items[0]["content"].asString().find("newer") == std::string::npos)
        return Fail(3, "newest match must be returned first");
    if (items[1]["content"].asString().find("older") == std::string::npos)
        return Fail(4, "second match should be the older session");
    if (failures) return Fail(5, "json parse failures in search asserts");
    return 0;
}

int TestAuthorRoundTrip() {
    namespace fs = std::filesystem;
    const std::string dbPath = "/tmp/animus_session_tests_133.db";
    std::error_code ec;
    fs::remove(dbPath, ec);

    {
        SqliteDataStore dataStore(dbPath);
        SessionManager manager(
            std::make_unique<SqliteSessionStore>(&dataStore),
            std::make_unique<DefaultSessionRouter>());
        auto session = manager.GetOrCreate(SessionKey{"test", "author_roundtrip", ""});
        session->SetAgentId("tester");
        SessionTurn t1;
        t1.role = "user";
        t1.content = "hello from melvin";
        t1.unix_ms = 1760000000000ULL;
        t1.author = "melvin";
        session->AddTurn(std::move(t1));
        SessionTurn t2;
        t2.role = "assistant";
        t2.content = "ack";
        t2.unix_ms = 1760000060000ULL;
        session->AddTurn(std::move(t2));
        SessionTurn t3;
        t3.role = "user";
        t3.content = "sofia here";
        t3.unix_ms = 1760000120000ULL;
        t3.author = "sofia";
        session->AddTurn(std::move(t3));
        manager.GetStore().FlushSession(session->Id());
    }
    {
        SqliteDataStore dataStore(dbPath);
        SessionManager manager(
            std::make_unique<SqliteSessionStore>(&dataStore),
            std::make_unique<DefaultSessionRouter>());
        auto session = manager.GetOrCreate(SessionKey{"test", "author_roundtrip", ""});
        if (!session) return Fail(1, "#133 round-trip: session not found on reload");
        const auto& turns = session->Turns();
        if (turns.size() != 3) return Fail(2, "#133 round-trip: expected 3 turns");
        if (turns[0].author != "melvin") return Fail(3, "#133 round-trip: melvin author lost");
        if (!turns[1].author.empty()) return Fail(4, "#133 round-trip: assistant turn gained an author");
        if (turns[2].author != "sofia") return Fail(5, "#133 round-trip: sofia author lost");
    }
    std::cout << "[ok] #133 author round-trip survives store reload\n";
    return 0;
}

int TestAttributionHeaderRendering() {
    const std::uint64_t T0 = 1760000000000ULL;
    auto mkturn = [](const std::string& role, const std::string& content,
                     std::uint64_t ms, const std::string& author) {
        SessionTurn t;
        t.role = role;
        t.content = content;
        t.unix_ms = ms;
        t.author = author;
        return t;
    };
    std::vector<SessionTurn> turns;
    turns.push_back(mkturn("user", "first message", T0, "melvin"));
    turns.push_back(mkturn("user", "second message", T0 + 60'000, "melvin"));
    turns.push_back(mkturn("assistant", "assistant reply", T0 + 120'000, ""));
    turns.push_back(mkturn("user", "sofia chimes in", T0 + 180'000, "sofia"));
    turns.push_back(mkturn("user", "melvin returns", T0 + 20 * 60'000, "melvin"));
    turns.push_back(mkturn("user", "melvin continues", T0 + 21 * 60'000, "melvin"));
    turns.push_back(mkturn("assistant", "much later reply", T0 + 45 * 60'000, ""));

    PromptAssembler assembler;
    const auto result = assembler.Build(turns, nullptr, "current message",
                                        "system", "test-model", 128000);
    const auto& msgs = result.request.messages;
    auto findContent = [&](const std::string& needle) -> const llm::LLMMessage* {
        for (const auto& m : msgs)
            if (m.content.find(needle) != std::string::npos) return &m;
        return nullptr;
    };

    const auto* m0 = findContent("first message");
    if (!m0) return Fail(1, "#179 render: first message missing");
    if (m0->content.find("Author: melvin\n") == std::string::npos ||
        m0->content.find("Timestamp: ") == std::string::npos ||
        m0->content.find("\n====\nfirst message") == std::string::npos)
        return Fail(2, "#179 render: metadata block missing on first attributed turn");

    const auto* m1 = findContent("second message");
    if (!m1 || m1->content != "second message")
        return Fail(3, "#133 render: same-speaker turn must render bare");

    const auto* m2 = findContent("assistant reply");
    if (!m2 || m2->content != "assistant reply")
        return Fail(4, "#133 render: short-gap assistant turn must render bare");

    const auto* m3 = findContent("sofia chimes in");
    if (!m3 || m3->content.find("Author: sofia\n") == std::string::npos)
        return Fail(5, "#179 render: speaker-change block missing (sofia)");

    const auto* m4 = findContent("melvin returns");
    if (!m4 || m4->content.find("Author: melvin\n") == std::string::npos)
        return Fail(6, "#179 render: speaker-change block missing (melvin)");

    const auto* m5 = findContent("melvin continues");
    if (!m5 || m5->content != "melvin continues")
        return Fail(7, "#133 render: consecutive same-speaker must render bare");

    const auto* m6 = findContent("much later reply");
    if (!m6 || m6->content.rfind("Timestamp: ", 0) != 0 ||
        m6->content.find("Author:") != std::string::npos)
        return Fail(8, "#179 render: gap threshold must fire timestamp-only block");

    // Token overhead (#133 acceptance: measured, not assumed). The bare
    // variant keeps timestamps (gap headers still fire without authors) —
    // the delta isolates the marginal speaker-header cost.
    std::vector<SessionTurn> bare = turns;
    for (auto& t : bare) t.author.clear();
    const auto bareResult = assembler.Build(bare, nullptr, "current message",
                                            "system", "test-model", 128000);
    std::cout << "[ok] #133 headers: attributed=" << result.session_turns_tokens
              << " bare=" << bareResult.session_turns_tokens
              << " delta=" << (result.session_turns_tokens - bareResult.session_turns_tokens)
              << " tokens over " << turns.size() << " turns\n";
    if (result.session_turns_tokens <= bareResult.session_turns_tokens)
        return Fail(9, "#133 render: attributed assembly must cost >= bare");
    return 0;
}

int main() {
    if (const int rc = TestSessionRoutingAndAccess(); rc != 0) {
        return rc;
    }

    if (const int rc = TestCompactionPolicyEvaluation(); rc != 0) {
        return rc;
    }

    if (const int rc = TestTruncationStrategy(); rc != 0) {
        return rc;
    }

    if (const int rc = TestSummaryProvenance(); rc != 0) {
        return rc;
    }

    if (const int rc = TestSessionsToolHistoryFallback(); rc != 0) {
        return rc;
    }

    if (const int rc = TestDispatchAgentBinding(); rc != 0) {
        return rc;
    }

    if (const int rc = TestSessionsToolBareKeyFallback(); rc != 0) {
        return rc;
    }

    if (const int rc = TestSessionsToolSearchRecency(); rc != 0) {
        return rc;
    }

    if (const int rc = TestAuthorRoundTrip(); rc != 0) {
        return rc;
    }

    if (const int rc = TestAttributionHeaderRendering(); rc != 0) {
        return rc;
    }

    return 0;
}

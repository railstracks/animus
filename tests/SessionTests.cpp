#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "animus_kernel/ConversationCompactor.h"
#include "animus_kernel/DefaultSessionRouter.h"
#include "animus_kernel/InMemorySessionStore.h"
#include "animus_kernel/SessionRoutingRule.h"
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

    return 0;
}

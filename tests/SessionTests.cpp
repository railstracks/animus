#include <iostream>
#include <stdexcept>

#include "animus_kernel/DefaultSessionRouter.h"
#include "animus_kernel/InMemorySessionStore.h"
#include "animus_kernel/SessionRoutingRule.h"
#include "animus_kernel/SessionManager.h"

using namespace animus::kernel;

int main() {
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
        std::cerr << "expected metadata-present condition to match\n";
        return 1;
    }

    SessionRoutingCondition equalCondition{
        SessionRoutingMatchType::MetadataEquals, "workspace_id", "ws-42"};
    if (!equalCondition.Matches(baseEvent)) {
        std::cerr << "expected metadata-equals condition to match\n";
        return 2;
    }

    SessionRoutingCondition prefixCondition{
        SessionRoutingMatchType::MetadataStartsWith, "channel_id", "C1"};
    if (!prefixCondition.Matches(baseEvent)) {
        std::cerr << "expected metadata-prefix condition to match\n";
        return 3;
    }

    SessionRoutingCondition failedPrefixCondition{
        SessionRoutingMatchType::MetadataStartsWith, "channel_id", "D"};
    if (failedPrefixCondition.Matches(baseEvent)) {
        std::cerr << "expected mismatched prefix condition to fail\n";
        return 4;
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
        std::cerr << "expected project routing rule to match\n";
        return 5;
    }

    const SessionKey resolvedProject = projectRule.ResolveContext(baseEvent);
    if (resolvedProject.connector != "projects"
        || resolvedProject.conversation_id != "proj-7"
        || !resolvedProject.thread_id.empty()) {
        std::cerr << "project routing rule resolved unexpected session key\n";
        return 6;
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
        std::cerr << "failed to resolve primary session";
        return 7;
    }

    // Same key should yield same session id.
    if (c1.primary.Id() != c2.primary.Id()) {
        std::cerr << "expected same session id for same routing key\n";
        return 8;
    }

    IncomingEvent e3 = e1;
    e3.metadata["thread_ts"] = "T1000";
    auto c3 = mgr.Resolve(e3);

    if (c3.primary.Id() == c1.primary.Id()) {
        std::cerr << "expected different session id for different thread\n";
        return 9;
    }

    // Basic session mutation.
    c1.primary.AddTurn(SessionTurn{"user", "hi", 0});
    if (c1.primary.Turns().empty()) {
        std::cerr << "turn did not persist\n";
        return 10;
    }

    if (c1.context.size() != 2) {
        std::cerr << "expected exactly two deduplicated context sessions\n";
        return 11;
    }

    if (c1.context.front().Mode() != SessionAccessMode::ReadOnly
        || c1.context.back().Mode() != SessionAccessMode::ReadOnly) {
        std::cerr << "expected context sessions to be read-only\n";
        return 12;
    }

    if (c1.context.front().Key().connector != "projects"
        || c1.context.front().Key().conversation_id != "proj-7") {
        std::cerr << "unexpected first context session key\n";
        return 13;
    }

    if (c1.context.back().Key().connector != "workspace"
        || c1.context.back().Key().conversation_id != "ws-42"
        || c1.context.back().Key().thread_id != "slack") {
        std::cerr << "unexpected second context session key\n";
        return 14;
    }

    if (c1.context.front().Id() != c2.context.front().Id()
        || c1.context.back().Id() != c2.context.back().Id()) {
        std::cerr << "expected context sessions to resolve consistently\n";
        return 15;
    }

    try {
        c1.context.front().AddTurn(SessionTurn{"user", "nope", 0});
        std::cerr << "expected read-only context to reject AddTurn\n";
        return 16;
    } catch (const std::runtime_error&) {
    }

    return 0;
}

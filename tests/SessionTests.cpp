#include <iostream>
#include <stdexcept>

#include "animus_kernel/DefaultSessionRouter.h"
#include "animus_kernel/InMemorySessionStore.h"
#include "animus_kernel/SessionManager.h"

using namespace animus::kernel;

class TestRouter final : public ISessionRouter {
public:
    SessionRoutingResult Route(const IncomingEvent& event) override {
        DefaultSessionRouter base;
        auto result = base.Route(event);
        SessionKey ctx = result.primary;
        ctx.thread_id = "context";
        result.context.push_back(ctx);
        return result;
    }
};

int main() {
    auto store = std::make_unique<InMemorySessionStore>();
    auto router = std::make_unique<TestRouter>();
    SessionManager mgr(std::move(store), std::move(router));

    IncomingEvent e1;
    e1.source = "slack";
    e1.metadata["channel_id"] = "C123";
    e1.metadata["thread_ts"] = "T999";
    e1.text = "hello";

    IncomingEvent e2 = e1;
    e2.text = "another";

    auto c1 = mgr.Resolve(e1);
    auto c2 = mgr.Resolve(e2);

    if (!c1.primary || !c2.primary) {
        std::cerr << "failed to resolve primary session";
        return 1;
    }

    // Same key should yield same session id.
    if (c1.primary.Id() != c2.primary.Id()) {
        std::cerr << "expected same session id for same routing key\n";
        return 2;
    }

    IncomingEvent e3 = e1;
    e3.metadata["thread_ts"] = "T1000";
    auto c3 = mgr.Resolve(e3);

    if (c3.primary.Id() == c1.primary.Id()) {
        std::cerr << "expected different session id for different thread\n";
        return 3;
    }

    // Basic session mutation.
    c1.primary.AddTurn(SessionTurn{"user", "hi", 0});
    if (c1.primary.Turns().empty()) {
        std::cerr << "turn did not persist\n";
        return 4;
    }

    if (c1.context.empty()) {
        std::cerr << "expected context session\n";
        return 5;
    }

    try {
        c1.context.front().AddTurn(SessionTurn{"user", "nope", 0});
        std::cerr << "expected read-only context to reject AddTurn\n";
        return 6;
    } catch (const std::runtime_error&) {
    }

    return 0;
}

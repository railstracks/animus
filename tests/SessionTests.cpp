#include <cassert>
#include <iostream>

#include "animus_kernel/DefaultSessionRouter.h"
#include "animus_kernel/InMemorySessionStore.h"
#include "animus_kernel/SessionManager.h"

using namespace animus::kernel;

int main() {
    auto store = std::make_unique<InMemorySessionStore>();
    auto router = std::make_unique<DefaultSessionRouter>();
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
    if (c1.primary->Id() != c2.primary->Id()) {
        std::cerr << "expected same session id for same routing key\n";
        return 2;
    }

    IncomingEvent e3 = e1;
    e3.metadata["thread_ts"] = "T1000";
    auto c3 = mgr.Resolve(e3);

    if (c3.primary->Id() == c1.primary->Id()) {
        std::cerr << "expected different session id for different thread\n";
        return 3;
    }

    // Basic session mutation.
    c1.primary->AddTurn(SessionTurn{"user", "hi", 0});
    if (c1.primary->Turns().empty()) {
        std::cerr << "turn did not persist\n";
        return 4;
    }

    return 0;
}

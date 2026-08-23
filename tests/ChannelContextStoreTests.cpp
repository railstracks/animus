#include "animus_kernel/ChannelContextStore.h"
#include "animus_kernel/SqliteDataStore.h"

#include <iostream>
#include <string>
#include <unistd.h>

using namespace animus::kernel;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_channelctx_test_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd >= 0) close(fd);
    return std::string(tmp) + ".db";
}

ChannelArrival MakeArrival(const std::string& sessionKey,
                           const std::string& agentId,
                           const std::string& messageId) {
    ChannelArrival a;
    a.session_key = sessionKey;
    a.agent_id = agentId;
    a.channel_type = "discord";
    a.channel_name = "PERSONAL";
    a.platform_id = "discord:PERSONAL";
    a.message_type = "mention";
    a.author_id = "111222333";
    a.author_handle = "someone";
    a.delivery = "tool";
    a.peer_id = "";
    a.post_id = "456";
    a.source_message_id = messageId;
    return a;
}

int TestAddAndPending() {
    std::cerr << "  [ChannelContext] add + pending list...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ChannelContextStore store(&dataStore);

    auto a1 = store.AddArrival(MakeArrival("channel:chat:discord:1", "agent-a", "m1"));
    auto a2 = store.AddArrival(MakeArrival("channel:chat:discord:1", "agent-a", "m2"));
    Assert(a1.id > 0 && a2.id > a1.id, "ids assigned monotonic");
    Assert(a1.created_at_unix_ms > 0, "timestamp populated");

    auto pending = store.PendingArrivals("channel:chat:discord:1", "agent-a");
    Assert(pending.size() == 2, "two pending");
    Assert(pending[0].source_message_id == "m1" &&
           pending[1].source_message_id == "m2",
           "oldest first");

    // Agent isolation
    auto other = store.PendingArrivals("channel:chat:discord:1", "agent-b");
    Assert(other.empty(), "agent isolation on pending");

    // Round-trip fidelity of all fields
    const auto& r = pending[0];
    Assert(r.channel_type == "discord" && r.channel_name == "PERSONAL" &&
           r.platform_id == "discord:PERSONAL" && r.message_type == "mention" &&
           r.author_id == "111222333" && r.author_handle == "someone" &&
           r.delivery == "tool" && r.post_id == "456",
           "all fields round-trip");
    Assert(!r.consumed, "arrives unconsumed");
    return 0;
}

int TestConsumeLifecycle() {
    std::cerr << "  [ChannelContext] consume lifecycle...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ChannelContextStore store(&dataStore);

    store.AddArrival(MakeArrival("channel:chat:discord:2", "agent-a", "m1"));
    store.AddArrival(MakeArrival("channel:chat:discord:2", "agent-a", "m2"));
    store.AddArrival(MakeArrival("channel:chat:discord:2", "agent-a", "m3"));

    auto latest = store.LatestArrival("channel:chat:discord:2", "agent-a");
    Assert(latest && latest->source_message_id == "m3", "latest is newest");

    int marked = store.MarkAllConsumed("channel:chat:discord:2", "agent-a");
    Assert(marked == 3, "all marked consumed");

    Assert(store.PendingArrivals("channel:chat:discord:2", "agent-a").empty(),
           "pending empty after consume");

    // Latest survives consumption — the default reply target for #16
    latest = store.LatestArrival("channel:chat:discord:2", "agent-a");
    Assert(latest && latest->source_message_id == "m3",
           "latest persists after consume (reply-target stability)");

    // Mixed state: consumed arrivals stay consumed
    store.AddArrival(MakeArrival("channel:chat:discord:2", "agent-a", "m4"));
    Assert(store.PendingArrivals("channel:chat:discord:2", "agent-a").size() == 1,
           "only new arrival pending");
    return 0;
}

int TestPrune() {
    std::cerr << "  [ChannelContext] prune...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ChannelContextStore store(&dataStore);

    for (int i = 0; i < 30; ++i) {
        store.AddArrival(MakeArrival("channel:chat:discord:3", "agent-a",
                                     "m" + std::to_string(i)));
    }
    const int removed = store.Prune("channel:chat:discord:3", "agent-a", 20);
    Assert(removed == 10, "pruned to keepLast (removed 10)");
    auto recent = store.RecentArrivals("channel:chat:discord:3", "agent-a", 100);
    Assert(recent.size() == 20, "20 remain");
    Assert(recent[0].source_message_id == "m29", "newest kept");
    return 0;
}

int TestSeenUris() {
    std::cerr << "  [ChannelContext] seen-uri watermark...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ChannelContextStore store(&dataStore);

    Assert(!store.HasSeenUri("channel:thread:bluesky:x", "agent-a", "at://foo"),
           "unseen initially");
    Assert(store.AddSeenUri("channel:thread:bluesky:x", "agent-a", "at://foo"),
           "first add returns true");
    Assert(store.HasSeenUri("channel:thread:bluesky:x", "agent-a", "at://foo"),
           "seen after add");
    Assert(!store.AddSeenUri("channel:thread:bluesky:x", "agent-a", "at://foo"),
           "duplicate add returns false");
    Assert(!store.HasSeenUri("channel:thread:bluesky:x", "agent-b", "at://foo"),
           "agent isolation on seen-set");
    return 0;
}

int TestReopenPersistence() {
    std::cerr << "  [ChannelContext] reopen persistence (schema idempotent)...\n";
    const auto dbPath = MakeTempDbPath();
    {
        SqliteDataStore dataStore(dbPath);
        ChannelContextStore store(&dataStore);
        store.AddArrival(MakeArrival("channel:chat:discord:4", "agent-a", "m1"));
        store.AddSeenUri("channel:chat:discord:4", "agent-a", "at://bar");
    }
    {
        SqliteDataStore dataStore(dbPath);
        ChannelContextStore store(&dataStore);  // EnsureSchema must not throw
        Assert(store.PendingArrivals("channel:chat:discord:4", "agent-a").size() == 1,
               "arrival survives reopen");
        Assert(store.HasSeenUri("channel:chat:discord:4", "agent-a", "at://bar"),
               "seen-uri survives reopen");
    }
    return 0;
}

} // namespace

int main() {
    std::cerr << "[ChannelContextStoreTests]\n";
    TestAddAndPending();
    TestConsumeLifecycle();
    TestPrune();
    TestSeenUris();
    TestReopenPersistence();
    if (g_failures == 0) {
        std::cerr << "  ALL PASSED\n";
        return 0;
    }
    std::cerr << "  " << g_failures << " FAILURES\n";
    return 1;
}

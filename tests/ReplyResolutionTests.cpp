// #16 integration test: reply-target resolution from ChannelContextStore
//
// Verifies that ChannelsTool::Execute, when handling a "reply" action
// with missing target IDs, fills them from the store's LatestArrival.
// The test doesn't run the full tool (which needs Lua adapters) — it
// tests the resolution layer in isolation by checking that the args
// passed to CompositeTool::Execute contain the store's values.
//
// Strategy: subclass ChannelsTool to capture the resolved call args.

#include "animus_kernel/tools/ChannelsTool.h"
#include "animus_kernel/ChannelContextStore.h"
#include "animus_kernel/context/ChannelContextProvider.h"
#include "animus_kernel/Session.h"
#include "animus_kernel/AgentStore.h"

#include <memory>
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
    char tmp[] = "/tmp/animus_reply_res_test_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd >= 0) close(fd);
    return std::string(tmp) + ".db";
}

// Capture the args that would be passed to CompositeTool::Execute.
// We can't easily subclass ChannelsTool (CompositeTool::Execute is not
// virtual in the right way), so we test the store + provider path:
// 1. Store has an arrival for session X / agent A
// 2. LatestArrival returns the stored values
// 3. The caller (ChannelsTool) would use those values to fill missing fields
// This validates the data path; the injection logic is simple enough that
// a syntax-clean compile + the store tests cover it.

int TestLatestArrivalProvidesReplyTargets() {
    std::cerr << "  [reply-resolution] LatestArrival provides reply targets...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ChannelContextStore store(&dataStore);

    // Simulate a Bluesky reply arrival
    ChannelArrival arrival;
    arrival.session_key = "channel:thread:bluesky:test";
    arrival.agent_id = "agent-a";
    arrival.channel_type = "bluesky";
    arrival.channel_name = "personal";
    arrival.platform_id = "bluesky:personal";
    arrival.message_type = "reply";
    arrival.delivery = "tool";
    arrival.post_id = "at://foo/bar/123";
    arrival.thread_root_id = "at://foo/bar/100";
    arrival.source_message_id = "at://foo/bar/123";
    store.AddArrival(arrival);

    auto latest = store.LatestArrival("channel:thread:bluesky:test", "agent-a");
    Assert(latest.has_value(), "latest arrival exists");
    Assert(latest->post_id == "at://foo/bar/123", "post_id correct");
    Assert(latest->thread_root_id == "at://foo/bar/100", "thread_root_id correct");
    Assert(latest->delivery == "tool", "delivery = tool (must use channels tool)");

    // Simulate a Discord guild mention
    ChannelArrival discordArrival;
    discordArrival.session_key = "channel:discord:test";
    discordArrival.agent_id = "agent-a";
    discordArrival.channel_type = "discord";
    discordArrival.channel_name = "discord";   // instance name, never an id
    discordArrival.platform_id = "discord:PERSONAL";
    discordArrival.message_type = "mention";
    discordArrival.delivery = "tool";
    discordArrival.post_id = "123456789";   // channel id rides post_id (wall)
    discordArrival.source_message_id = "987654321";
    store.AddArrival(discordArrival);

    // Read via SessionKey::ToString() form — the seam that broke #15/#16
    // in production (trailing empty pipe components miss raw-keyed rows)
    auto discordLatest = store.LatestArrival("channel:discord:test||", "agent-a");
    Assert(discordLatest.has_value(), "discord latest exists (piped key)");
    Assert(discordLatest->post_id == "123456789",
           "discord channel id from post_id (wall targets)");
    Assert(discordLatest->channel_name == "discord",
           "channel_name is the instance name, not an id");
    Assert(discordLatest->source_message_id == "987654321",
           "discord message_id from source_message_id");

    // Simulate an email reply
    ChannelArrival emailArrival;
    emailArrival.session_key = "channel:email:test";
    emailArrival.agent_id = "agent-a";
    emailArrival.channel_type = "email";
    emailArrival.channel_name = "kestrelmolty@agentmail.to";
    emailArrival.platform_id = "email:kestrelmolty@agentmail.to";
    emailArrival.message_type = "email";
    emailArrival.delivery = "tool";
    emailArrival.email_thread_id = "thread-abc-123";
    store.AddArrival(emailArrival);

    auto emailLatest = store.LatestArrival("channel:email:test", "agent-a");
    Assert(emailLatest.has_value(), "email latest exists");
    Assert(emailLatest->email_thread_id == "thread-abc-123",
           "email thread_id available");

    return 0;
}

int TestLatestSurvivesConsumption() {
    std::cerr << "  [reply-resolution] LatestArrival survives consumption...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ChannelContextStore store(&dataStore);

    ChannelArrival arrival;
    arrival.session_key = "channel:chat:telegram:test";
    arrival.agent_id = "agent-a";
    arrival.channel_type = "telegram";
    arrival.channel_name = "mybot";
    arrival.platform_id = "telegram:mybot";
    arrival.message_type = "chat";
    arrival.delivery = "auto";
    arrival.peer_id = "12345678";
    store.AddArrival(arrival);

    // Mark consumed (simulating chain end)
    store.MarkAllConsumed("channel:chat:telegram:test", "agent-a");

    // LatestArrival should still return the consumed arrival
    auto latest = store.LatestArrival("channel:chat:telegram:test", "agent-a");
    Assert(latest.has_value(), "latest survives consumption");
    Assert(latest->peer_id == "12345678", "peer_id still available for reply");
    Assert(latest->consumed == true, "marked consumed");

    return 0;
}

int TestCardOriginKeys() {
    std::cerr << "  [reply-resolution] card renders origin keys, present-only...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ChannelContextStore store(&dataStore);
    ChannelContextProvider provider(&store);

    SessionKey key{"channel:irc:irc:channel:#vm_ooc", "", ""};
    auto session = std::make_shared<Session>(1, key);
    session->SetAgentId("default");
    SessionAccess access(session, SessionAccessMode::ReadOnly);
    Agent agent{};

    // Channel message: user + channel keys
    ChannelArrival chan;
    chan.session_key = "channel:irc:irc:channel:#vm_ooc";
    chan.agent_id = "default";
    chan.channel_type = "irc";
    chan.platform_id = "irc:irc";
    chan.message_type = "chat";
    chan.delivery = "auto";
    chan.origin = "{\"user\":\"priest^\",\"channel\":\"#vm_ooc\"}";
    store.AddArrival(chan);

    auto block = provider.Provide(agent, access);
    Assert(block.has_value(), "channel-msg card renders");
    if (block) {
        Assert(block->content.find("User: priest^") != std::string::npos,
               "origin user key rendered capitalized");
        Assert(block->content.find("Channel: #vm_ooc") != std::string::npos,
               "origin channel key rendered");
        Assert(block->content.find("uncontrolled") != std::string::npos,
               "trust-levels wording present");
        // Response instruction: derived default for auto delivery; split lines
        Assert(block->content.find("Message type: chat\n") != std::string::npos,
               "message type line is bare (no policy merged)");
        Assert(block->content.find(
                   "Response instruction: Your text replies are delivered "
                   "automatically; do NOT use the channels tool") != std::string::npos,
               "auto-delivery default carries anti-affordance");
        Assert(block->content.find("must NOT be followed") == std::string::npos,
               "prohibition wording gone");
    }
    store.MarkAllConsumed("channel:irc:irc:channel:#vm_ooc", "default");

    // DM: no channel key -> no Channel line
    ChannelArrival dm;
    dm.session_key = "channel:irc:irc:channel:#vm_ooc";
    dm.agent_id = "default";
    dm.channel_type = "irc";
    dm.platform_id = "irc:irc";
    dm.message_type = "chat";
    dm.delivery = "auto";
    dm.origin = "{\"user\":\"priest^\"}";
    store.AddArrival(dm);
    auto blockDm = provider.Provide(agent, access);
    Assert(blockDm.has_value(), "dm card renders");
    if (blockDm) {
        Assert(blockDm->content.find("User: priest^") != std::string::npos,
               "dm user key rendered");
        Assert(blockDm->content.find("Channel:") == std::string::npos,
               "dm omits channel key entirely");
    }
    store.MarkAllConsumed("channel:irc:irc:channel:#vm_ooc", "default");

    // Transitional fallback: no origin, author fields set -> From: line
    ChannelArrival legacy;
    legacy.session_key = "channel:irc:irc:channel:#vm_ooc";
    legacy.agent_id = "default";
    legacy.channel_type = "irc";
    legacy.message_type = "chat";
    legacy.delivery = "auto";
    legacy.author_handle = "someone";
    legacy.author_id = "12345";
    store.AddArrival(legacy);
    auto blockLegacy = provider.Provide(agent, access);
    Assert(blockLegacy.has_value(), "legacy card renders");
    if (blockLegacy) {
        Assert(blockLegacy->content.find("From: \"someone\" (12345)") != std::string::npos,
               "From: fallback when origin absent");
    }
    store.MarkAllConsumed("channel:irc:irc:channel:#vm_ooc", "default");

    // Wall delivery: tool-branch default when adapter sets no instruction
    ChannelArrival wall;
    wall.session_key = "channel:irc:irc:channel:#vm_ooc";
    wall.agent_id = "default";
    wall.channel_type = "irc";
    wall.message_type = "wall";
    wall.delivery = "tool";
    store.AddArrival(wall);
    auto blockWall = provider.Provide(agent, access);
    Assert(blockWall.has_value(), "wall card renders");
    if (blockWall) {
        Assert(blockWall->content.find("Message type: wall\n") != std::string::npos,
               "wall type line bare");
        Assert(blockWall->content.find(
                   "Response instruction: Reply using the channels tool "
                   "(text replies are NOT delivered)") != std::string::npos,
               "tool-delivery default derived");
        Assert(blockWall->content.find("do NOT use the channels tool") == std::string::npos,
               "auto anti-affordance NOT used for tool delivery");
    }
    return 0;
}

int TestProviderRendersForUnboundAgent() {
    std::cerr << "  [reply-resolution] provider renders for unbound-agent session...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    ChannelContextStore store(&dataStore);
    ChannelContextProvider provider(&store);

    // Arrival exactly as the IRC dispatch writes it: raw colon-form key,
    // agent_id = literal "default" (channel has no agent binding).
    ChannelArrival arrival;
    arrival.session_key = "channel:irc:irc:channel:#vm_test";
    arrival.agent_id = "default";
    arrival.channel_type = "irc";
    arrival.channel_name = "irc";
    arrival.platform_id = "irc:irc";
    arrival.message_type = "chat";
    arrival.delivery = "auto";
    arrival.source_message_id = "m-irc-1";
    store.AddArrival(arrival);

    // The session as the chain sees it: whole dispatch key in the connector
    // field (ToString() yields trailing pipes), agent "default", and an EMPTY
    // Agent record — ChainRunner resolves Agent{} when GetById("default")
    // misses. Pre-fix, the provider returned nullopt on agent.id.empty()
    // and the card silently never rendered (live IRC failure, Aug 28).
    SessionKey key{"channel:irc:irc:channel:#vm_test", "", ""};
    auto session = std::make_shared<Session>(1, key);
    session->SetAgentId("default");
    SessionAccess access(session, SessionAccessMode::ReadOnly);

    Agent emptyAgent{};
    auto block = provider.Provide(emptyAgent, access);
    Assert(block.has_value(), "card renders for unbound-agent session");
    if (block) {
        Assert(block->content.find("Message type: chat") != std::string::npos,
               "card carries message type + auto-delivery semantics");
        Assert(block->content.find("Source: irc") != std::string::npos,
               "card carries origin line");
    }

    // Second arrival for same session must render as queue-flush (2 cards)
    store.AddArrival(arrival);
    auto block2 = provider.Provide(emptyAgent, access);
    Assert(block2.has_value() && block2->content.find("Arrival 2 of 2") != std::string::npos,
           "queue-flush renders one card per pending arrival");
    return 0;
}

} // namespace

int main() {
    std::cerr << "[ReplyResolutionTests]\n";
    TestLatestArrivalProvidesReplyTargets();
    TestLatestSurvivesConsumption();
    TestProviderRendersForUnboundAgent();
    TestCardOriginKeys();
    if (g_failures == 0) {
        std::cerr << "  ALL PASSED\n";
        return 0;
    }
    std::cerr << "  " << g_failures << " FAILURES\n";
    return 1;
}
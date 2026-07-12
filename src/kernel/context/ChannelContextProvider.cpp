// ChannelContextProvider.cpp
// Injects channel-specific context into the system prompt so the agent knows
// what channel it's on and how its responses will be routed back.
// ============================================================================

#include "animus_kernel/context/ChannelContextProvider.h"
#include "animus_kernel/Session.h"

#include <sstream>

namespace animus::kernel {

std::string ChannelContextProvider::Name() const { return "Channel Context"; }
int ChannelContextProvider::Priority() const { return 15; }

std::optional<ContextBlock> ChannelContextProvider::Provide(
        const Agent& agent,
        const SessionAccess& session) const {

    // Session key connector format: "channel:sessionType:channelName:routingValue"
    // e.g. connector = "channel:chat:discord:1428065181069742175"
    const std::string& connector = session.Key().connector;

    if (connector.find("channel:") != 0) {
        return std::nullopt;
    }

    std::istringstream ss(connector);
    std::string prefix, sessionType, channelName;
    std::getline(ss, prefix, ':');       // "channel"
    std::getline(ss, sessionType, ':');  // "chat", "wall", etc.
    std::getline(ss, channelName, ':');  // "discord", "vk", "telegram", etc.

    if (channelName.empty()) return std::nullopt;

    std::string content = BuildChannelContext(channelName);
    if (content.empty()) return std::nullopt;

    ContextBlock block;
    block.name = "Current Channel";
    block.content = content;
    block.priority = Priority();
    return block;
}

std::string ChannelContextProvider::BuildChannelContext(const std::string& channelName) const {
    std::string platformName;
    std::string routingInstructions;
    std::string socialToolNote;

    if (channelName == "discord") {
        platformName = "Discord";
        routingInstructions =
            "Your text response will be automatically sent as a reply in the originating "
            "Discord channel or DM.";
        socialToolNote =
            "Do NOT use the social tool to send replies -- your text output IS the reply. "
            "The social tool is only for performing additional operations on social platforms "
            "(searching, browsing, managing reactions, etc.).";
    } else if (channelName == "telegram") {
        platformName = "Telegram";
        routingInstructions =
            "Your text response will be automatically sent as a reply in the originating "
            "Telegram chat.";
        socialToolNote =
            "Do NOT use the social tool to send replies -- your text output IS the reply.";
    } else if (channelName == "vk") {
        platformName = "VK";
        routingInstructions =
            "Your text response will be automatically sent as a reply on VK.";
        socialToolNote =
            "Do NOT use the social tool to send replies -- your text output IS the reply.";
    } else if (channelName == "bluesky") {
        platformName = "Bluesky";
        routingInstructions =
            "Your text response will be automatically sent as a reply on Bluesky.";
        socialToolNote =
            "Do NOT use the social tool to send replies -- your text output IS the reply.";
    } else if (channelName == "twitter") {
        platformName = "Twitter";
        routingInstructions =
            "Your text response will be automatically sent as a reply on Twitter.";
        socialToolNote =
            "Do NOT use the social tool to send replies -- your text output IS the reply.";
    } else if (channelName == "irc") {
        platformName = "IRC";
        routingInstructions =
            "Your text response will be automatically sent to the originating IRC "
            "channel or query.";
    } else if (channelName == "moltbook") {
        platformName = "Moltbook";
        routingInstructions =
            "Your text response will NOT be sent to Moltbook. Moltbook is a poll-based "
            "platform — use the channels tool to post, comment, browse, and interact.";
        socialToolNote =
            "Use the channels tool (platform_id \"moltbook:default\") for all Moltbook "
            "interactions: posting, commenting, upvoting, searching, and reading "
            "notifications. Moltbook has no real-time message delivery.";
    } else if (channelName == "slack") {
        platformName = "Slack";
        routingInstructions =
            "Your text response will be automatically sent as a reply in the originating "
            "Slack channel or DM.";
        socialToolNote =
            "Do NOT use the social tool to send replies -- your text output IS the reply. "
            "The social tool is only for performing additional operations (searching, "
            "browsing, managing reactions, etc.).";
    } else if (channelName == "email") {
        platformName = "Email";
        routingInstructions =
            "Your text response will be automatically sent as an email reply in the "
            "same thread.";
    } else {
        platformName = channelName;
        routingInstructions =
            "Your text response will be routed back to the originating conversation.";
    }

    std::string content;
    content += "You are currently in a chat session on " + platformName + ".\n";
    content += routingInstructions + "\n";
    if (!socialToolNote.empty()) {
        content += socialToolNote;
    }

    return content;
}

} // namespace animus::kernel

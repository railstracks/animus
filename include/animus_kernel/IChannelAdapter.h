#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <json/value.h>

#include "animus_kernel/ChannelState.h"

namespace animus::kernel {

class HttpClient;
class AgentConfigStore;

/// Reply routing target — tells the adapter how to deliver a response.
/// This struct is owned by ChannelManager and passed to adapters.
struct ChannelReplyTarget {
    std::string channel_name;
    std::string channel_type;
    enum Type { Chat, Wall } type{Chat};
    std::string peer_id;
    std::string post_id;
    std::string reply_to_comment;
    std::string group_id;
    // IRC-specific
    std::string irc_target;
    std::string interface_name;
    // Email-specific
    std::string email_thread_id;
    std::string email_inbox_id;
};

/// Dispatch callback for routing inbound messages to agent sessions.
using ChannelDispatchCallback = std::function<void(
    const std::string& agentId,
    const std::string& sessionKey,
    const std::string& message,
    const std::string& sessionType,
    const ChannelReplyTarget& replyTarget)>;

/// Log callback for storing messages without triggering a chain.
using ChannelLogCallback = std::function<void(
    const std::string& agentId,
    const std::string& sessionKey,
    const std::string& message,
    const std::string& sessionType)>;

/// Query callback: returns the last N turn contents for a session.
/// Used by channel pollers to determine what's already been processed
/// (replaces separate watermark storage).
using ChannelSessionQueryCallback = std::function<std::vector<std::string>(
    const std::string& sessionKey,
    std::size_t maxTurns)>;

/// Interface for platform-specific channel connectors.
///
/// Each platform (IRC, Telegram, VK, Discord, Slack, Email, WhatsApp, etc.)
/// implements this interface. ChannelManager acts as a registry, delegating
/// lifecycle and reply operations to the appropriate adapter.
///
/// Migration status: ChannelManager still contains most connector implementations
/// inline. Adapters are being extracted incrementally. The interface establishes
/// the boundary so new connectors can be added as separate classes.
class IChannelAdapter {
public:
    virtual ~IChannelAdapter() = default;

    /// Start the connector (begin polling, connect socket, etc.)
    /// Returns true on success.
    virtual bool Start(const ChannelState& state, std::string* error) = 0;

    /// Stop the connector (disconnect, join threads).
    virtual void Stop() = 0;

    /// Send a reply message through this channel.
    virtual void SendReply(const ChannelReplyTarget& target,
                           const std::string& text) = 0;

    /// Check if the connector is currently connected/active.
    virtual bool IsConnected() const = 0;

    /// Validate a channel configuration for this adapter type.
    /// Returns true if valid; if false, sets *error.
    static bool ValidateConfig(const std::string& type,
                               const Json::Value& config,
                               std::string* error);

    /// Factory: create an adapter for a channel type.
    /// Returns nullptr if the type is unknown.
    /// Dependencies (httpClient, configStore, dispatch, log) are injected.
    static std::unique_ptr<IChannelAdapter> Create(
        const std::string& type,
        HttpClient& httpClient,
        AgentConfigStore* configStore,
        ChannelDispatchCallback dispatch,
        ChannelLogCallback logCallback);
};

} // namespace animus::kernel

#pragma once

#include "animus_kernel/IChannelAdapter.h"
#include "animus_kernel/ChannelContext.h"
#include "animus_kernel/ChannelHelpers.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace animus::kernel {

// Forward declarations to avoid heavy includes in header
namespace telegram { struct Message; struct Update; class TelegramBotApi; }
class IrcInterfaceRuntime;
struct ChannelState;

// ============================================================================
// PollerAdapterBase — common lifecycle for poller-based connectors
// ============================================================================

class PollerAdapterBase : public IChannelAdapter {
public:
    explicit PollerAdapterBase(ChannelContext& ctx) : m_ctx(ctx) {}
    ~PollerAdapterBase() override { Stop(); }

    bool Start(const ChannelState& state, std::string* error) override;
    void Stop() override;
    bool IsConnected() const override;

protected:
    ChannelContext& m_ctx;
    std::unique_ptr<PollerRuntime> m_runtime;
    std::atomic<bool> m_stopRequested{false};

    /// Subclass hook for adapter-specific init (load persisted state, etc.)
    virtual void OnInit(const ChannelState& state) { (void)state; }

    /// Subclass implements the polling loop entry point.
    virtual void RunLoop() = 0;

    /// Dispatch helper — routes message to agent session. Metadata and
    /// explicitSessionKey pass through to ChannelDispatch (Aug 31).
    void Dispatch(const std::string& routingKey,
                  const std::string& message,
                  const std::string& sessionType,
                  const std::string& metadata = "{}",
                  const std::string& explicitSessionKey = "");

    /// Log helper — records message without triggering chain
    void Log(const std::string& routingKey,
             const std::string& message,
             const std::string& sessionType);
};

// ============================================================================
// IrcAdapter
// ============================================================================

class IrcAdapter : public IChannelAdapter {
public:
    explicit IrcAdapter(ChannelContext& ctx) : m_ctx(ctx) {}
    ~IrcAdapter() override { Stop(); }

    bool Start(const ChannelState& state, std::string* error) override;
    void Stop() override;
    void SendReply(const ChannelReplyTarget& target, const std::string& text) override;
    bool IsConnected() const override;

private:
    ChannelContext& m_ctx;
    std::shared_ptr<IrcInterfaceRuntime> m_runtime;
    mutable std::mutex m_mutex;
    std::string m_channelName;
    std::string m_botNick;

    void OnMessage(const std::string& sourceNick,
                   const std::string& target,
                   const std::string& message,
                   bool isNotice);
    void OnStatus(bool connected, std::uint64_t eventUnixMs);
};

// ============================================================================
// TelegramAdapter
// ============================================================================

class TelegramAdapter : public PollerAdapterBase {
public:
    using PollerAdapterBase::PollerAdapterBase;
    void SendReply(const ChannelReplyTarget& target, const std::string& text) override;

protected:
    void RunLoop() override;
    void ProcessMessage(const telegram::Message& msg);
    void ProcessChatMemberUpdate(const telegram::Update& update);
};

// ============================================================================
// VkAdapter
// ============================================================================

class VkAdapter : public PollerAdapterBase {
public:
    using PollerAdapterBase::PollerAdapterBase;
    void SendReply(const ChannelReplyTarget& target, const std::string& text) override;

protected:
    void RunLoop() override;

private:
    void ProcessEvent(const std::string& eventType, const std::string& objectJson);
    void ProcessMessageNew(const std::string& objectJson);
    void ProcessWallPostNew(const std::string& objectJson);
    void ProcessWallReplyNew(const std::string& objectJson);
    bool FetchLongPollServer();
    std::string BuildLongPollUrl() const;
    std::unordered_map<std::string, std::string> ResolveUsers(
        const std::vector<std::string>& userIds);
    std::string FetchChatHistory(const std::string& peerId, int count);
};

// ============================================================================
// EmailAdapter (AgentMail)
// ============================================================================

class EmailAdapter : public PollerAdapterBase {
public:
    using PollerAdapterBase::PollerAdapterBase;
    void SendReply(const ChannelReplyTarget& target, const std::string& text) override;

protected:
    void RunLoop() override;

private:
    void WebSocketLoop();
    void PollLoop();
    void ProcessMessage(const std::string& threadId,
                        const std::string& messageId,
                        const std::string& sender,
                        const std::string& subject,
                        const std::string& bodyText);
};

// ============================================================================
// DiscordAdapter
// ============================================================================

class DiscordAdapter : public PollerAdapterBase {
public:
    using PollerAdapterBase::PollerAdapterBase;
    void SendReply(const ChannelReplyTarget& target, const std::string& text) override;

protected:
    void RunLoop() override;

private:
    void ProcessMessage(const std::string& channelId,
                        const std::string& messageId,
                        const std::string& authorId,
                        const std::string& authorUsername,
                        const std::string& content);
};

// ============================================================================
// WhatsAppAdapter
// ============================================================================

struct WhatsAppOutboundMessage {
    std::string jid;
    std::string baseJid;
    std::string text;
    bool is_group = false;
};

class WhatsAppAdapter : public PollerAdapterBase {
public:
    explicit WhatsAppAdapter(ChannelContext& ctx) : PollerAdapterBase(ctx) {}
    void SendReply(const ChannelReplyTarget& target, const std::string& text) override;

    std::string GetQrUrl() const;

protected:
    void RunLoop() override;

private:
    void GatewayLoopInner();

    std::mutex m_outboxMutex;
    std::vector<WhatsAppOutboundMessage> m_outbox;
};

// ============================================================================
// SlackAdapter
// ============================================================================

class SlackAdapter : public PollerAdapterBase {
public:
    explicit SlackAdapter(ChannelContext& ctx) : PollerAdapterBase(ctx) {}
    void SendReply(const ChannelReplyTarget& target, const std::string& text) override;

protected:
    void OnInit(const ChannelState& state) override;
    void RunLoop() override;

private:
    bool m_useSocketMode{false};
    void SocketModeLoop();
    void PollingLoop();
};

// ============================================================================
// NextcloudAdapter
// ============================================================================

class NextcloudAdapter : public PollerAdapterBase {
public:
    using PollerAdapterBase::PollerAdapterBase;
    void SendReply(const ChannelReplyTarget& target, const std::string& text) override;

protected:
    void RunLoop() override;
};

} // namespace animus::kernel

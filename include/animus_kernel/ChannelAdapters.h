#pragma once

#include "animus_kernel/IChannelAdapter.h"
#include "animus_kernel/ChannelContext.h"
#include "animus_kernel/ChannelHelpers.h"
#include "animus_kernel/ConnectionSupervisor.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace animus::kernel {

// Forward declarations to avoid heavy includes in header
namespace telegram { struct Message; struct Update; class TelegramBotApi; }
class IrcInterfaceRuntime;
struct ChannelState;
namespace calls { class SignalingClient; struct Utterance; }

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
    void SendAction(const ChannelReplyTarget& target, const std::string& text) override;
    bool JoinChannel(const ChannelReplyTarget& target, const std::string& key) override;
    bool PartChannel(const ChannelReplyTarget& target, const std::string& reason) override;
    bool IsConnected() const override;

private:
    ChannelContext& m_ctx;
    std::shared_ptr<IrcInterfaceRuntime> m_runtime;
    mutable std::mutex m_mutex;
    std::string m_channelName;
    std::string m_botNick;
    std::string m_agentId;      // #262: config agent_id, stored at Start()

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
    void Stop() override;
    bool IsConnected() const override;

protected:
    void RunLoop() override;

private:
    // Supervisor-managed websocket (reconnect + loud transitions — #60 fix).
    // Null before RunLoop starts and after Stop.
    std::unique_ptr<ConnectionSupervisor> m_supervisor;

    // #60: automatic degraded mode. While the WS is unhealthy beyond the
    // configured grace, the RunLoop thread bridges to the poll transport;
    // the gate owns the engage/disengage hysteresis. Both poll modes run
    // only on the RunLoop thread - never concurrently.
    std::atomic<bool> m_pollFallbackActive{false};
    std::string m_pollOwnAddress;  // lazily-resolved self-send guard (poll)

    void PollLoop();   // explicit degraded mode (config transport=poll)
    bool PollOnce();   // one HTTP poll pass (shared by both poll modes)
    void ProcessMessage(const Json::Value& msg);
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

    // #324 voice inbound: true when a chat payload is a voice note
    // (messageType=voice-message) or an audio/* file share. Out-params carry
    // the file name + mimetype for the fetch/STT path. Public static —
    // fixture-tested against live Talk 25 payloads (ChannelManagerTests).
    static bool DetectVoiceNote(const Json::Value& msg,
                                std::string& fileName,
                                std::string& mimetype);

    // #324 acceptance fix (Nov 18): DAV GET candidate targets for a shared
    // file name, from a received-shares JSON array. Composer-sent voice
    // notes arrive as the sender's per-room FOLDER share (room share type
    // 10 + per-user derivations, mimetype httpd/unix-directory) — the wav
    // is a child of the mount, so basename matching alone never sees it.
    // Direct basename targets come first, then directory-mount children.
    // Public static — fixture-tested against the live Nov 18 acceptance
    // shares (ChannelManagerTests).
    static std::vector<std::string> ResolveShareGetTargets(
            const Json::Value& shares, const std::string& fileName);

protected:
    void RunLoop() override;

private:
    // #324: room-shared file bytes via the received-shares route — the
    // share's file_target resolves in the RECIPIENT's own DAV namespace
    // (probe-proven Nov 17; direct sharer-path GETs 404). Empty on failure.
    std::string FetchSharedFileBytes(const std::string& serverUrl,
                                     const std::string& authHeader,
                                     const std::string& username,
                                     const std::string& fileName);
    // #324: whisper /inference multipart contract — the same service the
    // stt package calls (server converts formats via ffmpeg). Empty on
    // failure; callers decide how loud to be.
    std::string TranscribeAudio(const std::string& sttUrl,
                                const std::string& fileName,
                                const std::string& mimetype,
                                const std::string& audioBytes);
};

// ============================================================================
// MoltbookAdapter
// ============================================================================

class MoltbookAdapter : public PollerAdapterBase {
public:
    using PollerAdapterBase::PollerAdapterBase;
    void SendReply(const ChannelReplyTarget& target, const std::string& text) override;

protected:
    void RunLoop() override;

private:
    void ProcessNotification(const Json::Value& n);
};

#ifdef ANIMUS_WITH_CALLS
// ============================================================================
// CallAdapter — Nextcloud Talk calls lane (#336, Calls Tier 1)
// ============================================================================
// Event-driven (owns a calls::SignalingClient; NOT a poller). v1 lifecycle:
// ring → join → attend → leave; one live call per seat. Join = the Q9-proven
// seat flow (OCS settings → v2 JWT, REST room join → Talk room session,
// signaling join pinned to that session). Audio-in/out ride later tickets;
// this adapter owns transport, verbs, policy, and truth.

class CallAdapter : public IChannelAdapter {
public:
    explicit CallAdapter(ChannelContext& ctx) : m_ctx(ctx) {}
    ~CallAdapter() override;

    bool Start(const ChannelState& state, std::string* error) override;
    void Stop() override;
    void SendReply(const ChannelReplyTarget& target,
                   const std::string& text) override;
    bool IsConnected() const override;

    // --- Calls-lane verbs ---
    bool JoinCall(const std::string& roomToken, std::string* error) override;
    bool LeaveCall(std::string* error) override;
    Json::Value CallStatus() const override;

    /// Ring/invite hook: evaluate the join policy (decision 2) for an
    /// inbound invite and auto-join only when it allows. The invite event
    /// source (nextcloud poller's call-activity dispatch) calls this.
    bool TryAutoJoin(const std::string& roomToken, const std::string& fromUser);

    // --- Join policy (decision 2) — pure, unit-tested ---
    enum class JoinDecision { AutoAccept, RequireToolCall };
    /// "auto_owner" (0.5 default): auto-accept invites from the owner's
    /// account only; everything else needs the call.join tool verb.
    /// "manual": everything needs the tool verb. Unknown policies fail
    /// closed to manual. Self-initiated calls are structurally absent in
    /// v1 — no join path exists without an invite or a tool call.
    static JoinDecision EvaluateJoinPolicy(const std::string& policy,
                                            bool fromOwner);

    /// Config shape check for channel type "calls" (offline-testable).
    /// stt_url/stun/turn are optional (media-less listen probes work;
    /// media needs them).
    static bool ValidateConfigShape(const Json::Value& config,
                                    std::string* error);

private:
    ChannelContext& m_ctx;
    mutable std::mutex m_mutex;
    std::string m_channelName;
    std::string m_agentId;
    Json::Value m_config;
    std::shared_ptr<calls::SignalingClient> m_client; // one live call (v1)
    std::string m_activeRoom;

    // #337 media wiring: per-call media state (peer connections, opus
    // decoders, per-publisher pipelines) — defined in the .cpp; the
    // adapter owns teardown with the call.
    struct MediaState;
    std::shared_ptr<MediaState> m_media; // shared_ptr: incomplete in header TUs
    PollerRuntime m_dispatchRt;  // dispatch identity for ChannelDispatch

    // Signaling-frame router (offers/candidates/events → media layer).
    void HandleSignalFrame(const Json::Value& frame);
    void HandleOffer(const std::string& senderSid, const Json::Value& data);
    void HandleCandidate(const std::string& senderSid,
                         const Json::Value& data);
    void RequestOffersFromInCall();
    void FeedOpusFrame(const std::string& senderSid, const uint8_t* data,
                       size_t len);
    // Both take the call context the pipeline captured at build time: the
    // leave-time flush runs after the adapter's media state is detached.
    void OnUtterance(const calls::Utterance& u, const std::string& room);
    std::string TranscribePcm16k(const std::vector<int16_t>& pcm,
                                 const std::string& sttUrl);
    void TearDownMedia();
};
#endif // ANIMUS_WITH_CALLS

} // namespace animus::kernel

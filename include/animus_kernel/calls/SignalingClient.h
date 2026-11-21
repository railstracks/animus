#pragma once

#include <json/value.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace animus::kernel::calls {

/// Configuration for the standalone-signaling client. The internal secret
/// is vaulted upstream (adapter/admin state, decision 7) and never logged.
struct SignalingConfig {
    /// Signaling server URL: wss://host[:port][/path].
    std::string server_url;
    /// Nextcloud base URL sent in hello params (backend the server
    /// associates the client with).
    std::string backend_url;
    /// Server-side `[clients] internalsecret`.
    std::string internal_secret;
    /// Room token joined after hello.
    std::string room_id;
    /// Exponential reconnect backoff: base delay and cap (seconds).
    double reconnect_base_s = 1.0;
    double reconnect_max_s = 30.0;
    /// Deadline for hello+room-ack before recycling the connection.
    double hello_timeout_s = 10.0;
};

/// Standalone-signaling (Nextcloud Talk) WebSocket client — the protocol
/// layer of the calls lane (#335). Mechanical by design: it owns transport,
/// authentication, room join/resume, reconnect, and participant bookkeeping;
/// it applies no policy. Join/offer policy belongs to the call adapter
/// (#336); media to libdatachannel; nothing here touches audio.
///
/// Threading: the client owns one worker thread running a trantor event
/// loop. All public methods may be called from any thread; work is
/// marshalled onto the loop. Callbacks fire on the loop thread.
///
/// Protocol shape proven by Phase 0 leg 1 against production:
/// welcome → hello (internal auth, HMAC-SHA256) → room join → events → bye.
/// Resume is server-mediated (signaling ≥2.1): hello responses carry a
/// resumeid; reconnection replays it as hello.auth.type="resume", which
/// re-attaches the PRIOR session id, falling back to fresh internal
/// auth when the server rejects or omits it. Live-proven by
/// tools/calls_probe.cpp (#335 gate).
class SignalingClient {
public:
    enum class State {
        Idle,           // constructed, not started
        Connecting,     // WS connect in flight
        Authenticating, // WS open, hello sent, awaiting response
        Joining,        // hello accepted, room join sent
        Joined,         // room join acknowledged
        ReconnectWait,  // connection lost / failed, backing off
        Closed,         // terminal (Close() called)
    };
    static const char* StateName(State s);

    struct Participant {
        std::string session_id;
        std::string user_id; // empty for anonymous/internal sessions
        long long in_call = 0; // server flags; 0 = not in call
    };

    /// Every protocol frame not fully consumed internally (event/message/
    /// room/error/welcome/hello) is forwarded verbatim — the adapter
    /// branches on `frame["type"]`.
    using FrameCallback = std::function<void(const Json::Value& frame)>;
    using StateChangedCallback = std::function<void(State state)>;

    explicit SignalingClient(SignalingConfig cfg);
    ~SignalingClient();

    SignalingClient(const SignalingClient&) = delete;
    SignalingClient& operator=(const SignalingClient&) = delete;

    /// Start connecting (idempotent). Returns immediately; watch
    /// SetStateChangedCallback.
    void Start();
    /// Graceful terminal shutdown: sends bye, closes, joins the worker.
    void Close();

    /// Abrupt transport drop WITHOUT bye — the live-probe/recovery-drill
    /// hook (#335 live gate). Closes the underlying WS connection the
    /// same way an unannounced network loss would, riding the ordinary
    /// reconnect + room-resume path. Safe no-op when not connected.
    void DropTransport();

    State state() const;
    std::string session_id() const;
    std::vector<Participant> participants() const;

    /// Set our in-call flags (relayed by the server to participants).
    /// Returns false if not currently Joined.
    bool SetInCall(int flags);

    /// Relay a protocol message envelope to a specific session
    /// (offer/answer/candidate/…). Returns false if not Joined.
    bool SendToSession(const std::string& session_id, const Json::Value& data);

    /// Convenience: requestoffer to a publishing session (listen-only
    /// entry, per Phase 0 leg 2). Returns false if not Joined.
    bool RequestOffer(const std::string& session_id);

    void SetFrameCallback(FrameCallback cb);
    void SetStateChangedCallback(StateChangedCallback cb);

    // ---- pure protocol helpers; no network, unit-tested ----

    /// Splits a server URL into a drogon client URL (scheme://host[:port])
    /// and the upgrade request path. Normalizes http→ws, https→wss;
    /// defaults missing scheme to wss and missing path to "/".
    struct ServerEndpoint {
        std::string client_url;
        std::string path;
    };
    static ServerEndpoint ParseServerUrl(const std::string& url);

    /// HMAC-SHA256(random, secret) as lowercase hex — the internal-auth
    /// token (validated server-side against the same secret).
    static std::string ComputeInternalToken(const std::string& random,
                                            const std::string& secret);

    static std::string BuildHelloInternal(const std::string& random,
                                          const std::string& token,
                                          const std::string& backend_url,
                                          const std::string& id);
    /// Resume hello: re-attach a prior session after an abrupt drop by
    /// its server-issued resumeid (hello.auth.type = "resume"). Used by
    /// the reconnect path when the previous hello response carried a
    /// resumeid; a server that rejects it (or never issued one) falls
    /// back to fresh internal auth on the next attempt.
    static std::string BuildHelloResume(const std::string& resume_id,
                                        const std::string& id);
    /// Fresh join passes an empty session id; resume passes the prior one.
    static std::string BuildRoomJoin(const std::string& room_id,
                                     const std::string& session_id,
                                     const std::string& id);
    static std::string BuildInCall(int flags, const std::string& id);
    static std::string BuildBye(const std::string& id);
    static std::string BuildSessionMessage(const std::string& to_session,
                                           const Json::Value& data,
                                           const std::string& id);

    /// Exponential backoff for reconnect attempt n (0-based):
    /// base * 2^n, clamped to max. Attempt < 0 yields base.
    static double BackoffSeconds(int attempt, double base_s, double max_s);

    /// Applies a signaling `event` frame's participant bookkeeping
    /// (participants-update, room join/leave) to a session map. Accepts
    /// both wire spellings seen in Phase 0 (sessionId/userId and
    /// sessionid/userid). Exposed for tests and the adapter.
    static void ApplyParticipantEvent(
        const Json::Value& event,
        std::map<std::string, Participant>& participants);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace animus::kernel::calls

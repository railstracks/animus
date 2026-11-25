// CallAdapter — Nextcloud Talk calls lane (#336, Calls Tier 1).
// Event-driven channel adapter owning a calls::SignalingClient (NOT a
// poller). v1 lifecycle: ring → join → attend → leave; one live call per
// seat. JoinCall mints seat credentials via the Q9-proven flow (OCS
// signaling/settings → hello-v2 JWT; REST participants/active → Talk room
// session; signaling join pinned to that session). Audio-in/out ride
// later tickets (#295/#325 pipeline); this adapter owns transport,
// verbs, join policy, and truth.
//
// Threading: JoinCall/LeaveCall may be called from any thread (tool
// execution); the signaling client owns its own worker. m_mutex guards
// client/room lifecycle. HttpClient is thread-safe per request.

#include "animus_kernel/ChannelAdapters.h"
#include "animus_kernel/ChannelHelpers.h"
#include "animus_kernel/ChannelState.h"
#include "animus_kernel/Log.h"
#include "animus_kernel/calls/AudioInPipeline.h"
#include "animus_kernel/calls/SignalingClient.h"
#include "animus_kernel/calls/SignalingWire.h"
#include "animus_kernel/tools/HttpClient.h"

#include <rtc/rtc.hpp>
#include <fvad.h>
#include <opus.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <vector>

namespace animus::kernel {

using calls::SignalingAuthMode;
using calls::SignalingClient;
using namespace channel_detail; // GetString/UrlEncode/Base64EncodeStr/ParseJson

// ============================================================================
// Join policy (decision 2) — pure, offline-testable
// ============================================================================

CallAdapter::JoinDecision CallAdapter::EvaluateJoinPolicy(
    const std::string& policy, bool fromOwner) {
    if (policy == "auto_owner" && fromOwner) return JoinDecision::AutoAccept;
    // Everything else — manual, unknown, empty — fails closed to the
    // tool verb. No self-initiated calls exist in v1: every join needs
    // an invite (this gate) or call.join (the tool verb).
    return JoinDecision::RequireToolCall;
}

bool CallAdapter::ValidateConfigShape(const Json::Value& config,
                                      std::string* error) {
    if (GetString(config, "nextcloud_url").empty()) {
        if (error) *error = "Nextcloud server URL is required";
        return false;
    }
    if (GetString(config, "username").empty()) {
        if (error) *error = "Nextcloud username is required";
        return false;
    }
    if (GetString(config, "app_password").empty()) {
        if (error) *error = "Nextcloud app password is required";
        return false;
    }
    static const std::set<std::string> kCallsKeys = {
        "nextcloud_url", "username", "app_password", "owner_users",
        "join_policy", "agent_id", "stt_url", "stun_server",
        "turn_server", "turn_user", "turn_password",
        // Internal-lane fields (spike leg2): auth_mode "internal"
        // bypasses OCS mint/REST join and HMAC-hellos directly.
        "auth_mode", "server_url", "backend_url", "internal_secret"};
    for (const auto& key : config.getMemberNames()) {
        if (!kCallsKeys.count(key)) {
            if (error) *error = "Unknown calls channel config key: " + key;
            return false;
        }
    }
    return true;
}

// ============================================================================
// Lifecycle
// ============================================================================

CallAdapter::~CallAdapter() { Stop(); }

bool CallAdapter::Start(const ChannelState& state, std::string* error) {
    if (!ValidateConfigShape(state.config, error)) return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_channelName = state.name;
    m_agentId = GetString(state.config, "agent_id", "");
    m_config = state.config;

    // Dispatch identity for ChannelDispatch (utterance events).
    m_dispatchRt.channel_name = state.name;
    m_dispatchRt.channel_type = "calls";
    m_dispatchRt.agent_id = m_agentId;

    // No connection at Start: the signaling client is created per call
    // (join on verb/invite, per the design's lifecycle). IsConnected()
    // reports armed, not in-call — CallStatus() carries the live truth.
    ALOG_INFO("calls", "CallAdapter armed: " << state.name << " (seat "
              << GetString(state.config, "username", "?") << ")");
    return true;
}

void CallAdapter::Stop() {
    std::shared_ptr<SignalingClient> client;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        client = std::move(m_client);
        m_activeRoom.clear();
    }
    if (client) client->Close(); // graceful bye, joins worker
}

bool CallAdapter::IsConnected() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_channelName.empty(); // armed (one live call per seat, v1)
}

void CallAdapter::SendReply(const ChannelReplyTarget& target,
                            const std::string& text) {
    // v1 calls have no room-chat write path: the seat's chat rides the
    // nextcloud channel adapter; call audio is the only lane here.
    // Transcript visibility is the session's, not the room's.
    (void)target;
    (void)text;
    ALOG_WARNING("calls", "SendReply on calls adapter ignored (no room-chat "
                "surface in v1; use the chat channel)");
}

// ============================================================================
// Credential mint (Q9-proven flow, #335)
// ============================================================================

namespace {

// OCS envelope helper: {"ocs":{"data":...}} — returns data or null.
Json::Value OcsData(const Json::Value& root) {
    return root["ocs"]["data"];
}

const char* PcStateName(rtc::PeerConnection::State s) {
    switch (s) {
        case rtc::PeerConnection::State::New: return "new";
        case rtc::PeerConnection::State::Connecting: return "connecting";
        case rtc::PeerConnection::State::Connected: return "connected";
        case rtc::PeerConnection::State::Disconnected: return "disconnected";
        case rtc::PeerConnection::State::Failed: return "failed";
        case rtc::PeerConnection::State::Closed: return "closed";
    }
    return "?";
}

const char* IceStateName(rtc::PeerConnection::IceState s) {
    switch (s) {
        case rtc::PeerConnection::IceState::New: return "new";
        case rtc::PeerConnection::IceState::Checking: return "checking";
        case rtc::PeerConnection::IceState::Connected: return "connected";
        case rtc::PeerConnection::IceState::Completed: return "completed";
        case rtc::PeerConnection::IceState::Failed: return "failed";
        case rtc::PeerConnection::IceState::Disconnected: return "disconnected";
        case rtc::PeerConnection::IceState::Closed: return "closed";
    }
    return "?";
}

const char* JsonTypeName(Json::ValueType t) {
    switch (t) {
        case Json::nullValue: return "null";
        case Json::intValue: return "int";
        case Json::uintValue: return "uint";
        case Json::realValue: return "real";
        case Json::stringValue: return "string";
        case Json::booleanValue: return "bool";
        case Json::arrayValue: return "array";
        case Json::objectValue: return "object";
    }
    return "?";
}

// Live-gate observability for the audio-in path (#337). Run 19 was the
// first run with media connected and ended at 0 utterances with no line
// saying which stage dropped the speech. Call-wide totals: one human
// speaker is the gate's shape.
struct AudioInCounters {
    std::atomic<uint64_t> vadFrames{0};   // 10 ms frames through the VAD
    std::atomic<uint64_t> vadVoiced{0};   // ... flagged as speech
    std::atomic<uint64_t> packets{0};     // Opus frames handed to a decoder
    std::atomic<uint64_t> decoded{0};
    std::atomic<uint64_t> rejected{0};
    std::atomic<uint64_t> trackPackets{0}; // log-only track path (run 22)
};
AudioInCounters g_audioIn;

// #337 run 20: with a browser speaking in the room, the speech decoder
// was handed exactly one packet per second for the whole call. Before
// blaming the sender, count everything the transport hands to the peer
// connection, grouped by SSRC — the library drops RTP whose SSRC matches
// no track without logging it (PeerConnection::dispatchMedia).
class MediaCensus final : public rtc::MediaHandler {
public:
    // Run 21 verdict: the browser's speech RTP arrives with an SSRC the
    // negotiation never announced, so PeerConnection::dispatchMedia drops
    // every packet before a track can see it — while the audio track only
    // ever delivered the browser's 1/s RTCP sender reports. This hook runs
    // BEFORE that routing: the one place the probe sees every decrypted
    // media packet. Opus payloads feed the speech pipeline from here; the
    // audio track's onMessage is demoted to a drained, log-only counter.
    std::function<void(const uint8_t* payload, size_t len)> onAudioRtp;

    void incoming(rtc::message_vector& messages,
                  const rtc::message_callback& send) override {
        (void)send; // nothing is ever sent back
        for (const auto& m : messages) {
            if (!m) continue;
            Tally(*m);
            if (onAudioRtp && m->type == rtc::Message::Binary) {
                const size_t off = RtpPayloadOffset(*m);
                if (off < m->size()) {
                    onAudioRtp(
                        reinterpret_cast<const uint8_t*>(m->data()) + off,
                        m->size() - off);
                }
            }
        }
    }

private:
    // Byte offset of the opus payload in an audio RTP packet, or the
    // packet size when there is nothing to feed (not RTP, not opus):
    // fixed header + CSRCs + one header extension. Talk/Chrome opus is
    // payload type 111 (from the offer); the census log prints the pt of
    // every SSRC, so a change would be visible in one run.
    static size_t RtpPayloadOffset(const rtc::Message& m) {
        const size_t size = m.size();
        if (size < 12) return size;
        const auto* d = reinterpret_cast<const uint8_t*>(m.data());
        if ((d[0] >> 6) != 2 || (d[1] & 0x7f) != 111) return size;
        size_t off = 12 + 4 * (d[0] & 0x0f);
        if (d[0] & 0x10) { // one header extension: 2B profile, 2B words
            if (size < off + 4) return size;
            off += 4 + 4 * ((uint32_t(d[off + 2]) << 8) | d[off + 3]);
        }
        return off;
    }

    struct Stat {
        uint64_t packets = 0;
        uint64_t bytes = 0;
        bool rtp = false;
        unsigned pt = 0;
        unsigned firstSeq = 0;
        unsigned lastSeq = 0;
    };
    using Key = std::pair<bool, uint32_t>;

    static std::mutex& Mutex() {
        static std::mutex m;
        return m;
    }
    static std::map<Key, Stat>& Table() {
        static std::map<Key, Stat> t;
        return t;
    }
    static std::chrono::steady_clock::time_point& WindowStart() {
        static std::chrono::steady_clock::time_point start{};
        return start;
    }
    static std::string Hex(uint32_t v) {
        static const char* kDigits = "0123456789abcdef";
        std::string out(8, '0');
        for (int i = 7; i >= 0; --i) {
            out[static_cast<size_t>(i)] = kDigits[v & 0xf];
            v >>= 4;
        }
        return out;
    }

    // One line per second of media, e.g.
    //   media-in: ssrc=1a2b3c4d rtp pt=111 n=50 bytes=11230 seq=4012..4061
    //             | rtcp pt=200 n=1 bytes=71
    static void Tally(const rtc::Message& m) {
        const size_t size = m.size();
        const auto* d = reinterpret_cast<const uint8_t*>(m.data());
        const bool rtp = m.type == rtc::Message::Binary;
        uint32_t ssrc = m.stream;
        unsigned pt = 0;
        unsigned seq = 0;
        if (size >= 2) pt = rtp ? unsigned(d[1] & 0x7f) : unsigned(d[1]);
        if (rtp && size >= 12) {
            ssrc = (uint32_t(d[8]) << 24) | (uint32_t(d[9]) << 16) |
                   (uint32_t(d[10]) << 8) | uint32_t(d[11]);
            seq = unsigned((d[2] << 8) | d[3]);
        }
        std::ostringstream line;
        {
            std::lock_guard<std::mutex> lock(Mutex());
            const auto now = std::chrono::steady_clock::now();
            auto& start = WindowStart();
            if (start == std::chrono::steady_clock::time_point{}) start = now;
            Stat& s = Table()[Key(rtp, ssrc)];
            if (s.packets == 0) {
                s.rtp = rtp;
                s.pt = pt;
                s.firstSeq = seq;
            }
            ++s.packets;
            s.bytes += size;
            s.lastSeq = seq;
            if (now - start >= std::chrono::seconds(1)) {
                for (const auto& entry : Table()) {
                    const Stat& v = entry.second;
                    line << (line.tellp() ? " | " : " ")
                         << (v.rtp ? "ssrc=" + Hex(entry.first.second)
                                   : std::string("rtcp"))
                         << " pt=" << v.pt << " n=" << v.packets
                         << " bytes=" << v.bytes;
                    if (v.rtp) {
                        line << " seq=" << v.firstSeq << ".." << v.lastSeq;
                    }
                }
                Table().clear();
                start = now;
            }
        }
        if (line.tellp() > 0) ALOG_INFO("calls", "media-in:" << line.str());
    }
};

} // namespace

// MediaState: one peer connection per publishing session (leg-2 shape),
// one Opus decoder + one AudioInPipeline per publisher. The pipelines'
// callbacks (VAD/STT/utterance) are wired by the adapter, not the state.
// Defined before JoinCall — the first user.
struct CallAdapter::MediaState {
    struct Publisher {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::Track> track;
        // Per-mid track references: the single-slot assignment in onTrack
        // dropped the audio track's last external reference when the video
        // track arrived, libdatachannel then marked the m-line removed
        // (m=audio 0) and the MCU never sent audio — run-14/15 root cause,
        // reproduced offline in animus-calls-spike/answer_test Nov 23 2026.
        std::map<std::string, std::shared_ptr<rtc::Track>> tracks;
        OpusDecoder* decoder = nullptr; // 48 kHz stereo decode
        std::unique_ptr<calls::AudioInPipeline> pipeline;
        std::string userId; // Tier-0 speaker label
        std::string lastOfferSdp; // replay guard for re-offers
        std::chrono::steady_clock::time_point offerAt{}; // stalled-guard clock
    };
    std::map<std::string, Publisher> pubs; // by signaling session id
    std::string stt_url;
};

bool CallAdapter::JoinCall(const std::string& roomToken, std::string* error) {
    // MediaState (defined above): one peer connection + decoder +
    // pipeline per publishing session; created with the call.
    // Opt-in libdatachannel logging for live gates: ANIMUS_RTC_LOG=debug
    // turns on ICE gathering/STUN/TURN/DTLS visibility on stderr; off by
    // default (run-9 lesson: ICE failures were invisible without it).
    static bool rtcLogArmed = false;
    if (!rtcLogArmed && getenv("ANIMUS_RTC_LOG") != nullptr) {
        rtcLogArmed = true;
        rtc::InitLogger(rtc::LogLevel::Debug);
    }

    std::string authHeader;
    std::string nextcloudUrl;
    // Two auth lanes: "internal" (spike leg2 — HMAC hello, call-capable
    // today) and hello-v2 (Q9 OCS-minted seat — REST joinCall is closed
    // to API clients on this Talk: InjectionMiddleware resolves the
    // participant session from the PHP session store, cookies only).
    const bool internalMode =
        GetString(m_config, "auth_mode") == "internal";
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_client) {
            if (error) *error = "already in a call (one live call per seat in v1; "
                                "call.leave first)";
            return false;
        }
        nextcloudUrl = GetString(m_config, "nextcloud_url");
        if (nextcloudUrl.empty()) {
            if (error) *error = "adapter config incomplete (nextcloud_url)";
            return false;
        }
        if (!internalMode) {
            const std::string user = GetString(m_config, "username");
            const std::string pass = GetString(m_config, "app_password");
            if (user.empty() || pass.empty()) {
                if (error) *error =
                    "adapter config incomplete (username/app_password)";
                return false;
            }
            authHeader = "Basic " + Base64EncodeStr(user + ":" + pass);
        }
    }
    if (roomToken.empty()) {
        if (error) *error = "room token required";
        return false;
    }

    std::string server;       // signaling server base (both lanes)
    std::string roomSession;  // v2: REST-pinned Talk room session
    std::string v2Token;      // v2: OCS-minted hello-2.0 JWT
    if (!internalMode) {
        // 1) OCS settings → hello-v2 JWT + signaling server URL (v3
        //    route — v1 is gone in Talk 25+, live-proven #335).
        HttpClient::Request settingsReq;
        settingsReq.method = "GET";
        settingsReq.url = nextcloudUrl +
            "/ocs/v2.php/apps/spreed/api/v3/signaling/settings";
        settingsReq.headers["OCS-APIRequest"] = "true";
        settingsReq.headers["Accept"] = "application/json";
        settingsReq.headers["Authorization"] = authHeader;
        auto settingsResp = m_ctx.httpClient.Execute(settingsReq);
        if (settingsResp.status_code != 200) {
            if (error) *error = "OCS settings failed (" +
                                std::to_string(settingsResp.status_code) + ")";
            return false;
        }
        const Json::Value settings = OcsData(ParseJson(settingsResp.body));
        v2Token = settings["helloAuthParams"]["2.0"]["token"].asString();
        server = GetString(settings, "server");
        if (v2Token.empty() || server.empty()) {
            if (error) *error =
                "no hello-v2 token or signaling server in settings";
            return false;
        }

        // 2) REST room join → Talk room session (the signaling join pins
        //    THIS; the signaling session id answers no_such_room —
        //    live-proven, Q9).
        HttpClient::Request joinReq;
        joinReq.method = "POST";
        joinReq.url = nextcloudUrl + "/ocs/v2.php/apps/spreed/api/v4/room/" +
                      UrlEncode(roomToken) + "/participants/active";
        joinReq.headers["OCS-APIRequest"] = "true";
        joinReq.headers["Accept"] = "application/json";
        joinReq.headers["Authorization"] = authHeader;
        auto joinResp = m_ctx.httpClient.Execute(joinReq);
        if (joinResp.status_code != 200) {
            if (error) *error = "REST room join failed (" +
                                std::to_string(joinResp.status_code) + ")";
            return false;
        }
        roomSession =
            OcsData(ParseJson(joinResp.body)).get("sessionId", "").asString();
        if (roomSession.empty()) {
            if (error) *error = "no room session from REST join";
            return false;
        }
    } else {
        // Internal lane (spike leg2, live-proven): direct HMAC hello —
        // no OCS mint, no REST join; the backend resolves the seat by
        // its internalsecret identity. server_url carries the base URL
        // (…/standalone-signaling), normalized with the v2 lane below.
        server = GetString(m_config, "server_url");
        if (server.empty() ||
            GetString(m_config, "internal_secret").empty() ||
            GetString(m_config, "backend_url").empty()) {
            if (error) *error =
                "internal auth config incomplete (server_url/"
                "internal_secret/backend_url)";
            return false;
        }
    }

    // 3) Signaling client in seat mode, joined to the room.
    calls::SignalingConfig cfg;
    cfg.server_url = server;
    // Normalize: server base is https://host/standalone-signaling;
    // the WS endpoint appends /spreed (ParseServerUrl maps https→wss).
    while (!server.empty() && server.back() == '/') server.pop_back();
    server += "/spreed";
    cfg.server_url = server;
    cfg.room_id = roomToken;
    cfg.reconnect_base_s = 1.0;
    cfg.reconnect_max_s = 30.0;
    cfg.hello_timeout_s = 10.0;
    if (internalMode) {
        cfg.auth_mode = SignalingAuthMode::Internal;
        cfg.backend_url = GetString(m_config, "backend_url");
        cfg.internal_secret = GetString(m_config, "internal_secret");
    } else {
        cfg.backend_url = nextcloudUrl;
        cfg.hello_v2_token = v2Token;
        cfg.auth_mode = SignalingAuthMode::HelloV2;
        cfg.room_session_id = roomSession;
    }

    auto client = std::make_shared<SignalingClient>(std::move(cfg));

    // Wait for Joined (bounded): the state callback fires on the client's
    // worker thread; a shared wait-state turns that into a synchronous
    // join verb WITHOUT the callback touching JoinCall's stack after it
    // returns (the client outlives this call — shared_ptr, not refs).
    struct JoinWait {
        std::mutex mu;
        std::condition_variable cv;
        bool joined{false};
        bool failed{false};
    };
    auto waitState = std::make_shared<JoinWait>();
    client->SetStateChangedCallback([waitState](SignalingClient::State s) {
        std::lock_guard<std::mutex> lk(waitState->mu);
        if (s == SignalingClient::State::Joined) {
            waitState->joined = true; // stays set across resume cycles
        } else if (s == SignalingClient::State::Closed) {
            waitState->failed = true;
        } else if (s == SignalingClient::State::ReconnectWait &&
                   !waitState->joined) {
            // ReconnectWait BEFORE any Joined = handshake failing;
            // after Joined it is drop-recovery (normal, not failure).
            waitState->failed = true;
        }
        waitState->cv.notify_all();
    });
    client->SetFrameCallback(
        [this](const Json::Value& frame) { HandleSignalFrame(frame); });

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_client) { // raced with another join
            if (error) *error = "already in a call";
            return false;
        }
        m_client = std::move(client);
        m_activeRoom = roomToken;
    }
    m_client->Start();

    std::unique_lock<std::mutex> lk(waitState->mu);
    const bool ok = waitState->cv.wait_for(
        lk, std::chrono::seconds(15),
        [&] { return waitState->joined || waitState->failed; });
    if (!ok || !waitState->joined) {
        std::string reason = !ok ? "timeout" : "handshake failed";
        LeaveCall(nullptr);
        if (error) *error = "signaling join failed: " + reason;
        return false;
    }

    // 4) Enter the CALL partition — lane-dependent.
    //    Internal (spike leg2, live-proven): the internal incall WS
    //    message IS honored for HelloClientTypeInternal sessions
    //    (hub.go:2995 session.SetInCall); flags 3 = IN_CALL|WITH_AUDIO.
    //    Internal sessions also auto-enter in-call at hello time
    //    (clientsession.go:195) unless they declare internal-incall.
    //    Hello-v2: REST joinCall (browser path) — CLOSED to API seats
    //    on this Talk: InjectionMiddleware resolves the participant
    //    session from the PHP session store (TalkSession::
    //    getSessionForRoom, cookies only); Basic auth can never satisfy
    //    it, so joinCall 404s at the !Session check. Kept as
    //    documentation; the seat joins the ROOM but not the call.
    if (internalMode) {
        m_client->SetInCall(3);
    } else {
        HttpClient::Request callReq;
        callReq.method = "POST";
        callReq.url = nextcloudUrl +
            "/ocs/v2.php/apps/spreed/api/v4/call/" + UrlEncode(roomToken);
        callReq.headers["OCS-APIRequest"] = "true";
        callReq.headers["Accept"] = "application/json";
        callReq.headers["Authorization"] = authHeader;
        callReq.headers["Content-Type"] = "application/json";
        callReq.body = "{\"flags\":3}";
        auto callResp = m_ctx.httpClient.Execute(callReq);
        if (callResp.status_code != 200) {
            ALOG_WARNING("calls", "REST joinCall failed ("
                        << callResp.status_code
                        << ") — seat stays room-only: " << roomToken);
        } else {
            ALOG_INFO("calls", "Call entered via REST joinCall (flags=3)");
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_media) m_media = std::make_shared<MediaState>();
        m_media->stt_url = GetString(m_config, "stt_url");
    }
    // Ask every in-call publisher for its offer (leg-2: requestoffer is
    // the listen-only entry — publishers don't push offers to new seats).
    RequestOffersFromInCall();
    ALOG_INFO("calls", "Call joined: " << roomToken << " (seat "
              << GetString(m_config, "username", "?") << ")");
    return true;
}

bool CallAdapter::LeaveCall(std::string* error) {
    std::shared_ptr<SignalingClient> client;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_client) {
            if (error) *error = "not in a call";
            return false;
        }
        client = std::move(m_client);
        m_activeRoom.clear();
    }
    TearDownMedia(); // flush pipelines first — trailing speech emits
    client->SetInCall(0);
    client->Close(); // graceful bye, joins worker
    ALOG_INFO("calls", "Call left");
    return true;
}

Json::Value CallAdapter::CallStatus() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    Json::Value out(Json::objectValue);
    out["in_call"] = m_client != nullptr;
    out["room"] = m_activeRoom;
    if (m_client) {
        out["state"] = SignalingClient::StateName(m_client->state());
        out["session_id"] = m_client->session_id();
        Json::Value ps(Json::arrayValue);
        for (const auto& p : m_client->participants()) {
            Json::Value pj(Json::objectValue);
            pj["session_id"] = p.session_id;
            pj["user_id"] = p.user_id;
            pj["in_call"] = static_cast<Json::Int64>(p.in_call);
            ps.append(pj);
        }
        out["participants"] = ps;
    }
    return out;
}

bool CallAdapter::TryAutoJoin(const std::string& roomToken,
                              const std::string& fromUser) {
    const std::string policy = [&] {
        std::lock_guard<std::mutex> lock(m_mutex);
        return GetString(m_config, "join_policy", "auto_owner");
    }();
    bool fromOwner = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const Json::Value owners = m_config.get("owner_users", Json::Value());
        if (owners.isArray()) {
            for (const auto& o : owners) {
                if (o.asString() == fromUser) { fromOwner = true; break; }
            }
        }
    }
    if (EvaluateJoinPolicy(policy, fromOwner) != JoinDecision::AutoAccept) {
        ALOG_INFO("calls", "Ring from non-owner (" << fromUser << ") — "
                  "policy " << policy << " requires call.join");
        return false;
    }
    std::string err;
    return JoinCall(roomToken, &err);
}

// ============================================================================
// #337 media wiring — signaling-frame router and media handling
// ============================================================================

void CallAdapter::HandleSignalFrame(const Json::Value& frame) {
    const std::string type = frame.get("type", "").asString();
    if (type == "event") {
        // Participants changed under us (publisher joined/toggled after
        // our join snapshot): re-request offers from in-call seats.
        RequestOffersFromInCall();
        return;
    }
    if (type != "message") return; // offers/candidates arrive as messages
    const Json::Value& msg = frame["message"];
    const Json::Value& sidNode = msg["sender"]["sessionid"];
    const std::string senderSid = sidNode.isString() ? sidNode.asString() : "";
    const Json::Value& data = msg["data"];
    const std::string dtype = data.get("type", "").asString();
    if (dtype == "offer") {
        HandleOffer(senderSid, data);
    } else if (dtype == "candidate") {
        HandleCandidate(senderSid, data);
    }
}

void CallAdapter::HandleOffer(const std::string& senderSid,
                               const Json::Value& data) {
    const Json::Value& sdpNode = data["payload"]["sdp"];
    const std::string sdp = sdpNode.isString() ? sdpNode.asString() : "";
    if (sdp.empty() || senderSid.empty()) {
        ALOG_WARNING("calls", "Offer without sdp/sender dropped (sender="
                    << senderSid.substr(0, 8) << ")");
        return;
    }

    // Offer for a subscription we already hold? (run-18 lesson, Nov 24
    // 2026: the old guard keyed on "a track exists", but onTrack fires
    // at SDP-apply time — before ICE — so a negotiation that never
    // connected looked "up", the MCU's retry offer was discarded as a
    // "duplicate", and the seat stayed wedged on a peer connection that
    // could not even fail: with no remote candidates its ICE agent
    // never started a check.)
    //   identical SDP -> replay of what is already applied: ignore
    //   any other SDP  -> replace: the MCU re-offers only when the old
    //                     negotiation is dead or was superseded
    std::shared_ptr<rtc::PeerConnection> stalePc;
    std::string staleState;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_media) {
            auto it = m_media->pubs.find(senderSid);
            if (it != m_media->pubs.end() && it->second.pc) {
                if (it->second.lastOfferSdp == sdp) {
                    ALOG_INFO("calls", "Duplicate offer (identical SDP) from "
                              << senderSid.substr(0, 8) << " ignored");
                    return;
                }
                staleState = PcStateName(it->second.pc->state());
                stalePc = std::move(it->second.pc);
                it->second.pc.reset();
                it->second.track.reset();
                it->second.tracks.clear();
            }
        }
    }
    if (stalePc) {
        ALOG_WARNING("calls", "Replacing subscription to "
                    << senderSid.substr(0, 8) << " (old state "
                    << staleState << ")");
        try { stalePc->close(); } catch (...) {}
    }

    std::string mySid;
    std::string roomType = data.get("roomType", "video").asString();
    std::string recvSid = data.get("sid", "").asString();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        mySid = m_client ? m_client->session_id() : "";
        if (!m_media) m_media = std::make_shared<MediaState>();
    }

    rtc::Configuration rtcCfg;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const std::string stun = GetString(m_config, "stun_server");
        if (!stun.empty()) {
            rtcCfg.iceServers.emplace_back(stun);
        }
        const std::string turn = GetString(m_config, "turn_server");
        if (!turn.empty()) {
            // turn_server = "host[:port]"; service defaults to 3478
            // (eturnal's published TURN port in this deployment).
            const auto colon = turn.find(':');
            const std::string host =
                colon == std::string::npos ? turn : turn.substr(0, colon);
            const std::string service =
                colon == std::string::npos ? "3478" : turn.substr(colon + 1);
            const std::string turnUser = GetString(m_config, "turn_user", "");
            const std::string turnPass = GetString(m_config, "turn_password", "");
            // Leg-2's proven shape: BOTH relay transports. UDP relay
            // preferred; TCP carries media when server→host UDP is
            // filtered (run-9 ICE failure shape — both sides Failed with
            // no working candidate pair).
            rtcCfg.iceServers.emplace_back(host, service, turnUser, turnPass,
                                           rtc::IceServer::RelayType::TurnUdp);
            rtcCfg.iceServers.emplace_back(host, service, turnUser, turnPass,
                                           rtc::IceServer::RelayType::TurnTcp);
        }
    }

    auto pc = std::make_shared<rtc::PeerConnection>(rtcCfg);
    const std::string publisherUserId = [&] {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& p : m_client ? m_client->participants()
                                     : std::vector<calls::SignalingClient::Participant>{}) {
            if (p.session_id == senderSid) return p.user_id;
        }
        return std::string();
    }();

    MediaState::Publisher* pubPtr = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        pubPtr = &m_media->pubs[senderSid]; // map::operator[] creates the entry
    }
    MediaState::Publisher& pub = *pubPtr;
    pub.pc = pc;
    pub.lastOfferSdp = sdp;
    pub.offerAt = std::chrono::steady_clock::now();
    pub.userId = publisherUserId.empty() ? senderSid : publisherUserId;

    // The decoder is created stereo (Talk publishes Opus 48k stereo).
    // It and the pipeline outlive a replaced peer connection: a re-offer
    // must not drop an utterance in flight, and building a fresh decoder
    // here would leak the old one.
    if (!pub.decoder) {
        int opusErr = 0;
        pub.decoder = opus_decoder_create(48000, 2, &opusErr);
        if (!pub.decoder) {
            ALOG_WARNING("calls", "Opus decoder create failed (err="
                        << opusErr << ")");
        }
    }
    if (!pub.pipeline) {
        calls::AudioInConfig pipeCfg; // decision-3 defaults
        std::string speaker = pub.userId;
        // The STT endpoint and the room are fixed for the call's lifetime,
        // and the pipeline's callbacks carry them from here. A flush that
        // looked them up on the adapter found neither: TearDownMedia
        // detaches the media state before it flushes, and LeaveCall clears
        // the room before it calls TearDownMedia — so trailing speech was
        // dropped twice over (no STT endpoint, then no room to dispatch to).
        std::string sttUrl;
        std::string room;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            sttUrl = m_media->stt_url;
            room = m_activeRoom;
        }
        CallAdapter* self = this;
        pub.pipeline = std::make_unique<calls::AudioInPipeline>(
            pipeCfg, speaker,
            [](const int16_t* frame, int samples) -> bool {
                static thread_local Fvad* v = nullptr;
                if (!v) {
                    v = fvad_new();
                    fvad_set_sample_rate(v, 16000);
                    fvad_set_mode(v, 2); // leg-3: use 2
                }
                const bool voiced = fvad_process(v, frame, samples) == 1;
                if (voiced) ++g_audioIn.vadVoiced;
                const uint64_t frames = ++g_audioIn.vadFrames;
                if (frames % 500 == 0) { // one line per 5 s of timeline
                    ALOG_INFO("calls", "audio-in: " << frames / 100
                              << " s through the VAD, voiced "
                              << g_audioIn.vadVoiced.load() * 100 / frames
                              << "%");
                }
                return voiced;
            },
            [self, sttUrl](const std::vector<int16_t>& pcm) -> std::string {
                return self->TranscribePcm16k(pcm, sttUrl);
            },
            [self, room](const calls::Utterance& u) {
                self->OnUtterance(u, room);
            });
    }

    const std::weak_ptr<rtc::PeerConnection> selfRef = pc;
    // A callback acts only while its peer connection is still the live
    // subscription for this sender; events from a replaced connection
    // (a late candidate, its own teardown, a stale track) stop here.
    auto isLive = [this, senderSid, selfRef]() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_media) return false;
        auto it = m_media->pubs.find(senderSid);
        return it != m_media->pubs.end() && it->second.pc &&
               it->second.pc == selfRef.lock();
    };

    // One trickle message in the shape the hub accepts (SignalingWire.h:
    // a bare candidate string is rejected before it reaches the MCU —
    // the runs-11-18 failure).
    auto sendTrickle = [this, senderSid, recvSid, roomType](
                           const Json::Value& envelope) -> bool {
        Json::Value payload;
        payload["type"] = "candidate";
        payload["sid"] = recvSid;
        payload["roomType"] = roomType;
        payload["payload"]["type"] = "candidate";
        payload["payload"]["candidate"] = envelope;
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_client ? m_client->SendToSession(senderSid, payload) : false;
    };

    // Sees every incoming media packet before the library routes it to a
    // track (or silently drops it): see MediaCensus.
    auto census = std::make_shared<MediaCensus>();
    census->onAudioRtp = [this, senderSid](const uint8_t* payload, size_t len) {
        FeedOpusFrame(senderSid, payload, len);
    };
    pc->setMediaHandler(census);
    pc->onTrack([this, senderSid, isLive](std::shared_ptr<rtc::Track> track) {
        if (!isLive()) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_media) return;
        auto it = m_media->pubs.find(senderSid);
        if (it == m_media->pubs.end() || !track) return;
        it->second.tracks[track->mid()] = track; // keep every m-line alive
        const std::string kind = track->description().type();
        ALOG_INFO("calls", "Media track up from " << senderSid.substr(0, 8)
                  << " mid=" << track->mid() << " kind=" << kind);
        // Only the audio m-line carries speech. The audio depacketizer
        // does no more than strip the RTP header, so wiring it to the
        // video m-line handed video bytes to the speaker's Opus decoder;
        // some parse as valid frames and decode to noise spliced into the
        // speech timeline (run 19: media connected, 0 utterances). Video
        // is held in `tracks` and decoded nowhere. It is still read and
        // discarded: an unread track fills its 1024-message receive queue
        // and the library logs a drop warning every second (run 20).
        if (kind != "audio") {
            track->onMessage([](rtc::binary) {}, nullptr);
            return;
        }
        it->second.track = track; // legacy slot (audio ref survives in tracks)
        // Run 21: keep the depacketizer (the queue must still be drained)
        // but feed nothing from here — the SSRC-unregistered speech RTP
        // never reaches this track, and what it did deliver was the
        // browser's 1/s RTCP sender reports decoded as Opus. Speech feeds
        // from MediaCensus::onAudioRtp; this path only counts what the
        // library routes to the track (log-only, drained).
        track->setMediaHandler(std::make_shared<rtc::OpusRtpDepacketizer>());
        track->onMessage(
            [senderSid](rtc::binary b) {
                (void)b;
                const uint64_t n = ++g_audioIn.trackPackets;
                if (n <= 3 || n % 250 == 0) {
                    ALOG_INFO("calls", "audio-in: track path log-only, "
                              << n << " packets from "
                              << senderSid.substr(0, 8));
                }
            },
            nullptr);
    });

    pc->onLocalCandidate([this, senderSid, sdp, isLive, sendTrickle](
                             rtc::Candidate cand) {
        if (!isLive()) return;
        const std::string text = cand.candidate();
        if (text.empty()) return;
        // The answer mirrors the offer's mids in order, so the offer
        // locates the m-line this candidate belongs to.
        const int idx = calls::MidToMlineIndex(sdp, cand.mid());
        ALOG_INFO("calls", "Trickle -> " << senderSid.substr(0, 8)
                  << " mid=" << cand.mid() << " idx=" << idx << ' ' << text);
        if (!sendTrickle(calls::CandidateEnvelope(text, cand.mid(), idx))) {
            ALOG_WARNING("calls", "Trickle to " << senderSid.substr(0, 8)
                        << " not queued (signaling not joined)");
        }
    });

    pc->onGatheringStateChange([this, senderSid, isLive, sendTrickle](
                                   rtc::PeerConnection::GatheringState g) {
        if (g != rtc::PeerConnection::GatheringState::Complete) return;
        if (!isLive()) return;
        ALOG_INFO("calls", "Gathering complete for "
                  << senderSid.substr(0, 8) << " — end-of-candidates sent");
        sendTrickle(calls::EndOfCandidatesEnvelope());
    });

    pc->onIceStateChange([this, senderSid, isLive](
                             rtc::PeerConnection::IceState s) {
        if (!isLive()) return;
        ALOG_INFO("calls", "ICE state for " << senderSid.substr(0, 8)
                  << ": " << IceStateName(s));
    });

    pc->onStateChange([this, senderSid, isLive](rtc::PeerConnection::State s) {
        if (!isLive()) return; // a replaced connection's own end is expected
        ALOG_INFO("calls", "Peer connection to " << senderSid.substr(0, 8)
                  << " state " << PcStateName(s));
        // A dead pc must not wedge the seat: the subscription guard
        // keys on the pub entry, so drop it on failure — event-driven
        // re-requests can then rebuild from a fresh offer (run-9
        // witness: 23:46:17 re-request blocked by the stale entry).
        if (s != rtc::PeerConnection::State::Failed) return;
        std::shared_ptr<rtc::PeerConnection> dead;
        std::unique_ptr<calls::AudioInPipeline> pipeline;
        OpusDecoder* decoder = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_media) return;
            auto it = m_media->pubs.find(senderSid);
            if (it == m_media->pubs.end()) return;
            pipeline = std::move(it->second.pipeline);
            decoder = it->second.decoder;
            it->second.decoder = nullptr;
            dead = std::move(it->second.pc);
            m_media->pubs.erase(it);
        }
        // Outside the mutex: Flush() runs the utterance callback, which
        // re-enters this adapter and takes m_mutex itself.
        if (pipeline) pipeline->Flush();
        if (decoder) opus_decoder_destroy(decoder);
    });

    pc->onLocalDescription(
        [this, senderSid, mySid, roomType, recvSid, isLive](rtc::Description desc) {
            if (!isLive()) return;
            if (desc.type() != rtc::Description::Type::Answer) {
                // Anything but the answer here is an internal re-offer;
                // the MCU silently rejects offers from a subscriber and
                // re-offers forever (runs 14-17).
                ALOG_WARNING("calls", "Local description of unexpected type "
                            << static_cast<int>(desc.type()) << " for "
                            << senderSid.substr(0, 8) << " not sent");
                return;
            }
            Json::Value payload;
            payload["type"] = "answer";
            payload["to"] = senderSid;
            payload["from"] = mySid;
            payload["sid"] = recvSid;
            payload["roomType"] = roomType;
            payload["payload"]["type"] = "answer";
            payload["payload"]["nick"] =
                GetString(m_config, "username", "animus");
            const std::string answerSdp(desc);
            payload["payload"]["sdp"] = answerSdp;
            std::lock_guard<std::mutex> lock(m_mutex);
            const bool queued =
                m_client ? m_client->SendToSession(senderSid, payload) : false;
            ALOG_INFO("calls", "Answer " << (queued ? "sent" : "NOT queued")
                      << " to " << senderSid.substr(0, 8) << " ("
                      << answerSdp.size()
                      << " bytes; candidates follow on the trickle path)");
    });

    try {
        // setRemoteDescription(offer) generates and applies the answer
        // itself and fires onLocalDescription with it (libdatachannel
        // v0.24.6 auto-negotiation). The explicit setLocalDescription()
        // that used to follow produced a SECOND local description — an
        // offer, parked on the connection and never sent (run-18
        // finding, Nov 24 2026).
        pc->setRemoteDescription(rtc::Description(sdp, "offer"));
    } catch (const std::exception& e) {
        ALOG_WARNING("calls", "Offer handling failed: " << e.what());
    }
}

void CallAdapter::HandleCandidate(const std::string& senderSid,
                                   const Json::Value& data) {
    calls::RemoteCandidate rc;
    if (!calls::ParseRemoteCandidate(data, &rc)) {
        // Every drop is logged: the runs-11-18 handler returned here
        // silently for every candidate the MCU sent.
        ALOG_WARNING("calls", "Candidate from " << senderSid.substr(0, 8)
                    << " dropped: no usable payload.candidate (json type "
                    << JsonTypeName(data["payload"]["candidate"].type())
                    << ")");
        return;
    }
    std::shared_ptr<rtc::PeerConnection> pc;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_media) return;
        auto it = m_media->pubs.find(senderSid);
        if (it == m_media->pubs.end() || !it->second.pc) {
            ALOG_WARNING("calls", "Candidate from "
                        << senderSid.substr(0, 8)
                        << " dropped: no subscription (end-of-candidates="
                        << rc.endOfCandidates << ")");
            return;
        }
        pc = it->second.pc;
    }
    if (rc.endOfCandidates) {
        ALOG_INFO("calls", "Trickle <- " << senderSid.substr(0, 8)
                  << " end-of-candidates");
        return;
    }
    try {
        pc->addRemoteCandidate(rtc::Candidate(rc.candidate, rc.mid));
        ALOG_INFO("calls", "Trickle <- " << senderSid.substr(0, 8)
                  << " mid=" << rc.mid << ' ' << rc.candidate);
    } catch (const std::exception& e) {
        ALOG_WARNING("calls", "Candidate add failed (" << rc.candidate
                    << "): " << e.what());
    }
}

void CallAdapter::RequestOffersFromInCall() {
    std::vector<std::string> publishers;
    std::string mySid;
    int held = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_client) return;
        mySid = m_client->session_id();
        const auto now = std::chrono::steady_clock::now();
        for (const auto& p : m_client->participants()) {
            if (p.session_id == mySid || p.in_call == 0) continue;
            // Ask only for subscriptions we do not already hold. Each
            // request makes the MCU build a fresh subscriber handle and
            // offer, so re-asking for a live one tears working media
            // down (every participants-changed event used to re-ask for
            // all of them). A subscription that never left "New" is not
            // one we hold: re-ask after a grace period.
            if (m_media) {
                auto it = m_media->pubs.find(p.session_id);
                if (it != m_media->pubs.end() && it->second.pc) {
                    const auto st = it->second.pc->state();
                    const bool live =
                        st != rtc::PeerConnection::State::Failed &&
                        st != rtc::PeerConnection::State::Closed;
                    const bool stalled =
                        st == rtc::PeerConnection::State::New &&
                        now - it->second.offerAt > std::chrono::seconds(15);
                    if (live && !stalled) {
                        ++held;
                        continue;
                    }
                }
            }
            publishers.push_back(p.session_id);
        }
    }
    if (publishers.empty()) {
        if (held > 0) {
            ALOG_INFO("calls", "Offers already held for " << held
                      << " participant(s) — none requested");
        }
        return;
    }
    for (const auto& sid : publishers) {
        ALOG_INFO("calls", "Requesting offer from " << sid.substr(0, 8));
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_client) m_client->RequestOffer(sid);
    }
}

void CallAdapter::FeedOpusFrame(const std::string& senderSid,
                                const uint8_t* data, size_t len) {
    MediaState::Publisher* pub = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_media) return;
        auto it = m_media->pubs.find(senderSid);
        if (it == m_media->pubs.end() || !it->second.pipeline ||
            !it->second.decoder) {
            return;
        }
        pub = &it->second;
    }
    // Decode stereo 48k (leg-2: 3,840 B/20ms = 2ch x 960) → pipeline
    // intake deinterleaves + decimates to 16k mono + VAD-segments.
    // frame_size is samples PER CHANNEL of capacity: the buffer holds
    // 2x960 interleaved samples, so the honest capacity is 960. Passing
    // 2x960 let a >20 ms frame write up to twice the buffer — the
    // leading suspect for the teardown heap-corruption family (run 19
    // decoded video bytes here; a 40/60 ms TOC overflows). Larger frames
    // now return BUFFER_TOO_SMALL and land in the rejected counter.
    std::vector<opus_int16> pcm(2 * 960);
    const uint64_t packet = ++g_audioIn.packets;
    const int n = opus_decode(pub->decoder, data,
                              static_cast<opus_int32>(len), pcm.data(),
                              960, 0);
    if (n <= 0) { // DTX/comfort-noise frames decode to nothing
        const uint64_t rejected = ++g_audioIn.rejected;
        if (rejected <= 3) { // a rejected frame is not speech: log the shape
            ALOG_INFO("calls", "audio-in: frame rejected by the Opus "
                      "decoder, code=" << n << " bytes=" << len);
        }
        return;
    }
    ++g_audioIn.decoded;
    if (packet <= 3) { // the shape of what the audio track really delivers
        ALOG_INFO("calls", "audio-in: packet " << packet << " bytes=" << len
                  << " toc=" << static_cast<int>(data[0])
                  << " decoded_samples=" << n);
    }
    // opus_decode returns samples PER CHANNEL (the interleaved buffer
    // holds 2n) and the intake takes samples per channel — the replay
    // tool passes 960 for 20 ms. n/2 fed it half of every packet.
    pub->pipeline->FeedStereo48k(pcm.data(),
                                 static_cast<size_t>(n));
    if (packet % 250 == 0) { // one line per 5 s of audio
        ALOG_INFO("calls", "audio-in: " << packet
                  << " frames to the decoder, decoded="
                  << g_audioIn.decoded.load() << " rejected="
                  << g_audioIn.rejected.load());
    }
}

void CallAdapter::OnUtterance(const calls::Utterance& u,
                              const std::string& room) {
    // Decision 5: every utterance lands as its own thread message —
    // a call segment and a chat message are indistinguishable to the
    // session (one wake path). The room is the call's own, captured when
    // the pipeline was built: trailing speech flushed at leave still lands
    // in the room it was spoken in.
    if (room.empty()) return;
    const std::string prompt =
        "[Call speech from " + u.speaker +
        (u.split_bound ? "] (continuation)\n" : "]\n") + u.text;
    Json::Value meta;
    Json::Value origin;
    origin["user"] = u.speaker;
    origin["modality"] = "voice";
    meta["origin"] = origin;
    meta["room_token"] = room;
    meta["call"] = true;
    meta["utterance_start_s"] = u.start_s;
    meta["utterance_end_s"] = u.end_s;
    meta["delivery"] = "auto"; // voice events carry no tool-reply surface
    ChannelDispatch::Dispatch(m_ctx, &m_dispatchRt, "peer:" + room,
                               prompt, "default", JsonCompact(meta));
}

std::string CallAdapter::TranscribePcm16k(const std::vector<int16_t>& pcm,
                                          const std::string& sttUrl) {
    // The endpoint is the one the pipeline captured at build time, not a
    // lookup on the media state: the leave-time flush runs after
    // TearDownMedia has detached it.
    if (sttUrl.empty()) return ""; // no STT configured: audio drops here

    // WAV-wrap the 16k mono PCM (whisper's multipart contract — the
    // server converts via ffmpeg; the vault/attachment path is not
    // involved in the media lane).
    std::string wav;
    wav.reserve(44 + pcm.size() * 2);
    const uint32_t dataLen = static_cast<uint32_t>(pcm.size() * 2);
    const auto le32 = [](uint32_t v) {
        return std::string(reinterpret_cast<const char*>(&v), 4);
    };
    const auto le16 = [](uint16_t v) {
        return std::string(reinterpret_cast<const char*>(&v), 2);
    };
    wav += "RIFF" + le32(36 + dataLen) + "WAVE";
    wav += "fmt " + le32(16) + le16(1) + le16(1) + le32(16000) +
           le32(32000) + le16(2) + le16(16);
    wav += "data" + le32(dataLen);
    wav.append(reinterpret_cast<const char*>(pcm.data()), dataLen);

    const std::string boundary = "----animus-kernel-7b3d324";
    std::string body;
    body.reserve(wav.size() + 256);
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; "
            "filename=\"utterance.wav\"\r\n";
    body += "Content-Type: audio/wav\r\n\r\n";
    body += wav;
    body += "\r\n--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; "
            "name=\"response_format\"\r\n\r\njson\r\n";
    body += "--" + boundary + "--\r\n";

    HttpClient::Request req;
    req.method = "POST";
    req.url = sttUrl + "/inference";
    req.headers["Content-Type"] =
        "multipart/form-data; boundary=" + boundary;
    req.body = std::move(body);
    const auto resp = m_ctx.httpClient.Execute(req);
    if (resp.status_code != 200) {
        // Status 0 is a transport verdict, not a server answer, and the
        // named cause rides resp.error — the SSRF guard that silenced
        // every transcription in run 22 reads "URL blocked by security
        // policy" here. Logging the status alone says only "HTTP 0".
        const std::string why =
            resp.error.empty() ? std::string() : " - " + resp.error;
        ALOG_WARNING("calls", "audio-in: STT refused a "
                    << (pcm.size() / 1600) / 10.0 << " s utterance (HTTP "
                    << resp.status_code << why << ")");
        return "";
    }
    const Json::Value j = ParseJson(resp.body);
    return j.get("text", "").asString();
}

void CallAdapter::TearDownMedia() {
    std::shared_ptr<MediaState> media;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        media = std::move(m_media);
    }
    if (!media) return;
    // Flush pipelines first — trailing speech emits before teardown. The
    // media state is detached BEFORE this flush on purpose: that is what
    // makes in-flight packet callbacks bail (FeedOpusFrame), so nothing
    // feeds a pipeline while it is being flushed. The flush must therefore
    // not read the adapter's call state; it runs on what its pipeline
    // captured (STT endpoint, room).
    for (auto& [sid, pub] : media->pubs) {
        if (pub.pipeline) pub.pipeline->Flush();
        if (pub.pc) {
            try { pub.pc->close(); } catch (...) {}
        }
        if (pub.decoder) opus_decoder_destroy(pub.decoder);
    }
}

} // namespace animus::kernel

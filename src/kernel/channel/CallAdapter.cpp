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
#include "animus_kernel/tools/HttpClient.h"

#include <rtc/rtc.hpp>
#include <fvad.h>
#include <opus.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <set>

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
        "turn_server", "turn_user", "turn_password"};
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

} // namespace

// MediaState: one peer connection per publishing session (leg-2 shape),
// one Opus decoder + one AudioInPipeline per publisher. The pipelines'
// callbacks (VAD/STT/utterance) are wired by the adapter, not the state.
// Defined before JoinCall — the first user.
struct CallAdapter::MediaState {
    struct Publisher {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::Track> track;
        OpusDecoder* decoder = nullptr; // 48 kHz stereo decode
        std::unique_ptr<calls::AudioInPipeline> pipeline;
        std::string userId; // Tier-0 speaker label
    };
    std::map<std::string, Publisher> pubs; // by signaling session id
    std::string stt_url;
};

bool CallAdapter::JoinCall(const std::string& roomToken, std::string* error) {
    // MediaState (defined above): one peer connection + decoder +
    // pipeline per publishing session; created with the call.
    std::string authHeader;
    std::string nextcloudUrl;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_client) {
            if (error) *error = "already in a call (one live call per seat in v1; "
                                "call.leave first)";
            return false;
        }
        nextcloudUrl = GetString(m_config, "nextcloud_url");
        const std::string user = GetString(m_config, "username");
        const std::string pass = GetString(m_config, "app_password");
        if (nextcloudUrl.empty() || user.empty() || pass.empty()) {
            if (error) *error = "adapter config incomplete";
            return false;
        }
        authHeader = "Basic " + Base64EncodeStr(user + ":" + pass);
    }
    if (roomToken.empty()) {
        if (error) *error = "room token required";
        return false;
    }

    // 1) OCS settings → hello-v2 JWT + signaling server URL (v3 route —
    //    v1 is gone in Talk 25+, live-proven #335).
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
    const std::string token =
        settings["helloAuthParams"]["2.0"]["token"].asString();
    std::string server = GetString(settings, "server");
    if (token.empty() || server.empty()) {
        if (error) *error = "no hello-v2 token or signaling server in settings";
        return false;
    }

    // 2) REST room join → Talk room session (the signaling join pins THIS;
    //    the signaling session id answers no_such_room — live-proven, Q9).
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
    const std::string roomSession =
        OcsData(ParseJson(joinResp.body)).get("sessionId", "").asString();
    if (roomSession.empty()) {
        if (error) *error = "no room session from REST join";
        return false;
    }

    // 3) Signaling client in seat mode, joined to the room.
    calls::SignalingConfig cfg;
    cfg.server_url = server;
    // Normalize: settings server is https://host/standalone-signaling/;
    // the WS endpoint appends /spreed (ParseServerUrl maps https→wss).
    while (!cfg.server_url.empty() && cfg.server_url.back() == '/') {
        cfg.server_url.pop_back();
    }
    cfg.server_url += "/spreed";
    cfg.backend_url = nextcloudUrl;
    cfg.hello_v2_token = token;
    cfg.auth_mode = SignalingAuthMode::HelloV2;
    cfg.room_id = roomToken;
    cfg.room_session_id = roomSession;
    cfg.reconnect_base_s = 1.0;
    cfg.reconnect_max_s = 30.0;
    cfg.hello_timeout_s = 10.0;

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

    // 4) In-call flags up: the seat is attending.
    m_client->SetInCall(1);
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
    if (type != "message") return; // offers/candidates arrive as messages
    const Json::Value& msg = frame["message"];
    const std::string senderSid =
        msg["sender"]["sessionid"].asString();
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
    const std::string sdp = data["payload"]["sdp"].asString();
    if (sdp.empty() || senderSid.empty()) return;

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
            rtcCfg.iceServers.emplace_back(
                host, service, GetString(m_config, "turn_user", ""),
                GetString(m_config, "turn_password", ""));
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
    pub.userId = publisherUserId.empty() ? senderSid : publisherUserId;

    // The leg-2 anti-flatten evidence lives in the pipeline's intake;
    // the decoder is created stereo (Talk publishes Opus 48k stereo).
    int opusErr = 0;
    pub.decoder = opus_decoder_create(48000, 2, &opusErr);
    {
        calls::AudioInConfig pipeCfg; // decision-3 defaults
        std::string speaker = pub.userId;
        std::string sttUrl;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            sttUrl = m_media->stt_url;
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
                return fvad_process(v, frame, samples) == 1;
            },
            [self, sttUrl](const std::vector<int16_t>& pcm) -> std::string {
                return self->TranscribePcm16k(pcm);
            },
            [self](const calls::Utterance& u) { self->OnUtterance(u); });
    }

    pc->onTrack([this, senderSid](std::shared_ptr<rtc::Track> track) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_media) return;
        auto it = m_media->pubs.find(senderSid);
        if (it == m_media->pubs.end() || !track) return;
        it->second.track = track;
        // RTP → depacketized Opus payloads (48 kHz clock rate).
        track->setMediaHandler(std::make_shared<rtc::OpusRtpDepacketizer>());
        auto decoder = it->second.decoder;
        track->onMessage(
            [this, senderSid, decoder](rtc::binary b) {
                if (!b.empty() && decoder) {
                    FeedOpusFrame(senderSid,
                                  reinterpret_cast<const uint8_t*>(b.data()),
                                  b.size());
                }
            },
            nullptr);
        ALOG_INFO("calls", "Media track up from " << senderSid.substr(0, 8));
    });

    pc->onLocalCandidate([this, senderSid, mySid, roomType, recvSid](
                             rtc::Candidate cand) {
        Json::Value payload;
        payload["type"] = "candidate";
        payload["sid"] = recvSid;
        payload["roomType"] = roomType;
        payload["payload"]["candidate"] = std::string(cand);
        payload["payload"]["sdpMid"] = cand.mid();
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_client) m_client->SendToSession(senderSid, payload);
    });

    pc->onStateChange([senderSid](rtc::PeerConnection::State s) {
        if (s == rtc::PeerConnection::State::Failed ||
            s == rtc::PeerConnection::State::Closed) {
            ALOG_WARNING("calls", "Peer connection to "
                        << senderSid.substr(0, 8) << " state "
                        << static_cast<int>(s));
        }
    });

    try {
        pc->setRemoteDescription(rtc::Description(sdp, "offer"));
        pc->setLocalDescription(); // answer
        const auto answer = pc->localDescription();
        Json::Value payload;
        payload["type"] = "answer";
        payload["to"] = senderSid;
        payload["from"] = mySid;
        payload["sid"] = recvSid;
        payload["roomType"] = roomType;
        payload["payload"]["type"] = "answer";
        payload["payload"]["nick"] =
            GetString(m_config, "username", "animus");
        payload["payload"]["sdp"] = std::string(*answer);
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_client) m_client->SendToSession(senderSid, payload);
        ALOG_INFO("calls", "Answered offer from " << senderSid.substr(0, 8));
    } catch (const std::exception& e) {
        ALOG_WARNING("calls", "Offer handling failed: " << e.what());
    }
}

void CallAdapter::HandleCandidate(const std::string& senderSid,
                                   const Json::Value& data) {
    std::string cand = data["payload"]["candidate"].asString();
    if (cand.empty()) cand = data["payload"]["candiate"].asString(); // leg-2: seen typo
    if (cand.empty() || senderSid.empty()) return;
    std::shared_ptr<rtc::PeerConnection> pc;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_media) return;
        auto it = m_media->pubs.find(senderSid);
        if (it == m_media->pubs.end() || !it->second.pc) return;
        pc = it->second.pc;
    }
    try {
        pc->addRemoteCandidate(rtc::Candidate(cand));
    } catch (const std::exception& e) {
        ALOG_WARNING("calls", "Candidate add failed: " << e.what());
    }
}

void CallAdapter::RequestOffersFromInCall() {
    std::vector<std::string> publishers;
    std::string mySid;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_client) return;
        mySid = m_client->session_id();
        for (const auto& p : m_client->participants()) {
            if (p.session_id != mySid && (p.in_call & 1)) {
                publishers.push_back(p.session_id);
            }
        }
    }
    for (const auto& sid : publishers) {
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
    std::vector<opus_int16> pcm(2 * 960);
    const int n = opus_decode(pub->decoder, data,
                              static_cast<opus_int32>(len), pcm.data(),
                              2 * 960, 0);
    if (n <= 0) return; // DTX/comfort-noise frames decode to nothing
    pub->pipeline->FeedStereo48k(pcm.data(),
                                 static_cast<size_t>(n) / 2);
}

void CallAdapter::OnUtterance(const calls::Utterance& u) {
    // Decision 5: every utterance lands as its own thread message —
    // a call segment and a chat message are indistinguishable to the
    // session (one wake path).
    std::string room;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        room = m_activeRoom;
    }
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

std::string CallAdapter::TranscribePcm16k(
        const std::vector<int16_t>& pcm) {
    std::string sttUrl;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_media) return "";
        sttUrl = m_media->stt_url;
    }
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
    if (resp.status_code != 200) return "";
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
    // Flush pipelines first — trailing speech emits before teardown.
    for (auto& [sid, pub] : media->pubs) {
        if (pub.pipeline) pub.pipeline->Flush();
        if (pub.pc) {
            try { pub.pc->close(); } catch (...) {}
        }
        if (pub.decoder) opus_decoder_destroy(pub.decoder);
    }
}

} // namespace animus::kernel

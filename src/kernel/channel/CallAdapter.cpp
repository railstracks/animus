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
#include "animus_kernel/calls/SignalingClient.h"
#include "animus_kernel/tools/HttpClient.h"

#include <chrono>
#include <condition_variable>
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
        "join_policy", "agent_id"};
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

bool CallAdapter::JoinCall(const std::string& roomToken, std::string* error) {
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
    client->SetFrameCallback([](const Json::Value& frame) {
        // v1: event routing to the thread path rides the audio-in ticket.
        (void)frame;
    });

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

} // namespace animus::kernel

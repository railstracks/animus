// SignalingClient — standalone-signaling (Nextcloud Talk) WS client for the
// calls lane (#335). Transport/auth/join/resume/reconnect/participants;
// no policy (adapter #336), no media (libdatachannel lane).
//
// Protocol evidence: Phase 0 leg 1 (internal HMAC auth, room join) and
// leg 2 (requestoffer/message relay) against production signaling 2.1.1.

#include "animus_kernel/calls/SignalingClient.h"

#include <drogon/HttpRequest.h>
#include <drogon/WebSocketClient.h>
#include <json/json.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <trantor/net/EventLoop.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace animus::kernel::calls {
namespace {

std::string ToJsonString(const Json::Value& v) {
    Json::StreamWriterBuilder w;
    w.settings_["indentation"] = "";
    return Json::writeString(w, v);
}

bool ParseJsonDocument(const std::string& text, Json::Value* out) {
    Json::CharReaderBuilder rb;
    std::istringstream ss(text);
    std::string err;
    return Json::parseFromStream(rb, ss, out, &err);
}

std::string HexLower(const unsigned char* data, size_t len) {
    static const char kHex[] = "0123456789abcdef";
    std::string out(len * 2, '0');
    for (size_t i = 0; i < len; ++i) {
        out[2 * i] = kHex[data[i] >> 4];
        out[2 * i + 1] = kHex[data[i] & 0xF];
    }
    return out;
}

std::string RandomHex(size_t bytes) {
    static const char kHex[] = "0123456789abcdef";
    std::string out(bytes * 2, '0');
    unsigned char buf[64];
    size_t done = 0;
    while (done < bytes) {
        const size_t n = std::min(bytes - done, sizeof(buf));
        if (RAND_bytes(buf, static_cast<int>(n)) != 1) {
            // Deterministic fallback must not look random to the server:
            // fail loudly instead of sending a guessable nonce.
            std::cerr << "[calls-sig] RAND_bytes failed — aborting hello\n";
            return std::string();
        }
        for (size_t i = 0; i < n; ++i) {
            out[(done + i) * 2] = kHex[buf[i] >> 4];
            out[(done + i) * 2 + 1] = kHex[buf[i] & 0xF];
        }
        done += n;
    }
    return out;
}

} // namespace

const char* SignalingClient::StateName(State s) {
    switch (s) {
    case State::Idle: return "Idle";
    case State::Connecting: return "Connecting";
    case State::Authenticating: return "Authenticating";
    case State::Joining: return "Joining";
    case State::Joined: return "Joined";
    case State::ReconnectWait: return "ReconnectWait";
    case State::Closed: return "Closed";
    }
    return "?";
}

// ---- pure protocol helpers ----

SignalingClient::ServerEndpoint SignalingClient::ParseServerUrl(
    const std::string& url) {
    ServerEndpoint ep;
    std::string rest = url;
    const auto scheme_pos = url.find("://");
    if (scheme_pos == std::string::npos) {
        rest = "wss://" + url;
    } else {
        std::string scheme = url.substr(0, scheme_pos);
        if (scheme == "http") scheme = "ws";
        if (scheme == "https") scheme = "wss";
        if (scheme != "ws" && scheme != "wss") scheme = "wss";
        rest = scheme + "://" + url.substr(scheme_pos + 3);
    }
    const auto path_pos = rest.find('/', rest.find("://") + 3);
    if (path_pos == std::string::npos) {
        ep.client_url = rest;
        ep.path = "/";
    } else {
        ep.client_url = rest.substr(0, path_pos);
        ep.path = rest.substr(path_pos);
    }
    return ep;
}

std::string SignalingClient::ComputeInternalToken(const std::string& random,
                                                  const std::string& secret) {
    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int mac_len = 0;
    if (!HMAC(EVP_sha256(), secret.data(),
              static_cast<int>(secret.size()),
              reinterpret_cast<const unsigned char*>(random.data()),
              random.size(), mac, &mac_len)) {
        return std::string();
    }
    return HexLower(mac, mac_len);
}

std::string SignalingClient::BuildHelloInternal(const std::string& random,
                                                const std::string& token,
                                                const std::string& backend_url,
                                                const std::string& id) {
    Json::Value v;
    v["id"] = id;
    v["type"] = "hello";
    v["hello"]["version"] = "1.0";
    v["hello"]["auth"]["type"] = "internal";
    v["hello"]["auth"]["params"]["random"] = random;
    v["hello"]["auth"]["params"]["token"] = token;
    v["hello"]["auth"]["params"]["backend"] = backend_url;
    return ToJsonString(v);
}

std::string SignalingClient::BuildRoomJoin(const std::string& room_id,
                                           const std::string& session_id,
                                           const std::string& id) {
    Json::Value v;
    v["id"] = id;
    v["type"] = "room";
    v["room"]["roomid"] = room_id;
    v["room"]["sessionid"] = session_id;
    return ToJsonString(v);
}

std::string SignalingClient::BuildInCall(int flags, const std::string& id) {
    Json::Value v;
    v["id"] = id;
    v["type"] = "internal";
    v["internal"]["type"] = "incall";
    v["internal"]["incall"]["incall"] = flags;
    return ToJsonString(v);
}

std::string SignalingClient::BuildBye(const std::string& id) {
    Json::Value v;
    v["id"] = id;
    v["type"] = "bye";
    v["bye"] = Json::Value(Json::objectValue);
    return ToJsonString(v);
}

std::string SignalingClient::BuildSessionMessage(const std::string& to_session,
                                                 const Json::Value& data,
                                                 const std::string& id) {
    Json::Value v;
    v["id"] = id;
    v["type"] = "message";
    v["message"]["recipient"]["type"] = "session";
    v["message"]["recipient"]["sessionid"] = to_session;
    v["message"]["data"] = data;
    return ToJsonString(v);
}

double SignalingClient::BackoffSeconds(int attempt, double base_s,
                                       double max_s) {
    if (base_s <= 0) return 0;
    if (max_s < base_s) max_s = base_s;
    if (attempt < 0) attempt = 0;
    double d = base_s;
    for (int i = 0; i < attempt; ++i) {
        d *= 2.0;
        if (d >= max_s) return max_s;
    }
    return std::min(d, max_s);
}

void SignalingClient::ApplyParticipantEvent(
    const Json::Value& event, std::map<std::string, Participant>& ps) {
    if (!event.isObject()) return;
    const std::string target = event.get("target", "").asString();

    static const auto GetField = [](const Json::Value& o, const char* a,
                                    const char* b) -> std::string {
        if (o.isObject()) {
            if (o.isMember(a)) return o.get(a, "").asString();
            if (o.isMember(b)) return o.get(b, "").asString();
        }
        return std::string();
    };

    if (target == "participants") {
        const Json::Value& users = event["update"]["users"];
        if (users.isArray()) {
            for (const auto& u : users) {
                std::string sid = GetField(u, "sessionId", "sessionid");
                if (sid.empty()) continue;
                Participant p;
                p.session_id = sid;
                p.user_id = GetField(u, "userId", "userid");
                p.in_call = u.get("inCall", u.get("incall", 0)).asInt64();
                ps[sid] = p;
            }
        }
        return;
    }

    if (target == "room") {
        const std::string type = event.get("type", "").asString();
        if (type == "join") {
            const Json::Value& j = event["join"];
            auto add = [&](const Json::Value& e) {
                const std::string sid = GetField(e, "sessionid", "sessionId");
                if (sid.empty()) return;
                Participant& p = ps[sid];
                p.session_id = sid;
                const std::string uid = GetField(e, "userid", "userId");
                if (!uid.empty()) p.user_id = uid;
            };
            if (j.isArray()) {
                for (const auto& e : j) add(e);
            } else if (j.isObject()) {
                add(j);
            }
        } else if (type == "leave") {
            const Json::Value& l = event["leave"];
            auto rm = [&](const Json::Value& e) {
                const std::string sid =
                    e.isString() ? e.asString() : GetField(e, "sessionid", "sessionId");
                if (!sid.empty()) ps.erase(sid);
            };
            if (l.isArray()) {
                for (const auto& e : l) rm(e);
            } else if (l.isObject() || l.isString()) {
                rm(l);
            }
        }
    }
}

// ---- implementation ----

struct SignalingClient::Impl {
    explicit Impl(SignalingConfig c) : cfg(std::move(c)) {}

    SignalingConfig cfg;
    std::atomic<State> state{State::Idle};
    std::atomic<bool> started{false};
    std::thread worker;
    trantor::EventLoop* loop = nullptr; // valid while worker runs

    std::mutex mu; // participants + session_id
    std::map<std::string, Participant> participants;
    std::string session_id;

    drogon::WebSocketClientPtr ws;
    drogon::WebSocketConnectionPtr conn;
    int reconnect_attempt = 0;
    uint64_t next_id = 1;
    bool join_acked = false;
    FrameCallback frame_cb;
    StateChangedCallback state_cb;

    void SetState(State s) {
        state.store(s);
        if (state_cb) state_cb(s);
    }
    std::string NewId() { return "sig-" + std::to_string(next_id++); }
    bool SendText(const std::string& text) {
        if (conn) {
            conn->send(text);
            return true;
        }
        return false;
    }

    void Connect() {
        if (state.load() == State::Closed) return;
        join_acked = false;
        conn.reset();
        SetState(State::Connecting);

        const ServerEndpoint ep = ParseServerUrl(cfg.server_url);
        ws = drogon::WebSocketClient::newWebSocketClient(ep.client_url, loop);

        ws->setMessageHandler(
            [this](std::string&& message,
                   const drogon::WebSocketClientPtr&,
                   const drogon::WebSocketMessageType& type) {
                if (type != drogon::WebSocketMessageType::Text) return;
                Json::Value frame;
                if (!ParseJsonDocument(message, &frame)) {
                    std::cerr << "[calls-sig] unparseable frame ("
                              << message.size() << " B)\n";
                    return;
                }
                HandleFrame(frame);
            });

        ws->setConnectionClosedHandler(
            [this](const drogon::WebSocketClientPtr&) {
                conn.reset();
                if (state.load() != State::Closed &&
                    state.load() != State::Idle) {
                    std::cerr << "[calls-sig] connection closed — "
                                 "reconnecting\n";
                    ScheduleReconnect();
                }
            });

        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Get);
        req->setPath(ep.path);

        ws->connectToServer(
            req,
            [this](drogon::ReqResult r, const drogon::HttpResponsePtr&,
                       const drogon::WebSocketClientPtr& wsp) {
                if (r != drogon::ReqResult::Ok) {
                    std::cerr << "[calls-sig] connect failed\n";
                    ScheduleReconnect();
                    return;
                }
                conn = wsp->getConnection();
                const double deadline =
                    cfg.hello_timeout_s > 0 ? cfg.hello_timeout_s : 10.0;
                loop->runAfter(deadline, [this] {
                    if (!join_acked &&
                        (state.load() == State::Connecting ||
                         state.load() == State::Authenticating ||
                         state.load() == State::Joining)) {
                        std::cerr << "[calls-sig] hello/join deadline — "
                                     "recycling connection\n";
                        if (conn) conn->shutdown();
                        ScheduleReconnect();
                    }
                });
            });
    }

    void ScheduleReconnect() {
        if (state.load() == State::Closed) return;
        const double d = BackoffSeconds(reconnect_attempt++,
                                        cfg.reconnect_base_s,
                                        cfg.reconnect_max_s);
        SetState(State::ReconnectWait);
        loop->runAfter(d, [this] { Connect(); });
    }

    void SendHello() {
        const std::string random = RandomHex(32);
        if (random.empty()) {
            ScheduleReconnect();
            return;
        }
        const std::string token = ComputeInternalToken(random, cfg.internal_secret);
        SetState(State::Authenticating);
        SendText(BuildHelloInternal(random, token, cfg.backend_url, NewId()));
    }

    void HandleFrame(const Json::Value& frame) {
        const std::string type = frame.get("type", "").asString();

        if (type == "welcome") {
            SendHello();
            if (frame_cb) frame_cb(frame);
            return;
        }
        if (type == "ping") {
            SendText("{\"type\":\"pong\"}");
            return;
        }
        if (type == "hello") {
            if (frame.isMember("hello")) {
                {
                    std::lock_guard<std::mutex> lk(mu);
                    session_id =
                        frame["hello"].get("sessionid", "").asString();
                }
                SetState(State::Joining);
                std::string resume_id;
                {
                    std::lock_guard<std::mutex> lk(mu);
                    resume_id = session_id;
                }
                SendText(BuildRoomJoin(cfg.room_id, resume_id, NewId()));
            }
            if (frame_cb) frame_cb(frame);
            return;
        }
        if (type == "room") {
            if (frame.isMember("room")) {
                join_acked = true;
                reconnect_attempt = 0;
                SetState(State::Joined);
            }
            if (frame_cb) frame_cb(frame);
            return;
        }
        if (type == "error") {
            // Fatal during handshake (bad auth, room rejected): recycle.
            if (state.load() == State::Authenticating ||
                state.load() == State::Joining) {
                std::cerr << "[calls-sig] handshake error frame\n";
                if (conn) conn->shutdown();
                ScheduleReconnect();
            }
            if (frame_cb) frame_cb(frame);
            return;
        }
        if (type == "event") {
            {
                std::lock_guard<std::mutex> lk(mu);
                ApplyParticipantEvent(frame.get("event", Json::Value()),
                                      participants);
            }
            if (frame_cb) frame_cb(frame);
            return;
        }
        if (frame_cb) frame_cb(frame);
    }
};

SignalingClient::SignalingClient(SignalingConfig cfg)
    : impl_(std::make_unique<Impl>(std::move(cfg))) {}

SignalingClient::~SignalingClient() {
    if (impl_->started.load()) Close();
}

void SignalingClient::Start() {
    if (impl_->cfg.server_url.empty() || impl_->cfg.room_id.empty() ||
        impl_->cfg.internal_secret.empty()) {
        std::cerr << "[calls-sig] Start refused: incomplete config\n";
        impl_->SetState(State::Closed);
        return;
    }
    if (impl_->started.exchange(true)) return;
    impl_->worker = std::thread([this] {
        trantor::EventLoop loop;
        impl_->loop = &loop;
        loop.queueInLoop([this] { impl_->Connect(); });
        loop.loop();
        impl_->loop = nullptr;
    });
}

void SignalingClient::Close() {
    impl_->state.store(State::Closed);
    if (!impl_->started.exchange(false)) return;
    trantor::EventLoop* loop = impl_->loop;
    if (loop) {
        loop->queueInLoop([loop, this] {
            if (impl_->conn) {
                impl_->SendText(BuildBye(impl_->NewId()));
                impl_->conn->shutdown();
                impl_->conn.reset();
            }
            loop->queueInLoop([loop] { loop->quit(); });
        });
    }
    if (impl_->worker.joinable()) impl_->worker.join();
}

SignalingClient::State SignalingClient::state() const {
    return impl_->state.load();
}

std::string SignalingClient::session_id() const {
    std::lock_guard<std::mutex> lk(impl_->mu);
    return impl_->session_id;
}

std::vector<SignalingClient::Participant> SignalingClient::participants()
    const {
    std::lock_guard<std::mutex> lk(impl_->mu);
    std::vector<Participant> out;
    out.reserve(impl_->participants.size());
    for (const auto& [_, p] : impl_->participants) out.push_back(p);
    return out;
}

bool SignalingClient::SetInCall(int flags) {
    if (impl_->state.load() != State::Joined) return false;
    trantor::EventLoop* loop = impl_->loop;
    if (!loop) return false;
    loop->queueInLoop([this, flags] {
        impl_->SendText(BuildInCall(flags, impl_->NewId()));
    });
    return true;
}

bool SignalingClient::SendToSession(const std::string& session_id,
                                    const Json::Value& data) {
    if (impl_->state.load() != State::Joined) return false;
    trantor::EventLoop* loop = impl_->loop;
    if (!loop) return false;
    loop->queueInLoop([this, session_id, data] {
        impl_->SendText(BuildSessionMessage(session_id, data, impl_->NewId()));
    });
    return true;
}

bool SignalingClient::RequestOffer(const std::string& session_id) {
    Json::Value data;
    data["type"] = "requestoffer";
    data["roomType"] = "video";
    return SendToSession(session_id, data);
}

void SignalingClient::SetFrameCallback(FrameCallback cb) {
    impl_->frame_cb = std::move(cb);
}

void SignalingClient::SetStateChangedCallback(StateChangedCallback cb) {
    impl_->state_cb = std::move(cb);
}

} // namespace animus::kernel::calls

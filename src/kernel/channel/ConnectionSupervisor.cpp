#include "animus_kernel/ConnectionSupervisor.h"
#include "animus_kernel/Log.h"

#include <drogon/WebSocketClient.h>
#include <drogon/HttpRequest.h>
#include <trantor/net/EventLoop.h>

#include <algorithm>
#include <random>

namespace animus::kernel {

namespace {

std::string ReqResultReason(drogon::ReqResult r) {
    switch (r) {
        case drogon::ReqResult::Ok: return "ok";
        case drogon::ReqResult::BadResponse: return "bad response";
        case drogon::ReqResult::NetworkError: return "network error";
        case drogon::ReqResult::BadServerAddress: return "bad server address";
        case drogon::ReqResult::Timeout: return "timeout";
        default: return "unknown";
    }
}

}  // namespace

ConnectionSupervisor::ConnectionSupervisor() = default;
ConnectionSupervisor::~ConnectionSupervisor() { RequestStop(); }

const char* ConnectionSupervisor::StateName(State s) {
    switch (s) {
        case State::Disabled: return "disabled";
        case State::Connecting: return "connecting";
        case State::Connected: return "connected";
        case State::Backoff: return "backoff";
        case State::Error: return "error";
    }
    return "?";
}

State ConnectionSupervisor::state() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

int ConnectionSupervisor::consecutive_failures() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_consecutiveFailures;
}

std::string ConnectionSupervisor::last_error() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastError;
}

void ConnectionSupervisor::Transition(State to, const std::string& reason) {
    State from;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        from = m_state;
        if (from == to) {
            // Still log repeats of the same state (e.g. every retry attempt)
            ALOG_INFO("conn-supervisor",
                      "[" << m_cfg.name << "] " << StateName(to)
                          << " (repeat) reason=" << reason);
            return;
        }
        m_state = to;
        if (to == State::Error) m_lastError = reason;
    }
    // Every transition logs — the negative of #60, where the initial
    // connect failure produced no line at all.
    ALOG_INFO("conn-supervisor",
              "[" << m_cfg.name << "] " << StateName(from) << "→" << StateName(to)
                  << " reason=" << reason);
}

void ConnectionSupervisor::Run(const Config& cfg, Callbacks cbs) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cfg = cfg;
        m_cbs = std::move(cbs);
        m_consecutiveFailures = 0;
        m_lastError.clear();
        m_stopRequested = false;
        m_fatal = false;
    }

    if (cfg.host.empty() || cfg.name.empty()) {
        ALOG_ERROR("conn-supervisor",
                   "[" << cfg.name << "] config error: "
                       << (cfg.host.empty() ? "host" : "name") << " is empty — terminal, not retrying");
        Transition(State::Error, "config: empty host or name");
        return;
    }

    trantor::EventLoop loop;
    m_loop = &loop;

    ALOG_INFO("conn-supervisor",
              "[" << cfg.name << "] starting: host=" << cfg.host << cfg.path);

    Transition(State::Connecting, "start");
    loop.runInLoop([this] { Connect(); });
    StartWatchdog();
    loop.loop();

    // Loop exited: either RequestStop or FatalError.
    m_loop = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ws = nullptr;
    }
    State endState = m_fatal.load() ? State::Error : State::Disabled;
    if (endState == State::Error) {
        std::lock_guard<std::mutex> lock(m_mutex);
        ALOG_ERROR("conn-supervisor",
                   "[" << m_cfg.name << "] terminated: " << m_lastError);
    }
    Transition(endState, m_fatal.load() ? "fatal" : "stopped");
}

void ConnectionSupervisor::Connect() {
    if (m_stopRequested.load() || m_fatal.load()) return;

    auto wsPtr = drogon::WebSocketClient::newWebSocketClient(m_cfg.host, m_loop);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ws = wsPtr.get();
    }

    wsPtr->setMessageHandler(
        [this](std::string&& message,
               const drogon::WebSocketClientPtr&,
               const drogon::WebSocketMessageType& type) {
            if (type != drogon::WebSocketMessageType::Text) return;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_lastEvent = std::chrono::steady_clock::now();
            }
            if (m_cbs.on_message) m_cbs.on_message(message);
        });

    wsPtr->setConnectionClosedHandler(
        [this](const drogon::WebSocketClientPtr&) {
            if (m_stopRequested.load()) return;
            Transition(State::Backoff, "connection closed");
            if (m_cbs.on_closed) m_cbs.on_closed("connection closed");
            ScheduleReconnect("closed");
        });

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(m_cfg.path);
    for (const auto& [k, v] : m_cfg.query) req->setParameter(k, v);
    for (const auto& [k, v] : m_cfg.headers) req->addHeader(k, v);

    Transition(State::Connecting, "connect attempt");

    wsPtr->connectToServer(
        req,
        [this, wsPtr](drogon::ReqResult r,
                      const drogon::HttpResponsePtr&,
                      const drogon::WebSocketClientPtr&) {
            if (r != drogon::ReqResult::Ok) {
                if (m_stopRequested.load()) return;
                Transition(State::Backoff, std::string("connect failed: ") + ReqResultReason(r));
                ScheduleReconnect(ReqResultReason(r));
                return;
            }

            auto conn = wsPtr->getConnection();
            if (!conn) {
                Transition(State::Backoff, "connected but no connection object");
                ScheduleReconnect("no connection object");
                return;
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_consecutiveFailures = 0;
                m_lastEvent = std::chrono::steady_clock::now();
            }
            Transition(State::Connected, "handshake ok");
            conn->setPingMessage("",
                std::chrono::duration_cast<std::chrono::seconds>(m_cfg.pingInterval));
            if (m_cbs.on_connected) m_cbs.on_connected();
        });
}

void ConnectionSupervisor::ScheduleReconnect(const std::string& reason) {
    if (m_stopRequested.load() || m_fatal.load()) return;

    int n;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_consecutiveFailures++;
        n = m_consecutiveFailures;
    }

    // Exponential backoff with full jitter. Base 1s ×2^n, capped.
    auto base = std::min<long long>(
        m_cfg.backoffCap.count(),
        m_cfg.backoffBase.count() * (1LL << std::min(n - 1, 6)));
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<long long> dist(0, base);
    const double delaySec = static_cast<double>(dist(rng)) / 1000.0;

    ALOG_INFO("conn-supervisor",
              "[" << m_cfg.name << "] reconnect #" << n << " (" << reason
                  << ") in " << static_cast<int>(delaySec * 1000) << " ms");

    m_reconnectTimer = m_loop->runAfter(delaySec, [this] {
        if (m_stopRequested.load() || m_fatal.load()) return;
        Connect();
    });
}

void ConnectionSupervisor::StartWatchdog() {
    m_loop->runEvery(5.0, [this] {
        if (m_stopRequested.load() || m_fatal.load()) return;

        std::chrono::steady_clock::time_point lastEvent;
        State st;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            lastEvent = m_lastEvent;
            st = m_state;
        }
        if (st == State::Connected &&
            (std::chrono::steady_clock::now() - lastEvent) > m_cfg.stallTimeout) {
            ForceReconnect("stall: no events within timeout");
        }
    });
}

void ConnectionSupervisor::ForceReconnect(const std::string& reason) {
    ALOG_INFO("conn-supervisor", "[" << m_cfg.name << "] forcing reconnect: " << reason);
    // Cancel pending reconnect (if any) and drop the connection; the
    // closed-handler + ScheduleReconnect path drives the retry. We never
    // exit the loop from here — the watchdog heals, it does not kill.
    m_loop->cancelTimer(m_reconnectTimer);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastEvent = std::chrono::steady_clock::now();  // reset so we do not loop instantly
    }
    if (m_ws) m_ws->stop();
}

void ConnectionSupervisor::RequestStop() {
    if (m_stopRequested.exchange(true)) return;
    trantor::EventLoop* loop = m_loop;
    if (!loop) return;
    loop->queueInLoop([this, loop] {
        // Cancel any pending reconnect so the loop can drain and quit.
        loop->cancelTimer(m_reconnectTimer);
        if (m_ws) m_ws->stop();
        loop->quit();
    });
}

void ConnectionSupervisor::FatalError(const std::string& reason) {
    if (m_fatal.exchange(true)) return;
    Transition(State::Error, reason);
    trantor::EventLoop* loop = m_loop;
    if (!loop) return;
    loop->queueInLoop([this, loop] {
        loop->cancelTimer(m_reconnectTimer);
        if (m_ws) m_ws->stop();
        loop->quit();
    });
}

bool ConnectionSupervisor::SendText(const std::string& text) {
    if (state() != State::Connected || !m_loop) return false;
    if (m_loop->isInLoopThread()) {
        // on_connected fires on the loop thread — send directly.
        drogon::WebSocketClient* ws;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ws = m_ws;
        }
        if (ws && ws->getConnection()) {
            ws->getConnection()->send(text);
            return true;
        }
        return false;
    }
    // Cross-thread: queue fire-and-forget. Returns "accepted", not
    // "delivered" — callers that need certainty send from on_connected.
    std::string payload = text;
    m_loop->queueInLoop([this, payload = std::move(payload)] {
        drogon::WebSocketClient* ws;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ws = m_ws;
        }
        if (ws && ws->getConnection()) ws->getConnection()->send(payload);
    });
    return true;
}

}  // namespace animus::kernel

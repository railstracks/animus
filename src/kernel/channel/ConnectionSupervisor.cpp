#include "animus_kernel/ConnectionSupervisor.h"
#include "animus_kernel/Log.h"

#include <drogon/WebSocketClient.h>
#include <drogon/HttpRequest.h>
#include <trantor/net/EventLoop.h>

#include <algorithm>
#include <random>

namespace animus::kernel {

namespace {

// -- Reconnect-storm guards (2026-09-10 outage forensics, #60) --
// The pre-fix supervisor could schedule N concurrent reconnects: a
// fast-failing client's connect-callback AND close-handler both called
// ScheduleReconnect, and same-generation timers all passed the staleness
// check -> exponential fan-out (11,734 attempts in ~6 min on the
// tradingbot instance; animusd ballooned to 7.3GB RSS and the host OOM'd).
// The bounds below make that shape structurally impossible.
constexpr long long kBackoffFloorMs = 250;  // no zero-delay reconnect loops
// (circuit-break threshold + cooldown are Config: circuitBreakAfter/cooldownCap)

std::string ReqResultReason(drogon::ReqResult r) {
    switch (r) {
        case drogon::ReqResult::Ok: return "ok";
        case drogon::ReqResult::BadResponse: return "bad response";
        case drogon::ReqResult::NetworkFailure: return "network failure";
        case drogon::ReqResult::BadServerAddress: return "bad server address";
        case drogon::ReqResult::Timeout: return "timeout";
        case drogon::ReqResult::HandshakeError: return "handshake error";
        case drogon::ReqResult::InvalidCertificate: return "invalid certificate";
        case drogon::ReqResult::EncryptionFailure: return "encryption failure";
        default: return "unknown";
    }
}

}  // namespace

ConnectionSupervisor::ConnectionSupervisor() = default;
ConnectionSupervisor::~ConnectionSupervisor() { RequestStop(); }

const char* ConnectionSupervisor::StateName(ConnectionSupervisor::State s) {
    switch (s) {
        case State::Disabled: return "disabled";
        case State::Connecting: return "connecting";
        case State::Connected: return "connected";
        case State::Backoff: return "backoff";
        case State::Error: return "error";
    }
    return "?";
}

ConnectionSupervisor::State ConnectionSupervisor::state() const {
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
        if (to == State::Backoff) m_lastBackoffAt = std::chrono::steady_clock::now();
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
        m_generation = 0;
        m_connectInFlight = false;
        m_lastScheduledDelayMs = 0;
        const auto now = std::chrono::steady_clock::now();
        m_lastEvent = now;
        m_lastPong = now;
        m_lastBackoffAt = {};
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

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Single-flight: one connect attempt outstanding at a time, and
        // never while already connected. Every historical storm shape
        // funnels through here, so this gate is the hard bound.
        if (m_connectInFlight || m_state == State::Connected) return;
        m_connectInFlight = true;
    }

    auto wsPtr = drogon::WebSocketClient::newWebSocketClient(m_cfg.host, m_loop);
    drogon::WebSocketClient* stale = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_ws && m_ws != wsPtr.get()) stale = m_ws;
        m_ws = wsPtr.get();
    }
    if (stale) stale->stop();  // release the superseded client's socket (#31 family)

    wsPtr->setMessageHandler(
        [this, wsPtr](std::string&& message,
               const drogon::WebSocketClientPtr&,
               const drogon::WebSocketMessageType& type) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_ws != wsPtr.get()) return;  // stale client — ignore
                const auto now = std::chrono::steady_clock::now();
                if (type == drogon::WebSocketMessageType::Pong) m_lastPong = now;
                if (type == drogon::WebSocketMessageType::Text) m_lastEvent = now;
            }
            if (type == drogon::WebSocketMessageType::Text && m_cbs.on_message)
                m_cbs.on_message(message);
        });

    wsPtr->setConnectionClosedHandler(
        [this, wsPtr](const drogon::WebSocketClientPtr&) {
            if (m_stopRequested.load()) return;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                // A superseded client closing must not schedule anything —
                // its replacement already owns the reconnect cycle.
                if (m_ws != wsPtr.get()) return;
            }
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
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_ws != wsPtr.get()) return;  // superseded mid-handshake
                m_connectInFlight = false;
            }
            if (m_stopRequested.load() || m_fatal.load()) return;

            if (r != drogon::ReqResult::Ok) {
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

            int hadFailures;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                hadFailures = m_consecutiveFailures;
                m_consecutiveFailures = 0;
                const auto now = std::chrono::steady_clock::now();
                m_lastEvent = now;
                m_lastPong = now;
            }
            if (m_cfg.circuitBreakAfter > 0 && hadFailures >= m_cfg.circuitBreakAfter) {
                ALOG_INFO("conn-supervisor",
                          "[" << m_cfg.name << "] recovered after "
                              << hadFailures << " consecutive failures");
            }
            Transition(State::Connected, "handshake ok");
            conn->setPingMessage("",
                std::chrono::duration_cast<std::chrono::seconds>(m_cfg.pingInterval));
            if (m_cbs.on_connected) m_cbs.on_connected();
        });
}

void ConnectionSupervisor::ScheduleReconnect(const std::string& reason) {
    if (m_stopRequested.load() || m_fatal.load()) return;

    // Latest-wins: invalidate every earlier pending reconnect timer. Two
    // failure sources racing on one client (connect-callback +
    // close-handler), or a watchdog firing mid-failure, can no longer
    // stack reconnects — only the most recent schedule survives to call
    // Connect(). This plus single-flight is what bounds the fan-out.
    const uint64_t gen = m_generation.fetch_add(1) + 1;

    int n;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_consecutiveFailures++;
        n = m_consecutiveFailures;
    }

    // Exponential backoff with full jitter and a floor. Base 1s ×2^n,
    // capped at backoffCap; after kCircuitBreakAfter consecutive failures
    // the cap becomes a 5-minute cooldown — a hard-down server sees a
    // bounded probe rate (~12/hour), never a storm.
    const bool tripped = (m_cfg.circuitBreakAfter > 0 && n >= m_cfg.circuitBreakAfter);
    const long long cap = tripped
        ? std::max<long long>(m_cfg.backoffCap.count(), m_cfg.cooldownCap.count())
        : m_cfg.backoffCap.count();
    const auto base = std::min<long long>(
        cap, std::max<long long>(m_cfg.backoffBase.count(), 1) * (1LL << std::min(n - 1, 6)));
    static thread_local std::mt19937 rng{std::random_device{}()};
    // Jitter range must stay valid when base < floor (fast test configs):
    const long long lo = std::min<long long>(kBackoffFloorMs, base);
    std::uniform_int_distribution<long long> dist(lo, base);
    const long long delayMs = dist(rng);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastScheduledDelayMs = delayMs;
    }

    if (tripped) {
        ALOG_ERROR("conn-supervisor",
                   "[" << m_cfg.name << "] degraded: " << n
                       << " consecutive failures — cooldown probing every "
                       << cap << "ms (last: " << reason << ")");
    }
    ALOG_INFO("conn-supervisor",
              "[" << m_cfg.name << "] reconnect #" << n << " (" << reason
                  << ") in " << delayMs << " ms");

    m_loop->runAfter(static_cast<double>(delayMs) / 1000.0, [this, gen] {
        if (m_stopRequested.load() || m_fatal.load()) return;
        if (gen != m_generation.load()) return;  // superseded by a newer schedule
        Connect();
    });
}

void ConnectionSupervisor::StartWatchdog() {
    m_loop->runEvery(5.0, [this] {
        if (m_stopRequested.load() || m_fatal.load()) return;

        State st;
        std::chrono::steady_clock::time_point lastPong, lastBackoffAt;
        long long lastDelayMs = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            st = m_state;
            lastPong = m_lastPong;
            lastBackoffAt = m_lastBackoffAt;
            lastDelayMs = m_lastScheduledDelayMs;
        }
        const auto now = std::chrono::steady_clock::now();

        if (st == State::Connected) {
            // Transport-death detection ONLY. The pre-fix watchdog keyed on
            // application-message silence — but a quiet inbox is healthy,
            // not stalled (AgentMail sends no idle traffic), which forced a
            // reconnect every stallTimeout forever (~288/day baseline churn).
            // Drogon pings every pingInterval; a live server answers Pong
            // (RFC 6455). Missing pongs = genuine transport death. No ping
            // stream configured -> we cannot judge transport health this
            // way, so the check is disabled rather than false-alarmed.
            if (m_cfg.pingInterval > std::chrono::milliseconds(0)) {
                const auto pongTimeout = 4 * m_cfg.pingInterval;
                if (now - lastPong > pongTimeout) {
                    ForceReconnect("transport dead: no pong within timeout");
                }
            }
        } else if (st == State::Backoff && lastBackoffAt != std::chrono::steady_clock::time_point{}) {
            // Orphan heal: if a scheduled reconnect never fired (missed
            // close event, timer starvation), self-heal after 2× the last
            // scheduled delay instead of wedging in Backoff forever.
            const auto orphanAfter = std::chrono::milliseconds(
                std::max<long long>(20000, 2 * lastDelayMs));
            if (now - lastBackoffAt > orphanAfter) {
                ALOG_WARNING("conn-supervisor",
                             "[" << m_cfg.name << "] backoff orphan — self-healing reconnect");
                m_generation.fetch_add(1);  // any lost timer is dead by definition
                Connect();
            }
        }
    });
}

void ConnectionSupervisor::ForceReconnect(const std::string& reason) {
    ALOG_INFO("conn-supervisor", "[" << m_cfg.name << "] forcing reconnect: " << reason);
    // Invalidate any pending reconnect (generation bump), mark Backoff
    // honestly, then drop the connection; the closed-handler +
    // ScheduleReconnect path drives the retry. We never exit the loop from
    // here — the watchdog heals, it does not kill (#60's watchdog killed
    // the thread).
    m_generation.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastEvent = std::chrono::steady_clock::now();  // diagnostics
    }
    Transition(State::Backoff, reason);
    if (m_ws) m_ws->stop();
}

void ConnectionSupervisor::RequestStop() {
    if (m_stopRequested.exchange(true)) return;
    m_generation.fetch_add(1);  // pending reconnects no-op
    trantor::EventLoop* loop = m_loop;
    if (!loop) return;
    loop->queueInLoop([this, loop] {
        if (m_ws) m_ws->stop();
        loop->quit();
    });
}

void ConnectionSupervisor::FatalError(const std::string& reason) {
    if (m_fatal.exchange(true)) return;
    Transition(State::Error, reason);
    m_generation.fetch_add(1);  // pending reconnects no-op
    trantor::EventLoop* loop = m_loop;
    if (!loop) return;
    loop->queueInLoop([this, loop] {
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

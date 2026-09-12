#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace drogon { class WebSocketClient; class HttpRequest; }
namespace trantor { class EventLoop; }

namespace animus::kernel {

/// ConnectionSupervisor — owns one supervised websocket connection:
/// connect → (close/fail) → backoff+jitter → reconnect, forever; every
/// state transition logged loudly. Terminal config errors never retry.
///
/// Negative spec: issue #60 (the EmailAdapter lifecycle: silent permanent
/// death on failed initial connect, no reconnect on remote close, watchdog
/// that could kill the thread). Every behavior there is a must-not here.
/// First consumer: EmailAdapter. Later: api package connections
/// (docs/api/CONNECTIONS.md).
class ConnectionSupervisor {
public:
    struct Config {
        std::string name;        // identity in logs, e.g. "animus-email"
        std::string host;        // "wss://host" or "ws://host" (no path)
        std::string path = "/";  // upgrade request path
        std::map<std::string, std::string> query;    // query params
        std::map<std::string, std::string> headers;  // extra headers
        std::chrono::milliseconds pingInterval{30000};
        std::chrono::milliseconds stallTimeout{300000};  // no-event watchdog
        std::chrono::milliseconds backoffBase{1000};
        std::chrono::milliseconds backoffCap{60000};
        int circuitBreakAfter{20};                      // consecutive failures -> cooldown mode
        std::chrono::milliseconds cooldownCap{300000};  // attempt spacing once tripped
    };

    enum class State { Disabled, Connecting, Connected, Backoff, Error };

    struct Callbacks {
        /// Fires on EVERY successful (re)connect — the resubscribe hook.
        /// The supervisor is connected when this fires; SendText works.
        std::function<void()> on_connected;
        /// Text frames from the server.
        std::function<void(const std::string&)> on_message;
        /// Advisory: connection lost (reconnect is already scheduled).
        std::function<void(const std::string&)> on_closed;
    };

    ConnectionSupervisor();
    ~ConnectionSupervisor();

    /// Blocking: runs the event loop until RequestStop(). Intended to be
    /// called from the owning adapter's worker thread. Config errors
    /// (empty host/name) land in State::Error and return immediately.
    void Run(const Config& cfg, Callbacks cbs);

    /// Thread-safe. Tears down the connection and makes Run() return.
    /// Bounded: never hangs (queueInLoop + quit).
    void RequestStop();

    /// Report a fatal condition (e.g. server-side auth rejection): stops
    /// reconnecting, State::Error, Run() returns soon after. Loud.
    void FatalError(const std::string& reason);

    /// Thread-safe best-effort send of a text frame. Returns false when
    /// not connected.
    bool SendText(const std::string& text);

    State state() const;
    int consecutive_failures() const;
    std::string last_error() const;
    static const char* StateName(State s);

private:
    void Transition(State to, const std::string& reason);
    void Connect();
    void ScheduleReconnect(const std::string& reason);
    void StartWatchdog();
    void ForceReconnect(const std::string& reason);

    mutable std::mutex m_mutex;
    Config m_cfg;
    Callbacks m_cbs;
    State m_state{State::Disabled};
    int m_consecutiveFailures{0};
    std::string m_lastError;
    std::chrono::steady_clock::time_point m_lastEvent;
    // All below guarded by m_mutex (storm guards, #60 forensics 2026-09-10):
    bool m_connectInFlight{false};       // single-flight connect attempts
    std::chrono::steady_clock::time_point m_lastPong{};      // transport liveness
    std::chrono::steady_clock::time_point m_lastBackoffAt{}; // orphan-heal anchor
    long long m_lastScheduledDelayMs{0}; // orphan-heal threshold input

    trantor::EventLoop* m_loop{nullptr};
    drogon::WebSocketClient* m_ws{nullptr};  // owned by its loop via intrusive ptr
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_fatal{false};
    // This trantor has no timer cancellation — pending reconnects carry the
    // generation they were scheduled in and no-op when it has moved on.
    std::atomic<uint64_t> m_generation{0};
};

/// PollFallbackGate - pure decision logic for bridging an adapter to a
/// fallback poll transport while its supervised websocket is unhealthy
/// (#60: capture must survive WS storms; a quiet degraded mode beats a
/// blind one). Hysteresis: engage once the WS has been non-Connected for
/// engageAfter; disengage the moment it is Connected again. No clocks
/// inside - feed timestamps in. Terminal supervisor errors (auth
/// rejection) must NOT engage the poller; the adapter checks state()
/// itself before consulting the gate.
class PollFallbackGate {
public:
    explicit PollFallbackGate(std::chrono::milliseconds engageAfter)
        : m_engageAfter(engageAfter) {}

    enum class Decision { None, Engage, Disengage };

    /// One observation of the world. Call periodically (e.g. 1s ticks).
    Decision Tick(std::chrono::steady_clock::time_point now, bool wsConnected) {
        if (wsConnected) {
            m_unhealthySince = {};
            if (m_engaged) {
                m_engaged = false;
                return Decision::Disengage;
            }
            return Decision::None;
        }
        if (m_unhealthySince.time_since_epoch().count() == 0) {
            m_unhealthySince = now;
        }
        if (!m_engaged && now - m_unhealthySince >= m_engageAfter) {
            m_engaged = true;
            return Decision::Engage;
        }
        return Decision::None;
    }

    bool engaged() const { return m_engaged; }

private:
    std::chrono::milliseconds m_engageAfter;
    std::chrono::steady_clock::time_point m_unhealthySince{};
    bool m_engaged{false};
};

}  // namespace animus::kernel

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string>

#include <trantor/net/EventLoop.h>  // TimerId

namespace drogon { class WebSocketClient; class HttpRequest; }

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

    trantor::EventLoop* m_loop{nullptr};
    drogon::WebSocketClient* m_ws{nullptr};  // owned by its loop via intrusive ptr
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_fatal{false};
    trantor::TimerId m_reconnectTimer{0};
};

}  // namespace animus::kernel

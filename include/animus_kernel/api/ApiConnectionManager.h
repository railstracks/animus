#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include <json/json.h>

#include "animus_kernel/api/ApiRuntime.h"

namespace animus::kernel {

class ApiPackageStore;
class HttpClient;
struct ApiPackage;
struct ApiPackageConnection;

// ============================================================================
// ApiConnectionManager — drives api-package connections (manifest v1, "d").
//
// For every enabled package connection:
//   - longpoll: on the poll interval, interpolate url/headers/params from live
//     package state (secrets included; schema defaults overlaid), GET, parse
//     the response, extract the cursor via poll.cursor_path, and hand the
//     parsed body to the connection's on_message hook (ApiRuntime::RunHook).
//   - websocket: not driven here yet (longpoll lands first; ws arrives with
//     channel-grade supervisor reuse).
//
// Hook results may carry a "dispatches" array: {reason, prompt, ...}. Each
// dispatch is routed to the dispatch callback (the AgentKernel bridge), which
// runs a fresh agent session with the prompt.
//
// Threading: one poll thread. Polls are serialized per connection under
// m_stateMutex (previous tick's hook must finish before the next fires).
// ============================================================================

class ApiConnectionManager {
public:
    // Dispatch payload: what the hook asked to be routed to the agent.
    struct Dispatch {
        std::string packageId;
        std::string packageName;
        std::string connectionName;
        std::string reason;        // e.g. "price_trigger"
        std::string prompt;        // agent-facing text
        std::string payloadJson;   // full dispatch object (context)
    };

    using DispatchCallback = std::function<void(const Dispatch&)>;

    ApiConnectionManager(ApiPackageStore* store, ApiRuntime* runtime, HttpClient* http);
    ~ApiConnectionManager();

    ApiConnectionManager(const ApiConnectionManager&) = delete;
    ApiConnectionManager& operator=(const ApiConnectionManager&) = delete;

    // Route hook dispatches to the owning agent (AgentKernel bridge).
    void SetDispatchCallback(DispatchCallback cb);

    void Start();
    void Stop();

    // Poll all due connections once now (admin/debug surface). Returns the
    // number of connections polled.
    int PollOnce();

    bool IsRunning() const { return m_running.load(); }

private:
    struct ConnState {
        std::string lastCursor;
        int64_t lastPollMs{0};
        int consecutiveErrors{0};
        bool polling{false};  // in-flight guard: I/O runs OUTSIDE the mutex
    };

    // Result of one poll; applied to ConnState under the mutex afterwards.
    // (Copilot audit #2: network + Lua execution must not hold m_stateMutex.)
    struct PollOutcome {
        int consecutiveErrors{0};
        std::string newCursor;
        bool cursorChanged{false};
    };

    void Run();
    void Tick();
    PollOutcome PollConnection(const ApiPackage& pkg, const ApiPackageConnection& conn,
                               const std::string& lastCursor, int prevErrors);
    Json::Value BuildPollContext(const ApiPackage& pkg, const ApiPackageConnection& conn,
                                 const std::string& lastCursor);
    void HandleHookResult(const ApiPackage& pkg, const ApiPackageConnection& conn,
                         const Json::Value& result);

    ApiPackageStore* m_store;
    ApiRuntime* m_runtime;
    HttpClient* m_http;
    DispatchCallback m_dispatchCb;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};
    std::thread m_thread;

    // key: packageId + ":" + connectionName
    std::map<std::string, ConnState> m_connStates;
    std::mutex m_stateMutex;
};

}  // namespace animus::kernel

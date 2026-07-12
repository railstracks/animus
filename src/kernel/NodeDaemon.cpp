#include "animus_kernel/NodeDaemon.h"
#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/tools/ToolTypes.h"
#include "animus_kernel/tools/ShellExecTool.h"
#include "animus_kernel/tools/FileTool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include <json/json.h>
#include <drogon/WebSocketClient.h>
#include <trantor/net/EventLoop.h>

#ifdef __linux__
#include <sys/utsname.h>
#endif

namespace animus::kernel {

// ============================================================================
// NodeDaemon — lightweight daemon for --node mode
//
// Connects to central server via WebSocket, receives tool calls,
// executes them locally with registered tool handlers, returns results.
//
// Auto-reconnect with exponential backoff (1s → 2s → 4s → ... → 60s cap).
// Resets on successful connection.
// ============================================================================

static std::atomic<bool> g_nodeStopRequested{false};
static void HandleNodeSignal(int) { g_nodeStopRequested.store(true); }

// Reconnect timing constants
static constexpr double kInitialBackoffSec = 1.0;
static constexpr double kMaxBackoffSec = 60.0;

class NodeDaemon {
public:
    explicit NodeDaemon(NodeDaemonConfig config)
        : m_config(std::move(config)) {}

    int Run() {
        // Set up signal handling
        g_nodeStopRequested.store(false);
        std::signal(SIGINT, &HandleNodeSignal);
        std::signal(SIGTERM, &HandleNodeSignal);

        // Register local tools based on allowlist
        RegisterLocalTools();

        if (m_tools.Size() == 0) {
            std::cerr << "[node] WARNING: No tools registered. "
                      << "The node will connect but cannot execute any tool calls.\n";
        }

        // The WebSocket client + event loop must run in a dedicated thread
        // because Drogon/trantor creates an EventLoop in the main thread
        // during static initialization. Same pattern as DiscordGatewayLoop
        // and AgentMail WebSocket loops.
        std::atomic<bool> initFailed{false};

        std::thread wsThread([this, &initFailed]() {
            trantor::EventLoop loop;
            m_loop = &loop;

            // Initiate first connection (creates WebSocketClient internally)
            InitiateConnection(&loop);

            // Shutdown checker: poll every 200ms, quit loop when signaled
            loop.runEvery(0.2, [&loop]() {
                if (g_nodeStopRequested.load()) {
                    std::cerr << "[node] Shutdown requested, stopping event loop...\n";
                    loop.quit();
                }
            });

            // Heartbeat every 30 seconds (only sends if connected)
            loop.runEvery(30.0, [this]() { SendHeartbeat(); });

            // Run the event loop (blocks until loop.quit())
            std::cerr << "[node] Daemon running as '" << m_config.name
                      << "'. Send SIGTERM to stop.\n";
            loop.loop();
        });

        // Main thread waits for the WS thread to exit
        wsThread.join();

        // Cleanup
        if (m_wsConn) {
            m_wsConn->shutdown();
        }

        if (initFailed.load()) {
            std::cerr << "[node] Daemon exited: initialization failed\n";
            return 1;
        }

        std::cerr << "[node] Daemon stopped.\n";
        return 0;
    }

private:
    // -----------------------------------------------------------------------
    // Connection management
    // -----------------------------------------------------------------------

    /// Initiate a WebSocket connection attempt. Creates a fresh
    /// WebSocketClient each time (Drogon doesn't allow reuse after disconnect).
    /// Called for initial connect and reconnects.
    void InitiateConnection(trantor::EventLoop* loop) {
        if (g_nodeStopRequested.load()) return;

        // Create a fresh client for each attempt
        auto wsClient = drogon::WebSocketClient::newWebSocketClient(
            m_config.serverUrl, loop);
        if (!wsClient) {
            std::cerr << "[node] Failed to create WebSocket client\n";
            return;
        }
        m_wsClient = wsClient;

        // Set handlers on the new client
        wsClient->setMessageHandler(
            [this](std::string&& message,
                   const drogon::WebSocketClientPtr&,
                   const drogon::WebSocketMessageType& type) {
                if (type == drogon::WebSocketMessageType::Text) {
                    HandleServerMessage(message);
                }
            });

        wsClient->setConnectionClosedHandler(
            [this, loop](const drogon::WebSocketClientPtr&) {
                std::cerr << "[node] Disconnected from server\n";
                m_wsConn.reset();
                m_registered = false;

                if (!g_nodeStopRequested.load()) {
                    m_connectAttempts++;
                    m_currentBackoff = std::min(
                        kInitialBackoffSec * (1ULL << std::min(m_connectAttempts, 6)),
                        kMaxBackoffSec);
                    ScheduleReconnect(loop);
                }
            });

        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath(ExtractPath(m_config.serverUrl));
        req->addHeader("Authorization", "Bearer " + m_config.token);

        if (m_connectAttempts == 0) {
            std::cerr << "[node] Connecting to " << m_config.serverUrl << "...\n";
        } else {
            std::cerr << "[node] Reconnecting (attempt " << (m_connectAttempts + 1)
                      << ", backoff " << (int)m_currentBackoff << "s)...\n";
        }

        wsClient->connectToServer(
            req,
            [this, loop](drogon::ReqResult result,
                         const drogon::HttpResponsePtr&,
                         const drogon::WebSocketClientPtr& client) {
                if (result == drogon::ReqResult::Ok) {
                    m_wsConn = client->getConnection();
                    m_registered = false;
                    m_connectAttempts = 0;
                    m_currentBackoff = kInitialBackoffSec;
                    std::cerr << "[node] WebSocket connected\n";
                    SendRegistration();
                } else {
                    std::cerr << "[node] WebSocket connection failed\n";
                    m_wsConn.reset();

                    if (!g_nodeStopRequested.load()) {
                        m_connectAttempts++;
                        m_currentBackoff = std::min(
                            kInitialBackoffSec * (1ULL << std::min(m_connectAttempts, 6)),
                            kMaxBackoffSec);
                        ScheduleReconnect(loop);
                    }
                }
            });
    }

    /// Schedule a reconnect attempt after m_currentBackoff seconds.
    void ScheduleReconnect(trantor::EventLoop* loop) {
        double delay = m_currentBackoff;
        std::cerr << "[node] Will reconnect in " << (int)delay << "s\n";

        loop->runAfter(delay, [this, loop]() {
            if (!g_nodeStopRequested.load()) {
                InitiateConnection(loop);
            }
        });
    }

    // -----------------------------------------------------------------------
    // Tool registration
    // -----------------------------------------------------------------------

    void RegisterLocalTools() {
        for (const auto& toolName : m_config.allowedTools) {
            if (toolName == "system" || toolName == "exec") {
                auto shellTool = std::make_unique<ShellExecTool>("");
                m_tools.Register(std::move(shellTool));
                std::cerr << "[node] Registered tool: exec\n";
            } else if (toolName == "file") {
                auto fileTool = std::make_unique<FileTool>("");
                m_tools.Register(std::move(fileTool));
                std::cerr << "[node] Registered tool: file\n";
            } else {
                std::cerr << "[node] Unknown tool in allowlist: " << toolName << "\n";
            }
        }
        std::cerr << "[node] " << m_tools.Size() << " tools registered\n";
    }

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    /// Extract the path component from a WebSocket URL.
    /// e.g. "ws://host:port/ws/node" → "/ws/node"
    static std::string ExtractPath(const std::string& url) {
        size_t hostStart = 0;
        if (url.substr(0, 5) == "ws://") {
            hostStart = 5;
        } else if (url.substr(0, 6) == "wss://") {
            hostStart = 6;
        }

        size_t pathStart = url.find('/', hostStart);
        if (pathStart != std::string::npos) {
            return url.substr(pathStart);
        }
        return "/";
    }

    /// Get system hostname and OS info via uname().
    static void GetSystemInfo(std::string& hostname, std::string& osInfo) {
#ifdef __linux__
        struct utsname buf;
        if (uname(&buf) == 0) {
            hostname = buf.nodename;
            osInfo = std::string(buf.sysname) + " " + buf.release;
            return;
        }
#endif
        hostname = "unknown";
        osInfo = "unknown";
    }

    // -----------------------------------------------------------------------
    // Message sending
    // -----------------------------------------------------------------------

    void SendRegistration() {
        if (!m_wsConn) return;

        std::string hostname, osInfo;
        GetSystemInfo(hostname, osInfo);

        Json::Value reg(Json::objectValue);
        reg["type"] = "register";
        reg["name"] = m_config.name;
        reg["hostname"] = hostname;
        reg["os"] = osInfo;

        Json::Value toolsArr(Json::arrayValue);
        for (const auto& t : m_config.allowedTools) toolsArr.append(t);
        reg["tools"] = toolsArr;

        SendJson(reg);
        std::cerr << "[node] Registration sent for '" << m_config.name
                  << "' (" << hostname << ", " << osInfo << ")\n";
    }

    void SendHeartbeat() {
        if (!m_wsConn || !m_registered) return;
        Json::Value hb(Json::objectValue);
        hb["type"] = "heartbeat";
        SendJson(hb);
    }

    void SendJson(const Json::Value& val) {
        if (!m_wsConn) return;
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        m_wsConn->send(Json::writeString(wb, val));
    }

    // -----------------------------------------------------------------------
    // Message handling
    // -----------------------------------------------------------------------

    void HandleServerMessage(const std::string& message) {
        Json::Value payload;
        Json::CharReaderBuilder builder;
        std::string parseErr;
        auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
        if (!reader->parse(message.c_str(), message.c_str() + message.size(),
                           &payload, &parseErr)) {
            std::cerr << "[node] Failed to parse server message: " << parseErr << "\n";
            return;
        }

        std::string msgType = payload.get("type", "").asString();

        if (msgType == "tool_call") {
            HandleToolCall(payload);
        } else if (msgType == "registered") {
            m_registered = true;
            std::cerr << "[node] Server confirmed registration\n";
        } else {
            std::cerr << "[node] Unknown server message type: " << msgType << "\n";
        }
    }

    void HandleToolCall(const Json::Value& payload) {
        std::string callId = payload.get("call_id", "").asString();
        std::string toolName = payload.get("tool", "").asString();

        ToolCall tc;
        tc.name = toolName;
        tc.id = callId;

        Json::Value argsJson = payload.get("arguments", Json::objectValue);
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        tc.arguments = Json::writeString(wb, argsJson);

        ToolResult result;
        auto* handler = m_tools.Find(toolName);
        if (handler) {
            std::cerr << "[node] Executing tool: " << toolName << "\n";
            result = handler->Execute(tc);
        } else {
            result.call_id = callId;
            result.success = false;
            result.error = "unknown tool: " + toolName;
            std::cerr << "[node] Unknown tool requested: " << toolName << "\n";
        }

        Json::Value resultMsg(Json::objectValue);
        resultMsg["type"] = "tool_result";
        resultMsg["call_id"] = callId;
        resultMsg["success"] = result.success;
        resultMsg["output"] = result.output;
        if (!result.error.empty()) {
            resultMsg["error"] = result.error;
        }

        SendJson(resultMsg);
    }

    // -----------------------------------------------------------------------
    // Member variables
    // -----------------------------------------------------------------------

    NodeDaemonConfig m_config;
    ToolRegistry m_tools;
    drogon::WebSocketClientPtr m_wsClient;
    drogon::WebSocketConnectionPtr m_wsConn;
    trantor::EventLoop* m_loop{nullptr};

    // Connection state
    int m_connectAttempts{0};
    double m_currentBackoff{kInitialBackoffSec};
    std::atomic<bool> m_registered{false};
};

// ============================================================================
// Public entry point — called from main.cpp
// ============================================================================

int RunNodeDaemon(const NodeDaemonConfig& config) {
    NodeDaemon daemon(config);
    return daemon.Run();
}

} // namespace animus::kernel

// ConnectionSupervisorTests — verifies the #60 fix end to end:
// connect, message delivery, reconnect-on-close, recovery after
// server-down-at-start, bounded teardown, terminal config errors, fatal
// errors, and SendText from the on_connected context.
//
// Uses a hand-rolled local websocket server (raw TCP + HTTP upgrade +
// minimal frame codec) so every scenario — including hard TCP closes —
// is deterministic.

#include "animus_kernel/ConnectionSupervisor.h"
#include "animus_kernel/ChannelHelpers.h"  // Base64EncodeStr

#include <openssl/sha.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace animus::kernel;
using namespace animus::kernel::channel_detail;  // Base64EncodeStr
using namespace std::chrono_literals;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string Sha1Base64(const std::string& in) {
    unsigned char digest[20];
    SHA1(reinterpret_cast<const unsigned char*>(in.data()), in.size(), digest);
    return Base64EncodeStr(std::string(reinterpret_cast<char*>(digest), 20));
}

// ---------------------------------------------------------------------------
// Minimal websocket server: one client at a time, scriptable
// ---------------------------------------------------------------------------

struct WsServer {
    explicit WsServer(uint16_t port) : m_port(port) {}
    ~WsServer() { Stop(); }

    void Start();
    void Stop();

    std::atomic<bool> refuseConnections{false};  // close accepted sockets instantly
    std::atomic<int> totalAccepts{0};

    void SendText(const std::string& text);
    void CloseClient();  // hard TCP close of current client connection

    std::string lastReceivedText;
    std::mutex rxMutex;

private:
    void AcceptLoop();
    void ClientLoop(int fd);
    std::atomic<bool> m_running{false};
    int m_listenFd{-1};
    std::atomic<int> m_clientFd{-1};
    std::thread m_acceptThread;
    uint16_t m_port;
    std::mutex m_sendMutex;
};

void WsServer::Start() {
    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(m_port);
    if (bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "  [server] bind failed on port " << m_port << " (test may fail)\n";
        close(m_listenFd);
        m_listenFd = -1;
        return;
    }
    listen(m_listenFd, 4);
    m_running = true;
    m_acceptThread = std::thread([this] { AcceptLoop(); });
}

void WsServer::Stop() {
    if (!m_running.exchange(false)) return;
    if (m_listenFd >= 0) {
        shutdown(m_listenFd, SHUT_RDWR);
        close(m_listenFd);
        m_listenFd = -1;
    }
    CloseClient();
    if (m_acceptThread.joinable()) m_acceptThread.join();
}

void WsServer::AcceptLoop() {
    while (m_running) {
        int fd = accept(m_listenFd, nullptr, nullptr);
        if (fd < 0) break;
        totalAccepts++;
        if (refuseConnections) {
            close(fd);
            continue;
        }
        CloseClient();  // one client at a time is enough for these tests
        m_clientFd = fd;
        std::thread([this, fd] { ClientLoop(fd); }).detach();
    }
}

void WsServer::ClientLoop(int fd) {
    auto dropAndReturn = [this, fd] {
        int expected = fd;
        m_clientFd.compare_exchange_strong(expected, -1);
        close(fd);
    };

    // --- Read the HTTP upgrade request, respond 101 ---
    std::string req;
    char buf[4096];
    while (req.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return dropAndReturn();
        req.append(buf, static_cast<size_t>(n));
    }
    auto keyPos = req.find("Sec-WebSocket-Key:");
    if (keyPos == std::string::npos) keyPos = req.find("sec-websocket-key:");
    std::string wsKey;
    if (keyPos != std::string::npos) {
        size_t v = req.find(':', keyPos) + 1;
        size_t e = req.find("\r\n", v);
        wsKey = req.substr(v, e - v);
        while (!wsKey.empty() && wsKey.front() == ' ') wsKey.erase(0, 1);
        while (!wsKey.empty() && (wsKey.back() == ' ' || wsKey.back() == '\r')) wsKey.pop_back();
    }
    std::string accept = Sha1Base64(wsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    if (send(fd, resp.data(), resp.size(), 0) < 0) return dropAndReturn();

    // --- Frame loop: decode client frames (masked), answer pings ---
    std::string pending;
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        pending.append(buf, static_cast<size_t>(n));

        bool exitLoop = false;
        while (pending.size() >= 2 && !exitLoop) {
            unsigned char b0 = static_cast<unsigned char>(pending[0]);
            unsigned char b1 = static_cast<unsigned char>(pending[1]);
            bool masked = (b1 & 0x80) != 0;
            uint64_t len = b1 & 0x7F;
            size_t headerLen = 2;
            if (len == 126) {
                if (pending.size() < 4) break;
                len = (static_cast<uint64_t>(static_cast<unsigned char>(pending[2])) << 8) |
                       static_cast<unsigned char>(pending[3]);
                headerLen = 4;
            } else if (len == 127) {
                if (pending.size() < 10) break;
                len = 0;
                for (int i = 0; i < 8; ++i)
                    len = (len << 8) | static_cast<unsigned char>(pending[2 + i]);
                headerLen = 10;
            }
            size_t maskLen = masked ? 4 : 0;
            if (pending.size() < headerLen + maskLen + len) break;

            std::string mask = pending.substr(headerLen, maskLen);
            std::string payload = pending.substr(headerLen + maskLen, len);
            if (masked) {
                for (size_t i = 0; i < payload.size(); ++i)
                    payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
            }
            pending.erase(0, headerLen + maskLen + len);

            unsigned char opcode = b0 & 0x0F;
            if (opcode == 0x8) {  // close
                exitLoop = true;
                break;
            }
            if (opcode == 0x9) {  // ping -> pong
                std::string pong;
                pong += static_cast<char>(0x8A);
                pong += static_cast<char>(payload.size());
                pong += payload;
                send(fd, pong.data(), pong.size(), 0);
                continue;
            }
            if (opcode == 0x1) {  // text
                std::lock_guard<std::mutex> lock(rxMutex);
                lastReceivedText = payload;
            }
        }
        if (exitLoop) break;
    }
    dropAndReturn();
}

void WsServer::SendText(const std::string& text) {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    int fd = m_clientFd;
    if (fd < 0) return;
    std::string frame;
    frame += static_cast<char>(0x81);
    if (text.size() < 126) {
        frame += static_cast<char>(text.size());
    } else if (text.size() < 65536) {
        frame += static_cast<char>(126);
        frame += static_cast<char>((text.size() >> 8) & 0xFF);
        frame += static_cast<char>(text.size() & 0xFF);
    } else {
        frame += static_cast<char>(127);
        for (int i = 7; i >= 0; --i)
            frame += static_cast<char>((text.size() >> (8 * i)) & 0xFF);
    }
    frame += text;
    send(fd, frame.data(), frame.size(), 0);
}

void WsServer::CloseClient() {
    int fd = m_clientFd.exchange(-1);
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
}

// ---------------------------------------------------------------------------
// Test scaffolding
// ---------------------------------------------------------------------------

struct SupervisorRun {
    ConnectionSupervisor sup;
    std::thread thread;
    std::atomic<int> connects{0};
    std::atomic<int> messages{0};
    std::mutex msgMutex;
    std::vector<std::string> received;

    ConnectionSupervisor::Config MakeFastConfig(const std::string& port) {
        ConnectionSupervisor::Config cfg;
        cfg.name = "test-conn";
        cfg.host = "ws://127.0.0.1:" + port;
        cfg.path = "/v0";
        cfg.pingInterval = std::chrono::milliseconds(100000);
        cfg.stallTimeout = std::chrono::milliseconds(100000);
        cfg.backoffBase = std::chrono::milliseconds(50);
        cfg.backoffCap = std::chrono::milliseconds(300);
        return cfg;
    }

    void Start(const ConnectionSupervisor::Config& cfg) {
        ConnectionSupervisor::Callbacks cbs;
        cbs.on_connected = [this] { connects++; };
        cbs.on_message = [this](const std::string& m) {
            std::lock_guard<std::mutex> lock(msgMutex);
            received.push_back(m);
            messages++;
        };
        thread = std::thread(
            [this, cfg, cbs = std::move(cbs)]() mutable { sup.Run(cfg, std::move(cbs)); });
    }

    void StopAndJoin() {
        sup.RequestStop();
        if (thread.joinable()) thread.join();
    }
};

template <typename Pred>
bool WaitUntil(Pred pred, std::chrono::milliseconds timeout = 3000ms,
               std::chrono::milliseconds step = 25ms) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
        if (pred()) return true;
        std::this_thread::sleep_for(step);
    }
    return pred();
}

uint16_t PickPort() {
    static std::mt19937 rng{std::random_device{}()};
    return static_cast<uint16_t>(20000 + (rng() % 20000));
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

int TestConnectSubscribeAndMessage() {
    std::cerr << "  [supervisor] connect + resubscribe-on-reconnect + message...\n";
    uint16_t port = PickPort();
    WsServer server(port);
    server.Start();

    SupervisorRun run;
    run.Start(run.MakeFastConfig(std::to_string(port)));

    Assert(WaitUntil([&] { return run.sup.state() == ConnectionSupervisor::State::Connected; }),
           "reaches connected");
    Assert(run.connects.load() == 1, "on_connected fired exactly once");

    server.SendText("{\"hello\":\"world\"}");
    Assert(WaitUntil([&] { return run.messages.load() == 1; }), "message delivered");
    {
        std::lock_guard<std::mutex> lock(run.msgMutex);
        Assert(run.received[0].find("world") != std::string::npos, "payload intact");
    }

    // Hard close -> reconnect -> on_connected fires AGAIN (resubscribe path)
    server.CloseClient();
    Assert(WaitUntil([&] { return run.connects.load() >= 2; }, 5000ms),
           "reconnected and resubscribed after hard close");
    Assert(run.sup.state() == ConnectionSupervisor::State::Connected,
           "back to connected after reconnect");

    run.StopAndJoin();
    server.Stop();
    return 0;
}

int TestServerDownThenUp() {
    std::cerr << "  [supervisor] server down at start, recovers when it appears...\n";
    uint16_t port = PickPort();
    SupervisorRun run;
    run.Start(run.MakeFastConfig(std::to_string(port)));  // nothing listening

    // #60's core failure was the silent permanent death on failed initial
    // connect — here failures must accumulate visibly, then recovery works.
    Assert(WaitUntil([&] { return run.sup.consecutive_failures() >= 2; }, 5000ms),
           "failed connects counted with backoff (no silent death)");

    WsServer server(port);
    server.Start();
    Assert(WaitUntil([&] { return run.sup.state() == ConnectionSupervisor::State::Connected; },
                     5000ms),
           "connects once server appears");
    Assert(run.connects.load() >= 1, "on_connected fired");

    run.StopAndJoin();
    server.Stop();
    return 0;
}

int TestBoundedStop() {
    std::cerr << "  [supervisor] Stop is bounded (no hang)...\n";
    uint16_t port = PickPort();
    WsServer server(port);
    server.Start();

    SupervisorRun run;
    run.Start(run.MakeFastConfig(std::to_string(port)));
    Assert(WaitUntil([&] { return run.sup.state() == ConnectionSupervisor::State::Connected; }),
           "connected first");

    auto t0 = std::chrono::steady_clock::now();
    run.StopAndJoin();
    auto elapsed = std::chrono::steady_clock::now() - t0;
    Assert(elapsed < 3000ms, "stop+join completes well under 3s");

    server.Stop();
    return 0;
}

int TestTerminalConfigError() {
    std::cerr << "  [supervisor] empty host = terminal error, Run returns...\n";
    ConnectionSupervisor sup;
    ConnectionSupervisor::Config cfg;  // host empty
    cfg.name = "bad";
    sup.Run(cfg, ConnectionSupervisor::Callbacks{});  // must return, not block
    Assert(sup.state() == ConnectionSupervisor::State::Error, "error state");
    Assert(!sup.last_error().empty(), "error reason recorded");
    return 0;
}

int TestFatalError() {
    std::cerr << "  [supervisor] FatalError stops reconnecting (auth rejection)...\n";
    uint16_t port = PickPort();
    WsServer server(port);
    server.Start();

    SupervisorRun run;
    run.Start(run.MakeFastConfig(std::to_string(port)));
    Assert(WaitUntil([&] { return run.sup.state() == ConnectionSupervisor::State::Connected; }),
           "connected first");

    // What EmailAdapter does on an agentmail auth-rejection frame:
    run.sup.FatalError("agentmail auth rejected: invalid_api_key");
    if (run.thread.joinable()) run.thread.join();
    Assert(run.sup.state() == ConnectionSupervisor::State::Error, "terminal error state");
    Assert(run.connects.load() == 1, "no reconnect after fatal");

    server.Stop();
    return 0;
}

int TestSendText() {
    std::cerr << "  [supervisor] SendText delivers from on_connected context...\n";
    uint16_t port = PickPort();
    WsServer server(port);
    server.Start();

    SupervisorRun run;
    auto cfg = run.MakeFastConfig(std::to_string(port));
    ConnectionSupervisor::Callbacks cbs;
    cbs.on_connected = [&run] {
        run.connects++;
        run.sup.SendText("{\"type\":\"subscribe\"}");  // loop-thread direct send
    };
    cbs.on_message = [&run](const std::string& m) {
        std::lock_guard<std::mutex> lock(run.msgMutex);
        run.received.push_back(m);
        run.messages++;
    };
    run.thread = std::thread(
        [&run, cfg, cbs = std::move(cbs)]() mutable { run.sup.Run(cfg, std::move(cbs)); });

    Assert(WaitUntil([&] { return run.connects.load() == 1; }), "connected");
    Assert(WaitUntil([&] {
        std::lock_guard<std::mutex> lock(server.rxMutex);
        return server.lastReceivedText.find("subscribe") != std::string::npos;
    }), "server saw the subscribe frame");

    run.StopAndJoin();
    server.Stop();
    return 0;
}

}  // namespace

int main() {
    std::cerr << "ConnectionSupervisor tests:\n";
    TestConnectSubscribeAndMessage();
    TestServerDownThenUp();
    TestBoundedStop();
    TestTerminalConfigError();
    TestFatalError();
    TestSendText();
    if (g_failures == 0) std::cerr << "All supervisor tests passed.\n";
    else std::cerr << g_failures << " failures.\n";
    return g_failures == 0 ? 0 : 1;
}

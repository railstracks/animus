#include "animus_kernel/interfaces/IrcInterface.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace animus::kernel {

bool IrcInterfaceRuntime::ParseConfig(const Json::Value& input, Config* out, std::string* error) {
    if (!out) {
        if (error) {
            *error = "internal config output is null";
        }
        return false;
    }
    if (!input.isObject()) {
        if (error) {
            *error = "config must be an object";
        }
        return false;
    }

    Config cfg;
    cfg.host = input.get("host", "").asString();
    cfg.port = input.get("port", 6667).asInt();
    cfg.serverPassword = input.get("server_password", "").asString();
    cfg.nick = input.get("nick", "").asString();
    cfg.username = input.get("username", cfg.username).asString();
    cfg.realname = input.get("realname", cfg.realname).asString();
    if (cfg.username.empty()) {
        cfg.username = "animus";
    }
    if (cfg.realname.empty()) {
        cfg.realname = "Animus Agent";
    }
    if (cfg.host.empty()) {
        if (error) {
            *error = "config.host is required";
        }
        return false;
    }
    if (cfg.nick.empty()) {
        if (error) {
            *error = "config.nick is required";
        }
        return false;
    }
    if (cfg.port <= 0 || cfg.port > 65535) {
        if (error) {
            *error = "config.port must be between 1 and 65535";
        }
        return false;
    }

    if (input.isMember("channels") && input["channels"].isArray()) {
        for (Json::ArrayIndex i = 0; i < input["channels"].size(); ++i) {
            const Json::Value& entry = input["channels"][i];
            if (!entry.isObject()) {
                continue;
            }
            const std::string name = entry.get("name", "").asString();
            if (name.empty()) {
                continue;
            }
            ChannelConfig channel;
            channel.name = name;
            channel.key = entry.get("key", "").asString();
            cfg.channels.push_back(std::move(channel));
        }
    }

    if (input.isMember("reconnect") && input["reconnect"].isObject()) {
        const Json::Value& reconnect = input["reconnect"];
        cfg.reconnectEnabled = reconnect.get("enabled", cfg.reconnectEnabled).asBool();
        cfg.reconnectInitialDelayMs = reconnect.get("initial_delay_ms", cfg.reconnectInitialDelayMs).asInt();
        cfg.reconnectMaxDelayMs = reconnect.get("max_delay_ms", cfg.reconnectMaxDelayMs).asInt();
    }
    if (cfg.reconnectInitialDelayMs < 200) {
        cfg.reconnectInitialDelayMs = 200;
    }
    if (cfg.reconnectMaxDelayMs < cfg.reconnectInitialDelayMs) {
        cfg.reconnectMaxDelayMs = cfg.reconnectInitialDelayMs;
    }
    cfg.forceIpv4 = input.get("force_ipv4", cfg.forceIpv4).asBool();
    cfg.useTls = input.get("use_tls", cfg.useTls).asBool();
    *out = std::move(cfg);
    return true;
}

IrcInterfaceRuntime::IrcInterfaceRuntime(
    std::string interfaceName,
    Config config,
    MessageCallback messageCallback,
    StatusCallback statusCallback)
    : m_config(std::move(config))
    , m_messageCallback(std::move(messageCallback))
    , m_statusCallback(std::move(statusCallback)) {
    (void)interfaceName;
}

IrcInterfaceRuntime::~IrcInterfaceRuntime() {
    Stop();
}

void IrcInterfaceRuntime::Start() {
    bool expected = false;
    if (!m_started.compare_exchange_strong(expected, true)) {
        return;
    }
    m_stopRequested.store(false);
    m_thread = std::thread([this] { RunLoop(); });
}

void IrcInterfaceRuntime::Stop() {
    m_stopRequested.store(true);
    CloseSocket();
    if (m_thread.joinable()) {
        // With non-blocking connect + poll()-based recv, the thread
        // checks m_stopRequested every 1-2 seconds, so join completes
        // quickly.
        m_thread.join();
    }
}

bool IrcInterfaceRuntime::SendPrivmsg(const std::string& target, const std::string& message) {
    if (target.empty() || message.empty()) {
        return false;
    }
    std::string body = message;
    std::replace(body.begin(), body.end(), '\r', ' ');
    std::replace(body.begin(), body.end(), '\n', ' ');
    const std::size_t maxChunk = 380; // conservative under RFC line limit.
    for (std::size_t pos = 0; pos < body.size(); pos += maxChunk) {
        const std::string chunk = body.substr(pos, maxChunk);
        if (!SendRaw("PRIVMSG " + target + " :" + chunk)) {
            return false;
        }
    }
    return true;
}

IrcInterfaceRuntime::ParsedLine IrcInterfaceRuntime::ParseLine(const std::string& line) {
    ParsedLine parsed;
    std::size_t cursor = 0;
    if (!line.empty() && line[0] == ':') {
        const std::size_t prefixEnd = line.find(' ');
        if (prefixEnd != std::string::npos) {
            parsed.prefix = line.substr(1, prefixEnd - 1);
            cursor = prefixEnd + 1;
        }
    }
    while (cursor < line.size() && line[cursor] == ' ') {
        ++cursor;
    }
    const std::size_t commandEnd = line.find(' ', cursor);
    if (commandEnd == std::string::npos) {
        parsed.command = line.substr(cursor);
        return parsed;
    }
    parsed.command = line.substr(cursor, commandEnd - cursor);
    cursor = commandEnd + 1;
    while (cursor < line.size()) {
        if (line[cursor] == ':') {
            parsed.trailing = line.substr(cursor + 1);
            break;
        }
        const std::size_t next = line.find(' ', cursor);
        if (next == std::string::npos) {
            parsed.params.push_back(line.substr(cursor));
            break;
        }
        parsed.params.push_back(line.substr(cursor, next - cursor));
        cursor = next + 1;
        while (cursor < line.size() && line[cursor] == ' ') {
            ++cursor;
        }
    }
    return parsed;
}

std::string IrcInterfaceRuntime::NickFromPrefix(const std::string& prefix) {
    const std::size_t bang = prefix.find('!');
    if (bang != std::string::npos) {
        return prefix.substr(0, bang);
    }
    return prefix;
}

void IrcInterfaceRuntime::ReportStatus(bool connected, const std::string& error) {
    if (m_statusCallback) {
        m_statusCallback(connected, error);
    }
}

// ---------------------------------------------------------------------------
// RunLoop — reconnect loop
// ---------------------------------------------------------------------------

void IrcInterfaceRuntime::RunLoop() {
    std::cerr << "[irc] run loop started — nick=" << m_config.nick
              << " host=" << m_config.host << ":" << m_config.port
              << " channels=" << m_config.channels.size()
              << " tls=" << (m_config.useTls ? "on" : "off")
              << " reconnect=" << (m_config.reconnectEnabled ? "on" : "off")
              << std::endl;

    int delayMs = m_config.reconnectInitialDelayMs;
    int attempt = 0;

    while (!m_stopRequested.load()) {
        ++attempt;
        std::string error;
        std::cerr << "[irc] connect attempt " << attempt
                  << " (backoff=" << delayMs << "ms)" << std::endl;

        const bool ok = ConnectAndRun(&error);

        if (m_stopRequested.load()) {
            std::cerr << "[irc] stop requested, exiting run loop" << std::endl;
            break;
        }
        if (ok && !m_config.reconnectEnabled) {
            std::cerr << "[irc] connected (no reconnect), exiting run loop" << std::endl;
            break;
        }

        if (!error.empty()) {
            std::cerr << "[irc] disconnected: " << error << std::endl;
            ReportStatus(false, error);
        } else {
            std::cerr << "[irc] disconnected" << std::endl;
            ReportStatus(false, "disconnected");
        }

        if (!m_config.reconnectEnabled) {
            std::cerr << "[irc] reconnect disabled, giving up" << std::endl;
            break;
        }

        const int sleepMs = std::max(200, std::min(delayMs, m_config.reconnectMaxDelayMs));
        std::cerr << "[irc] waiting " << sleepMs << "ms before reconnect" << std::endl;
        for (int elapsed = 0; elapsed < sleepMs && !m_stopRequested.load(); elapsed += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        delayMs = std::min(delayMs * 2, m_config.reconnectMaxDelayMs);
    }

    std::cerr << "[irc] run loop ended" << std::endl;
    ReportStatus(false, "");
}

// ---------------------------------------------------------------------------
// ConnectAndRun — connect, register, then recv loop
// ---------------------------------------------------------------------------

bool IrcInterfaceRuntime::ConnectAndRun(std::string* error) {
    if (!Connect(error)) {
        return false;
    }

    std::cerr << "[irc] TCP connected, sending registration..." << std::endl;

    if (!m_config.serverPassword.empty()) {
        if (!SendRaw("PASS " + m_config.serverPassword)) {
            if (error) *error = "failed to send PASS";
            return false;
        }
        std::cerr << "[irc] >> PASS ****" << std::endl;
    }

    if (!SendRaw("NICK " + m_config.nick)) {
        if (error) *error = "failed to send NICK";
        return false;
    }
    std::cerr << "[irc] >> NICK " << m_config.nick << std::endl;

    if (!SendRaw("USER " + m_config.username + " 0 * :" + m_config.realname)) {
        if (error) *error = "failed to send USER";
        return false;
    }
    std::cerr << "[irc] >> USER " << m_config.username << std::endl;

    bool joined = false;
    std::string readBuffer;
    std::array<char, 4096> buf{};

    while (!m_stopRequested.load()) {
        struct pollfd pfd;
        pfd.fd = m_socket.load();
        pfd.events = POLLIN;
        pfd.revents = 0;

        if (pfd.fd < 0) {
            if (error) *error = "socket closed during shutdown";
            break;
        }

        const int pollResult = ::poll(&pfd, 1, 2000);
        if (m_stopRequested.load()) break;
        if (pollResult == 0) continue;  // timeout, recheck stop flag
        if (pollResult < 0) {
            if (errno == EINTR) continue;
            if (error) *error = std::string("poll failed: ") + std::strerror(errno);
            break;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            if (error) *error = "connection closed by peer (poll)";
            break;
        }
        if (!(pfd.revents & POLLIN)) continue;

        const ssize_t n = [this, &buf, &pfd]() -> ssize_t {
            if (m_ssl) {
                int r = SSL_read(m_ssl, buf.data(), static_cast<int>(buf.size()));
                return (r > 0) ? r : ((SSL_get_error(m_ssl, r) == SSL_ERROR_WANT_READ) ? 0 : -1);
            }
            return ::recv(pfd.fd, buf.data(), buf.size(), 0);
        }();
        if (n <= 0) {
            if (error) {
                *error = (n == 0) ? "connection closed by peer"
                                  : std::string("recv failed: ") + std::strerror(errno);
            }
            break;
        }
        readBuffer.append(buf.data(), static_cast<std::size_t>(n));

        std::size_t lineEnd = 0;
        while ((lineEnd = readBuffer.find("\r\n")) != std::string::npos) {
            const std::string line = readBuffer.substr(0, lineEnd);
            readBuffer.erase(0, lineEnd + 2);
            if (line.empty()) continue;
            HandleLine(line, &joined);
        }
    }

    CloseSocket();
    return false;
}

// ---------------------------------------------------------------------------
// HandleLine — dispatch a single IRC line
// ---------------------------------------------------------------------------

void IrcInterfaceRuntime::HandleLine(const std::string& line, bool* joined) {
    const ParsedLine parsed = ParseLine(line);

    // --- Log all numerics and key events ---
    bool isNumeric = parsed.command.size() == 3
        && std::all_of(parsed.command.begin(), parsed.command.end(), ::isdigit);

    if (isNumeric) {
        bool isError = (parsed.command[0] == '4');
        std::cerr << (isError ? "[irc] !! ERROR " : "[irc] << ")
                  << parsed.command;
        if (!parsed.params.empty()) {
            for (const auto& p : parsed.params) std::cerr << " " << p;
        }
        if (!parsed.trailing.empty()) std::cerr << " :" << parsed.trailing;
        std::cerr << std::endl;
    } else if (parsed.command == "JOIN") {
        std::cerr << "[irc] << JOIN " << parsed.trailing
                  << " (by " << NickFromPrefix(parsed.prefix) << ")" << std::endl;
    } else if (parsed.command == "PART" || parsed.command == "QUIT"
               || parsed.command == "KICK") {
        std::cerr << "[irc] << " << parsed.command;
        if (!parsed.params.empty()) std::cerr << " " << parsed.params[0];
        if (!parsed.trailing.empty()) std::cerr << " :" << parsed.trailing;
        std::cerr << std::endl;
    }

    // --- PING/PONG ---
    if (parsed.command == "PING") {
        const std::string payload = !parsed.trailing.empty()
            ? parsed.trailing
            : (parsed.params.empty() ? "" : parsed.params.front());
        SendRaw("PONG :" + payload);
        return;
    }

    // --- 001 RPL_WELCOME — registration confirmed, join channels ---
    if (parsed.command == "001" && !(*joined)) {
        std::cerr << "[irc] registration confirmed, joining "
                  << m_config.channels.size() << " channel(s)..." << std::endl;
        for (const auto& channel : m_config.channels) {
            if (channel.key.empty()) {
                std::cerr << "[irc] >> JOIN " << channel.name << std::endl;
                SendRaw("JOIN " + channel.name);
            } else {
                std::cerr << "[irc] >> JOIN " << channel.name << " (with key)" << std::endl;
                SendRaw("JOIN " + channel.name + " " + channel.key);
            }
        }
        *joined = true;
        ReportStatus(true, "");
        return;
    }

    // --- PRIVMSG / NOTICE — dispatch to message callback ---
    const bool isPrivmsg = parsed.command == "PRIVMSG";
    const bool isNotice = parsed.command == "NOTICE";
    if ((!isPrivmsg && !isNotice) || parsed.params.empty() || parsed.trailing.empty()) {
        return;
    }
    const std::string sourceNick = NickFromPrefix(parsed.prefix);
    const std::string target = parsed.params.front();
    if (sourceNick.empty() || target.empty()) {
        return;
    }
    if (m_messageCallback) {
        m_messageCallback(sourceNick, target, parsed.trailing, isNotice);
    }
}

// ---------------------------------------------------------------------------
// Connect — DNS resolve + non-blocking connect with 10s timeout
// ---------------------------------------------------------------------------

bool IrcInterfaceRuntime::Connect(std::string* error) {
    const std::string portText = std::to_string(m_config.port);
    struct addrinfo hints {};
    hints.ai_family = m_config.forceIpv4 ? AF_INET : AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::cerr << "[irc] resolving " << m_config.host << ":" << m_config.port << "..."
              << std::endl;

    struct addrinfo* result = nullptr;
    const int rc = ::getaddrinfo(m_config.host.c_str(), portText.c_str(), &hints, &result);
    if (rc != 0) {
        if (error) {
            *error = std::string("getaddrinfo failed: ") + gai_strerror(rc);
        }
        std::cerr << "[irc] DNS failed: " << gai_strerror(rc) << std::endl;
        return false;
    }

    int connectedFd = -1;
    int addrIndex = 0;
    for (struct addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
        ++addrIndex;
        if (m_stopRequested.load()) break;

        char addrStr[INET6_ADDRSTRLEN] = {};
        if (ai->ai_family == AF_INET) {
            ::inet_ntop(AF_INET, &((struct sockaddr_in*)ai->ai_addr)->sin_addr,
                        addrStr, sizeof(addrStr));
        } else if (ai->ai_family == AF_INET6) {
            ::inet_ntop(AF_INET6, &((struct sockaddr_in6*)ai->ai_addr)->sin6_addr,
                        addrStr, sizeof(addrStr));
        }
        std::cerr << "[irc] trying addr[" << addrIndex << "] " << addrStr << "..."
                  << std::endl;

        const int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            std::cerr << "[irc]   socket() failed: " << std::strerror(errno) << std::endl;
            continue;
        }

        // Non-blocking connect
        int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        int ret = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (ret == 0) {
            ::fcntl(fd, F_SETFL, flags);
            connectedFd = fd;
            std::cerr << "[irc]   connected immediately" << std::endl;
            break;
        }
        if (ret < 0 && errno != EINPROGRESS) {
            std::cerr << "[irc]   connect() failed: " << std::strerror(errno) << std::endl;
            ::close(fd);
            continue;
        }

        std::cerr << "[irc]   waiting for connect (up to 60s)..." << std::endl;

        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;

        bool timedOut = true;
        for (int waited = 0; waited < 60000 && !m_stopRequested.load(); waited += 1000) {
            const int pr = ::poll(&pfd, 1, 1000);
            if (pr > 0) { timedOut = false; break; }
            if (pr < 0 && errno != EINTR) { timedOut = false; break; }
        }

        if (m_stopRequested.load()) {
            std::cerr << "[irc]   connect cancelled by stop" << std::endl;
            ::close(fd);
            break;
        }
        if (timedOut) {
            std::cerr << "[irc]   connect timed out (60s)" << std::endl;
            ::close(fd);
            continue;
        }

        int sockErr = 0;
        socklen_t errLen = sizeof(sockErr);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockErr, &errLen) < 0 || sockErr != 0) {
            std::cerr << "[irc]   connect failed: "
                      << (sockErr ? std::strerror(sockErr) : "getsockopt failed")
                      << std::endl;
            ::close(fd);
            continue;
        }

        ::fcntl(fd, F_SETFL, flags);
        connectedFd = fd;
        std::cerr << "[irc]   connected" << std::endl;
        break;
    }
    ::freeaddrinfo(result);

    if (connectedFd < 0) {
        if (error) {
            *error = m_stopRequested.load() ? "connect cancelled"
                                            : "failed to connect to IRC server";
        }
        std::cerr << "[irc] all addresses failed"
                  << (m_stopRequested.load() ? " (stopped)" : "")
                  << std::endl;
        return false;
    }

    m_socket.store(connectedFd);
    std::cerr << "[irc] socket ready (fd=" << connectedFd << ")" << std::endl;

    // --- TLS handshake ---
    if (m_config.useTls) {
        std::cerr << "[irc] starting TLS handshake..." << std::endl;

        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            if (error) *error = "SSL_CTX_new failed";
            std::cerr << "[irc] SSL_CTX_new failed" << std::endl;
            ::close(connectedFd);
            m_socket.store(-1);
            return false;
        }

        // Don't verify by default — many IRC servers use self-signed certs
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

        SSL* ssl = SSL_new(ctx);
        SSL_CTX_free(ctx);  // SSL holds its own ref
        if (!ssl) {
            if (error) *error = "SSL_new failed";
            std::cerr << "[irc] SSL_new failed" << std::endl;
            ::close(connectedFd);
            m_socket.store(-1);
            return false;
        }

        SSL_set_fd(ssl, connectedFd);
        SSL_set_tlsext_host_name(ssl, m_config.host.c_str());

        // Non-blocking TLS handshake with timeout
        int sslFlags = ::fcntl(connectedFd, F_GETFL, 0);
        ::fcntl(connectedFd, F_SETFL, sslFlags | O_NONBLOCK);

        bool handshakeDone = false;
        for (int waited = 0; waited < 15000 && !m_stopRequested.load(); waited += 1000) {
            int ret = SSL_connect(ssl);
            if (ret == 1) {
                handshakeDone = true;
                break;
            }
            int err = SSL_get_error(ssl, ret);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                // Expected for non-blocking — wait and retry
                struct pollfd pfd;
                pfd.fd = connectedFd;
                pfd.events = (err == SSL_ERROR_WANT_READ) ? POLLIN : POLLOUT;
                pfd.revents = 0;
                ::poll(&pfd, 1, 1000);
                continue;
            }
            // Hard error
            if (error) {
                *error = std::string("TLS handshake failed: ")
                    + ERR_reason_error_string(ERR_get_error());
            }
            std::cerr << "[irc] TLS handshake failed: "
                      << ERR_reason_error_string(ERR_get_error()) << std::endl;
            break;
        }

        // Restore blocking mode
        ::fcntl(connectedFd, F_SETFL, sslFlags);

        if (!handshakeDone) {
            if (m_stopRequested.load()) {
                if (error) *error = "connect cancelled";
                std::cerr << "[irc] TLS handshake cancelled by stop" << std::endl;
            }
            SSL_free(ssl);
            ::close(connectedFd);
            m_socket.store(-1);
            return false;
        }

        m_ssl = ssl;
        std::cerr << "[irc] TLS handshake done (cipher=" << SSL_get_cipher(ssl)
                  << ")" << std::endl;
    }

    return true;
}

// ---------------------------------------------------------------------------
// SendRaw / CloseSocket
// ---------------------------------------------------------------------------

bool IrcInterfaceRuntime::SendRaw(const std::string& line) {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    const int fd = m_socket.load();
    if (fd < 0) {
        return false;
    }
    const std::string payload = line + "\r\n";
    const char* data = payload.data();
    std::size_t remaining = payload.size();
    while (remaining > 0) {
        ssize_t sent;
        if (m_ssl) {
            int s = SSL_write(m_ssl, data, static_cast<int>(remaining));
            sent = (s > 0) ? s : -1;
        } else {
            sent = ::send(fd, data, remaining, 0);
        }
        if (sent <= 0) {
            return false;
        }
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

void IrcInterfaceRuntime::CloseSocket() {
    if (m_ssl) {
        SSL_shutdown(m_ssl);
        SSL_free(m_ssl);
        m_ssl = nullptr;
    }
    const int fd = m_socket.exchange(-1);
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
}

} // namespace animus::kernel

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "animus_kernel/AgentKernel.h"

namespace {

int Fail(int code, const std::string& message) {
    std::cerr << message << "\n";
    return code;
}

std::uint16_t ReserveFreePort() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return 0;
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        ::close(fd);
        return 0;
    }

    const std::uint16_t port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

bool HttpGet(
    const std::string& host,
    std::uint16_t port,
    const std::string& path,
    int* statusCodeOut,
    std::string* bodyOut) {
    if (statusCodeOut) {
        *statusCodeOut = 0;
    }
    if (bodyOut) {
        bodyOut->clear();
    }

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        return false;
    }

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return false;
    }

    const std::string request =
        "GET " + path + " HTTP/1.1\r\nHost: " + host
        + "\r\nConnection: close\r\nAccept: application/json\r\n\r\n";

    if (::send(fd, request.data(), request.size(), 0) < 0) {
        ::close(fd);
        return false;
    }

    std::string response;
    char buffer[4096];
    while (true) {
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n < 0) {
            ::close(fd);
            return false;
        }
        if (n == 0) {
            break;
        }
        response.append(buffer, static_cast<std::size_t>(n));
    }
    ::close(fd);

    const std::size_t lineEnd = response.find("\r\n");
    if (lineEnd == std::string::npos) {
        return false;
    }
    const std::string statusLine = response.substr(0, lineEnd);
    const std::size_t firstSpace = statusLine.find(' ');
    if (firstSpace == std::string::npos) {
        return false;
    }
    const std::size_t secondSpace = statusLine.find(' ', firstSpace + 1);
    const std::string codeText = statusLine.substr(
        firstSpace + 1,
        secondSpace == std::string::npos
            ? std::string::npos
            : secondSpace - (firstSpace + 1));

    int code = 0;
    try {
        code = std::stoi(codeText);
    } catch (...) {
        return false;
    }

    const std::size_t bodyPos = response.find("\r\n\r\n");
    std::string body;
    if (bodyPos != std::string::npos) {
        body = response.substr(bodyPos + 4);
    }

    if (statusCodeOut) {
        *statusCodeOut = code;
    }
    if (bodyOut) {
        *bodyOut = body;
    }
    return true;
}

bool WaitForStatusReady(const std::string& host, std::uint16_t port) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        std::string body;
        int code = 0;
        if (HttpGet(host, port, "/api/v1/status", &code, &body) && code == 200) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

int TestReloadFromDisk() {
    const std::filesystem::path cfgPath =
        std::filesystem::path("build") / "test_data" / "agent_config_reload_test.json";

    {
        std::error_code ec;
        std::filesystem::create_directories(cfgPath.parent_path(), ec);
    }

    {
        std::ofstream out(cfgPath, std::ios::trunc);
        out
            << "{\n"
            << "  \"model\" : {\n"
            << "    \"provider\" : \"openai\",\n"
            << "    \"model_id\" : \"gpt-4.1\",\n"
            << "    \"context_window\" : 222222,\n"
            << "    \"api_key\" : \"sk-reload-token\"\n"
            << "  },\n"
            << "  \"temperature\" : 0.9,\n"
            << "  \"system_prompt\" : \"loaded from disk\",\n"
            << "  \"budget\" : {\n"
            << "    \"max_chain_steps\" : 44,\n"
            << "    \"max_tool_calls_per_chain\" : 11,\n"
            << "    \"timeout_seconds\" : 1900,\n"
            << "    \"token_budget_per_prompt\" : 210000\n"
            << "  }\n"
            << "}\n";
    }

    const std::uint16_t port = ReserveFreePort();
    if (port == 0) {
        return Fail(1, "failed to reserve port for reload test");
    }

    animus::kernel::KernelConfig config;
    config.admin.enabled = true;
    config.admin.host = "127.0.0.1";
    config.admin.port = port;
    config.agentStorage.persistToDisk = true;
    config.agentStorage.filePath = cfgPath.string();

    animus::kernel::AgentKernel kernel;
    std::string error;
    if (!kernel.Start(config, &error)) {
        return Fail(2, "kernel start failed for reload test: " + error);
    }

    if (!WaitForStatusReady(config.admin.host, config.admin.port)) {
        kernel.Stop();
        return Fail(3, "status endpoint did not become ready for reload test");
    }

    std::string body;
    int code = 0;
    if (!HttpGet(config.admin.host, config.admin.port, "/api/v1/agent/model", &code, &body)) {
        kernel.Stop();
        return Fail(4, "GET /api/v1/agent/model failed in reload test");
    }
    if (code != 200) {
        kernel.Stop();
        return Fail(5, "GET /api/v1/agent/model did not return 200 in reload test");
    }
    if (body.find("\"model_id\":\"gpt-4.1\"") == std::string::npos) {
        kernel.Stop();
        return Fail(6, "reloaded model_id does not match expected value");
    }
    if (body.find("\"context_window\":222222") == std::string::npos) {
        kernel.Stop();
        return Fail(7, "reloaded context_window does not match expected value");
    }
    if (body.find("\"api_key\":\"sk-r...****\"") == std::string::npos) {
        kernel.Stop();
        return Fail(8, "reloaded API key mask does not match expected value");
    }

    kernel.Stop();
    return 0;
}

} // namespace

int main() {
    return TestReloadFromDisk();
}
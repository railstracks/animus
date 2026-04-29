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

bool CanConnect(const std::string& host, std::uint16_t port) {
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

    const int result = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::close(fd);
    return result == 0;
}

bool HttpRequest(
    const std::string& host,
    std::uint16_t port,
    const std::string& method,
    const std::string& path,
    const std::string& jsonBody,
    std::string* responseOut,
    int* statusCodeOut,
    std::string* bodyOut) {
    if (responseOut) {
        responseOut->clear();
    }
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

    std::string request;
    request += method + " " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Connection: close\r\n";
    request += "Accept: application/json\r\n";
    if (!jsonBody.empty()) {
        request += "Content-Type: application/json\r\n";
        request += "Content-Length: " + std::to_string(jsonBody.size()) + "\r\n";
    }
    request += "\r\n";
    request += jsonBody;

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
    if (responseOut) {
        *responseOut = response;
    }
    if (bodyOut) {
        *bodyOut = body;
    }
    return true;
}

bool WaitForStatusReady(const std::string& host, std::uint16_t port) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        std::string response;
        std::string body;
        int code = 0;
        if (HttpRequest(host, port, "GET", "/api/v1/status", "", &response, &code, &body)
            && code == 200) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

int TestAdminServerStatusAndShutdown() {
    const std::uint16_t port = ReserveFreePort();
    if (port == 0) {
        return Fail(1, "failed to reserve a free port for admin server test");
    }

    animus::kernel::KernelConfig config;
    config.admin.enabled = true;
    config.admin.host = "127.0.0.1";
    config.admin.port = port;

    animus::kernel::AgentKernel kernel;
    std::string error;
    if (!kernel.Start(config, &error)) {
        return Fail(2, "kernel start failed with admin enabled: " + error);
    }

    if (!WaitForStatusReady(config.admin.host, config.admin.port)) {
        kernel.Stop();
        return Fail(3, "status endpoint did not become ready in time");
    }

    std::string response;
    std::string body;
    int code = 0;
    if (!HttpRequest(
            config.admin.host,
            config.admin.port,
            "GET",
            "/api/v1/status",
            "",
            &response,
            &code,
            &body)) {
        kernel.Stop();
        return Fail(4, "failed to fetch /api/v1/status");
    }
    if (code != 200) {
        kernel.Stop();
        return Fail(5, "status endpoint did not return HTTP 200");
    }
    if (body.find("\"status\":\"ok\"") == std::string::npos) {
        kernel.Stop();
        return Fail(6, "status response does not contain status=ok");
    }
    if (body.find("\"uptime\"") == std::string::npos) {
        kernel.Stop();
        return Fail(7, "status response does not contain uptime");
    }

    kernel.Stop();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (CanConnect(config.admin.host, config.admin.port)) {
        return Fail(8, "admin server still accepts connections after kernel stop");
    }

    return 0;
}

int TestAdminServerDisabled() {
    const std::uint16_t port = ReserveFreePort();
    if (port == 0) {
        return Fail(9, "failed to reserve a free port for admin-disabled test");
    }

    animus::kernel::KernelConfig config;
    config.admin.enabled = false;
    config.admin.host = "127.0.0.1";
    config.admin.port = port;

    animus::kernel::AgentKernel kernel;
    std::string error;
    if (!kernel.Start(config, &error)) {
        return Fail(10, "kernel start failed with admin disabled: " + error);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (CanConnect(config.admin.host, config.admin.port)) {
        kernel.Stop();
        return Fail(11, "admin-disabled kernel unexpectedly opened port");
    }

    kernel.Stop();
    return 0;
}

int TestAgentConfigApiAndPersistence() {
    const std::uint16_t port = ReserveFreePort();
    if (port == 0) {
        return Fail(12, "failed to reserve port for agent config API test");
    }

    const std::filesystem::path cfgPath =
        std::filesystem::path("build") / "test_data" / "agent_config_api_test.json";
    {
        std::error_code ec;
        std::filesystem::remove(cfgPath, ec);
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
        return Fail(13, "kernel start failed for agent config API test: " + error);
    }

    if (!WaitForStatusReady(config.admin.host, config.admin.port)) {
        kernel.Stop();
        return Fail(14, "admin server did not become ready for agent config API test");
    }

    {
        std::string response;
        std::string body;
        int code = 0;
        const std::string patch =
            R"({"temperature":1.2,"system_prompt":"hello","budget":{"max_chain_steps":25}})";
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "PATCH",
                "/api/v1/agent",
                patch,
                &response,
                &code,
                &body)) {
            kernel.Stop();
            return Fail(15, "PATCH /api/v1/agent request failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(16, "PATCH /api/v1/agent did not return 200");
        }
        if (body.find("\"temperature\":1.2") == std::string::npos) {
            kernel.Stop();
            return Fail(17, "PATCH /api/v1/agent did not update temperature");
        }
        if (body.find("\"max_chain_steps\":25") == std::string::npos) {
            kernel.Stop();
            return Fail(18, "PATCH /api/v1/agent did not update budget.max_chain_steps");
        }
    }

    {
        std::string response;
        std::string body;
        int code = 0;
        const std::string putModel =
            R"({"provider":"openai","model_id":"gpt-4.1","context_window":200000,"api_key":"sk-secret-token"})";
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "PUT",
                "/api/v1/agent/model",
                putModel,
                &response,
                &code,
                &body)) {
            kernel.Stop();
            return Fail(19, "PUT /api/v1/agent/model request failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(20, "PUT /api/v1/agent/model did not return 200");
        }
        if (body.find("\"model_id\":\"gpt-4.1\"") == std::string::npos) {
            kernel.Stop();
            return Fail(21, "PUT /api/v1/agent/model did not update model_id");
        }
        if (body.find("\"api_key\":\"sk-s...****\"") == std::string::npos) {
            kernel.Stop();
            return Fail(22, "PUT /api/v1/agent/model did not mask API key as expected");
        }
    }

    {
        std::string response;
        std::string body;
        int code = 0;
        const std::string invalidPatch = R"({"temperature":4.5})";
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "PATCH",
                "/api/v1/agent",
                invalidPatch,
                &response,
                &code,
                &body)) {
            kernel.Stop();
            return Fail(23, "invalid PATCH /api/v1/agent request failed unexpectedly");
        }
        if (code != 400) {
            kernel.Stop();
            return Fail(24, "invalid PATCH /api/v1/agent did not return 400");
        }
    }

    kernel.Stop();

    if (!std::filesystem::exists(cfgPath)) {
        return Fail(25, "agent config file was not persisted to disk");
    }

    {
        std::ifstream in(cfgPath);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (content.find("\"model_id\" : \"gpt-4.1\"") == std::string::npos) {
            return Fail(26, "persisted config file does not include updated model_id");
        }
        if (content.find("\"api_key\" : \"sk-secret-token\"") == std::string::npos) {
            return Fail(27, "persisted config file does not include raw api_key for reload");
        }
    }

    const std::uint16_t restartPort = ReserveFreePort();
    if (restartPort == 0) {
        return Fail(28, "failed to reserve restart port for persistence test");
    }

    animus::kernel::KernelConfig restartConfig;
    restartConfig.admin.enabled = true;
    restartConfig.admin.host = "127.0.0.1";
    restartConfig.admin.port = restartPort;
    restartConfig.agentStorage.persistToDisk = true;
    restartConfig.agentStorage.filePath = cfgPath.string();

    animus::kernel::AgentKernel restartedKernel;
    if (!restartedKernel.Start(restartConfig, &error)) {
        return Fail(29, "restart kernel failed for persistence test: " + error);
    }

    if (!WaitForStatusReady(restartConfig.admin.host, restartConfig.admin.port)) {
        restartedKernel.Stop();
        return Fail(30, "restart admin server did not become ready");
    }

    {
        std::string response;
        std::string body;
        int code = 0;
        if (!HttpRequest(
                restartConfig.admin.host,
                restartConfig.admin.port,
                "GET",
                "/api/v1/agent/model",
                "",
                &response,
                &code,
                &body)) {
            restartedKernel.Stop();
            return Fail(31, "GET /api/v1/agent/model failed after restart");
        }
        if (code != 200) {
            restartedKernel.Stop();
            return Fail(32, "GET /api/v1/agent/model did not return 200 after restart");
        }
        if (body.find("\"model_id\":\"gpt-4.1\"") == std::string::npos) {
            restartedKernel.Stop();
            return Fail(33, "persisted model_id was not loaded after restart");
        }
        if (body.find("\"api_key\":\"sk-s...****\"") == std::string::npos) {
            restartedKernel.Stop();
            return Fail(34, "api key masking missing after restart");
        }
    }

    restartedKernel.Stop();
    return 0;
}

} // namespace

int main() {
    int rc = TestAdminServerStatusAndShutdown();
    if (rc != 0) {
        return rc;
    }
    rc = TestAdminServerDisabled();
    if (rc != 0) {
        return rc;
    }
    rc = TestAgentConfigApiAndPersistence();
    if (rc != 0) {
        return rc;
    }
    return 0;
}
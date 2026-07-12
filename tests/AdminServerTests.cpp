#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "animus_kernel/AgentKernel.h"
#include "animus_kernel/Session.h"
#include "animus_kernel/SessionManager.h"

namespace {

// Forward declarations
bool HttpRequest(
    const std::string& host,
    std::uint16_t port,
    const std::string& method,
    const std::string& path,
    const std::string& jsonBody,
    int* statusCodeOut,
    std::string* bodyOut);

int Fail(int code, const std::string& message) {
    std::cerr << message << "\n";
    return code;
}

bool ExtractJsonNumber(const std::string& body, const std::string& key, double* out) {
    if (!out) {
        return false;
    }
    const std::regex pattern("\"" + key + "\":(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch match;
    if (!std::regex_search(body, match, pattern) || match.size() < 2) {
        return false;
    }
    try {
        *out = std::stod(match[1].str());
    } catch (...) {
        return false;
    }
    return true;
}

bool ExtractJsonUInt64(const std::string& body, const std::string& key, std::uint64_t* out) {
    if (!out) {
        return false;
    }
    const std::regex pattern("\"" + key + "\":([0-9]+)");
    std::smatch match;
    if (!std::regex_search(body, match, pattern) || match.size() < 2) {
        return false;
    }
    try {
        *out = std::stoull(match[1].str());
        return true;
    } catch (...) {
        return false;
    }
}

bool ExtractJsonString(const std::string& body, const std::string& key, std::string* out) {
    if (!out) {
        return false;
    }
    const std::regex pattern("\"" + key + "\":\"([^\"]*)\"");
    std::smatch match;
    if (!std::regex_search(body, match, pattern) || match.size() < 2) {
        return false;
    }
    *out = match[1].str();
    return true;
}

bool ReadExact(int fd, void* buffer, std::size_t size) {
    char* dst = static_cast<char*>(buffer);
    std::size_t total = 0;
    while (total < size) {
        const ssize_t n = ::recv(fd, dst + total, size - total, 0);
        if (n == 0) {
            return false;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        total += static_cast<std::size_t>(n);
    }
    return true;
}

bool ConnectWebSocket(
    const std::string& host,
    std::uint16_t port,
    const std::string& path,
    int* fdOut,
    std::string* responseOut) {
    if (fdOut) {
        *fdOut = -1;
    }
    if (responseOut) {
        responseOut->clear();
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
    request += "GET " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + ":" + std::to_string(port) + "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    request += "\r\n";

    if (::send(fd, request.data(), request.size(), 0) < 0) {
        ::close(fd);
        return false;
    }

    std::string response;
    std::array<char, 1024> buffer{};
    while (response.find("\r\n\r\n") == std::string::npos) {
        const ssize_t n = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (n <= 0) {
            ::close(fd);
            return false;
        }
        response.append(buffer.data(), static_cast<std::size_t>(n));
        if (response.size() > 64 * 1024) {
            ::close(fd);
            return false;
        }
    }

    if (response.rfind("HTTP/1.1 101", 0) != 0 && response.rfind("HTTP/1.0 101", 0) != 0) {
        if (responseOut) {
            *responseOut = response;
        }
        ::close(fd);
        return false;
    }

    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 200 * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (fdOut) {
        *fdOut = fd;
    } else {
        ::close(fd);
    }
    if (responseOut) {
        *responseOut = response;
    }
    return true;
}

bool SendWebSocketText(int fd, const std::string& payload) {
    std::vector<std::uint8_t> frame;
    frame.reserve(payload.size() + 16);
    frame.push_back(0x81U);  // FIN + text

    const std::size_t length = payload.size();
    if (length <= 125U) {
        frame.push_back(static_cast<std::uint8_t>(0x80U | length));
    } else if (length <= 65535U) {
        frame.push_back(0x80U | 126U);
        frame.push_back(static_cast<std::uint8_t>((length >> 8) & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>(length & 0xFFU));
    } else {
        return false;
    }

    const std::array<std::uint8_t, 4> mask{{0x12U, 0x34U, 0x56U, 0x78U}};
    frame.insert(frame.end(), mask.begin(), mask.end());

    for (std::size_t i = 0; i < payload.size(); ++i) {
        frame.push_back(static_cast<std::uint8_t>(payload[i]) ^ mask[i % 4U]);
    }

    const ssize_t n = ::send(fd, frame.data(), frame.size(), 0);
    return n == static_cast<ssize_t>(frame.size());
}

bool ReadWebSocketFrame(int fd, int* opcodeOut, std::string* payloadOut) {
    std::uint8_t header[2] = {0, 0};
    if (!ReadExact(fd, header, sizeof(header))) {
        return false;
    }

    const int opcode = static_cast<int>(header[0] & 0x0FU);
    std::uint64_t length = static_cast<std::uint64_t>(header[1] & 0x7FU);
    const bool masked = (header[1] & 0x80U) != 0U;

    if (length == 126U) {
        std::uint8_t ext[2] = {0, 0};
        if (!ReadExact(fd, ext, sizeof(ext))) {
            return false;
        }
        length = (static_cast<std::uint64_t>(ext[0]) << 8) | ext[1];
    } else if (length == 127U) {
        std::uint8_t ext[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        if (!ReadExact(fd, ext, sizeof(ext))) {
            return false;
        }
        length = 0U;
        for (std::size_t i = 0; i < 8U; ++i) {
            length = (length << 8U) | ext[i];
        }
    }

    std::array<std::uint8_t, 4> mask{{0, 0, 0, 0}};
    if (masked) {
        if (!ReadExact(fd, mask.data(), mask.size())) {
            return false;
        }
    }

    std::vector<std::uint8_t> bytes(length);
    if (length > 0U && !ReadExact(fd, bytes.data(), bytes.size())) {
        return false;
    }

    if (masked) {
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] ^= mask[i % 4U];
        }
    }

    if (payloadOut) {
        payloadOut->assign(bytes.begin(), bytes.end());
    }
    if (opcodeOut) {
        *opcodeOut = opcode;
    }
    return true;
}

bool WaitForWebSocketPayloadContaining(
    int fd,
    const std::string& needle,
    int timeoutMs,
    std::string* matchedPayloadOut) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        int opcode = 0;
        std::string payload;
        if (!ReadWebSocketFrame(fd, &opcode, &payload)) {
            continue;
        }
        if (opcode == 1 && payload.find(needle) != std::string::npos) {
            if (matchedPayloadOut) {
                *matchedPayloadOut = payload;
            }
            return true;
        }
    }
    return false;
}


bool WaitForConsolidationIdle(
    const std::string& host,
    std::uint16_t port,
    std::uint64_t expectedLastJobId,
    std::string* statusOut) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                host,
                port,
                "GET",
                "/api/v1/memory/consolidation/status",
                "",
                &code,
                &body) || code != 200) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        const std::string stateNeedle = "\"state\":\"idle\"";
        const std::string jobNeedle = "\"last_job_id\":" + std::to_string(expectedLastJobId);
        if (body.find(stateNeedle) != std::string::npos
            && body.find(jobNeedle) != std::string::npos) {
            if (statusOut) {
                *statusOut = body;
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
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

bool HttpRequest(
    const std::string& host,
    std::uint16_t port,
    const std::string& method,
    const std::string& path,
    const std::string& jsonBody,
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
        if (HttpRequest(host, port, "GET", "/api/v1/status", "", &code, &body) && code == 200) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

int TestAdminServerAndAgentApi() {
    const std::uint16_t port = ReserveFreePort();
    if (port == 0) {
        return Fail(1, "failed to reserve a free port for admin API test");
    }

    const std::filesystem::path cfgPath =
        std::filesystem::path("build") / "test_data" / "agent_config_api_test.json";
    const std::filesystem::path interfacePath =
        std::filesystem::path("build") / "test_data" / "interface_api_test.json";
    const std::filesystem::path memoryPath =
        std::filesystem::path("build") / "test_data" / "memory_api_test.json";
    const std::filesystem::path constitutionPath =
        std::filesystem::path("build") / "test_data" / "constitution_api_test.json";
    const std::filesystem::path providersPath =
        std::filesystem::path("build") / "test_data" / "providers_api_test.json";
    const std::filesystem::path authPath =
        std::filesystem::path("build") / "test_data" / "auth_api_test.json";
    {
        std::error_code ec;
        std::filesystem::remove(cfgPath, ec);
        std::filesystem::remove(interfacePath, ec);
        std::filesystem::remove(memoryPath, ec);
        std::filesystem::remove(constitutionPath, ec);
        std::filesystem::remove(providersPath, ec);
        std::filesystem::remove(authPath, ec);
        // AgentKernel currently uses fixed datastore/session paths.
        // Clear these to keep this test deterministic across runs.
        std::filesystem::remove("state/memory.db", ec);
        std::filesystem::remove("state/sessions.json", ec);
        std::filesystem::remove("state/sessions.json.migrated", ec);
        std::filesystem::remove("build/state/memory.db", ec);
        std::filesystem::remove("build/state/sessions.json", ec);
        std::filesystem::remove("build/state/sessions.json.migrated", ec);
    }
    std::filesystem::create_directories(cfgPath.parent_path());

    {
        std::ofstream out(memoryPath, std::ios::trunc);
        if (!out.is_open()) {
            return Fail(2, "failed to create memory fixture file");
        }
        out << R"({
  "layers": [
    {
      "name": "day",
      "description": "Day layer",
      "retention_policy": "48h",
      "horizon": "1d",
      "consolidation_interval": "1h",
      "perspective_past": "past-day retrospective",
      "perspective_current": "current-day picture",
      "perspective_future": "next-day expectation"
    },
    {"name": "week", "description": "Week layer", "retention_policy": "14d"},
    {"name": "month", "description": "Month layer", "retention_policy": "60d"}
  ],
  "observations": [
    {
      "id": "obs_day_high",
      "text": "High-weight day observation",
      "tags": ["ops", "priority"],
      "timestamp_unix_ms": 1700000100000,
      "weight": 2.5,
      "decay_rate": 0.92,
      "layer": "day",
      "created_in_layer": "day"
    },
    {
      "id": "obs_day_low",
      "text": "Low-weight day observation",
      "tags": ["ops"],
      "timestamp_unix_ms": 1700000200000,
      "weight": 0.7,
      "decay_rate": 0.97,
      "layer": "day",
      "created_in_layer": "day"
    },
    {
      "id": "obs_week",
      "text": "Week layer observation",
      "tags": ["planning"],
      "timestamp_unix_ms": 1690000000000,
      "weight": 1.4,
      "decay_rate": 0.95,
      "layer": "week",
      "created_in_layer": "day"
    }
  ],
  "consolidation": {
    "last_run_unix_ms": 1700000300000,
    "last_job_id": 7,
    "last_error": "",
    "last_summary": {
      "promoted": 1,
      "demoted": 2,
      "archived": 0
    }
  }
}
)";
    }

    {
        std::ofstream out(constitutionPath, std::ios::trunc);
        if (!out.is_open()) {
            return Fail(57, "failed to create constitution fixture file");
        }
        out << R"({
  "core": {
    "identity": "animus-test",
    "invariants": ["protect_core_identity"]
  },
  "contracts": [
    {
      "id": "contract.test",
      "approval_requirement": "governance_approved"
    }
  ],
  "adaptation": {
    "interaction_style": {
      "tone": "balanced",
      "verbosity": "medium"
    },
    "retrieval_preference": {
      "weight_recency": 0.6
    }
  },
  "audit_log": []
}
)";
    }

    {
        std::ofstream out(providersPath, std::ios::trunc);
        if (!out.is_open()) {
            return Fail(58, "failed to create providers fixture file");
        }
        // Use a loopback endpoint that refuses quickly so websocket tests can
        // validate deterministic error/done flow without external dependencies.
        out << R"({
  "default_provider": "openai",
  "providers": {
    "openai": {
      "provider_type": "openai",
      "base_url": "http://127.0.0.1:9/v1",
      "api_key": "sk-test",
      "default_model": "gpt-4.1-mini",
      "default_context_window": 128000,
      "auth_type": "api_key",
      "concurrency": 1
    }
  }
}
)";
    }

    {
        std::ofstream out(authPath, std::ios::trunc);
        if (!out.is_open()) {
            return Fail(59, "failed to create auth fixture file");
        }
        out << "{}\n";
    }

    animus::kernel::KernelConfig config;
    config.admin.enabled = true;
    config.admin.host = "127.0.0.1";
    config.admin.port = port;
    config.admin.authToken = "top-secret-token";
    config.admin.allowUnauthenticatedLocalhost = true;
    config.admin.websocketMaxMessageBytes = 1024;
    config.agentStorage.persistToDisk = true;
    config.agentStorage.filePath = cfgPath.string();
    config.interfaceStorage.persistToDisk = true;
    config.interfaceStorage.filePath = interfacePath.string();
    config.memory.persistToDisk = true;
    config.memory.filePath = memoryPath.string();
    config.constitutionStorage.persistToDisk = true;
    config.constitutionStorage.filePath = constitutionPath.string();
    config.providerStorage.persistToDisk = true;
    config.providerStorage.providersFilePath = providersPath.string();
    config.providerStorage.authFilePath = authPath.string();
    std::filesystem::path modulePath =
        std::filesystem::path("dist") / "modules" / "libanimus_module_example_hello.so";
    if (!std::filesystem::exists(modulePath)) {
        const std::filesystem::path altPath =
            std::filesystem::path("..") / "dist" / "modules" / "libanimus_module_example_hello.so";
        if (std::filesystem::exists(altPath)) {
            modulePath = altPath;
        } else {
            return Fail(39, "expected example module binary is missing");
        }
    }
    config.modulePaths.push_back(modulePath.string());

    animus::kernel::AgentKernel kernel;
    std::string error;
    if (!kernel.Start(config, &error)) {
        return Fail(3, "kernel start failed with admin enabled: " + error);
    }

    if (!WaitForStatusReady(config.admin.host, config.admin.port)) {
        kernel.Stop();
        return Fail(4, "status endpoint did not become ready in time");
    }

    {
        std::string createBody;
        int createCode = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "POST",
                "/api/v1/memory/layers",
                R"({"name":"day_sql","horizon":"1 day","sort_order":0,"evaluation_interval_seconds":86400,"cron_expr":"0 * * * *","consolidation_prompt":"review","token_budget":4096,"enabled":true})",
                &createCode,
                &createBody)) {
            kernel.Stop();
            return Fail(100, "POST /api/v1/memory/layers failed");
        }
        if (createCode != 201) {
            kernel.Stop();
            return Fail(1001, "POST /api/v1/memory/layers did not return 201");
        }
        if (createBody.find("\"name\":\"day_sql\"") == std::string::npos
            || createBody.find("\"horizon\":\"1 day\"") == std::string::npos
            || createBody.find("\"evaluation_interval_seconds\":86400") == std::string::npos
            || createBody.find("\"cron_expr\":\"0 * * * *\"") == std::string::npos) {
            kernel.Stop();
            return Fail(1002, "created SQL layer payload missing expected fields");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/status",
                "",
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
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/scheduler/status",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(701, "failed to fetch /api/v1/scheduler/status");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(702, "scheduler status endpoint did not return HTTP 200");
        }
        if (body.find("\"running\":") == std::string::npos) {
            kernel.Stop();
            return Fail(703, "scheduler status response does not include running field");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/scheduler/schedules?agent_id=system&include_disabled=true",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(704, "failed to fetch /api/v1/scheduler/schedules");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(705, "scheduler schedules endpoint did not return HTTP 200");
        }
        if (body.find("\"schedules\"") == std::string::npos) {
            kernel.Stop();
            return Fail(706, "scheduler schedules response does not include schedules field");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/memory/templates/review?locale=en",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(709, "failed to fetch /api/v1/memory/templates/review");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(710, "memory template endpoint did not return HTTP 200");
        }
        if (body.find("\"content\"") == std::string::npos) {
            kernel.Stop();
            return Fail(711, "memory template endpoint did not include content");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/ontology/categories",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(712, "failed to fetch /api/v1/ontology/categories");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(713, "ontology categories endpoint did not return HTTP 200");
        }
        if (body.find("\"category\":\"concepts\"") == std::string::npos) {
            kernel.Stop();
            return Fail(714, "ontology categories response missing concepts category");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/ontology/entities",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(715, "failed to fetch /api/v1/ontology/entities");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(716, "ontology entities endpoint did not return HTTP 200");
        }
        if (body.find("[") == std::string::npos) {
            kernel.Stop();
            return Fail(717, "ontology entities response is not an array");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/ontology/mutations",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(718, "failed to fetch /api/v1/ontology/mutations");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(719, "ontology mutations endpoint did not return HTTP 200");
        }
        if (body.find("[") == std::string::npos) {
            kernel.Stop();
            return Fail(720, "ontology mutations response is not an array");
        }
    }

    std::uint64_t memoryFileId = 0;
    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "POST",
                "/api/v1/memory-files/import",
                R"({"source_path":"/tmp/memory-file-1.txt","file_type":"external_doc","content":"ticket047keyword from raw memory file"})",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(721, "failed to import memory file");
        }
        if (code != 201) {
            kernel.Stop();
            return Fail(722, "memory file import did not return HTTP 201");
        }
        if (!ExtractJsonUInt64(body, "id", &memoryFileId) || memoryFileId == 0) {
            kernel.Stop();
            return Fail(723, "memory file import response missing id");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/memory-files",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(724, "failed to fetch memory file list");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(725, "memory file list endpoint did not return HTTP 200");
        }
        if (body.find("memory-file-1.txt") == std::string::npos) {
            kernel.Stop();
            return Fail(726, "memory file list missing imported row");
        }
        if (body.find("ticket047-keyword") != std::string::npos) {
            kernel.Stop();
            return Fail(727, "memory file list should not expose full content");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/memory-files/" + std::to_string(memoryFileId),
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(728, "failed to fetch memory file detail");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(729, "memory file detail endpoint did not return HTTP 200");
        }
        if (body.find("ticket047keyword from raw memory file") == std::string::npos) {
            kernel.Stop();
            return Fail(730, "memory file detail should include content");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "PATCH",
                "/api/v1/memory-files/" + std::to_string(memoryFileId) + "/metadata",
                R"({"source_path":"/tmp/memory-file-1-v2.txt","content":"ticket047keyword content updated","content_mutable":true})",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(731, "failed to patch memory file metadata");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(732, "memory file metadata patch did not return HTTP 200");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/memory-files/" + std::to_string(memoryFileId),
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(7321, "failed to fetch memory file detail after update");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(7322, "memory file detail after update did not return HTTP 200");
        }
        if (body.find("ticket047keyword content updated") == std::string::npos) {
            kernel.Stop();
            return Fail(7323, "memory file content update was not persisted");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "PATCH",
                "/api/v1/memory-files/" + std::to_string(memoryFileId) + "/metadata",
                R"({"content_mutable":false})",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(7324, "failed to disable memory file content mutability");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(7325, "disabling memory file mutability did not return HTTP 200");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "PATCH",
                "/api/v1/memory-files/" + std::to_string(memoryFileId) + "/metadata",
                R"({"content":"blocked immutable write"})",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(7326, "failed to test immutable memory file write");
        }
        if (code != 400) {
            kernel.Stop();
            return Fail(7327, "immutable memory file write should return HTTP 400");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/memory-files/stats",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(733, "failed to fetch memory file stats");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(734, "memory file stats did not return HTTP 200");
        }
        if (body.find("\"total\":") == std::string::npos
            || body.find("\"external_doc\":") == std::string::npos) {
            kernel.Stop();
            return Fail(735, "memory file stats missing expected fields");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "POST",
                "/api/v1/memory/search",
                R"({"query":"ticket047keyword","domains":{"observations":false,"ontology":false,"raw_files":true,"diary":false},"limit":10})",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(736, "failed to perform memory search");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(737, "memory search did not return HTTP 200");
        }
        if (body.find("\"domain\":\"memory_file\"") == std::string::npos) {
            kernel.Stop();
            return Fail(738, "memory search missing memory_file domain result");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "DELETE",
                "/api/v1/memory-files/" + std::to_string(memoryFileId),
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(739, "failed to delete memory file");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(740, "memory file delete did not return HTTP 200");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/memory-files/" + std::to_string(memoryFileId),
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(741, "failed to fetch deleted memory file detail");
        }
        if (code != 404) {
            kernel.Stop();
            return Fail(742, "deleted memory file detail should return HTTP 404");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/scheduler/schedules",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(707, "failed to fetch /api/v1/scheduler/schedules without required params");
        }
        if (code != 400) {
            kernel.Stop();
            return Fail(708, "scheduler schedules endpoint should require agent_id");
        }
    }

    {
        std::string body;
        int code = 0;
        const std::string patch =
            R"({"temperature":1.2,"identity":"hello","budget":{"max_chain_steps":25}})";
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "PATCH",
                "/api/v1/agent",
                patch,
                &code,
                &body)) {
            kernel.Stop();
            return Fail(8, "PATCH /api/v1/agent request failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(9, "PATCH /api/v1/agent did not return 200");
        }
        if (body.find("\"temperature\":1.2") == std::string::npos) {
            kernel.Stop();
            return Fail(10, "PATCH /api/v1/agent did not update temperature");
        }
        if (body.find("\"max_chain_steps\":25") == std::string::npos) {
            kernel.Stop();
            return Fail(11, "PATCH /api/v1/agent did not update budget.max_chain_steps");
        }
    }

    {
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
                &code,
                &body)) {
            kernel.Stop();
            return Fail(12, "PUT /api/v1/agent/model request failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(13, "PUT /api/v1/agent/model did not return 200");
        }
        if (body.find("\"model_id\":\"gpt-4.1\"") == std::string::npos) {
            kernel.Stop();
            return Fail(14, "PUT /api/v1/agent/model did not update model_id");
        }
        if (body.find("\"api_key\":\"sk-s...****\"") == std::string::npos) {
            kernel.Stop();
            return Fail(15, "PUT /api/v1/agent/model did not mask API key as expected");
        }
    }

    {
        std::string body;
        int code = 0;
        const std::string invalidPatch = R"({"temperature":4.5})";
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "PATCH",
                "/api/v1/agent",
                invalidPatch,
                &code,
                &body)) {
            kernel.Stop();
            return Fail(16, "invalid PATCH /api/v1/agent request failed unexpectedly");
        }
        if (code != 400) {
            kernel.Stop();
            return Fail(17, "invalid PATCH /api/v1/agent did not return 400");
        }
    }

    // --- Reasoning mode toggle tests ---
    {
        // GET /api/v1/agent should show reasoning disabled by default
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/agent",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(190, "GET /api/v1/agent for reasoning state failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(191, "GET /api/v1/agent for reasoning state did not return 200");
        }
        if (body.find("\"reasoning\"") == std::string::npos) {
            kernel.Stop();
            return Fail(192, "GET /api/v1/agent missing reasoning field");
        }
        if (body.find("\"enabled\":false") == std::string::npos) {
            kernel.Stop();
            return Fail(193, "reasoning should be disabled by default");
        }
    }

    {
        // PATCH /api/v1/agent with reasoning enabled
        std::string body;
        int code = 0;
        const std::string enableReasoning = R"({"reasoning":{"enabled":true,"instruction":"Think step by step"}})";
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "PATCH",
                "/api/v1/agent",
                enableReasoning,
                &code,
                &body)) {
            kernel.Stop();
            return Fail(194, "PATCH /api/v1/agent reasoning enable failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(195, "PATCH /api/v1/agent reasoning enable did not return 200");
        }
        if (body.find("\"enabled\":true") == std::string::npos) {
            kernel.Stop();
            return Fail(196, "reasoning enable did not reflect enabled=true");
        }
        if (body.find("\"instruction\":\"Think step by step\"") == std::string::npos) {
            kernel.Stop();
            return Fail(197, "reasoning enable did not reflect instruction");
        }
    }

    {
        // PATCH /api/v1/agent with reasoning disabled
        std::string body;
        int code = 0;
        const std::string disableReasoning = R"({"reasoning":{"enabled":false}})";
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "PATCH",
                "/api/v1/agent",
                disableReasoning,
                &code,
                &body)) {
            kernel.Stop();
            return Fail(198, "PATCH /api/v1/agent reasoning disable failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(199, "PATCH /api/v1/agent reasoning disable did not return 200");
        }
        if (body.find("\"enabled\":false") == std::string::npos) {
            kernel.Stop();
            return Fail(200, "reasoning disable did not reflect enabled=false");
        }
    }

    const std::string interfaceName = "example.hello";
    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/interfaces",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(40, "GET /api/v1/interfaces failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(41, "GET /api/v1/interfaces did not return 200");
        }
        if (body.find("\"name\":\"" + interfaceName + "\"") == std::string::npos) {
            kernel.Stop();
            return Fail(42, "interface list does not reflect loaded module");
        }
        if (body.find("\"enabled\":true") == std::string::npos
            || body.find("\"connected\":true") == std::string::npos) {
            kernel.Stop();
            return Fail(43, "interface list missing expected status flags");
        }
    }

    {
        std::string body;
        int code = 0;
        const std::string path = "/api/v1/interfaces/" + interfaceName;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                path,
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(44, "GET /api/v1/interfaces/:name failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(45, "GET /api/v1/interfaces/:name did not return 200");
        }
        if (body.find("\"type\":\"generic\"") == std::string::npos) {
            kernel.Stop();
            return Fail(46, "interface detail missing expected type");
        }
    }

    {
        std::string body;
        int code = 0;
        const std::string path = "/api/v1/interfaces/" + interfaceName;
        const std::string patch =
            R"({"config":{"api_key":"sk-super-secret","channel":"C123"}})";
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "PATCH",
                path,
                patch,
                &code,
                &body)) {
            kernel.Stop();
            return Fail(47, "PATCH /api/v1/interfaces/:name failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(48, "PATCH /api/v1/interfaces/:name did not return 200");
        }
        if (body.find("\"api_key\":\"sk-s...****\"") == std::string::npos) {
            kernel.Stop();
            return Fail(49, "interface PATCH response did not mask sensitive field");
        }
    }

    {
        std::string body;
        int code = 0;
        const std::string path = "/api/v1/interfaces/" + interfaceName + "/disable";
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "POST",
                path,
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(50, "POST /api/v1/interfaces/:name/disable failed");
        }
        if (code != 200 || body.find("\"enabled\":false") == std::string::npos
            || body.find("\"connected\":false") == std::string::npos) {
            kernel.Stop();
            return Fail(51, "disable endpoint did not toggle status");
        }
    }

    {
        std::string body;
        int code = 0;
        const std::string path = "/api/v1/interfaces/" + interfaceName + "/enable";
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "POST",
                path,
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(52, "POST /api/v1/interfaces/:name/enable failed");
        }
        if (code != 200 || body.find("\"enabled\":true") == std::string::npos
            || body.find("\"connected\":true") == std::string::npos) {
            kernel.Stop();
            return Fail(53, "enable endpoint did not toggle status");
        }
    }

    const std::string customInterfaceName = "irc-tenant-a";
    {
        std::string body;
        int code = 0;
        const std::string createPayload = R"({
            "name":"irc-tenant-a",
            "type":"irc",
            "enabled":true,
            "config":{
                "host":"irc.example.net",
                "port":6667,
                "nick":"animus_a",
                "dm_only":true
            }
        })";
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "POST",
                "/api/v1/interfaces",
                createPayload,
                &code,
                &body)) {
            kernel.Stop();
            return Fail(201, "POST /api/v1/interfaces failed");
        }
        if (code != 200 || body.find("\"name\":\"" + customInterfaceName + "\"") == std::string::npos
            || body.find("\"type\":\"irc\"") == std::string::npos) {
            kernel.Stop();
            return Fail(202, "interface create endpoint did not return created IRC interface");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/interfaces",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(203, "GET /api/v1/interfaces after create failed");
        }
        if (code != 200 || body.find("\"name\":\"" + customInterfaceName + "\"") == std::string::npos) {
            kernel.Stop();
            return Fail(204, "created interface missing from list");
        }
    }

    {
        std::string body;
        int code = 0;
        const std::string path = "/api/v1/interfaces/" + customInterfaceName;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "DELETE",
                path,
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(205, "DELETE /api/v1/interfaces/:name failed");
        }
        if (code != 200 || body.find("\"deleted\":true") == std::string::npos) {
            kernel.Stop();
            return Fail(206, "interface delete endpoint did not confirm deletion");
        }
    }

    {
        std::string body;
        int code = 0;
        const std::string path = "/api/v1/interfaces/" + customInterfaceName;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                path,
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(207, "GET deleted interface failed");
        }
        if (code != 404) {
            kernel.Stop();
            return Fail(208, "deleted interface should return 404");
        }
    }

    animus::kernel::SessionKey sessionKey1{"slack", "C123", "thread-1"};
    auto session1 = kernel.Sessions().GetOrCreate(sessionKey1);
    animus::kernel::SessionTurn s1t1;
    s1t1.role = "user";
    s1t1.content = "hello from user";
    s1t1.unix_ms = 1700000000000ULL;
    session1->AddTurn(s1t1);

    animus::kernel::SessionTurn s1t2;
    s1t2.role = "assistant";
    s1t2.content = "assistant reply";
    s1t2.unix_ms = 1700000001000ULL;
    session1->AddTurn(s1t2);

    animus::kernel::SessionKey sessionKey2{"cli", "global", ""};
    auto session2 = kernel.Sessions().GetOrCreate(sessionKey2);
    animus::kernel::SessionTurn s2t1;
    s2t1.role = "user";
    s2t1.content = "second session";
    s2t1.unix_ms = 1700000002000ULL;
    session2->AddTurn(s2t1);

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/sessions",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(18, "GET /api/v1/sessions failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(19, "GET /api/v1/sessions did not return 200");
        }
        if (body.find("\"total\":2") == std::string::npos) {
            kernel.Stop();
            return Fail(20, "GET /api/v1/sessions total count mismatch");
        }
    }

    {
        std::string body;
        int code = 0;
        const std::string path = "/api/v1/sessions/" + std::to_string(session1->Id());
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                path,
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(21, "GET /api/v1/sessions/:id failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(22, "GET /api/v1/sessions/:id did not return 200");
        }
        if (body.find("\"message_count\":2") == std::string::npos) {
            kernel.Stop();
            return Fail(23, "GET /api/v1/sessions/:id message count mismatch");
        }
        if (body.find("\"source\":\"slack\"") == std::string::npos) {
            kernel.Stop();
            return Fail(24, "GET /api/v1/sessions/:id source mismatch");
        }
    }

    {
        std::string body;
        int code = 0;
        const std::string path = "/api/v1/sessions/" + std::to_string(session1->Id())
            + "/history?page=1&limit=1";
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                path,
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(25, "GET /api/v1/sessions/:id/history failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(26, "GET /api/v1/sessions/:id/history did not return 200");
        }
        if (body.find("\"total\":2") == std::string::npos) {
            kernel.Stop();
            return Fail(27, "history total mismatch");
        }
        if (body.find("\"content\":\"assistant reply\"") == std::string::npos) {
            kernel.Stop();
            return Fail(28, "history is not ordered most-recent-first");
        }
    }

    {
        std::string body;
        int code = 0;
        const std::string path = "/api/v1/sessions/" + std::to_string(session1->Id()) + "/context";
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                path,
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(29, "GET /api/v1/sessions/:id/context failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(30, "GET /api/v1/sessions/:id/context did not return 200");
        }
        if (body.find("\"configured_token_budget_per_prompt\":200000") == std::string::npos) {
            kernel.Stop();
            return Fail(31, "session context token budget payload mismatch");
        }
    }

    {
        std::string body;
        int code = 0;
        const std::string path = "/api/v1/sessions/" + std::to_string(session1->Id());
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "DELETE",
                path,
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(32, "DELETE /api/v1/sessions/:id failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(33, "DELETE /api/v1/sessions/:id did not return 200");
        }
    }

    {
        std::string body;
        int code = 0;
        const std::string path = "/api/v1/sessions/" + std::to_string(session1->Id());
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                path,
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(34, "post-delete GET /api/v1/sessions/:id request failed");
        }
        if (code != 404) {
            kernel.Stop();
            return Fail(35, "deleted session still retrievable");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/memory/layers",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(101, "GET /api/v1/memory/layers failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(102, "GET /api/v1/memory/layers did not return 200");
        }
        if (body.find("[") == std::string::npos) {
            kernel.Stop();
            return Fail(103, "memory layers payload is not an array");
        }
        if (body.find("\"name\":\"day_sql\"") == std::string::npos) {
            kernel.Stop();
            return Fail(105, "memory layers do not include created SQL layer");
        }
        if (body.find("\"horizon\":\"1 day\"") == std::string::npos
            || body.find("\"evaluation_interval_seconds\":86400") == std::string::npos
            || body.find("\"cron_expr\":\"0 * * * *\"") == std::string::npos
            || body.find("\"sort_order\":0") == std::string::npos) {
            kernel.Stop();
            return Fail(176, "memory layer metadata missing post-042 fields");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/memory/layers/day/observations?page=1&limit=1&sort=weight&order=desc",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(107, "GET layer observations (page 1) failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(108, "GET layer observations (page 1) did not return 200");
        }
        if (body.find("\"total\":2") == std::string::npos
            || body.find("\"limit\":1") == std::string::npos) {
            kernel.Stop();
            return Fail(110, "layer observations pagination metadata mismatch");
        }
        if (body.find("\"id\":\"obs_day_high\"") == std::string::npos) {
            kernel.Stop();
            return Fail(111, "weight sort did not return highest-weight observation first");
        }

        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/memory/layers/day/observations?page=2&limit=1&sort=weight&order=desc",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(112, "GET layer observations (page 2) failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(113, "GET layer observations (page 2) did not return 200");
        }
        if (body.find("\"id\":\"obs_day_low\"") == std::string::npos) {
            kernel.Stop();
            return Fail(115, "pagination page 2 did not return second observation");
        }

        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/memory/layers/day/observations?page=1&limit=999",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(116, "GET layer observations (limit clamp) failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(117, "GET layer observations (limit clamp) did not return 200");
        }
        if (body.find("\"limit\":200") == std::string::npos) {
            kernel.Stop();
            return Fail(119, "layer observations limit max clamp expected 200");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/memory/observations/obs_day_high",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(120, "GET observation detail failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(121, "GET observation detail did not return 200");
        }
        if (body.find("\"tags\"") == std::string::npos
            || body.find("\"weight\"") == std::string::npos
            || body.find("\"decay_rate\"") == std::string::npos
            || body.find("\"timestamp\"") == std::string::npos
            || body.find("\"timestamp_unix_ms\"") == std::string::npos
            || body.find("\"content\"") == std::string::npos
            || body.find("\"text\"") == std::string::npos
            || body.find("\"created_in_layer\"") == std::string::npos
            || body.find("\"demotion_reason\"") == std::string::npos
            || body.find("\"demotion_timestamp\"") == std::string::npos
            || body.find("\"demotion_timestamp_unix_ms\"") == std::string::npos) {
            kernel.Stop();
            return Fail(123, "observation detail is missing expected metadata fields");
        }
        if (body.find("\"content\":\"High-weight day observation\"") == std::string::npos) {
            kernel.Stop();
            return Fail(124, "observation content field does not match source text");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "PATCH",
                "/api/v1/memory/observations/obs_day_low",
                R"({"tags":["ops","triaged"]})",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(125, "PATCH observation tags failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(126, "PATCH observation tags did not return 200");
        }
        if (body.find("\"tags\":[\"ops\",\"triaged\"]") == std::string::npos) {
            kernel.Stop();
            return Fail(128, "PATCH observation tags did not apply partial tag update");
        }
        double weightAfterTagPatch = 0.0;
        if (!ExtractJsonNumber(body, "weight", &weightAfterTagPatch)
            || std::fabs(weightAfterTagPatch - 0.7) > 0.0001) {
            kernel.Stop();
            return Fail(129, "PATCH tags unexpectedly changed weight");
        }

        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "PATCH",
                "/api/v1/memory/observations/obs_day_low",
                R"({"weight":1.9})",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(130, "PATCH observation weight failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(131, "PATCH observation weight did not return 200");
        }
        double weightAfterWeightPatch = 0.0;
        if (!ExtractJsonNumber(body, "weight", &weightAfterWeightPatch)
            || std::fabs(weightAfterWeightPatch - 1.9) > 0.0001) {
            kernel.Stop();
            return Fail(133, "PATCH observation weight did not apply partial weight update");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "DELETE",
                "/api/v1/memory/observations/obs_week",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(134, "DELETE observation failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(135, "DELETE observation did not return 200");
        }
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/memory/observations/obs_week",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(136, "GET archived observation failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(137, "GET archived observation did not return 200");
        }
        if (body.find("\"layer\":\"archive\"") == std::string::npos
            || body.find("\"demotion_reason\":\"removed_via_api\"") == std::string::npos) {
            kernel.Stop();
            return Fail(139, "DELETE observation did not archive with demotion metadata");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "POST",
                "/api/v1/memory/consolidate",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(140, "POST /api/v1/memory/consolidate failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(141, "POST /api/v1/memory/consolidate did not return 200");
        }
        std::uint64_t jobId = 0;
        if (!ExtractJsonUInt64(body, "job_id", &jobId)) {
            kernel.Stop();
            return Fail(142, "POST /api/v1/memory/consolidate missing job_id");
        }
        if (jobId != 8) {
            kernel.Stop();
            return Fail(143, "consolidation job id continuity mismatch (expected 8 from fixture)");
        }
        std::string status;
        if (!WaitForConsolidationIdle(config.admin.host, config.admin.port, jobId, &status)) {
            kernel.Stop();
            return Fail(144, "consolidation status did not reach idle with expected last_job_id");
        }
        if (status.find("\"last_summary\"") == std::string::npos) {
            kernel.Stop();
            return Fail(145, "consolidation status missing last_summary");
        }
        std::uint64_t lastRunUnixMs = 0;
        if (!ExtractJsonUInt64(status, "last_run_unix_ms", &lastRunUnixMs) || lastRunUnixMs == 0) {
            kernel.Stop();
            return Fail(146, "consolidation status missing last_run_unix_ms");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "GET",
                "/api/v1/constitution",
                "",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(177, "GET /api/v1/constitution failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(178, "GET /api/v1/constitution did not return 200");
        }
        if (body.find("\"layers\"") == std::string::npos
            || body.find("\"layer_type\":\"core\"") == std::string::npos
            || body.find("\"layer_type\":\"contracts\"") == std::string::npos
            || body.find("\"layer_type\":\"adaptation\"") == std::string::npos) {
            kernel.Stop();
            return Fail(179, "constitution payload missing layer labels");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "PATCH",
                "/api/v1/constitution/core",
                R"({"identity":"new-core"})",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(180, "PATCH /api/v1/constitution/core failed");
        }
        if (code != 405) {
            kernel.Stop();
            return Fail(181, "PATCH /api/v1/constitution/core did not return 405");
        }
    }

    {
        std::string body;
        int code = 0;
        if (!HttpRequest(
                config.admin.host,
                config.admin.port,
                "PATCH",
                "/api/v1/constitution/adaptation",
                R"({"interaction_style":{"verbosity":"high"},"retrieval_preference":{"weight_relevance":0.3}})",
                &code,
                &body)) {
            kernel.Stop();
            return Fail(182, "PATCH /api/v1/constitution/adaptation failed");
        }
        if (code != 200) {
            kernel.Stop();
            return Fail(183, "PATCH /api/v1/constitution/adaptation did not return 200");
        }
        if (body.find("\"verbosity\":\"high\"") == std::string::npos
            || body.find("\"weight_recency\"") == std::string::npos
            || body.find("\"weight_relevance\"") == std::string::npos
            || body.find("\"audit_log_size\":1") == std::string::npos) {
            kernel.Stop();
            return Fail(184, "adaptation PATCH did not apply partial merge/audit update");
        }
    }

    {
        int wsFd = -1;
        std::string upgradeResponse;
        if (!ConnectWebSocket(
                config.admin.host,
                config.admin.port,
                "/ws/chat",
                &wsFd,
                &upgradeResponse)) {
            kernel.Stop();
            return Fail(147, "failed websocket upgrade for /ws/chat");
        }

        if (!SendWebSocketText(wsFd, R"({"type":"list_sessions"})")) {
            ::close(wsFd);
            kernel.Stop();
            return Fail(148, "failed sending list_sessions over websocket");
        }
        std::string sessionsPayload;
        if (!WaitForWebSocketPayloadContaining(wsFd, "\"type\":\"sessions\"", 3000, &sessionsPayload)) {
            ::close(wsFd);
            kernel.Stop();
            return Fail(149, "did not receive sessions response over websocket");
        }

        if (!SendWebSocketText(wsFd, R"({"type":"message","content":"Hello via websocket"})")) {
            ::close(wsFd);
            kernel.Stop();
            return Fail(150, "failed sending websocket message payload");
        }

        std::string contextPayload;
        if (!WaitForWebSocketPayloadContaining(wsFd, "\"type\":\"context\"", 3000, &contextPayload)) {
            ::close(wsFd);
            kernel.Stop();
            return Fail(151, "did not receive context payload for websocket message");
        }

        std::string donePayload;
        bool sawToken = false;
        bool sawError = false;
        bool sawDone = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
        while (std::chrono::steady_clock::now() < deadline) {
            int opcode = 0;
            std::string payload;
            if (!ReadWebSocketFrame(wsFd, &opcode, &payload)) {
                continue;
            }
            if (opcode != 1) {
                continue;
            }
            if (payload.find("\"type\":\"token\"") != std::string::npos) {
                sawToken = true;
            }
            if (payload.find("\"type\":\"error\"") != std::string::npos) {
                sawError = true;
            }
            if (payload.find("\"type\":\"done\"") != std::string::npos) {
                sawDone = true;
                donePayload = payload;
                break;
            }
        }
        if (!sawDone) {
            ::close(wsFd);
            kernel.Stop();
            return Fail(153, "did not receive done payload over websocket");
        }
        if (!sawToken && !sawError) {
            ::close(wsFd);
            kernel.Stop();
            return Fail(152, "did not receive token or error payload over websocket");
        }

        std::smatch sessionMatch;
        std::smatch messageMatch;
        const std::regex sessionPattern("\"session_id\":\"([0-9]+)\"");
        const std::regex messagePattern("\"message_id\":\"([0-9]+)\"");
        if (!std::regex_search(donePayload, sessionMatch, sessionPattern)
            || !std::regex_search(donePayload, messageMatch, messagePattern)
            || sessionMatch.size() < 2
            || messageMatch.size() < 2) {
            ::close(wsFd);
            kernel.Stop();
            return Fail(154, "done payload missing session_id/message_id: " + donePayload);
        }
        const std::string persistedSessionId = sessionMatch[1].str();

        if (!SendWebSocketText(wsFd, "not-json")) {
            ::close(wsFd);
            kernel.Stop();
            return Fail(155, "failed to send invalid websocket payload");
        }
        std::string errorPayload;
        if (!WaitForWebSocketPayloadContaining(wsFd, "\"type\":\"error\"", 3000, &errorPayload)) {
            ::close(wsFd);
            kernel.Stop();
            return Fail(156, "invalid payload did not emit structured websocket error");
        }

        const std::string oversized = std::string(1200U, 'x');
        const std::string oversizedMessage =
            std::string("{\"type\":\"message\",\"content\":\"") + oversized + "\"}";
        if (!SendWebSocketText(wsFd, oversizedMessage)) {
            ::close(wsFd);
            kernel.Stop();
            return Fail(157, "failed to send oversized websocket payload");
        }
        if (!WaitForWebSocketPayloadContaining(
                wsFd,
                "message exceeds websocket size limit",
                3000,
                &errorPayload)) {
            ::close(wsFd);
            kernel.Stop();
            return Fail(158, "oversized websocket payload did not emit size-limit error");
        }
        ::close(wsFd);

        int wsFdReconnect = -1;
        if (!ConnectWebSocket(
                config.admin.host,
                config.admin.port,
                "/ws/chat",
                &wsFdReconnect,
                &upgradeResponse)) {
            kernel.Stop();
            return Fail(159, "failed websocket reconnect for /ws/chat");
        }

        const std::string reconnectMsg =
            std::string("{\"type\":\"message\",\"session_id\":\"")
            + persistedSessionId
            + "\",\"content\":\"Follow-up\"}";
        if (!SendWebSocketText(wsFdReconnect, reconnectMsg)) {
            ::close(wsFdReconnect);
            kernel.Stop();
            return Fail(160, "failed sending websocket reconnect message");
        }
        if (!WaitForWebSocketPayloadContaining(
                wsFdReconnect,
                "\"type\":\"done\"",
                3000,
                &donePayload)) {
            ::close(wsFdReconnect);
            kernel.Stop();
            return Fail(161, "reconnected websocket message did not reuse persisted session");
        }
        if (donePayload.find(std::string("\"session_id\":\"") + persistedSessionId + "\"")
            == std::string::npos) {
            ::close(wsFdReconnect);
            kernel.Stop();
            return Fail(162, "done payload session_id did not match reconnected session");
        }
        ::close(wsFdReconnect);
    }

    {
        int obsFd1 = -1;
        int obsFd2 = -1;
        int wsTriggerFd = -1;
        int obsFdReplay = -1;
        std::string upgradeResponse;
        auto closeIfOpen = [](int fd) {
            if (fd >= 0) {
                ::close(fd);
            }
        };

        if (!ConnectWebSocket(
                config.admin.host,
                config.admin.port,
                "/ws/observations?last_seen_timestamp_unix_ms=9999999999999",
                &obsFd1,
                &upgradeResponse)) {
            kernel.Stop();
            return Fail(163, "failed websocket upgrade for /ws/observations client #1");
        }
        if (!ConnectWebSocket(
                config.admin.host,
                config.admin.port,
                "/ws/observations?last_seen_timestamp_unix_ms=9999999999999",
                &obsFd2,
                &upgradeResponse)) {
            closeIfOpen(obsFd1);
            kernel.Stop();
            return Fail(164, "failed websocket upgrade for /ws/observations client #2");
        }

        if (!SendWebSocketText(
                obsFd2,
                R"({"type":"filter","layers":["day"],"tags":["does_not_match_stream"]})")) {
            closeIfOpen(obsFd1);
            closeIfOpen(obsFd2);
            kernel.Stop();
            return Fail(165, "failed to send observation filter payload");
        }
        std::string filterAppliedPayload;
        if (!WaitForWebSocketPayloadContaining(
                obsFd2,
                "\"type\":\"filter_applied\"",
                2000,
                &filterAppliedPayload)) {
            closeIfOpen(obsFd1);
            closeIfOpen(obsFd2);
            kernel.Stop();
            return Fail(166, "observation stream filter did not acknowledge");
        }

        if (WaitForWebSocketPayloadContaining(obsFd1, "\"type\":\"observation\"", 500, nullptr)) {
            closeIfOpen(obsFd1);
            closeIfOpen(obsFd2);
            kernel.Stop();
            return Fail(167, "observation stream emitted payload while idle");
        }

        if (!ConnectWebSocket(
                config.admin.host,
                config.admin.port,
                "/ws/chat",
                &wsTriggerFd,
                &upgradeResponse)) {
            closeIfOpen(obsFd1);
            closeIfOpen(obsFd2);
            kernel.Stop();
            return Fail(168, "failed websocket upgrade for chat trigger client");
        }

        const std::string triggerContent = "Obs-stream trigger message";
        const std::string triggerPayload =
            std::string("{\"type\":\"message\",\"content\":\"") + triggerContent + "\"}";
        if (!SendWebSocketText(wsTriggerFd, triggerPayload)) {
            closeIfOpen(obsFd1);
            closeIfOpen(obsFd2);
            closeIfOpen(wsTriggerFd);
            kernel.Stop();
            return Fail(169, "failed to send chat trigger message");
        }

        if (!WaitForWebSocketPayloadContaining(
                wsTriggerFd,
                "\"type\":\"done\"",
                3000,
                nullptr)) {
            closeIfOpen(obsFd1);
            closeIfOpen(obsFd2);
            closeIfOpen(wsTriggerFd);
            kernel.Stop();
            return Fail(170, "chat trigger did not complete");
        }

        std::string observationPayload;
        if (!WaitForWebSocketPayloadContaining(
                obsFd1,
                triggerContent,
                1000,
                &observationPayload)) {
            closeIfOpen(obsFd1);
            closeIfOpen(obsFd2);
            closeIfOpen(wsTriggerFd);
            kernel.Stop();
            return Fail(171, "observation stream did not deliver capture within 1 second");
        }

        if (WaitForWebSocketPayloadContaining(obsFd2, triggerContent, 1000, nullptr)) {
            closeIfOpen(obsFd1);
            closeIfOpen(obsFd2);
            closeIfOpen(wsTriggerFd);
            kernel.Stop();
            return Fail(172, "observation stream filter did not suppress non-matching event");
        }

        std::string observedId;
        std::uint64_t observedTimestamp = 0;
        if (!ExtractJsonString(observationPayload, "id", &observedId)
            || !ExtractJsonUInt64(observationPayload, "timestamp_unix_ms", &observedTimestamp)
            || observedId.empty()
            || observedTimestamp == 0) {
            closeIfOpen(obsFd1);
            closeIfOpen(obsFd2);
            closeIfOpen(wsTriggerFd);
            kernel.Stop();
            return Fail(173, "observation stream payload missing id/timestamp");
        }

        closeIfOpen(obsFd1);
        closeIfOpen(obsFd2);
        closeIfOpen(wsTriggerFd);

        const std::uint64_t lastSeen = (observedTimestamp > 0) ? (observedTimestamp - 1) : 0;
        const std::string replayPath =
            std::string("/ws/observations?last_seen_timestamp_unix_ms=")
            + std::to_string(lastSeen)
            + "&layers=day&tags=ws_chat";
        if (!ConnectWebSocket(
                config.admin.host,
                config.admin.port,
                replayPath,
                &obsFdReplay,
                &upgradeResponse)) {
            kernel.Stop();
            return Fail(174, "failed websocket reconnect for observation catch-up");
        }
        if (!WaitForWebSocketPayloadContaining(obsFdReplay, observedId, 2000, nullptr)) {
            closeIfOpen(obsFdReplay);
            kernel.Stop();
            return Fail(175, "observation catch-up replay did not include last seen boundary event");
        }
        closeIfOpen(obsFdReplay);
    }

    kernel.Stop();

    if (!std::filesystem::exists(cfgPath)) {
        return Fail(36, "agent config file was not persisted to disk");
    }

    {
        std::ifstream in(cfgPath);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (content.find("\"model_id\" : \"gpt-4.1\"") == std::string::npos) {
            return Fail(37, "persisted config file does not include updated model_id");
        }
        if (content.find("\"api_key\" : \"sk-secret-token\"") == std::string::npos) {
            return Fail(38, "persisted config file does not include raw api_key for reload");
        }
    }

    if (!std::filesystem::exists(interfacePath)) {
        return Fail(54, "interface config file was not persisted to disk");
    }

    {
        std::ifstream in(interfacePath);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (content.find("\"api_key\" : \"sk-super-secret\"") == std::string::npos) {
            return Fail(55, "persisted interface config did not keep raw secret");
        }
        if (content.find("\"enabled\" : true") == std::string::npos) {
            return Fail(56, "persisted interface config did not keep enabled state");
        }
    }

    if (!std::filesystem::exists(constitutionPath)) {
        return Fail(185, "constitution file was not persisted to disk");
    }

    {
        std::ifstream in(constitutionPath);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (content.find("\"verbosity\" : \"high\"") == std::string::npos) {
            return Fail(186, "persisted constitution file missing adaptation update");
        }
        if (content.find("\"audit_log\"") == std::string::npos
            || content.find("\"action\" : \"patch_adaptation\"") == std::string::npos) {
            return Fail(187, "persisted constitution file missing adaptation audit trail");
        }
    }

    return 0;
}

} // namespace

int main() {
    return TestAdminServerAndAgentApi();
}

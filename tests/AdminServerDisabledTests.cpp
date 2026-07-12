#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
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

int TestAdminServerDisabled() {
    const std::uint16_t port = ReserveFreePort();
    if (port == 0) {
        return Fail(1, "failed to reserve a free port for admin-disabled test");
    }

    animus::kernel::KernelConfig config;
    config.admin.enabled = false;
    config.admin.host = "127.0.0.1";
    config.admin.port = port;

    animus::kernel::AgentKernel kernel;
    std::string error;
    if (!kernel.Start(config, &error)) {
        return Fail(2, "kernel start failed with admin disabled: " + error);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (CanConnect(config.admin.host, config.admin.port)) {
        kernel.Stop();
        return Fail(3, "admin-disabled kernel unexpectedly opened port");
    }

    kernel.Stop();
    return 0;
}

} // namespace

int main() {
    return TestAdminServerDisabled();
}
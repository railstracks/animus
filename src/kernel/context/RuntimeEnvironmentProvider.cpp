#include "animus_kernel/context/RuntimeEnvironmentProvider.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/Session.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#ifdef __linux__
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace animus::kernel {

namespace {

std::string GetHostname() {
#ifdef __linux__
    struct utsname buf;
    if (uname(&buf) == 0) return buf.nodename;
#endif
    return "unknown";
}

std::string GetOsInfo() {
#ifdef __linux__
    struct utsname buf;
    if (uname(&buf) == 0) {
        return std::string(buf.sysname) + " " + buf.release + " " + buf.machine;
    }
#endif
    return "unknown";
}

unsigned int GetCpuCores() {
#ifdef __linux__
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0) return static_cast<unsigned int>(n);
#endif
    return 0;
}

// Available RAM in MB (rounded).
unsigned long GetAvailableRamMb() {
#ifdef __linux__
    // /proc/meminfo is more accurate than sysinfo() for available memory
    // (sysinfo() doesn't account for reclaimable caches).
    std::ifstream in("/proc/meminfo");
    std::string line;
    unsigned long memTotalKb = 0;
    unsigned long memAvailableKb = 0;
    while (std::getline(in, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            std::sscanf(line.c_str(), "MemTotal: %lu", &memTotalKb);
        } else if (line.rfind("MemAvailable:", 0) == 0) {
            std::sscanf(line.c_str(), "MemAvailable: %lu", &memAvailableKb);
            break;
        }
    }
    if (memAvailableKb > 0) return memAvailableKb / 1024;
    if (memTotalKb > 0) return memTotalKb / 1024;

    // Fallback to sysinfo()
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        return static_cast<unsigned long>(si.totalram * si.mem_unit / (1024 * 1024));
    }
#endif
    return 0;
}

} // namespace

std::optional<ContextBlock> RuntimeEnvironmentProvider::Provide(
        const Agent& agent,
        const SessionAccess& session) const {

    std::ostringstream out;

    // Host info
    out << "Host: " << GetHostname() << "\n";
    out << "OS: " << GetOsInfo() << "\n";

    const auto cores = GetCpuCores();
    if (cores > 0) {
        out << "CPU cores: " << cores << "\n";
    }

    const auto ramMb = GetAvailableRamMb();
    if (ramMb > 0) {
        out << "RAM: " << ramMb << " MB\n";
    }

    // Agent runtime budget
    out << "Chain budget: " << agent.budget.maxChainSteps << " steps, "
        << agent.budget.timeoutSeconds << "s timeout\n";

    // Session type
    const auto& st = session.SessionType();
    if (!st.empty()) {
        out << "Session type: " << st << "\n";
    }

    ContextBlock block;
    block.name = "RUNTIME ENVIRONMENT";
    block.content = out.str();
    block.priority = 5;
    return block;
}

} // namespace animus::kernel

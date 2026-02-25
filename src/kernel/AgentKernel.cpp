#include "animus_kernel/AgentKernel.h"

#include <iostream>
#include <memory>

#include "kernel/module/ModuleManager.h"

namespace animus::kernel {

AgentKernel::AgentKernel() : m_moduleManager(nullptr) {
    m_moduleManager = new module::ModuleManager();
}

AgentKernel::~AgentKernel() {
    Stop();
    delete m_moduleManager;
    m_moduleManager = nullptr;
}

bool AgentKernel::Start(const KernelConfig& config, std::string* error) {
    if (m_running) {
        return true;
    }

    m_config = config;

    if (!m_jobs.Start(m_config.jobSystemConfig)) {
        if (error) {
            *error = "failed to start job system";
        }
        return false;
    }

    // Minimal host vtable (kernel logs to stderr for now).
    animus_host_vtable_t host{};
    host.struct_size = sizeof(animus_host_vtable_t);
    host.log = [](void* /*ctx*/, animus_log_level_t level, animus_string_view msg) {
        const char* lvl = "INFO";
        switch (level) {
            case ANIMUS_LOG_DEBUG: lvl = "DEBUG"; break;
            case ANIMUS_LOG_INFO: lvl = "INFO"; break;
            case ANIMUS_LOG_WARN: lvl = "WARN"; break;
            case ANIMUS_LOG_ERROR: lvl = "ERROR"; break;
            default: break;
        }
        std::cerr << "[module:" << lvl << "] "
                  << std::string(msg.data ? msg.data : "", msg.size)
                  << "\n";
    };

    std::string modErr;
    if (!m_moduleManager->LoadFromConfig(m_config, &host, nullptr, &modErr)) {
        // If module loading fails, stop jobs too (clean failure).
        m_jobs.Stop(jobs::JobSystemStopMode::Drain);
        if (error) {
            *error = modErr;
        }
        return false;
    }

    m_running = true;
    return true;
}

void AgentKernel::Stop() {
    if (!m_running) {
        return;
    }

    if (m_moduleManager) {
        m_moduleManager->UnloadAll();
    }

    m_jobs.Stop(jobs::JobSystemStopMode::Drain);
    m_running = false;
}

bool AgentKernel::IsRunning() const {
    return m_running;
}

jobs::JobSystem& AgentKernel::Jobs() {
    return m_jobs;
}

} // namespace animus::kernel

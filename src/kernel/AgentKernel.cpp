#include "animus_kernel/AgentKernel.h"

#include <iostream>
#include <memory>

#include "kernel/module/ModuleManager.h"

#include "animus_kernel/DefaultSessionRouter.h"
#include "animus_kernel/InMemorySessionStore.h"
#include "animus_kernel/SessionManager.h"
#include "animus_kernel/llm/LLMProviderRegistry.h"

namespace animus::kernel {

AgentKernel::AgentKernel()
    : m_moduleManager(nullptr),
      m_sessionManager(nullptr),
      m_llmRegistry(std::make_unique<llm::LLMProviderRegistry>()) {
    m_moduleManager = new module::ModuleManager();

    // Default session layer: in-memory store + metadata-derived routing.
    m_sessionManager = new SessionManager(
        std::make_unique<InMemorySessionStore>(),
        std::make_unique<DefaultSessionRouter>());
}

AgentKernel::~AgentKernel() {
    Stop();

    delete m_moduleManager;
    m_moduleManager = nullptr;

    delete m_sessionManager;
    m_sessionManager = nullptr;
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

SessionManager& AgentKernel::Sessions() {
    return *m_sessionManager;
}

llm::LLMProviderRegistry& AgentKernel::LLMRegistry() {
    return *m_llmRegistry;
}

} // namespace animus::kernel

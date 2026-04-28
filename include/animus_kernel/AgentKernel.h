#pragma once

#include <memory>
#include <string>

#include "animus/Jobs.h"
#include "animus_kernel/KernelConfig.h"

namespace animus::kernel::module {
class ModuleManager;
}

namespace animus::kernel::llm {
class LLMProviderRegistry;
}

namespace animus::kernel {
class SessionManager;

// AgentKernel is the always-on core runtime.
//
// Milestone 2 scope: lifecycle + module loading + job system ownership.
class AgentKernel {
public:
    AgentKernel();
    ~AgentKernel();

    AgentKernel(const AgentKernel&) = delete;
    AgentKernel& operator=(const AgentKernel&) = delete;

    bool Start(const KernelConfig& config, std::string* error);
    void Stop();

    bool IsRunning() const;

    jobs::JobSystem& Jobs();
    SessionManager& Sessions();

    /// Access the LLM provider registry.
    llm::LLMProviderRegistry& LLMRegistry();

private:
    KernelConfig m_config{};
    jobs::JobSystem m_jobs;
    module::ModuleManager* m_moduleManager;   // pimpl-ish (kept simple for now)
    SessionManager* m_sessionManager;         // storage-agnostic session layer
    std::unique_ptr<llm::LLMProviderRegistry> m_llmRegistry;
    bool m_running{false};
};

} // namespace animus::kernel

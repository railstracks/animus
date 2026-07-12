#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include <drogon/WebSocketConnection.h>

#include "animus_kernel/KernelConfig.h"
#include "animus_kernel/Session.h"

namespace animus::jobs {
class JobSystem;
}

namespace animus::kernel {

class AgentStore;
class ChainRunner;
class CompactionService;
class ProviderThrottle;
class Session;
class SessionManager;
class ProviderManager;

namespace llm {
class LLMProviderRegistry;
}

class ChatSessionService {
public:
    struct Dependencies {
        SessionManager* sessions{nullptr};
        ChainRunner* chainRunner{nullptr};
        CompactionService* compactionService{nullptr};
        llm::LLMProviderRegistry* providerRegistry{nullptr};
        ProviderManager* providerManager{nullptr};
        AgentStore* agentStore{nullptr};
        ProviderThrottle* providerThrottle{nullptr};
        ::animus::jobs::JobSystem* jobs{nullptr};
        std::mutex* chatMutex{nullptr};
        const KernelConfig::AgentRuntimeConfig* agentConfig{nullptr};
    };

    struct Request {
        drogon::WebSocketConnectionPtr wsConnection;
        std::shared_ptr<Session> session;
        SessionId sessionId{0};
        std::string userContent;
        std::shared_ptr<std::atomic<bool>> stopSignal;
        std::string requestedProviderOverride;
        std::string requestedModelOverride;
    };

    void Configure(const Dependencies& deps);
    bool EnqueueStreamingResponse(const Request& request) const;

private:
    Dependencies m_deps{};
};

} // namespace animus::kernel
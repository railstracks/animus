#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <memory>

#include <json/json.h>
#include "animus_kernel/KernelConfig.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/admin/AgentManager.h"
#include "animus_kernel/admin/ChatSessionService.h"
#include "animus_kernel/admin/ConstitutionManager.h"
#include "animus_kernel/admin/DiaryManager.h"
#include "animus_kernel/admin/InterfaceManager.h"
#include "animus_kernel/ChannelManager.h"
#include "animus_kernel/admin/MemoryManager.h"
#include "animus_kernel/admin/ObservationStreamHub.h"
#include "animus_kernel/admin/OAuthManager.h"
#include "animus_kernel/admin/ProviderManager.h"
#include "animus_kernel/llm/LLMProviderConfig.h"
#include "animus_kernel/llm/LLMTypes.h"

#include "animus_kernel/AuthStore.h"
#include "animus_kernel/AuthManager.h"
#include "animus_kernel/AuthHelpers.h"
#include "animus_kernel/DiffusionStore.h"
#include "animus_kernel/AttachmentStore.h"

namespace animus::kernel {

class ChainRunner;
class CompactionService;
class ProviderThrottle;
class Scheduler;

class ToolRegistry;
class HttpClient;

namespace llm { class LLMProviderRegistry; }
namespace memory {
class MemoryStore;
class MemoryFileStore;
class MemorySearch;
}
namespace ontology { class OntologyStore; }
class SessionManager;
class GallivantingStore;
class NodeManager;
class ScriptStore;
class FilesystemScriptLoader;
struct DiscoveredScript;
class ChannelsTool;
class LuaState;
class SessionNotesStore;
class SessionReportStore;
class ContextProviderRegistry;
class SessionTagsStore;
class PromptLogStore;

class AdminServer {
public:
    AdminServer();
    ~AdminServer();

    AdminServer(const AdminServer&) = delete;
    AdminServer& operator=(const AdminServer&) = delete;

    bool Start(
        const KernelConfig::AdminServerConfig& config,
        const KernelConfig::AgentRuntimeConfig& initialAgentConfig,
        const KernelConfig::AgentConfigStorage& agentStorage,
        const KernelConfig::InterfaceConfigStorage& interfaceStorage,
        const std::vector<KernelConfig::InterfaceModuleInfo>& interfaceModules,
        const KernelConfig::MemoryConfig& memoryConfig,
        const KernelConfig::ConstitutionConfigStorage& constitutionStorage,
        const KernelConfig::ProviderConfigStorage& providerStorage,
        llm::LLMProviderRegistry* providerRegistry,
        SessionManager* sessions,
        ::animus::jobs::JobSystem* jobSystem,
        std::chrono::steady_clock::time_point kernelStartedAt,
        std::string* error);

    void Stop();

    bool IsRunning() const;
    std::uint64_t UptimeSeconds() const;
    bool SaveMemoryToDisk(std::string* error) const;
    bool SaveInterfacesToDisk(std::string* error) const;
    bool SaveConstitutionToDisk(std::string* error) const;
    bool LoadAgentConfigFromDisk(std::string* error);
    bool SaveAgentConfigToDisk(
        const KernelConfig::AgentRuntimeConfig& config,
        std::string* error) const;
    bool LoadMemoryFromDisk(std::string* error);
    bool LoadConstitutionFromDisk(std::string* error);
    bool LoadInterfacesFromDisk(std::string* error);
    bool TriggerModelClientReinitialization(std::string* error);
    bool LoadProvidersFromDisk(std::string* error);
    bool SaveProvidersToDisk(std::string* error) const;
    bool LoadAuthFromDisk(const std::string& providerId, Json::Value* out, std::string* error) const;
    bool SaveAuthToDisk(const std::string& providerId, const Json::Value& auth, std::string* error) const;

    // Look up provider config by id. Returns populated LLMProviderConfig with API keys,
    // base URL, default model, etc. Returns nullopt if provider not found.
    std::optional<llm::LLMProviderConfig> GetProviderConfig(const std::string& providerId) const;

    // Look up provider concurrency limit by id. Returns 1 if provider not found.
    int GetProviderConcurrency(const std::string& providerId) const;

    // Look up provider state by id. Returns nullopt if provider not found.
    std::optional<ProviderState> GetProvider(const std::string& providerId) const;

    // Get the default provider id (first configured provider, or empty string).
    std::string GetDefaultProviderId() const;

    // Resolve context window from provider state, model, and fallback.
    static std::uint32_t ResolveProviderContextWindow(
        const ProviderState& state,
        const std::string& modelId,
        std::uint32_t fallback,
        std::uint32_t agentContextWindow = 0);

    // Config (public for WebSocket controller access in same TU)
    KernelConfig::AdminServerConfig m_config{};
    KernelConfig::AgentRuntimeConfig m_agentConfig{};
    KernelConfig::AgentConfigStorage m_agentStorage{};
    SessionManager* m_sessions{nullptr};
    ChainRunner* m_chainRunner{nullptr};
    CompactionService* m_compactionService{nullptr};
    ProviderThrottle* m_providerThrottle{nullptr};
    memory::MemoryStore* m_memoryStore{nullptr};
    memory::MemoryFileStore* m_memoryFileStore{nullptr};
    memory::MemorySearch* m_memorySearch{nullptr};
    SessionNotesStore* m_sessionNotesStore{nullptr};
    SessionReportStore* m_sessionReportStore{nullptr};
    ContextProviderRegistry* m_contextRegistry{nullptr};
    SessionTagsStore* m_sessionTagsStore{nullptr};
    ontology::OntologyStore* m_ontologyStore{nullptr};
    AgentStore* m_agentStore{nullptr};
    NodeManager* m_nodeManager{nullptr};
    ToolRegistry* m_toolRegistry{nullptr};
    Scheduler* m_scheduler{nullptr};
    ::animus::jobs::JobSystem* m_jobs{nullptr};

    void SetChainRunner(ChainRunner* runner) {
        m_chainRunner = runner;
        RefreshChatSessionServiceDependencies();
    }
    ChainRunner* GetChainRunner() const { return m_chainRunner; }
    void SetCompactionService(CompactionService* service) {
        m_compactionService = service;
        RefreshChatSessionServiceDependencies();
    }
    void SetProviderThrottle(ProviderThrottle* throttle) {
        m_providerThrottle = throttle;
        RefreshChatSessionServiceDependencies();
    }
    void SetMemoryStore(memory::MemoryStore* store) { m_memoryStore = store; }
    void SetMemoryFileStore(memory::MemoryFileStore* store) { m_memoryFileStore = store; }
    void SetMemorySearch(memory::MemorySearch* search) { m_memorySearch = search; }

    SessionNotesStore* GetSessionNotesStore() const { return m_sessionNotesStore; }
    void SetSessionNotesStore(SessionNotesStore* store) { m_sessionNotesStore = store; }
    SessionReportStore* GetSessionReportStore() const { return m_sessionReportStore; }
    void SetSessionReportStore(SessionReportStore* store) { m_sessionReportStore = store; }
    void SetContextRegistry(ContextProviderRegistry* registry) { m_contextRegistry = registry; }
    void SetSessionTagsStore(SessionTagsStore* store) { m_sessionTagsStore = store; }
    void SetOntologyStore(ontology::OntologyStore* store) { m_ontologyStore = store; }
    void SetAgentStore(AgentStore* store) {
        m_agentStore = store;
        m_agentManager.Configure(m_agentStore, &m_providerManager);
        RefreshChatSessionServiceDependencies();
    }
    void SetToolRegistry(ToolRegistry* reg) { m_toolRegistry = reg; }
    void SetNodeManager(NodeManager* nm) { m_nodeManager = nm; }
    void SetScheduler(Scheduler* scheduler) { m_scheduler = scheduler; }
    void SetDiaryStore(DiaryStore* store) { m_diaryManager.Configure(store); }
    void SetGallivantingStore(GallivantingStore* store) { m_gallivantingStore = store; }
    void SetPromptLogStore(PromptLogStore* store) { m_promptLogStore = store; }
    void SetScriptStore(ScriptStore* store) { m_scriptStore = store; }
    void SetLuaScriptDir(const std::string& dir) { m_luaScriptDir = dir; }
    void SetChannelsTool(ChannelsTool* tool) { m_channelsTool = tool; }
    void SetChannelManager(ChannelManager* mgr) { m_channelManager = mgr; }

    // WhatsApp QR code retrieval
    Json::Value GetWhatsAppQr(const std::string& channelName) const;
    void SetLuaStates(std::unordered_map<std::string, std::unique_ptr<LuaState>>* states) { m_luaStates = states; }
    void SetConfigStore(AgentConfigStore* store) { m_configStore = store; }
    void SetDataDir(const std::filesystem::path& dir) { m_dataDir = dir; }

    // Web search config (ticket 069)
    void SetSearchConfig(KernelConfig::SearchConfig* cfg) { m_searchConfig = cfg; }
    void SetHttpClient(HttpClient* client) { m_httpClientPtr = client; }

    // Auth
    void ConfigureAuth(const std::string& staticToken, IDataStore* dataStore);
    AuthManager& GetAuthManager() { return m_authManager; }

    // Diffusion
    void SetDiffusionStore(DiffusionStore* store) { m_diffusionStore = store; }
    // Attachments
    void SetAttachmentStore(AttachmentStore* store) { m_attachmentStore = store; }
    DiaryManager& GetDiaryManager() { return m_diaryManager; }
    std::chrono::steady_clock::time_point m_kernelStartedAt{};

    // Runtime state
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_started{false};
    std::atomic<std::uint64_t> m_modelReinitCount{0};
    std::string m_startError;
    std::mutex m_startMutex;
    mutable std::mutex m_agentMutex;
    mutable std::mutex m_chatMutex;

    // Provider config state
    llm::LLMProviderRegistry* m_providerRegistry{nullptr};
    ProviderManager m_providerManager{};
    OAuthManager m_oauthManager{};
    AgentManager m_agentManager{};
    MemoryManager m_memoryManager{};
    ConstitutionManager m_constitutionManager{};
    DiaryManager m_diaryManager{};
    GallivantingStore* m_gallivantingStore{nullptr};
    PromptLogStore* m_promptLogStore{nullptr};
    ScriptStore* m_scriptStore{nullptr};
    std::string m_luaScriptDir{"state/lua"};
    ChannelsTool* m_channelsTool{nullptr};
    AgentConfigStore* m_configStore{nullptr};
    std::filesystem::path m_dataDir{"state"};
    std::unordered_map<std::string, std::unique_ptr<LuaState>>* m_luaStates{nullptr};
    ChatSessionService m_chatSessionService{};
    ObservationStreamHub m_observationStreamHub{};

    // WebSocket state
    std::atomic<std::uint64_t> m_nextWsConnectionId{1};
    std::atomic<std::uint64_t> m_nextWsConversationId{1};
    std::atomic<std::uint64_t> m_nextObservationId{1};

    // Chat state
    std::string m_prompt;
    std::chrono::steady_clock::time_point m_clock;

private:
    void RunLoop();
    void RegisterHandlersOnce();
    void RegisterWebSocketControllers();
    void RegisterRoutesProvidersAndSpa();
    void RegisterRoutesAgentsAndRuntime();
    void RegisterRoutesInterfacesSessionsMemory();
    void RegisterRoutesDiary();
    void RegisterRoutesGallivanting();
    void RegisterRoutesLua();
    void RegisterRoutesSocial();
    void RegisterRoutesChannels();
    void RegisterRoutesNodes();
    void RegisterRoutesSearch();
    void RegisterRoutesPromptLogs();
    void RegisterRoutesAuth();
    void RegisterRoutesDiffusion();
    void SyncIrcInterfaces();
    void RefreshChatSessionServiceDependencies();

    InterfaceManager m_interfaceManager{};
    ChannelManager* m_channelManager{nullptr};

    // Web search config
    KernelConfig::SearchConfig* m_searchConfig{nullptr};
    HttpClient* m_httpClientPtr{nullptr};

    // Auth
    AuthManager m_authManager{};
    AuthStore* m_authStore{nullptr};
    std::string m_staticTokenRaw;

    // Diffusion
    DiffusionStore* m_diffusionStore{nullptr};
    // Attachments
    AttachmentStore* m_attachmentStore{nullptr};
};

} // namespace animus::kernel

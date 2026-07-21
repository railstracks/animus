#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

#include "animus/Jobs.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/EmbeddingService.h"
#include "animus_kernel/KernelConfig.h"
#include "animus_kernel/llm/LLMProviderRegistry.h"
#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/lua/LuaState.h"
#include "animus_kernel/lua/ScriptStore.h"
#include "animus_kernel/lua/FilesystemScriptLoader.h"
#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/ChannelManager.h"
#include "animus_kernel/MessageQueue.h"
#include "animus_kernel/tools/ProjectTool.h"

namespace animus::kernel {

class IDataStore;
class AdminServer;
class ProviderThrottle;
class SessionManager;
class ChainRunner;
class CompactionService;
class Scheduler;
class ConsolidationPipeline;
class NodeManager;

namespace module { class ModuleManager; }
namespace memory {
class MemoryStore;
class MemoryFileStore;
class MemorySearch;
}
namespace ontology { class OntologyStore; }

class AgentStore;
class DiaryStore;
class DiffusionStore;
class AttachmentStore;
class SopStore;
class GallivantingStore;
class SessionNotesStore;
class SessionReportStore;
class ScriptStore;
class ChannelsTool;
class ContextProviderRegistry;
class SessionTagsStore;
class PromptLogStore;

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
    ::animus::kernel::llm::LLMProviderRegistry& ProviderRegistry();
    ChainRunner& Chains();
    ToolRegistry& Tools();
    AgentStore& Agents();
    HttpClient& WebClient();
    bool IsAdminServerRunning() const;

private:
    void RegisterBuiltinTools(const KernelConfig& config);

    // Compute embeddings for MemoryFile chunks that don't have them yet.
    // Also re-chunks files that have changed content since last chunking.
    void ComputePendingEmbeddings();

    /// Send the assistant's text response back to the originating social conversation.
    /// For VK/Telegram/IRC auto-reply through ChannelManager.
    void SendAutoReply(const ChannelManager::ReplyTarget& target,
                       const std::string& text);

    /// Execute a channel dispatch: run the LLM chain on the session and send the reply.
    /// Used by both the direct dispatch path and the MessageQueue flush callback.
    void ExecuteChannelDispatch(
        const std::string& sessionKey,
        const std::string& message,
        const ChannelManager::ReplyTarget& replyTarget,
        const std::string& agentId = "");

    /// Read min_response_interval from a channel's config. Returns 0 if not set.
    int GetChannelInterval(const std::string& channelName) const;

    /// Resolve the effective context window for an agent: provider window
    /// clamped to the agent's configured context_window if set.
    std::size_t ResolveContextWindow(const std::string& agentId,
                                     const std::string& providerId,
                                     const std::string& model) const;

    /// Ensure a LuaState exists for the given agent.
    void EnsureLuaState(const std::string& agentId);

    /// Load all enabled Lua scripts from DB (ScriptStore) and filesystem.
    /// DB scripts load first, then filesystem scripts override on name conflict.
    /// Called during startup (Phase 1 of two-phase Lua initialization).
    void LoadLuaScripts();

    KernelConfig m_config{};
    jobs::JobSystem m_jobs;
    ToolRegistry m_tools;
    HttpClient m_httpClient;
    AgentStore* m_agentStore{nullptr};
    module::ModuleManager* m_moduleManager;   // pimpl-ish (kept simple for now)
    AdminServer* m_adminServer;               // embedded admin API server
    SessionManager* m_sessionManager;         // storage-agnostic session layer
    IDataStore* m_dataStore;                  // shared database connection (SQLite or PostgreSQL)
    ProviderThrottle* m_providerThrottle;
    ::animus::kernel::llm::LLMProviderRegistry* m_providerRegistry; // LLM provider factory
    ChainRunner* m_chainRunner;               // chain execution pipeline
    CompactionService* m_compactionService{nullptr}; // session context compaction
    memory::MemoryStore* m_memoryStore;           // constitutional memory layers
    memory::MemoryFileStore* m_memoryFileStore{nullptr}; // raw artifact memory files
    memory::MemorySearch* m_memorySearch{nullptr}; // unified memory search
    ontology::OntologyStore* m_ontologyStore{nullptr}; // semantic ontology memory
    EmbeddingService* m_embeddingService{nullptr};   // in-process embedding model
    NodeManager* m_nodeManager{nullptr};             // external node manager
    DiaryStore* m_diaryStore{nullptr};           // per-agent diary store
    GallivantingStore* m_gallivantingStore{nullptr}; // gallivanting thread/session store
    SessionNotesStore* m_sessionNotesStore{nullptr}; // per-session persistent notes
    SessionReportStore* m_sessionReportStore{nullptr}; // per-session temporal reports
    ContextProviderRegistry* m_contextRegistry{nullptr}; // prompt assembly providers
    SessionTagsStore* m_sessionTagsStore{nullptr}; // session tag keywords
    PromptLogStore* m_promptLogStore{nullptr}; // LLM call logging
    Scheduler* m_scheduler{nullptr};              // cron-like schedule subsystem
    ConsolidationPipeline* m_consolidation{nullptr}; // memory consolidation pipeline
    std::unordered_map<std::string, std::unique_ptr<LuaState>> m_luaStates; // per-agent Lua VMs
    ScriptStore* m_scriptStore{nullptr};              // SQLite-backed script persistence
    std::unique_ptr<FilesystemScriptLoader> m_fsScriptLoader;  // filesystem-based Lua loader
    AgentConfigStore* m_configStore{nullptr};          // persistent agent config (key-value)
    ChannelsTool* m_channelsTool{nullptr};                    // composite social tool
    ChannelManager* m_channelManager{nullptr};             // unified channel management
    std::unique_ptr<MessageQueue> m_messageQueue;            // per-session message batching queue
    ProjectStore* m_projectStore{nullptr};             // project/task persistence
    std::mutex m_pendingReplyTargetsMutex;                   // guards m_pendingReplyTargets
    struct PendingDispatch {
        ChannelManager::ReplyTarget replyTarget;
        std::string agentId;
    };
    std::unordered_map<std::string, PendingDispatch> m_pendingReplyTargets;
    std::chrono::steady_clock::time_point m_startedAt{};
   bool m_running{false};

    // Diffusion
    std::unique_ptr<DiffusionStore> m_diffusionStore;
    std::unique_ptr<AttachmentStore> m_attachmentStore;
    std::unique_ptr<SopStore> m_sopStore;
};

} // namespace animus::kernel

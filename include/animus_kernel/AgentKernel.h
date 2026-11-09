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
#include "animus_kernel/NodeIdentity.h"
#include "animus_kernel/ChannelManager.h"
#include "animus_kernel/MessageQueue.h"
#include "animus_kernel/tools/ProjectTool.h"

namespace animus::kernel {

class IDataStore;
class AdminServer;
class Session;
class ProviderThrottle;
class SessionManager;
class ChainRunner;
class CompactionService;
class SecretsVault;
class SyncStore;
class FederationKeys;
class PeerSyncService;
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
class MediaVault;
class SopStore;
class GallivantingStore;
class SessionNotesStore;
class ChannelContextStore;
class ApiPackageStore;
class ApiRuntime;
class ApiConnectionManager;
class AgendaStore;
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

    // Compute embeddings for session reports that don't have them yet.
    // Runs after import or startup to backfill missing embedding vectors.
    void ComputePendingReportEmbeddings();

    /// Send the assistant's text response back to the originating social conversation.
    /// For VK/Telegram/IRC auto-reply through ChannelManager.
    void SendAutoReply(const ChannelManager::ReplyTarget& target,
                       const std::string& text);

    /// Delivery-failure witness (#30): write a session note when a yielded
    /// channel reply fails to land, so the agent (and admin UI) can see it.
    void ReportChannelSendFailure(const ChannelReplyTarget& target,
                                  const std::string& error);

    /// Execute a channel dispatch: run the LLM chain on the session and send the reply.
    /// Used by both the direct dispatch path and the MessageQueue flush callback.
    // #158: inbound media vaulting at dispatch time. Raw adapter-declared
    // attachments (URLs + metadata) become vault refs on the arrival row;
    // failures persist as loud {url, error} entries. Never blocks dispatch.
    std::string VaultInboundAttachments(
        const std::string& rawAttachments,
        const ChannelManager::ReplyTarget& replyTarget);
    // Media egress scope for a channel: platform CDN defaults + operator
    // config (media_hosts, pds) — message content never influences it.
    std::vector<std::string> MediaScopeFor(
        const ChannelManager::ReplyTarget& replyTarget) const;
    // Download auth for scoped CDNs (Slack bot token) — kernel-side only.
    std::map<std::string, std::string> MediaAuthHeadersFor(
        const ChannelManager::ReplyTarget& replyTarget) const;
    void ExecuteChannelDispatch(
        const std::string& sessionKey,
        const std::string& message,
        const ChannelManager::ReplyTarget& replyTarget,
        const std::string& agentId = "",
        const std::string& metadata = "{}");

    /// Read min_response_interval from a channel's config. Returns 0 if not set.
    int GetChannelInterval(const std::string& channelName) const;

    /// #268: re-bind empty/sentinel-bound channel sessions to the
    /// dispatching agent when that agent is actually configured. Sessions
    /// bound to a real agent never re-bind (cross-agent isolation).
    void TryAdoptDispatchAgent(Session& session,
                               const std::string& dispatchAgentId,
                               const std::string& sessionKeyForLog);

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
    ChannelContextStore* m_channelContextStore{nullptr}; // trusted channel arrivals (#14)
    ApiPackageStore* m_apiPackageStore{nullptr}; // api package persistence (#26 b)
    ApiRuntime* m_apiRuntime{nullptr};               // api package runtime (#26 c)
    SecretsVault* m_secretsVault{nullptr};           // encrypted package secrets (#23)
    ApiConnectionManager* m_apiConnManager{nullptr};  // connection driver + dispatch bridge (#26 d)
    AgendaStore* m_agendaStore{nullptr}; // per-agent calendar/agenda
    SessionReportStore* m_sessionReportStore{nullptr}; // per-session temporal reports
    ContextProviderRegistry* m_contextRegistry{nullptr}; // prompt assembly providers
    SessionTagsStore* m_sessionTagsStore{nullptr}; // session tag keywords
    PromptLogStore* m_promptLogStore{nullptr}; // LLM call logging
    SyncStore* m_syncStore{nullptr};
    FederationKeys* m_federationKeys{nullptr};         // #274 epoch DEK envelopes
    NodeIdentity m_nodeIdentity;                  // #182 fed-secrets X25519 identity
    bool m_nodeIdentityOk{false};
    const NodeIdentity& GetNodeIdentity() const { return m_nodeIdentity; }
    PeerSyncService* m_peerSyncService{nullptr};   // #78 P1c pull loop
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
        std::string metadata;
    };
    std::unordered_map<std::string, PendingDispatch> m_pendingReplyTargets;
    std::chrono::steady_clock::time_point m_startedAt{};
   bool m_running{false};

    // Diffusion
    std::unique_ptr<DiffusionStore> m_diffusionStore;
    std::unique_ptr<AttachmentStore> m_attachmentStore;
    std::unique_ptr<SopStore> m_sopStore;

    // #158: inbound media — content-addressed vault. Bytes live on disk
    // (sha256-named); the arrivals table and agent context carry refs only.
    // Dedicated HttpClient: media sizes must not inherit the web tools' 1MB
    // default, and the web allowlist must not gate media scope.
    std::unique_ptr<MediaVault> m_mediaVault;
    HttpClient m_mediaHttp;
};

} // namespace animus::kernel

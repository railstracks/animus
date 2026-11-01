#include "animus_kernel/IdRanges.h"
#include "animus_kernel/SyncStore.h"
#include "animus_kernel/PeerSyncService.h"
#include "animus_kernel/AgentKernel.h"
#include "animus_kernel/lua/SharedScriptHealer.h"
#include "animus_kernel/Log.h"

#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <unordered_set>

#include "kernel/module/ModuleManager.h"

#include "animus_kernel/AdminServer.h"
#include "animus_kernel/ChainRunner.h"
#include "animus_kernel/CompactionService.h"
#include "animus_kernel/DefaultSessionRouter.h"
#include "animus_kernel/consolidation/ConsolidationPipeline.h"
#include "animus_kernel/GallivantingStore.h"
#include "animus_kernel/InMemorySessionStore.h"
#include "animus_kernel/MemoryFileStore.h"
#include "animus_kernel/EmbeddingService.h"
#include "animus_kernel/memory/EmbeddingSweep.h"
#include "animus_kernel/NodeManager.h"
#include "animus_kernel/MemorySearch.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/OntologyStore.h"
#include "animus_kernel/SessionManager.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/PgDataStore.h"
#include "animus_kernel/DatabaseConfig.h"
#include "animus_kernel/SqliteSessionStore.h"
#include "animus_kernel/ProviderThrottle.h"
#include "animus_kernel/ChannelManager.h"
#include <set>
#include <sstream>

#include <json/json.h>
#include "animus_kernel/llm/CohereProvider.h"
#include "animus_kernel/llm/MistralProvider.h"
#include "animus_kernel/llm/AlibabaProvider.h"
#include "animus_kernel/llm/DeepSeekProvider.h"
#include "animus_kernel/llm/OpenRouterProvider.h"
#include "animus_kernel/llm/OpenAICompatProvider.h"
#include "animus_kernel/llm/OllamaProvider.h"
#include "animus_kernel/llm/OpenAICodexProvider.h"
#include "animus_kernel/llm/OpenAIProvider.h"
#include "animus_kernel/llm/ZaiProvider.h"
#include "animus_kernel/llm/ZaiCodingProvider.h"
#include "animus_kernel/tools/FileTool.h"
#include "animus_kernel/tools/GallivantingTool.h"
#include "animus_kernel/tools/HttpTool.h"
#include "animus_kernel/tools/LuaTool.h"
#include "animus_kernel/lua/ScriptStore.h"
#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/tools/MemoryTool.h"
#include "kernel/admin/TemplateResources.h"
#include <filesystem>
#include <fstream>
#include "animus_kernel/tools/NodeTool.h"
#include "animus_kernel/tools/ShellExecTool.h"
#include "animus_kernel/tools/WebFetchTool.h"
#include "animus_kernel/tools/StoredLinksTool.h"
#include "animus_kernel/tools/RssTool.h"
#include "animus_kernel/tools/WebSearchTool.h"
#include "animus_kernel/tools/DiaryTool.h"
#include "animus_kernel/tools/AgentTool.h"
#include "animus_kernel/tools/ProjectTool.h"
#include "animus_kernel/tools/ConsolidationTool.h"
#include "animus_kernel/tools/ScheduleTool.h"
#include "animus_kernel/tools/SessionsTool.h"
#include "animus_kernel/tools/AgendaTool.h"
#include "animus_kernel/tools/ChannelsTool.h"
#include "animus_kernel/tools/EmailTool.h"
#include "animus_kernel/scheduler/Scheduler.h"
#include "animus_kernel/scheduler/ScheduleLeaseStore.h"
#include "animus_kernel/admin/DiaryManager.h"
#include "animus_kernel/SessionNotesStore.h"
#include "animus_kernel/ChannelContextStore.h"
#include "animus_kernel/ApiPackageStore.h"
#include "animus_kernel/api/ApiRuntime.h"
#include "animus_kernel/api/SecretsVault.h"
#include "animus_kernel/api/ApiConnectionManager.h"
#include "animus_kernel/tools/ApiTool.h"
#include "animus_kernel/AgendaStore.h"
#include "animus_kernel/SessionReportStore.h"
#include "animus_kernel/SessionTagsStore.h"
#include "animus_kernel/PromptLogStore.h"
#include "animus_kernel/ContextProviderRegistry.h"
#include "animus_kernel/context/IdentityProvider.h"
#include "animus_kernel/context/SessionNotesProvider.h"
#include "animus_kernel/context/ChannelContextProvider.h"
#include "animus_kernel/context/SessionReportProvider.h"
#include "animus_kernel/context/ActiveMemoryProvider.h"
#include "animus_kernel/context/RuntimeEnvironmentProvider.h"
#include "animus_kernel/context/TemporalContextProvider.h"

#include "animus_kernel/tools/DiceTool.h"
#include "animus_kernel/tools/CalculatorTool.h"
#include "animus_kernel/tools/ToolsTool.h"
#include "animus_kernel/tools/ImageTool.h"
#include "animus_kernel/tools/DiffusionTool.h"
#include "animus_kernel/DiffusionStore.h"
#include "animus_kernel/AttachmentStore.h"
#include "animus_kernel/tools/AttachmentTool.h"
#include "animus_kernel/SopStore.h"
#include "animus_kernel/tools/SopTool.h"

namespace animus::kernel {

namespace {

std::string ExtractPlatformType(const std::string& platformId) {
    auto pos = platformId.find(':');
    return (pos != std::string::npos) ? platformId.substr(0, pos) : platformId;
}

} // anonymous namespace

AgentKernel::AgentKernel()
    : m_moduleManager(nullptr)
    , m_adminServer(nullptr)
    , m_sessionManager(nullptr)
    , m_dataStore(nullptr)
    , m_providerThrottle(nullptr)
    , m_providerRegistry(nullptr)
    , m_chainRunner(nullptr)
    , m_memoryStore(nullptr)
    , m_memoryFileStore(nullptr)
    , m_memorySearch(nullptr)
    , m_ontologyStore(nullptr) {
    m_moduleManager = new module::ModuleManager();
    m_adminServer = new AdminServer();

    // Default session layer: in-memory store + metadata-derived routing.
    m_providerRegistry = new ::animus::kernel::llm::LLMProviderRegistry();
    m_chainRunner = nullptr;
    m_memoryStore = nullptr;
    m_sessionManager = new SessionManager(
        std::make_unique<InMemorySessionStore>(),
        std::make_unique<DefaultSessionRouter>());
}

AgentKernel::~AgentKernel() {
    Stop();

    delete m_channelManager; m_channelManager = nullptr;
    if (m_messageQueue) { m_messageQueue->Shutdown(); m_messageQueue.reset(); }
    // #224: drop the layer-change hook before the pipeline it targets dies.
    if (m_memoryStore) {
        m_memoryStore->SetLayerChangedCallback(nullptr);
    }
    delete m_consolidation; m_consolidation = nullptr;
    delete m_chainRunner;
    m_chainRunner = nullptr;
    delete m_memorySearch; m_memorySearch = nullptr;
    delete m_memoryFileStore; m_memoryFileStore = nullptr;
    delete m_memoryStore; m_memoryStore = nullptr;
    delete m_ontologyStore; m_ontologyStore = nullptr;
    delete m_embeddingService; m_embeddingService = nullptr;
    delete m_nodeManager; m_nodeManager = nullptr;
    delete m_providerRegistry;
    m_providerRegistry = nullptr;
    delete m_adminServer;
    m_adminServer = nullptr;
    delete m_agentStore; m_agentStore = nullptr;
    delete m_projectStore; m_projectStore = nullptr;
    delete m_dataStore; m_dataStore = nullptr;
    delete m_providerThrottle; m_providerThrottle = nullptr;
    delete m_peerSyncService; m_peerSyncService = nullptr;
    delete m_syncStore; m_syncStore = nullptr;
    delete m_scheduler; m_scheduler = nullptr;
    delete m_sessionNotesStore; m_sessionNotesStore = nullptr;
    delete m_channelContextStore; m_channelContextStore = nullptr;
    if (m_apiConnManager) { m_apiConnManager->Stop(); delete m_apiConnManager; m_apiConnManager = nullptr; }
    delete m_apiRuntime; m_apiRuntime = nullptr;
    delete m_apiPackageStore; m_apiPackageStore = nullptr;
    delete m_secretsVault; m_secretsVault = nullptr;
    delete m_agendaStore; m_agendaStore = nullptr;
    delete m_sessionReportStore; m_sessionReportStore = nullptr;
    delete m_contextRegistry; m_contextRegistry = nullptr;
    delete m_sessionTagsStore; m_sessionTagsStore = nullptr;
    delete m_promptLogStore; m_promptLogStore = nullptr;

    delete m_moduleManager;
    m_moduleManager = nullptr;

    delete m_sessionManager;
    m_sessionManager = nullptr;
}

bool AgentKernel::Start(const KernelConfig& config, std::string* error) {
    if (m_running) {
        return true;
    }

    // Configure log level from environment
    if (const char* lvl = std::getenv("ANIMUS_LOG_LEVEL"); lvl && *lvl) {
        SetLogLevelFromString(lvl);
    }

    m_config = config;
    m_startedAt = std::chrono::steady_clock::now();

    // --- Shared DataStore ---
    // Backend selection: default SQLite, optional PostgreSQL via config.storage_backend
    if (config.storage_backend == "postgresql") {
#ifdef ANIMUS_WITH_POSTGRESQL
        auto* pg = new PgDataStore(PgDataStore::Config{
            .host = config.pg_host.empty() ? "localhost" : config.pg_host,
            .port = config.pg_port > 0 ? config.pg_port : 5432,
            .database = config.pg_database.empty() ? "animus" : config.pg_database,
            .username = config.pg_username.empty() ? "animus" : config.pg_username,
            .password = config.pg_password,
            .pool_size = config.pg_pool_size > 0 ? config.pg_pool_size : 10
        });
        m_dataStore = pg;
        if (!pg->IsOpen()) {
            if (error) *error = "failed to connect to PostgreSQL: " + pg->ErrMsg();
            delete m_dataStore; m_dataStore = nullptr;
            delete m_providerThrottle; m_providerThrottle = nullptr;
            return false;
        }
#else
        if (error) *error = "PostgreSQL backend requested but ANIMUS_WITH_POSTGRESQL is not enabled";
        delete m_providerThrottle; m_providerThrottle = nullptr;
        return false;
#endif
    } else {
        m_dataStore = new SqliteDataStore(config.sqlite_path.empty()
            ? (config.dataDir / "memory.db").string()
            : config.sqlite_path);
        if (!m_dataStore->IsOpen()) {
            if (error) *error = "failed to open database";
            delete m_dataStore; m_dataStore = nullptr;
            delete m_providerThrottle; m_providerThrottle = nullptr;
            return false;
        }
    }

    // --- Replace temporary session manager with SQLite-backed one ---
    delete m_sessionManager;
    m_sessionManager = new SessionManager(
        std::make_unique<SqliteSessionStore>(m_dataStore,
            (config.dataDir / "sessions.json").string()),
        std::make_unique<DefaultSessionRouter>());

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
        ALOG_DEBUG("module", "[" << lvl << "] "
                  << std::string(msg.data ? msg.data : "", msg.size));
    };

    std::string modErr;
    if (!m_moduleManager->LoadFromConfig(m_config, &host, nullptr, &modErr)) {
        m_jobs.Stop(::animus::jobs::JobSystemStopMode::Drain);
        if (error) {
            *error = modErr;
        }
        return false;
    }

    m_config.interfaceModules = m_moduleManager->ListLoadedInterfaceModules();

    // --- Configure shared HTTP client ---
    m_httpClient.SetAllowlist(config.web.urlAllowlist);
    m_httpClient.SetDenylist(config.web.urlDenylist);
    m_httpClient.SetMaxBodySize(config.web.maxResponseBytes);
    m_httpClient.SetTlsVerify(config.web.tlsVerify);
    m_httpClient.SetAllowPrivateAddresses(config.web.allowPrivateAddresses);

    // --- Register built-in tools ---

    // Register LLM providers
    {
        using namespace ::animus::kernel::llm;
        auto& reg = *m_providerRegistry;
        reg.Register("openai", [](const LLMProviderConfig& cfg) {
            return std::make_unique<OpenAIProvider>(cfg);
        });
        reg.Register("openai-codex", [](const LLMProviderConfig& cfg) {
            return std::make_unique<OpenAICodexProvider>(cfg);
        });
        reg.Register("zai", [](const LLMProviderConfig& cfg) {
            return std::make_unique<ZaiProvider>(cfg);
        });
        reg.Register("zai-coder", [](const LLMProviderConfig& cfg) {
            return std::make_unique<ZaiCodingProvider>(cfg);
        });
        reg.Register("ollama", [](const LLMProviderConfig& cfg) {
            return std::make_unique<OllamaProvider>(cfg);
        });
        reg.Register("cohere", [](const LLMProviderConfig& cfg) {
            return std::make_unique<CohereProvider>(cfg);
        });
        reg.Register("mistral", [](const LLMProviderConfig& cfg) {
            return std::make_unique<MistralProvider>(cfg);
        });
        reg.Register("alibaba", [](const LLMProviderConfig& cfg) {
            return std::make_unique<AlibabaProvider>(cfg);
        });
        reg.Register("openai-compatible", [](const LLMProviderConfig& cfg) {
            return std::make_unique<OpenAICompatProvider>(cfg);
        });
        reg.Register("deepseek", [](const LLMProviderConfig& cfg) {
            return std::make_unique<DeepSeekProvider>(cfg);
        });
        reg.Register("openrouter", [](const LLMProviderConfig& cfg) {
            return std::make_unique<OpenRouterProvider>(cfg);
        });

        // Config lookup: reads from AdminServer's provider state to populate
        // LLMProviderConfig with API keys, base URLs, etc.
        auto configLookup = [this](const std::string& id) -> std::optional<::animus::kernel::llm::LLMProviderConfig> {
            if (!m_adminServer) return std::nullopt;
            return m_adminServer->GetProviderConfig(id);
        };

        m_chainRunner = new ChainRunner(reg, *m_sessionManager, m_tools, configLookup);

        // Configure reasoning mode from agent config
        m_chainRunner->SetReasoningEnabled(config.agent.reasoningEnabled);
        m_chainRunner->SetReasoningInstruction(config.agent.reasoningInstruction);
        m_chainRunner->SetMaxChainSteps(config.agent.budget.maxChainSteps);
        m_chainRunner->SetMaxToolCallsPerChain(config.agent.budget.maxToolCallsPerChain);

        m_adminServer->SetChainRunner(m_chainRunner);

        // --- Compaction Service (session context compression) ---
        m_compactionService = new CompactionService(m_dataStore, m_sessionManager, CompactionService::CompactionConfig{});
        m_adminServer->SetCompactionService(m_compactionService);

        // Memory store — uses shared DataStore
        m_memoryStore = new memory::MemoryStore(m_dataStore);
        m_memoryFileStore = new memory::MemoryFileStore(m_dataStore);
        m_ontologyStore = new ontology::OntologyStore(m_dataStore);
        m_ontologyStore->SyncPropertyStatesFromObservations(m_memoryStore);

        // --- Embedding service (loads GGUF model for semantic similarity) ---
        if (config.embedding_enabled) {
            m_embeddingService = new EmbeddingService(config.embedding_model_path);
            if (!m_embeddingService->IsAvailable()) {
                ALOG_WARNING("kernel", "Embedding service in degraded mode — keyword fallback active");
            }
        }

        // --- Node manager (external node connections) ---
        m_nodeManager = new NodeManager(m_dataStore);
        m_chainRunner->SetNodeManager(m_nodeManager);

        // --- Agent store (SQLite-backed, after MemoryStore so observations table exists) ---
        m_agentStore = new AgentStore(m_dataStore);

        // --- Project store (project/task persistence) ---
        m_projectStore = new ProjectStore(m_dataStore);
        m_chainRunner->SetAgentStore(m_agentStore);
        m_adminServer->SetMemoryStore(m_memoryStore);
        m_adminServer->SetMemoryFileStore(m_memoryFileStore);
        m_adminServer->SetOntologyStore(m_ontologyStore);
        m_adminServer->SetAgentStore(m_agentStore);
        m_nodeManager->SetAgentStore(m_agentStore);
        m_adminServer->SetToolRegistry(&m_tools);
        m_adminServer->SetDataDir(m_config.dataDir);
        m_adminServer->SetDataStore(m_dataStore);
        m_adminServer->SetNodeManager(m_nodeManager);
        m_adminServer->SetSearchConfig(&m_config.search);
        m_adminServer->SetHttpClient(&m_httpClient);

        // --- Diary store (SQLite-backed, per-agent private diary) ---
        m_diaryStore = new DiaryStore(m_dataStore);
        m_adminServer->SetDiaryStore(m_diaryStore);

        // --- #78 P2a: scheduler tables join the replication set ----------
        // schedules/task_runs schemas must exist BEFORE SyncStore installs
        // triggers (install is live-schema-read at boot; a missing table is
        // skipped). The Scheduler object itself is constructed later (its
        // FireCallback captures this kernel), so pre-create the schemas via
        // lightweight store objects here — ScheduleStore/TaskRunStore ctors
        // are pure schema-ensurers on the shared IDataStore.
        {
            ScheduleStore schedulePre(m_dataStore);
            TaskRunStore taskRunPre(m_dataStore);
            taskRunPre.EnsureSchema();   // TaskRunStore ctor does NOT ensure
            ScheduleLeaseStore leasePre(m_dataStore);
            leasePre.EnsureSchema();     // table must pre-exist sync trigger install
            // #93 P3: agent_config (config store creates it at line ~576,
            // AFTER trigger install) and api_package_secrets (vault, ~472)
            // joined the synced set — pre-create both so P3 tables get
            // triggers on THIS boot, not the next one.
            AgentConfigStore configPre(m_dataStore);
            SecretsVault vaultPre(m_dataStore, "");
            vaultPre.EnsureSchema();     // table only — empty key path = no key file
        }

        // --- #78 P1a: node-scoped id ranges (federation identity) ---
        // After ALL agent-global stores exist (tables + sequences created),
        // seed this node's id range so every agent-global insert allocates
        // from [nodeId<<40, (nodeId+1)<<40). Raise-only; 0 = no-op.
        if (m_config.node.id > 0) {
            std::string idRangeError;
            const int seededTables = SeedAgentGlobalIdRanges(
                m_dataStore, m_config.node.id, &idRangeError);
            if (seededTables < 0) {
                ALOG_ERROR("kernel", "id-range seeding failed: " << idRangeError);
            }
        }

        // --- #78 P1b: replication outbox + LWW apply (agent-global tables) ---
        // Trigger-based change capture for every agent-global table. Installed
        // on every node (single-node nodes simply accumulate an unpulled
        // outbox — schema stays uniform and federation-ready).
        m_syncStore = new SyncStore(m_dataStore, m_config.node.id);
        {
            std::string syncErr;
            if (!m_syncStore->EnsureSchema(&syncErr)) {
                ALOG_WARNING("kernel", "sync store init failed (single-node "
                             "operation continues): " << syncErr);
            }
        }

        // --- #78 P1c: federation transport (pull loop + sync API) ---
        // Admin routes expose the outbox/handshake/status on every node;
        // the pull loop runs only when peers are configured.
        m_adminServer->SetSyncStore(m_syncStore);
        if (m_config.node.id > 0 && !m_config.node.peers.empty()) {
            m_peerSyncService = new PeerSyncService(
                m_syncStore, m_config.node.peers, m_config.node.syncToken);
            m_adminServer->SetPeerSyncService(m_peerSyncService);
        } else {
            ALOG_INFO("kernel", "node federation: no peers configured — "
                      "sync API serves, pull loop idle");
        }
        m_tools.Register(std::make_unique<DiaryTool>(&m_adminServer->GetDiaryManager()));

        // --- Agent Self-Management Tool (view/update own settings) ---
        m_tools.Register(std::make_unique<AgentTool>(m_agentStore, m_config.agent.allowSelfIdentityEdit));

        // --- Project Tool (persistent multi-step objectives) ---
        m_tools.Register(std::make_unique<ProjectTool>(m_projectStore));

        // --- Session Report Store (temporal summaries for active memory) ---
        // MUST be created before ConsolidationTool which captures the raw pointer
        m_sessionReportStore = new SessionReportStore(m_dataStore);
        m_adminServer->SetSessionReportStore(m_sessionReportStore);

        // --- Session Notes Store (SQLite-backed, per-session persistent bullet points) ---
        m_sessionNotesStore = new SessionNotesStore(m_dataStore);
        m_adminServer->SetSessionNotesStore(m_sessionNotesStore);

        // --- Channel Context Store (trusted channel arrivals, #14) ---
        // Written by ExecuteChannelDispatch (both direct and queue-flush paths
        // funnel through it); read by the ChannelContextProvider (#15).
        m_channelContextStore = new ChannelContextStore(m_dataStore);
        m_adminServer->SetChannelContextStore(m_channelContextStore);

        // --- API Package Store (#26 data layer, build order b) ---
        // Installs disabled by default; tool/sandbox layers (c/d) consume it.
        m_apiPackageStore = new ApiPackageStore(m_dataStore);

        // --- Secrets Vault (#23) ---
        // Master key lives OUTSIDE the database (auto-generated key file next
        // to the data dir, 0600). Legacy literal secrets migrate into the
        // vault at boot — idempotent, one loud log line per secret moved.
        m_secretsVault = new SecretsVault(m_dataStore, (m_config.dataDir / "api-vault.key").string());
        m_secretsVault->EnsureSchema();
        {
            std::string migErr;
            const int migrated = m_secretsVault->MigrateLegacyStateSecrets(*m_apiPackageStore, migErr);
            if (migrated < 0)
                ALOG_ERROR("api", "[vault] legacy secret migration failed: " << migErr);
        }

        // --- Agenda Store (per-agent calendar/agenda events) ---
        m_agendaStore = new AgendaStore(m_dataStore);

        // --- Consolidation Tool (session-gated: only available during consolidation sessions) ---
        m_tools.Register(std::make_unique<ConsolidationTool>(m_memoryStore, m_ontologyStore, m_sessionManager, m_memoryFileStore, m_agentStore, m_sessionReportStore, m_embeddingService));

        // --- Session Tags Store (keywords for ontology retrieval) ---
        m_sessionTagsStore = new SessionTagsStore(m_dataStore);
        m_adminServer->SetSessionTagsStore(m_sessionTagsStore);

        // --- Prompt Log Store (LLM call logging for benchmarking/audit) ---
        m_promptLogStore = new PromptLogStore(m_dataStore);
        m_chainRunner->SetPromptLogStore(m_promptLogStore);
        m_chainRunner->SetPromptLogLevel(ParsePromptLogLevel(config.promptLogLevel));
        m_adminServer->SetPromptLogStore(m_promptLogStore);

        // --- Context Provider Registry ---
        m_contextRegistry = new ContextProviderRegistry();
        m_contextRegistry->Register(std::make_unique<IdentityProvider>());
        m_contextRegistry->Register(std::make_unique<RuntimeEnvironmentProvider>());
        m_contextRegistry->Register(std::make_unique<TemporalContextProvider>(m_agendaStore));
        m_contextRegistry->Register(
            std::make_unique<SessionNotesProvider>(m_sessionNotesStore));

        // Channel Context Provider (#15) — renders trusted channel arrivals
        // as a system-message context block. Reads from ChannelContextStore
        // (#14), written by ExecuteChannelDispatch before the chain runs.
        if (m_channelContextStore) {
            m_contextRegistry->Register(
                std::make_unique<ChannelContextProvider>(m_channelContextStore));
        }
        m_contextRegistry->Register(
            std::make_unique<SessionReportProvider>(
                m_sessionReportStore, m_embeddingService));
        m_contextRegistry->Register(
            std::make_unique<ActiveMemoryProvider>(
                m_memoryStore, m_diaryStore, m_ontologyStore, m_memoryFileStore,
                m_sessionTagsStore, m_sessionManager, m_scheduler, m_embeddingService));
        m_chainRunner->SetContextRegistry(m_contextRegistry);
        m_adminServer->SetContextRegistry(m_contextRegistry);

        // --- Sessions Tool (agent-facing session layer access) ---
        m_tools.Register(std::make_unique<SessionsTool>(
            m_sessionManager, m_sessionNotesStore, m_sessionTagsStore, m_compactionService));

        // --- Agenda Tool (agent-facing calendar/agenda) ---
        m_tools.Register(std::make_unique<AgendaTool>(m_agendaStore));
        m_memorySearch = new memory::MemorySearch(
            m_memoryStore, m_ontologyStore, m_memoryFileStore, m_diaryStore);
        m_memorySearch->SetSessionManager(m_sessionManager);
        m_memorySearch->SetAgentStore(m_agentStore);

        // Verify FTS sync at startup — auto-rebuild if out of sync.
        {
            auto diaryStats = m_memorySearch->VerifyFtsSync("diary");
            if (diaryStats.source_count != diaryStats.fts_count) {
                ALOG_WARNING("kernel", "Diary FTS out of sync ("
                          << diaryStats.fts_count << "/" << diaryStats.source_count
                          << "), rebuilding...");
                m_memorySearch->RebuildDiaryIndex();
            }
            auto obsStats = m_memorySearch->VerifyFtsSync("observations");
            if (obsStats.source_count != obsStats.fts_count) {
                ALOG_WARNING("kernel", "Observations FTS out of sync ("
                          << obsStats.fts_count << "/" << obsStats.source_count
                          << "), running backfill...");
                // Re-run the backfill SQL directly.
                if (m_dataStore) {
                    m_dataStore->Exec(
                        "INSERT INTO observations_fts(rowid, text) "
                        "SELECT id, text FROM observations "
                        "WHERE id NOT IN (SELECT rowid FROM observations_fts)");
                }
            }
        }

        m_adminServer->SetMemorySearch(m_memorySearch);

        // --- Memory Tool (agent-facing composite search) ---
        m_tools.Register(std::make_unique<MemoryTool>(
            m_memorySearch, m_memoryStore, m_sessionManager, m_memoryFileStore, m_agentStore));

        // --- Node discovery tool ---
        m_tools.Register(std::make_unique<NodeTool>(m_nodeManager));

        // --- Gallivanting Store + Tool (threads + sessions) ---
        m_gallivantingStore = new GallivantingStore(m_dataStore);
        m_adminServer->SetGallivantingStore(m_gallivantingStore);
        m_tools.Register(std::make_unique<GallivantingTool>(m_gallivantingStore));

        // --- Lua scripting subsystem (per-agent VM, script persistence) ---
        m_scriptStore = new ScriptStore(m_dataStore);
        m_adminServer->SetScriptStore(m_scriptStore);
        m_adminServer->SetApiPackageStore(m_apiPackageStore);
        m_adminServer->SetLuaScriptDir(m_config.lua_script_dir);
        m_configStore = new AgentConfigStore(m_dataStore);
        // #93 P3a: the config store resolves/vaults credential-shaped values
        // through the same vault the api packages use (agent:<id> scope).
        m_configStore->SetVault(m_secretsVault);
        // #93 P3 slice 3: provider persistence rides the replicated config
        // store (kv rows under "__providers"; api keys + auth blobs vaulted).
        m_adminServer->SetProviderConfigStore(m_configStore);
        // #93 P3: replication coherence — remote agent_config applies
        // invalidate this store's cache (sync writes bypass the API), and
        // "__providers" rows additionally rebuild the in-memory provider
        // model (a peer changed provider config).
        if (m_syncStore)
            m_syncStore->SetApplyNotifier(
                [this](const std::string& t, const std::string& k) {
                    m_configStore->OnSyncApplied(t, k);
                    m_adminServer->OnProviderConfigSyncApplied(t, k);
                });
        {
            std::string agentMigErr;
            const int agentMigrated =
                m_secretsVault->MigrateAgentConfigSecrets(agentMigErr);
            if (agentMigrated < 0)
                ALOG_ERROR("api", "[vault] agent credential migration failed: "
                           << agentMigErr);
            else if (agentMigrated > 0)
                ALOG_INFO("api", "[vault] migrated " << agentMigrated
                           << " agent credential(s) into the vault");
        }

        // Load persisted search config if available
        m_configStore->WarmCache("__kernel__");
        std::string persistedSearch = m_configStore->Get("__kernel__", "search_config");
        if (!persistedSearch.empty()) {
            Json::Value searchJson;
            Json::CharReaderBuilder reader;
            std::istringstream ss(persistedSearch);
            std::string errs;
            if (Json::parseFromStream(reader, ss, &searchJson, &errs)) {
                if (searchJson.isMember("provider") && m_config.search.provider.empty())
                    m_config.search.provider = searchJson["provider"].asString();
                if (searchJson.isMember("api_key") && m_config.search.api_key.empty())
                    m_config.search.api_key = searchJson["api_key"].asString();
                if (searchJson.isMember("endpoint") && m_config.search.endpoint.empty())
                    m_config.search.endpoint = searchJson["endpoint"].asString();
                if (searchJson.isMember("default_count") && m_config.search.default_count == 5)
                    m_config.search.default_count = searchJson["default_count"].asInt();
                if (searchJson.isMember("rate_limit_per_minute") && m_config.search.rate_limit_per_minute == 10)
                    m_config.search.rate_limit_per_minute = searchJson["rate_limit_per_minute"].asInt();
            }
        }

        // Load stored_links and rss_feeds from persisted config (if not set via CLI)
        {
            std::string persistedLinks = m_configStore->Get("__kernel__", "stored_links");
            if (!persistedLinks.empty() && m_config.stored_links.empty()) {
                Json::Value linksJson;
                Json::CharReaderBuilder reader;
                std::istringstream ss(persistedLinks);
                std::string errs;
                if (Json::parseFromStream(reader, ss, &linksJson, &errs) && linksJson.isArray()) {
                    for (const auto& item : linksJson) {
                        KernelConfig::StoredLinkConfig sl;
                        sl.id = item.get("id", "").asString();
                        sl.label = item.get("label", "").asString();
                        sl.url = item.get("url", "").asString();
                        if (!sl.id.empty() && !sl.url.empty()) {
                            m_config.stored_links.push_back(std::move(sl));
                        }
                    }
                }
            }

            std::string persistedFeeds = m_configStore->Get("__kernel__", "rss_feeds");
            if (!persistedFeeds.empty() && m_config.rss_feeds.empty()) {
                Json::Value feedsJson;
                Json::CharReaderBuilder reader;
                std::istringstream ss(persistedFeeds);
                std::string errs;
                if (Json::parseFromStream(reader, ss, &feedsJson, &errs) && feedsJson.isArray()) {
                    for (const auto& item : feedsJson) {
                        KernelConfig::RssFeedConfig rf;
                        rf.id = item.get("id", "").asString();
                        rf.label = item.get("label", "").asString();
                        rf.url = item.get("url", "").asString();
                        if (!rf.id.empty() && !rf.url.empty()) {
                            m_config.rss_feeds.push_back(std::move(rf));
                        }
                    }
                }
            }
        }

        // ChannelsTool composite — must exist before Lua scripts load
        // so adapters can register into it during Phase 2
        m_channelsTool = new ChannelsTool();
        m_channelsTool->SetConfigStore(m_configStore);
        m_channelsTool->SetChannelContextStore(m_channelContextStore);
        // Note: m_channelManager is null here — wired in Initialize()
        m_tools.Register(std::unique_ptr<IToolHandler>(m_channelsTool));
        m_adminServer->SetChannelsTool(m_channelsTool);
        m_adminServer->SetLuaStates(&m_luaStates);
        m_adminServer->SetConfigStore(m_configStore);

        m_tools.Register(std::make_unique<LuaTool>(m_luaStates, m_tools, m_httpClient, *m_scriptStore, m_configStore));

    // --- Diffusion store (must be initialized before RegisterBuiltinTools) ---
    m_diffusionStore = std::make_unique<DiffusionStore>(m_dataStore);
    m_adminServer->SetDiffusionStore(m_diffusionStore.get());
    ALOG_INFO("diffusion", "Store initialized");

    // --- Attachment store (Ticket 123) ---
    m_attachmentStore = std::make_unique<AttachmentStore>(m_dataStore);
    m_adminServer->SetAttachmentStore(m_attachmentStore.get());
    ALOG_INFO("attachment", "Store initialized");

    // Load persisted SOP server list if available
    if (m_configStore) {
        std::string persisted = m_configStore->Get("__kernel__", "sop_servers");
        if (!persisted.empty()) {
            Json::Value arr;
            Json::CharReaderBuilder reader;
            std::istringstream iss(persisted);
            std::string errs;
            if (Json::parseFromStream(reader, iss, &arr, &errs) && arr.isArray()) {
                m_config.sop_servers.clear();
                for (const auto& item : arr) {
                    if (item.isString()) m_config.sop_servers.push_back(item.asString());
                }
            }
        }
    }
    // --- SOP store (Ticket 125) ---
    m_sopStore = std::make_unique<SopStore>(m_config.dataDir / "sops", &m_httpClient, m_config.sop_servers);
    m_sopStore->Refresh();
    m_adminServer->SetSopStore(m_sopStore.get());
    ALOG_INFO("sop", "Store initialized");

    // --- Register built-in tools ---
    RegisterBuiltinTools(m_config);

        // --- Scheduler (cron-like, fires IncomingEvent → session pipeline) ---
        m_scheduler = new Scheduler(m_dataStore);
        m_scheduler->SetNodeId(std::to_string(m_config.node.id));
        m_scheduler->SetLeaseTtlMs(
            static_cast<int64_t>(m_config.node.leaseTtlMs));
        m_scheduler->SetLeaseGraceMs(
            static_cast<int64_t>(m_config.node.leaseGraceMs));
        // #176: task-run listing default (0 = unlimited)
        m_scheduler->SetRunListDefaultLimit(m_config.scheduler.taskRunListLimit);
        // #78 P2b: wire the lease acker to the peer layer (no-op when
        // single-node — PeerSyncService won't exist).
        if (m_peerSyncService) {
            m_scheduler->SetLeaseAckFn(
                [this](const std::string& scheduleId,
                       int64_t epoch) -> Scheduler::LeasePeerState {
                    auto ps = m_peerSyncService->CheckLeaseAck(scheduleId, epoch);
                    Scheduler::LeasePeerState out;
                    out.peersConfigured = ps.peersConfigured;
                    out.anyAcked = ps.anyAcked;
                    out.allDown = ps.allDown;
                    out.lastExchangeOkMs = ps.lastExchangeOkMs;
                    return out;
                });
            m_scheduler->SetClaimAckFn(
                [this](const std::string& runUuid)
                        -> Scheduler::ClaimPeerState {
                    auto cs = m_peerSyncService->CheckClaimAck(runUuid);
                    Scheduler::ClaimPeerState out;
                    out.peersConfigured = cs.peersConfigured;
                    out.confirmedMine = cs.confirmedMine;
                    out.showsOther = cs.showsOther;
                    out.allDown = cs.allDown;
                    return out;
                });
        }
        m_adminServer->SetScheduler(m_scheduler);
        m_scheduler->SetFireCallback([this](const IncomingEvent& event) {
            const std::string agentId = event.metadata.count("agent_id")
                ? event.metadata.at("agent_id") : "";
            const std::string tag = !event.metadata.empty() &&
                event.metadata.find("tag") != event.metadata.end()
                ? event.metadata.at("tag") : "";
            const std::string message = !event.metadata.empty() &&
                event.metadata.find("message") != event.metadata.end()
                ? event.metadata.at("message") : "";

            // Route consolidation schedules to agent-driven sessions.
            // The consolidation tool is only available when session_type="consolidation",
            // so the agent can review, promote, merge, retire, and generate perspectives.
            if (tag == "consolidation") {
                std::string consolidationPrompt;
                std::string sessionSubtype;

                ALOG_DEBUG("scheduler", "consolidation fired: agent=" << agentId
                          << " message=" << message);

                if (message.find("consolidate:") == 0) {
                    const std::string layerName = message.substr(12);
                    sessionSubtype = "review:" + layerName;

                    // Skip review if no observations are due for review on the target layer
                    if (m_memoryStore) {
                        int64_t targetLayerId = -1;
                        // Find the layer scoped to this agent
                        for (const auto& l : m_memoryStore->ListLayersForAgent(agentId)) {
                            if (l.name == layerName) { targetLayerId = l.id; break; }
                        }
                        if (targetLayerId < 0) {
                            // Agent has no layer with this name. Seed defaults
                            // in case the agent was created without them.
                            m_memoryStore->CreateDefaultLayersForAgent(agentId);
                            for (const auto& l : m_memoryStore->ListLayersForAgent(agentId)) {
                                if (l.name == layerName) { targetLayerId = l.id; break; }
                            }
                        }
                        if (targetLayerId >= 0) {
                            // For the due-for-review check, find the real agent ID if we have one
                            std::string reviewAgentId = agentId;
                            if (m_agentStore) {
                                auto agents = m_agentStore->List();
                                if (!agents.empty()) {
                                    // Use the first agent that has observations on this layer
                                    for (const auto& a : agents) {
                                        auto dueForReview = m_memoryStore->ListObservationsDueForReview(
                                            a.id, targetLayerId,
                                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::system_clock::now().time_since_epoch()).count());
                                        if (!dueForReview.empty()) {
                                            reviewAgentId = a.id;
                                            break;
                                        }
                                    }
                                }
                            }
                            auto dueForReview = m_memoryStore->ListObservationsDueForReview(
                                reviewAgentId, targetLayerId,
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch()).count());
                            if (dueForReview.empty()) {
                                ALOG_WARNING("scheduler", "skipping review: no observations due for layer "
                                          << layerName << " agent=" << reviewAgentId);
                                return "skipped:review_nothing_due";
                            }
                        }
                    }

                    // Use the layer's consolidation_review_prompt if available
                    consolidationPrompt = "Review your " + layerName +
                        "-layer observations. Promote, merge, or retire as needed. "
                        "Then generate updated perspectives using the consolidation tool with action perspective:generate.\n\n"
                        "Start with the consolidation tool (action: review) to see overdue observations, "
                        "then use actions promote, merge, or retire as appropriate. "
                        "Finish with action perspective:generate for "
                        "retrospective, current, and future perspectives, then call action summary.";
                } else if (message == "intake" || message.find("intake:") == 0) {
                    // Skip intake if there's no new data to process for any known agent
                    if (m_consolidation && !m_consolidation->HasAnyPendingIntakeData()) {
                        ALOG_WARNING("scheduler", "skipping intake: no pending data");
                        return "skipped:no_pending_intake";
                    }
                    sessionSubtype = "intake";
                    std::string layerHint;
                    std::optional<int64_t> targetLayerId;
                    if (message.find("intake:") == 0) {
                        const std::string layerName = message.substr(7);
                        layerHint = " for layer \"" + layerName + "\"";
                        // Resolve layer name to ID for automated intake
                        if (m_memoryStore) {
                            for (const auto& l : m_memoryStore->ListLayersForAgent(agentId)) {
                                if (l.name == layerName) {
                                    targetLayerId = l.id;
                                    break;
                                }
                            }
                            if (!targetLayerId.has_value()) {
                                m_memoryStore->CreateDefaultLayersForAgent(agentId);
                                for (const auto& l : m_memoryStore->ListLayersForAgent(agentId)) {
                                    if (l.name == layerName) { targetLayerId = l.id; break; }
                                }
                            }
                        }
                    }

                    // Run automated intake pipeline BEFORE agent session
                    // — scans diary entries and session turns, creates observations
                    if (m_consolidation) {
                        // Discover real agent IDs instead of hardcoding "default"
                        std::vector<std::string> intakeAgentIds;
                        if (m_agentStore) {
                            for (const auto& agent : m_agentStore->List()) {
                                intakeAgentIds.push_back(agent.id);
                            }
                        }
                        if (intakeAgentIds.empty()) {
                            // No agents registered — nothing to do
                        }
                        // Note: the agent will call fetch_pending via the consolidation tool
                        // to retrieve and process turns. No automated LLM call here.
                    }

                    consolidationPrompt =
                        "Process unprocessed session turns into lasting observations and update semantic memory.\n\n"
                        "1. Call consolidation tool with action \"fetch_pending\" to retrieve pending turns.\n"
                        "2. For each lasting observation worth keeping, call action \"create\" with "
                        "params: {text, tags, weight, layer}.\n"
                        "3. For entities, relationships, or facts worth remembering semantically, "
                        "call action \"ontology:upsert\" with params: {root_category, path, "
                        "properties: {key: {value, value_type, motivation}}}. "
                        "Valid root categories: persons, concepts, procedures, events, locations, organizations, projects. "
                        "Paths use / as separator, relative to the root category (e.g. \"Kestrel\" under persons, \"Animus\" under projects). "
                        "Valid value_types: text (or string), number, date, reference, url.\n"
                        "4. If observations reveal a significant shift in understanding, call "
                        "action \"perspective:generate\" for the relevant layer to update its temporal perspectives.\n"
                        "5. When finished, call action \"summary\".\n"
                        "6. If unprocessed MemoryFiles are returned by \"memory_file:fetch_pending\", read their content and extract observations and ontology entities from them. After processing each file, call \"memory_file:mark_processed\" with its id.\n"
                        "\n"
                        "Guidelines:\n"
                        "- Create observations for specific facts, decisions, and events.\n"
                        "- Create ontology entities for named things (projects, people, concepts) and their properties.\n"
                        "- Each layer has a horizon representing the duration that observations stored there should be relevant. Place information in the layer whose horizon matches the information's expected relevance.\n"
                        "- Review existing observations first (action \"review\" with filter \"all\") to avoid duplicates.\n"
                        "- Skip trivial, redundant, or already-known information.\n\n";

                // Build dynamic layer list from actual agent layers
                if (m_memoryStore && m_agentStore) {
                    std::set<std::string> seenHorizons;
                    std::vector<std::string> layerLines;
                    for (const auto& agent : m_agentStore->List()) {
                        for (const auto& l : m_memoryStore->ListLayersForAgent(agent.id)) {
                            if (!l.enabled) continue;
                            std::string line = l.name + " (" + l.horizon + ")";
                            if (seenHorizons.insert(line).second) {
                                layerLines.push_back(line);
                            }
                        }
                    }
                    if (layerLines.empty()) {
                        // Fallback if no layers found
                        consolidationPrompt += "Layers: day (1 day), week (1 week), month (1 month), year (1 year)";
                    } else {
                        consolidationPrompt += "Layers: ";
                        for (size_t i = 0; i < layerLines.size(); ++i) {
                            if (i > 0) consolidationPrompt += ", ";
                            consolidationPrompt += layerLines[i];
                        }
                    }
                } else {
                    consolidationPrompt += "Layers: day (1 day), week (1 week), month (1 month), year (1 year)";
                }
                } else if (message == "session_report" || message.find("session_report:") == 0) {
                    // Dedicated session reporting process (Ticket 134)
                    // Fires when sessions have turns newer than their last report.
                    // Uses session_reports.updated_at_unix_ms as watermark, NOT
                    // intake_processed — these are independent processes.
                    if (m_sessionReportStore &&
                        !m_sessionReportStore->HasSessionsNeedingReport(agentId)) {
                        ALOG_DEBUG("scheduler", "skipping session_report: no sessions need updating");
                        return "skipped:no_sessions_needing_report";
                    }
                    sessionSubtype = "session_report";

                    consolidationPrompt =
                        "Update session reports for sessions that have new activity.\n\n"
                        "1. Call the `consolidation` tool with action \"fetch_pending\" to retrieve pending turns.\n"
                        "2. For each session that had pending turns, update its session report by calling the `consolidation` tool (NOT the `sessions` tool) with action \"sessions:report\" and params: {session_id, summary, past_events, current_activity, forward_look}. Use these guidelines:\n"
                        "   - summary: A stable one-sentence description of what this session is about (update only if it has changed).\n"
                        "   - past_events: Key developments that have transpired since the last report (compress if the session has a long history).\n"
                        "   - current_activity: What is happening right now in this session.\n"
                        "   - forward_look: What you expect to happen next in this session.\n"
                        "   Each field should be ~200 chars max. The report is upserted — it replaces any previous report for that session.\n"
                        "3. When finished, call the `consolidation` tool with action \"summary\".\n\n"
                        "IMPORTANT: All actions in this session (fetch_pending, sessions:report, summary) are on the `consolidation` tool. Do NOT use the `sessions` tool for reporting.\n\n"
                        "Do NOT create observations, update ontology, or generate perspectives. This session is strictly for session reports.\n";
                } else {
                    // Unknown consolidation message — skip
                    ALOG_DEBUG("scheduler", "unknown consolidation message: " << message);
                    return "skipped:unknown_message";
                }

                // Create a consolidation session for intake or review
                // Intake sessions: agent calls fetch_pending to retrieve turns,
                // then creates observations.
                // Review sessions: agent reviews overdue observations.
                {
                    std::vector<std::string> targetAgents;
                    if (sessionSubtype == "intake") {
                        // Intake processes data for all known agents
                        targetAgents = m_consolidation
                            ? m_consolidation->GetKnownAgents()
                            : std::vector{agentId};
                    } else {
                        // Review is scoped to the schedule's agent
                        targetAgents = m_agentStore
                            ? std::vector{agentId}
                            : std::vector{agentId};
                    }
                    for (const auto& curAgent : targetAgents) {
                        // Unique session per consolidation run (timestamp suffix)
                        // so each run starts fresh without delete-and-recreate.
                        // ConsolidationTool uses substring matching on the key
                        // (find("consolidation:intake:")), so the timestamp suffix
                        // doesn't break mode detection.
                        std::string consKey = "consolidation:" + sessionSubtype + ":" + curAgent
                            + ":" + std::to_string(
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch()).count());

                        SessionKey key{ consKey, "" };

                        auto session = m_sessionManager->GetOrCreate(key);
                        if (!session) continue;

                        ALOG_DEBUG("scheduler", "consolidation session created: key=" << consKey
                                  << " agent=" << curAgent);

                        session->SetAgentId(curAgent);
                        session->SetSessionType("consolidation");

                        std::string providerId;
                        std::string model;
                        auto agent = m_agentStore->GetById(curAgent);
                        if (agent) {
                            // Default provider/model
                            if (!agent->default_provider.empty()) providerId = agent->default_provider;
                            if (!agent->default_model.empty()) model = agent->default_model;

                            // Consolidation type overrides — empty falls through to default
                            if (sessionSubtype == "intake" || sessionSubtype.rfind("intake:", 0) == 0) {
                                if (!agent->intake_provider.empty()) providerId = agent->intake_provider;
                                if (!agent->intake_model.empty()) model = agent->intake_model;
                            } else if (sessionSubtype == "review" || sessionSubtype.rfind("review:", 0) == 0) {
                                if (!agent->review_provider.empty()) providerId = agent->review_provider;
                                if (!agent->review_model.empty()) model = agent->review_model;
                            } else if (sessionSubtype == "session_report") {
                                if (!agent->session_report_provider.empty()) providerId = agent->session_report_provider;
                                if (!agent->session_report_model.empty()) model = agent->session_report_model;
                            }
                        }

                        std::string registryKey = providerId;
                        if (!providerId.empty() && m_adminServer) {
                            auto ps = m_adminServer->GetProvider(providerId);
                            if (ps) registryKey = ps->providerType.empty() ? ps->providerId : ps->providerType;
                        }
                        std::string identity = m_config.agent.identity;
                        std::size_t contextWindow = ResolveContextWindow(curAgent, providerId, model);

                        m_jobs.EnqueueInLane(
                            ::animus::jobs::JobLane::Cognition,
                            [this, session, consolidationPrompt, providerId, registryKey, model, identity, contextWindow]() {
                                auto sessionAccess = SessionAccess(session, SessionAccessMode::ReadWrite);
                                auto result = m_chainRunner->ExecuteOnSession(
                                    sessionAccess,
                                    consolidationPrompt,
                                    identity,
                                    registryKey,
                                    providerId,
                                    model,
                                    contextWindow);
                                if (m_sessionManager) {
                                    m_sessionManager->FlushSession(session->Id());
                                    if (result.triggered_compaction && m_compactionService) {
                                        m_compactionService->CompactIfNeeded(
                                            session->Id(), identity, registryKey, model, contextWindow);
                                    }
                                }
                            });
                    }
                }
                return "dispatched:consolidation";
            }

            // Gallivanting sessions get a fresh session each trigger so
            // exploration isn't contaminated by prior gallivanting context.
            // The session type is set to "gallivanting" for tool filtering.
            bool isGallivanting = (tag == "gallivanting");

            std::string connector;
            if (isGallivanting) {
                connector = "gallivanting:" + agentId + ":" + std::to_string(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
            } else {
                // Unique session per fire (timestamp suffix) so each run
                // starts fresh instead of accumulating context in one
                // long-lived session. Same pattern as consolidation and
                // gallivanting above. Poll loop is single-threaded, so the
                // millisecond timestamp is collision-free per schedule.
                connector = "scheduled:" + tag + ":" + agentId + ":" + std::to_string(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
            }

            SessionKey key{ connector, "" };
            auto session = m_sessionManager->GetOrCreate(key);
            if (!session) return "error:session_create_failed";

            if (session->AgentId().empty()) {
                session->SetAgentId(agentId);
            }
            if (isGallivanting && session->SessionType().empty()) {
                session->SetSessionType("gallivanting");
            }

            // If this is a gallivanting session, check for an active thread
            // with a custom prompt_template. If found, use it instead of
            // the schedule message. The thread's prompt takes precedence.
            // If no thread prompt exists, append tool-usage guidance to the
            // schedule message so the agent knows to use the gallivanting tool.
            //
            // Defensive fallback: if the message still contains a JSON blob
            // with a "prompt_template" field (legacy schedule format before
            // metadata column was added), extract the prompt text.
            std::string effectiveMessage = message;
            if (isGallivanting) {
                // Defensive: extract prompt_template from legacy JSON-in-message
                if (!effectiveMessage.empty() && effectiveMessage[0] == '{') {
                    Json::Value parsed;
                    Json::CharReaderBuilder builder;
                    std::istringstream stream(effectiveMessage);
                    std::string errs;
                    if (Json::parseFromStream(builder, stream, &parsed, &errs) &&
                        parsed.isMember("prompt_template") &&
                        parsed["prompt_template"].isString() &&
                        !parsed["prompt_template"].asString().empty()) {
                        effectiveMessage = parsed["prompt_template"].asString();
                    }
                }

                if (m_gallivantingStore) {
                    auto threads = m_gallivantingStore->ListThreads(agentId);
                    bool foundThreadPrompt = false;
                    for (const auto& t : threads) {
                        if (t.enabled && !t.prompt_template.empty()) {
                            effectiveMessage = t.prompt_template;
                            foundThreadPrompt = true;
                            break;
                        }
                    }
                    // No thread prompt — load the default gallivanting template.
                    // Priority: embedded (compiled into binary) → filesystem → minimal fallback.
                    if (!foundThreadPrompt) {
                        bool loaded = false;

                        // 1. Embedded templates (compiled with ANIMUS_TEMPLATES_EMBED=ON)
                        if (!loaded && HasEmbeddedTemplates()) {
                            EmbeddedTemplate embedded;
                            if (GetEmbeddedTemplate("gallivanting/en.md", &embedded)) {
                                effectiveMessage = std::string(
                                    reinterpret_cast<const char*>(embedded.data),
                                    embedded.size);
                                loaded = true;
                            }
                        }

                        // 2. Filesystem (templates/gallivanting/en.md relative to cwd)
                        if (!loaded) {
                            const auto cwd = std::filesystem::current_path();
                            const std::vector<std::filesystem::path> roots = {
                                cwd / "templates",
                                cwd / ".." / "templates",
                            };
                            for (const auto& root : roots) {
                                auto path = root / "gallivanting" / "en.md";
                                if (std::filesystem::exists(path)) {
                                    std::ifstream in(path);
                                    if (in.is_open()) {
                                        std::ostringstream ss;
                                        ss << in.rdbuf();
                                        effectiveMessage = ss.str();
                                        loaded = true;
                                        break;
                                    }
                                }
                            }
                        }

                        // 3. Minimal fallback — should never happen if built with embedding
                        if (!loaded) {
                            ALOG_WARNING("scheduler", "gallivanting template not found "
                                      "(neither embedded nor on disk)");
                            effectiveMessage =
                                "# Gallivanting Block\n\n"
                                "Unstructured time to explore and create. "
                                "Use the `gallivanting` tool to manage threads. "
                                "**Call action \"record\" at session end** to persist your work.\n";
                        }
                    }
                }
            }

            std::string providerId = session->ProviderId();
            if (providerId.empty() && m_agentStore) {
                auto agent = m_agentStore->GetById(agentId);
                if (agent && !agent->default_provider.empty()) {
                    providerId = agent->default_provider;
                }
            }

            std::string registryKey = providerId;
            if (!providerId.empty() && m_adminServer) {
                auto ps = m_adminServer->GetProvider(providerId);
                if (ps) registryKey = ps->providerType.empty() ? ps->providerId : ps->providerType;
            }

            std::string model;
            if (m_agentStore) {
                auto agent = m_agentStore->GetById(agentId);
                if (agent && !agent->default_model.empty()) {
                    model = agent->default_model;
                }
            }
            std::size_t contextWindow = ResolveContextWindow(agentId, providerId, model);

            m_jobs.EnqueueInLane(
                ::animus::jobs::JobLane::Cognition,
                [this, session, effectiveMessage, providerId, registryKey, model, contextWindow]() {
                    auto sessionAccess = SessionAccess(session, SessionAccessMode::ReadWrite);
                    auto result = m_chainRunner->ExecuteOnSession(
                        sessionAccess,
                        effectiveMessage,
                        m_config.agent.identity,
                        registryKey,
                        providerId,
                        model,
                        contextWindow);
                    if (m_sessionManager) {
                        m_sessionManager->FlushSession(session->Id());
                        if (result.triggered_compaction && m_compactionService) {
                            m_compactionService->CompactIfNeeded(
                                session->Id(), m_config.agent.identity, registryKey, model, contextWindow);
                        }
                    }
                });

            return "dispatched:event";
        });
        m_tools.Register(std::make_unique<ScheduleTool>(m_scheduler));

        // --- Consolidation Pipeline (memory intake, layer consolidation, perspectives) ---
        m_consolidation = new ConsolidationPipeline(
            m_dataStore,
            m_memoryStore,
            m_ontologyStore,
            m_diaryStore,
            m_sessionManager,
            m_agentStore,
            // LLM callback — uses the chain runner's provider infrastructure
            [this](const std::string& agentId,
                   const std::string& systemPrompt,
                   const std::string& userPrompt) -> std::string {
                // Resolve provider instance -> type (same pattern as AdminServer dispatch)
                std::string providerId;
                std::string registryKey;
                std::string model;
                if (m_agentStore) {
                    auto agent = m_agentStore->GetById(agentId);
                    if (agent) {
                        if (!agent->default_provider.empty())
                            providerId = agent->default_provider;
                        if (!agent->default_model.empty())
                            model = agent->default_model;
                    }
                }
                if (!providerId.empty() && m_adminServer) {
                    auto ps = m_adminServer->GetProvider(providerId);
                    if (ps) {
                        registryKey = ps->providerType.empty()
                            ? ps->providerId : ps->providerType;
                    }
                }
                if (registryKey.empty()) registryKey = providerId;

                // Create a consolidation session with proper type so only
                // consolidation-safe tools are available (no email, social, http, etc.)
                // Unique key per run (timestamp suffix) — no delete-and-recreate needed.
                SessionKey key{
                    "consolidation:intake:" + agentId + ":" + std::to_string(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count()),
                    ""
                };

                auto session = m_sessionManager->GetOrCreate(key);
                if (!session) return "{}";

                session->SetAgentId(agentId);
                session->SetSessionType("consolidation");

                std::size_t contextWindow = ResolveContextWindow(agentId, providerId, model);

                auto sessionAccess = SessionAccess(session, SessionAccessMode::ReadWrite);
                auto result = m_chainRunner->ExecuteOnSession(
                    sessionAccess,
                    userPrompt,
                    systemPrompt,
                    registryKey,
                    providerId,
                    model,
                    contextWindow);

                if (m_sessionManager) {
                    m_sessionManager->FlushSession(session->Id());
                    if (result.triggered_compaction && m_compactionService) {
                        m_compactionService->CompactIfNeeded(
                            session->Id(), systemPrompt, registryKey, model, contextWindow);
                    }
                }

                return result.success ? result.response : "{}";
            });
        {
            ConsolidationPipeline::Config cfg;
            cfg.intake_enabled = true;
            cfg.intake_cron_expr = "*/5 * * * *";
            m_consolidation->Configure(cfg);
            m_consolidation->SetMemoryFileStore(m_memoryFileStore);
            m_consolidation->SetContextWindowResolver([this](const std::string& aid) -> std::size_t {
                std::string providerId, model;
                if (m_agentStore) {
                    auto agent = m_agentStore->GetById(aid);
                    if (agent) {
                        if (!agent->default_provider.empty()) providerId = agent->default_provider;
                        if (!agent->default_model.empty()) model = agent->default_model;
                    }
                }
                return ResolveContextWindow(aid, providerId, model);
            });
        }
        // #224: layer create/update/delete re-registers the consolidation
        // schedule set live — no restart needed (ChannelManager restart-hook
        // pattern). RefreshSchedules is a no-op unless the pipeline is
        // running, so pre-Start seeding and post-Stop callbacks are inert.
        // Pointer stability: both raw members are owned by the kernel and
        // ~AgentKernel deletes m_consolidation before m_memoryStore, so the
        // callback cannot dangle; after consolidation->Stop() the refresh
        // guard keeps late fires harmless.
        if (m_memoryStore && m_consolidation) {
            m_memoryStore->SetLayerChangedCallback(
                [consolidation = m_consolidation]() {
                    consolidation->RefreshSchedules();
                });
        }
        {
            std::string consErr;
            if (!m_consolidation->Start(m_scheduler, &consErr)) {
                ALOG_WARNING("kernel", "consolidation pipeline start failed: "
                          << consErr);
            }
        }

        // Start scheduler AFTER consolidation has registered its cron schedules.
        // Old schedules are cleaned up during registration; starting the poll loop
        // before cleanup allows stale schedules to fire immediately on boot.
        {
            std::string schedErr;
            if (!m_scheduler->Start(&schedErr)) {
                ALOG_WARNING("kernel", "scheduler start failed: " << schedErr);
            }
        }

        // Start the federation pull loop after the admin API is accepting
        // (peers may immediately pull our outbox) and after the scheduler,
        // so a flood of first-sync applies can't delay schedule startup.
        if (m_peerSyncService) {
            std::string syncErr;
            if (!m_peerSyncService->Start(&syncErr)) {
                ALOG_WARNING("kernel", "peer sync start failed: " << syncErr);
            }
        }

        // Provider throttle — concurrency limiter per provider
        m_providerThrottle = new ProviderThrottle(
            [this](const std::string& id) -> int {
                if (!m_adminServer) return 1;
                return m_adminServer->GetProviderConcurrency(id);
            });
        m_adminServer->SetProviderThrottle(m_providerThrottle);
    }

    // Configure authentication
    if (m_adminServer) {
        const char* authToken = std::getenv("ANIMUS_AUTH_TOKEN");
        std::string staticToken = authToken ? authToken : "";
        m_adminServer->ConfigureAuth(staticToken, m_dataStore);
        if (!staticToken.empty()) {
            ALOG_DEBUG("auth", "Static auth token configured from ANIMUS_AUTH_TOKEN");
        } else if (m_adminServer->GetAuthManager().HasUsers()) {
            ALOG_DEBUG("auth", "Authentication required (user accounts exist)");
        } else {
           ALOG_WARNING("auth", "WARNING: No auth configured. Set ANIMUS_AUTH_TOKEN or create user accounts.");
       }
    }

    std::string adminErr;
    if (m_adminServer && !m_adminServer->Start(
            m_config.admin,
            m_config.agent,
            m_config.agentStorage,
            m_config.interfaceStorage,
            m_config.interfaceModules,
            m_config.memory,
            m_config.constitutionStorage,
            m_config.providerStorage,
            m_providerRegistry,
            m_sessionManager,
            &m_jobs,
            m_startedAt,
            &adminErr)) {
        if (m_moduleManager) {
            m_moduleManager->UnloadAll();
        }
        m_jobs.Stop(::animus::jobs::JobSystemStopMode::Drain);
        if (error) {
            *error = adminErr;
        }
        return false;
    }

    // Two-phase Lua startup:
    // Phase 1: Load all stored scripts and execute their registration blocks.
    //          Scripts call animus.register_tool/interface/social, which creates
    //          LuaToolHandler entries in the ToolRegistry.
    // Phase 2: Composite tools finalize their merged schemas from registered plugins.
    LoadLuaScripts();

    // --- Phase 2: Wire registered social adapters into ChannelsTool composite ---
    if (m_channelsTool) {
        for (auto& [agentId, luaState] : m_luaStates) {
            for (const auto& plugin : luaState->GetRegisteredPlugins()) {
                if (plugin.type != "social") continue;
                if (m_channelsTool->HasPlugin(plugin.id)) continue; // already wired

                // Build a CompositePlugin from the LuaPluginInfo
                CompositePlugin cp;
                cp.id = plugin.id;
                cp.name = plugin.name;
                cp.capabilities = plugin.capabilities;
                cp.actionSchemas = plugin.actionSchemas;

                // Handler: route through the LuaToolHandler stored in plugin info.
                // The handler is NOT in the ToolRegistry — it's owned by LuaPluginInfo.
                const std::string adapterId = plugin.id;
                IToolHandler* handlerPtr = plugin.handler.get();

                cp.handler = [adapterId, handlerPtr](const ToolCall& call) -> ToolResult {
                    if (!handlerPtr) {
                        ToolResult err;
                        err.call_id = call.id;
                        err.success = false;
                        err.error = "social adapter '" + adapterId + "' handler is null";
                        return err;
                    }

                    // Parse args to inject adapter_id so Lua handler sees it
                    Json::Value args;
                    Json::CharReaderBuilder builder;
                    std::istringstream stream(call.arguments);
                    std::string errors;
                    Json::parseFromStream(builder, stream, &args, &errors);

                    // Inject adapter_id (bare type) so Lua handler knows which
                    // adapter type is handling this, alongside platform_id (instance)
                    args["adapter_id"] = adapterId;
                    Json::StreamWriterBuilder wb;
                    wb.settings_["indentation"] = "";
                    std::string newArgs = Json::writeString(wb, args);

                    ToolCall routedCall = call;
                    routedCall.arguments = newArgs;

                    return handlerPtr->Execute(routedCall);
                };

                m_channelsTool->RegisterPlugin(cp);
            }
        }
        m_channelsTool->FinalizeSchema();
    }

    // --- Channel Manager (Ticket 056) ---
    // Unified management for all communication channels (IRC, Telegram, VK, etc.)
    {
        // Create the message queue first — the dispatch lambda captures it.
        // The flush callback retrieves the stored ReplyTarget and calls ExecuteChannelDispatch.
        m_messageQueue = std::make_unique<MessageQueue>(
            [this](const std::string& queueKey, const std::string& concatenatedMessage) {
                // Retrieve the stored ReplyTarget and agentId for this session
                ChannelManager::ReplyTarget replyTarget;
                std::string agentId;
                std::string metadata;
                {
                    std::lock_guard<std::mutex> lock(m_pendingReplyTargetsMutex);
                    auto it = m_pendingReplyTargets.find(queueKey);
                    if (it != m_pendingReplyTargets.end()) {
                        replyTarget = it->second.replyTarget;
                        agentId = it->second.agentId;
                        metadata = it->second.metadata;
                    }
                }
                // Strip the "channel:" prefix to get the session key
                std::string sessionKey = queueKey;
                if (sessionKey.size() > 8 && sessionKey.substr(0, 8) == "channel:") {
                    sessionKey = sessionKey.substr(8);
                }
                ExecuteChannelDispatch(sessionKey, concatenatedMessage, replyTarget, agentId, metadata);
            });

        // Wire MessageQueue into ChainRunner for interjection support
        m_chainRunner->SetMessageQueue(m_messageQueue.get());

        auto dispatch = [this](const std::string& agentId,
                               const std::string& sessionKey,
                               const std::string& message,
                               const std::string& sessionType,
                               const ChannelManager::ReplyTarget& rt,
                               const std::string& metadata) {
            ALOG_DEBUG("channels:dispatch", "agentId=" << agentId
                      << " sessionKey=" << sessionKey
                      << " type=" << sessionType);

            // Stamp session identity onto the target so send failures can be
            // written back to the originating session (#30).
            ChannelManager::ReplyTarget replyTarget = rt;
            replyTarget.session_key = sessionKey;
            replyTarget.agent_id = agentId;

            // Channel queueing knobs (read per-arrival, both optional):
            //   min_response_interval — cooldown between chains (seconds)
            //   aggregation_window_ms — pre-chain debounce for burst arrivals
            //     (multi-PRIVMSG splits, cross-talk): first arrival arms a
            //     fixed window, the batch flushes as one concatenated turn.
            const int interval = GetChannelInterval(replyTarget.channel_name);
            int aggregationWindowMs = 0;
            if (m_channelManager) {
                auto ch = m_channelManager->GetChannel(replyTarget.channel_name);
                if (ch) {
                    aggregationWindowMs = ch->config.get("aggregation_window_ms", 0).asInt();
                }
            }

            if ((interval > 0 || aggregationWindowMs > 0) && m_messageQueue) {
                // Sender label for queued aggregates (#150/#173): the shared
                // parse seam (top-level author_handle/author, then
                // origin.user). Empty metadata → empty label → bare
                // concatenation, unchanged for adapters that don't emit.
                std::string sender = AuthorLabelFromMetadata(metadata);

                // Check if a chain is currently active on this session
                std::string queueKey = "channel:" + sessionKey;

                // Set interjection mode from channel config (Ticket 115)
                // Default: enabled. Community channels may disable it.
                bool allowInterjection = true;
                if (m_channelManager) {
                    auto ch = m_channelManager->GetChannel(replyTarget.channel_name);
                    if (ch) {
                        allowInterjection = ch->config.get("allow_interjection", true).asBool();
                    }
                }
                m_messageQueue->SetInterjectionEnabled(queueKey, allowInterjection);

                // Always push to queue — the queue handles the logic:
                // - If chain is active: accumulate (will be flushed when chain ends)
                // - If no chain and no timer: set timer, accumulate
                // - If timer running: accumulate
                // - If max queued hit: force flush
                // The flush callback will call ExecuteChannelDispatch.
                // But we need to preserve the ReplyTarget for the flush callback...
                // Store it in a per-session map.
                {
                    std::lock_guard<std::mutex> lock(m_pendingReplyTargetsMutex);
                    m_pendingReplyTargets[queueKey] = {replyTarget, agentId, metadata};
                }

                auto now = std::chrono::system_clock::now();
                auto unixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()).count();

                m_messageQueue->Push(queueKey, sender, message,
                    static_cast<std::uint64_t>(unixMs),
                    interval,
                    50,  // max queued — TODO: read from channel config
                    aggregationWindowMs);

                // If no chain is active and no timer is running, the Push already
                // started a timer. When it fires, the flush callback will execute.
                // If a chain IS active, the message will be flushed when the chain ends
                // and the cooldown expires.
                return;
            }

            // No interval configured — dispatch directly (existing behavior)
            // Ensure the session has the right agent assigned
            SessionKey key{"channel:" + sessionKey, ""};
            auto session = m_sessionManager->GetOrCreate(key);
            if (!session) return;

            if (session->AgentId().empty()) {
                session->SetAgentId(agentId);
            } else if (session->Turns().empty()) {
                session->SetAgentId(agentId);
            }
            if (m_agentStore && !session->AgentId().empty()
                && !m_agentStore->GetById(session->AgentId()).has_value()) {
                ALOG_DEBUG("kernel", "session references unknown agent: "
                          << session->AgentId() << ", skipping dispatch");
            }

            ExecuteChannelDispatch(sessionKey, message, replyTarget, agentId, metadata);
        };

        // Log callback — appends message to session history without firing a chain
        auto logCallback = [this](
            const std::string& agentId,
            const std::string& sessionKey,
            const std::string& message,
            const std::string& sessionType) {
            SessionKey key{"channel:" + sessionKey, ""};
            auto session = m_sessionManager->GetOrCreate(key);
            if (!session) return;

            if (!agentId.empty() && session->AgentId().empty()) {
                session->SetAgentId(agentId);
            }

            auto now = std::chrono::system_clock::now();
            auto unixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();

            SessionTurn turn;
            turn.role = "user";
            turn.content = message;
            turn.unix_ms = static_cast<std::uint64_t>(unixMs);
            session->AddTurn(std::move(turn));

            // Persist session
            m_sessionManager->FlushSession(session->Id());
        };

        m_channelManager = new ChannelManager(
            m_httpClient, m_configStore, dispatch, logCallback,
            // Session query callback: return metadata values for dedup
            [this](const std::string& sessionKey, const std::string& metadataKey) -> std::vector<std::string> {
                if (!m_sessionManager) return {};
                std::string fullKey = "channel:" + sessionKey + "||";
                return m_sessionManager->GetMetadataValues(fullKey, metadataKey);
            });

        // Wire ChannelManager into AdminServer for route handlers
        if (m_adminServer) {
            m_adminServer->SetChannelManager(m_channelManager);
        }

        // Send-failure witness (#30): a yielded reply that does not land
        // becomes a session note the agent and admin UI can see — not just a
        // kernel log line.
        m_channelManager->SetSendFailureCallback(
            [this](const ChannelReplyTarget& target, const std::string& error) {
                ReportChannelSendFailure(target, error);
            });

        // Wire ChannelManager into ChannelsTool for list action
        if (m_channelsTool) {
            m_channelsTool->SetChannelManager(m_channelManager);
        }

        // #144: live view of the Lua plugin registry so channel start can
        // distinguish plugin-owned types from types with no implementation
        // anywhere and WARN on the latter. Read at channel-start time, so
        // agents whose Lua state registers later are seen on their channels'
        // next start/restart.
        m_channelManager->SetSocialPluginProbe(
            [this](const std::string& type) {
                for (auto& [agentId, luaState] : m_luaStates) {
                    for (const auto& plugin : luaState->GetRegisteredPlugins()) {
                        if (plugin.type == "social" && plugin.id == type) {
                            return true;
                        }
                    }
                }
                return false;
            });

        m_channelManager->Initialize();
    }

    m_running = true;

    // Compute pending embeddings after all stores are initialized
    if (m_embeddingService && m_embeddingService->IsAvailable()) {
        m_jobs.Enqueue([this]() {
            ComputePendingEmbeddings();
            ComputePendingReportEmbeddings();
        });
    }

    return true;
}

void AgentKernel::SendAutoReply(const ChannelManager::ReplyTarget& target,
                                   const std::string& text) {
    if (!m_channelManager) return;
    m_channelManager->SendReply(target, text);
}

void AgentKernel::ReportChannelSendFailure(const ChannelReplyTarget& target,
                                           const std::string& error) {
    ALOG_WARNING("channels", "send failure [" << target.channel_type << "] "
              << error);
    // Session-visible witness; guarded so delivery notes never crowd out the
    // agent's own notes at the per-session cap.
    if (target.session_key.empty() || !m_sessionNotesStore) return;
    const std::string fullSessionKey = "channel:" + target.session_key;
    const auto count = m_sessionNotesStore->CountForSession(fullSessionKey, target.agent_id);
    if (count >= SessionNotesStore::kMaxNotesPerSession) return;

    std::string peer = target.peer_id.empty() ? target.post_id : target.peer_id;
    std::string brief = error;
    if (brief.size() > 200) brief = brief.substr(0, 197) + "...";
    const std::string bullet = "[delivery-failed] " + target.channel_type
        + " reply to " + target.channel_name + "/" + peer
        + " was NOT delivered: " + brief
        + ". The reader did not see it.";
    m_sessionNotesStore->Create(fullSessionKey, target.agent_id, bullet,
                                static_cast<int>(count));
}

int AgentKernel::GetChannelInterval(const std::string& channelName) const {
    if (!m_channelManager) return 0;
    auto ch = m_channelManager->GetChannel(channelName);
    if (!ch) return 0;
    return ch->config.get("min_response_interval", 0).asInt();
}

void AgentKernel::ExecuteChannelDispatch(
        const std::string& sessionKey,
        const std::string& message,
        const ChannelManager::ReplyTarget& replyTarget,
        const std::string& agentId,
        const std::string& metadata) {
    SessionKey key{"channel:" + sessionKey, ""};
    auto session = m_sessionManager->GetOrCreate(key);
    if (!session) return;

    // Ensure the session has the correct agent_id. Unbound adapters pass
    // empty — 'default' is the canonical unbound id and the store's partition
    // key. Live-verified failure (Aug 28, DM session 298): fresh sessions
    // stayed empty → arrival.agent_id='' → provider bails on the empty check
    // → no Channel Context card → model fell back to tool-posting its reply.
    if (session->AgentId().empty()) {
        session->SetAgentId(agentId.empty() ? "default" : agentId);
    }

    // Resolve provider
    std::string providerId = session->ProviderId();
    if (providerId.empty() && m_agentStore && !session->AgentId().empty()) {
        auto agent = m_agentStore->GetById(session->AgentId());
        if (agent && !agent->default_provider.empty()) {
            providerId = agent->default_provider;
        }
    }
    if (providerId.empty() && m_adminServer) {
        providerId = m_adminServer->GetDefaultProviderId();
    }
    if (providerId.empty()) {
        ALOG_WARNING("channels:dispatch", "No provider resolved for sessionKey="
                  << sessionKey << " — dropping message");
        return;
    }

    std::string registryKey = providerId;
    ProviderState providerState;
    bool hasProviderState = false;
    if (m_adminServer) {
        auto ps = m_adminServer->GetProvider(providerId);
        if (ps) {
            providerState = *ps;
            hasProviderState = true;
            registryKey = providerState.providerType.empty()
                ? providerState.providerId : providerState.providerType;
        }
    }

    SlotGuard throttleGuard;
    if (m_providerThrottle) {
        throttleGuard = m_providerThrottle->Acquire(providerId);
    }

    std::string model = hasProviderState ? providerState.defaultModel : "";
    if (m_agentStore && !session->AgentId().empty()) {
        auto agent = m_agentStore->GetById(session->AgentId());
        if (agent && !agent->default_model.empty()) {
            model = agent->default_model;
        }
    }
    std::size_t contextWindow = ResolveContextWindow(session->AgentId(), providerId, model);
    if (model.empty()) {
        model = "gpt-4o";
    }

    const std::string identity = m_config.agent.identity;
    const int interval = GetChannelInterval(replyTarget.channel_name);

    // Notify queue that a chain is starting (cancels any pending timer)
    if (m_messageQueue && interval > 0) {
        m_messageQueue->NotifyChainStart("channel:" + sessionKey);
    }

    std::string dispatchAuthor;  // #133: author handle for this trigger, survives the guard block
    // Record the trusted arrival (#14) BEFORE the chain runs — single write
    // point covering both direct dispatch and queue-flush (both funnel here).
    // The provider (#15) renders pending arrivals as the channel-context card.
    if (m_channelContextStore) {
        ChannelArrival arrival;
        arrival.session_key = "channel:" + sessionKey;
        arrival.agent_id = session->AgentId();
        arrival.channel_type = replyTarget.channel_type;
        arrival.channel_name = replyTarget.channel_name;
        arrival.platform_id = replyTarget.channel_type + ":" + replyTarget.channel_name;
        arrival.delivery = (replyTarget.type == ChannelManager::ReplyTarget::Chat)
                               ? "auto" : "tool";
        arrival.peer_id = replyTarget.peer_id;
        arrival.post_id = replyTarget.post_id;
        arrival.group_id = replyTarget.group_id;
        arrival.email_thread_id = replyTarget.email_thread_id;
        // message_type / author_* / ids: adapter-supplied dispatch metadata,
        // parsed through the shared seam (#173 — was inline here AND in the
        // queue-flush lane; behavior-identical extraction, and the emission
        // contract is now testable without a live kernel).
        ParseDispatchMetadata(
            metadata,
            (replyTarget.type == ChannelManager::ReplyTarget::Chat) ? "chat" : "wall",
            arrival);
        dispatchAuthor = arrival.author_handle;  // #133: capture final value (post origin-fallback)
        m_channelContextStore->AddArrival(arrival);
        m_channelContextStore->Prune(arrival.session_key, arrival.agent_id);
    }

    // Auto-delivery decision: Chat targets default to auto, but adapters may
    // declare "tool" delivery in dispatch metadata (see arrival override above).
    bool autoDeliver = (replyTarget.type == ChannelManager::ReplyTarget::Chat);
    {
        Json::Value meta;
        Json::CharReaderBuilder rb;
        std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
        std::string errs;
        if (reader->parse(metadata.data(), metadata.data() + metadata.size(),
                          &meta, &errs)
            && meta.isObject()
            && meta.get("delivery", "").asString() == "tool") {
            autoDeliver = false;
        }
    }

    m_jobs.EnqueueInLane(
        ::animus::jobs::JobLane::Cognition,
        [this, session, sessionKey, message, identity, registryKey, providerId, model, contextWindow, replyTarget, interval, metadata, autoDeliver, dispatchAuthor]() {
            // Per-message callback: send each assistant message to the channel
            // immediately rather than only the final response.
            // Only for Chat (DM) targets — Wall (post reply/mention) targets
            // require the agent to reply explicitly via the channels tool with
            // post_id/root_id; auto-posting the final text caused duplicate
            // and low-quality replies (agent narration leaking to the feed).
            // Adapters may opt out of auto-delivery entirely (delivery:"tool").
            ChainAssistantMessageCallback assistantCb;
            if (autoDeliver) {
                assistantCb = [this, replyTarget](const std::string& text) {
                    SendAutoReply(replyTarget, text);
                };
            }

            auto sessionAccess = SessionAccess(session, SessionAccessMode::ReadWrite);
            auto result = m_chainRunner->ExecuteOnSession(
                sessionAccess,
                message,
                identity,
                registryKey,
                providerId,
                model,
                contextWindow,
                nullptr,      // thinkingCallback
                nullptr,      // toolCallCallback
                assistantCb, // assistantMessageCallback
                dispatchAuthor); // author (#133)

            if (m_sessionManager) {
                m_sessionManager->FlushSession(session->Id());

                // Store adapter metadata on the last user turn (for message dedup)
                if (metadata != "{}" && metadata != "") {
                    m_sessionManager->SetLastUserTurnMetadata(
                        "channel:" + sessionKey + "||", metadata);
                }

                if (result.triggered_compaction && m_compactionService) {
                    m_compactionService->CompactIfNeeded(
                        session->Id(),
                        identity,
                        registryKey,
                        model,
                        contextWindow);
                }
            }
            if (!result.success) {
                ALOG_ERROR("channels:dispatch", "LLM execution failed: "
                          << result.error);
            }
            // Don't SendAutoReply here — the assistant message callback
            // already sent each message during the chain.
            // result.response holds the last message text, but it was already
            // delivered via the callback.

            // Chain saw the pending arrivals — mark them consumed (#14).
            if (m_channelContextStore) {
                m_channelContextStore->MarkAllConsumed("channel:" + sessionKey,
                                                       session->AgentId());
            }

            // Notify queue that the chain has ended — arms cooldown and/or
            // aggregation-window timers for any messages that accumulated
            // mid-chain (NotifyChainEnd itself decides by the session's
            // stored knobs; calling unconditionally is safe and covers the
            // aggregation_window-only case, which previously fell through
            // the interval>0 guard and left post-chain backlog stalled).
            if (m_messageQueue) {
                m_messageQueue->NotifyChainEnd("channel:" + sessionKey, interval);
            }
        });
}

std::size_t AgentKernel::ResolveContextWindow(const std::string& agentId,
                                             const std::string& providerId,
                                             const std::string& model) const {
    std::size_t contextWindow = 128000;  // Fallback if provider resolution fails

    // Resolve from provider state (per-model override → provider default → capability detection)
    if (m_adminServer) {
        auto ps = m_adminServer->GetProvider(providerId);
        if (ps) {
            contextWindow = AdminServer::ResolveProviderContextWindow(
                *ps, model, static_cast<std::uint32_t>(contextWindow));
        }
    }

    // Clamp to agent's context_window if set (>0 means user configured a cap)
    if (m_agentStore && !agentId.empty()) {
        auto agent = m_agentStore->GetById(agentId);
        if (agent && agent->context_window > 0) {
            contextWindow = std::min(contextWindow,
                                     static_cast<std::size_t>(agent->context_window));
        }
    }

    return contextWindow;
}

void AgentKernel::RegisterBuiltinTools(const KernelConfig& config) {
    // Native thinking mode: no ReplyTool needed.
    // Providers handle thinking/reasoning natively (Ollama think, Cohere thinking).

    // File operations — apply security policy
    auto fileTool = std::make_unique<FileTool>("");
    fileTool->SetPathAllowlist(config.file.pathAllowlist);
    fileTool->SetPathDenylist(config.file.pathDenylist);
    fileTool->SetRestrictToWorkspace(config.file.restrictToWorkspace);
    m_tools.Register(std::move(fileTool));

    // Shell execution — apply security policy
    auto shellTool = std::make_unique<ShellExecTool>("");
    shellTool->SetEnabled(config.shell.enabled);
    shellTool->SetCommandAllowlist(config.shell.commandAllowlist);
    shellTool->SetCommandDenylist(config.shell.commandDenylist);
    shellTool->SetMaxTimeout(config.shell.maxTimeoutSeconds);
    m_tools.Register(std::move(shellTool));

    // Web tools — share the kernel's HttpClient
    m_tools.Register(std::make_unique<HttpTool>(m_httpClient));

    // API packages (#26 c): runtime + agent-facing `api` tool
    if (m_apiPackageStore) {
        ApiRuntime::Config apiCfg;
        apiCfg.filesRoot = (m_config.dataDir / "api-files").string();
        m_apiRuntime = new ApiRuntime(m_apiPackageStore, &m_httpClient, apiCfg, m_secretsVault);
        if (m_adminServer) m_adminServer->SetApiRuntime(m_apiRuntime);
        m_tools.Register(std::make_unique<ApiTool>(m_apiRuntime));

        // Connection driver (#26 d): polls longpoll connections, runs
        // on_message hooks, routes dispatches to the owning agent.
        m_apiConnManager = new ApiConnectionManager(m_apiPackageStore, m_apiRuntime,
                                                    &m_httpClient);
        m_apiConnManager->SetDispatchCallback(
            [this](const ApiConnectionManager::Dispatch& d) {
                // Bridge: fresh session per dispatch (scheduler fire-callback
                // pattern — no context accumulation across fires).
                const std::string connector =
                    "api-trigger:" + d.packageName + ":" + std::to_string(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());
                SessionKey key{connector, ""};
                auto session = m_sessionManager->GetOrCreate(key);
                if (!session) return;
                if (session->AgentId().empty() && m_agentStore) {
                    auto agents = m_agentStore->List();
                    if (!agents.empty()) session->SetAgentId(agents.front().id);
                }

                std::string providerId = session->ProviderId();
                if (providerId.empty() && m_agentStore) {
                    auto agent = m_agentStore->GetById(session->AgentId());
                    if (agent && !agent->default_provider.empty())
                        providerId = agent->default_provider;
                }
                if (providerId.empty() && m_adminServer)
                    providerId = m_adminServer->GetDefaultProviderId();
                std::string registryKey = providerId;
                if (!providerId.empty() && m_adminServer) {
                    auto ps = m_adminServer->GetProvider(providerId);
                    if (ps) registryKey = ps->providerType.empty() ? ps->providerId
                                                                   : ps->providerType;
                }
                std::string model;
                if (m_agentStore) {
                    auto agent = m_agentStore->GetById(session->AgentId());
                    if (agent && !agent->default_model.empty()) model = agent->default_model;
                }
                const std::size_t contextWindow =
                    ResolveContextWindow(session->AgentId(), providerId, model);

                const std::string prompt =
                    "Package '" + d.packageName + "' fired a trigger (connection '" +
                    d.connectionName + "', reason: " + d.reason + ").\n\n" +
                    "Dispatch payload:\n" + d.payloadJson + "\n\n" +
                    "Prompt from the trigger:\n" + d.prompt + "\n\n" +
                    "Use the `api` tool to inspect current state (e.g. `api " +
                    d.packageName + "` for available commands) and act on the trigger.";

                m_jobs.EnqueueInLane(
                    ::animus::jobs::JobLane::Cognition,
                    [this, session, prompt, providerId, registryKey, model, contextWindow]() {
                        auto sessionAccess = SessionAccess(session, SessionAccessMode::ReadWrite);
                        auto result = m_chainRunner->ExecuteOnSession(
                            sessionAccess, prompt, m_config.agent.identity, registryKey,
                            providerId, model, contextWindow);
                        if (!result.success && !result.error.empty()) {
                            ALOG_WARNING("api-conn", "[" << session->Key().conversation_id
                                        << "] chain failed: " << result.error);
                        }
                        if (m_sessionManager) {
                            m_sessionManager->FlushSession(session->Id());
                            if (result.triggered_compaction && m_compactionService) {
                                m_compactionService->CompactIfNeeded(
                                    session->Id(), m_config.agent.identity, registryKey,
                                    model, contextWindow);
                            }
                        }
                    });
            });
        m_apiConnManager->Start();
    }
    m_tools.Register(std::make_unique<WebFetchTool>(m_httpClient));

    // Stored links tool — pre-configured HTTP endpoints (ticket 127)
    // Always registered for discoverability; returns helpful error if no links configured.
    {
        std::vector<StoredLinksTool::StoredLink> links;
        for (const auto& sl : m_config.stored_links) {
            links.push_back({sl.id, sl.label, sl.url});
        }
        m_tools.Register(std::make_unique<StoredLinksTool>(m_httpClient, std::move(links)));
    }

    // RSS tool — pre-configured feeds (ticket 127)
    // Always registered for discoverability; returns helpful error if no feeds configured.
    {
        std::vector<RssTool::RssFeed> feeds;
        for (const auto& rf : m_config.rss_feeds) {
            feeds.push_back({rf.id, rf.label, rf.url});
        }
        m_tools.Register(std::make_unique<RssTool>(m_httpClient, std::move(feeds)));
    }

    m_tools.Register(std::make_unique<EmailTool>(m_httpClient, m_configStore));

    // Web Search tool (ticket 069) — Brave provider
    // Always register for discoverability; returns "not configured" if no API key.
    if (!m_config.search.api_key.empty()) {
        BraveSearchProvider::Config braveCfg;
        braveCfg.api_key = m_config.search.api_key;
        braveCfg.rate_limit_per_minute = m_config.search.rate_limit_per_minute;
        if (braveCfg.rate_limit_per_minute <= 0) braveCfg.rate_limit_per_minute = 10;

        auto provider = std::make_unique<BraveSearchProvider>(m_httpClient, std::move(braveCfg));
        m_tools.Register(std::make_unique<WebSearchTool>(
            std::move(provider),
            m_config.search.default_count > 0 ? m_config.search.default_count : 5,
            20));
        ALOG_DEBUG("kernel", "Web search tool registered (provider: brave)");
    } else {
        m_tools.Register(std::make_unique<WebSearchTool>(
            nullptr,
            m_config.search.default_count > 0 ? m_config.search.default_count : 5,
            20));
        ALOG_WARNING("kernel", "Web search tool registered (no provider — API key not configured)");
    }

    // Utility tools (tickets 085, 097, 099)
    m_tools.Register(std::make_unique<DiceTool>());
    m_tools.Register(std::make_unique<CalculatorTool>());
    m_tools.Register(std::make_unique<ToolsTool>(m_tools, m_agentStore));

    // Image tool (ticket 068) — analyze + generate
    {
        ImageTool::Config imgCfg;
        // Generation provider configs — populated from provider config lookup
        imgCfg.gen_ollama.base_url = "https://ollama.com/v1";
        imgCfg.gen_ollama.default_model = "x/z-image-turbo";
        imgCfg.gen_zai.base_url = "https://api.z.ai/api";
        imgCfg.gen_zai.default_model = "glm-image";
        imgCfg.gen_zai.default_size = "1280x1280";
        imgCfg.gen_openai.default_model = "gpt-image-2";
        imgCfg.gen_openai.default_size = "1024x1024";

        // Try to get API keys from configured providers
        if (m_chainRunner) {
            auto& lookup = m_chainRunner->GetConfigLookup();
            if (auto cfg = lookup("ollama")) {
                if (!cfg->api_key.empty()) imgCfg.gen_ollama.api_key = cfg->api_key;
                if (!cfg->base_url.empty()) imgCfg.gen_ollama.base_url = cfg->base_url;
            }
            if (auto cfg = lookup("zai")) {
                imgCfg.gen_zai.api_key = cfg->api_key;
                if (!cfg->base_url.empty()) imgCfg.gen_zai.base_url = cfg->base_url;
            }
            if (auto cfg = lookup("openai")) {
                imgCfg.gen_openai.api_key = cfg->api_key;
                if (!cfg->base_url.empty()) imgCfg.gen_openai.base_url = cfg->base_url;
            }
        }

        m_tools.Register(std::make_unique<ImageTool>(
           m_httpClient, std::move(imgCfg), m_chainRunner, m_agentStore));
   }

    // Diffusion tool (ticket 122)
    if (m_diffusionStore) {
        DiffusionTool::Config diffCfg;
        diffCfg.output_dir = "media/generated";
        m_tools.Register(std::make_unique<DiffusionTool>(
            m_httpClient, m_diffusionStore.get(), std::move(diffCfg)));
        ALOG_DEBUG("diffusion", "Tool registered");
    }

    // Attachment tool (Ticket 123)
    if (m_attachmentStore) {
        m_tools.Register(std::make_unique<AttachmentTool>(
            m_attachmentStore.get(), m_sessionManager));
        ALOG_DEBUG("attachment", "Tool registered");
    }

    // SOP tool (Ticket 125)
    if (m_sopStore && m_sopStore->HasSops()) {
        m_tools.Register(std::make_unique<SopTool>(
            m_sopStore.get(), m_memoryFileStore));
        ALOG_DEBUG("sop", "Tool registered");
    }

    ALOG_DEBUG("kernel", "Registered " << m_tools.Size() << " built-in tools");
}

void AgentKernel::Stop() {
    if (!m_running) {
        return;
    }

    if (m_adminServer) {
        m_adminServer->Stop();
    }

    if (m_channelManager) {
        m_channelManager->Shutdown();
    }

    if (m_peerSyncService) {
        m_peerSyncService->Stop();
    }

    if (m_scheduler) {
        m_scheduler->Stop();
    }

    if (m_consolidation) {
        m_consolidation->Stop();
    }

    if (m_moduleManager) {
        m_moduleManager->UnloadAll();
    }

    m_jobs.Stop(::animus::jobs::JobSystemStopMode::Drain);
    m_running = false;
}

bool AgentKernel::IsRunning() const {
    return m_running;
}

animus::jobs::JobSystem& AgentKernel::Jobs() {
    return m_jobs;
}

SessionManager& AgentKernel::Sessions() {
    return *m_sessionManager;
}

animus::kernel::llm::LLMProviderRegistry& AgentKernel::ProviderRegistry() {
    return *m_providerRegistry;
}

ChainRunner& AgentKernel::Chains() {
    return *m_chainRunner;
}

ToolRegistry& AgentKernel::Tools() {
    return m_tools;
}

HttpClient& AgentKernel::WebClient() {
    return m_httpClient;
}

bool AgentKernel::IsAdminServerRunning() const {
    if (!m_adminServer) {
        return false;
    }
    return m_adminServer->IsRunning();
}

AgentStore& AgentKernel::Agents() {
    return *m_agentStore;
}

// ============================================================================
// Lua Script Loading (Two-Phase Startup — Phase 1)
// ============================================================================

void AgentKernel::EnsureLuaState(const std::string& agentId) {
    if (m_luaStates.find(agentId) != m_luaStates.end()) return;

    LuaState::Config config;
    config.agentId = agentId;
    config.configStore = m_configStore;
    auto state = std::make_unique<LuaState>(config, m_tools, m_httpClient);
    m_luaStates.emplace(agentId, std::move(state));
}

void AgentKernel::LoadLuaScripts() {
    // Gather known agent IDs
    std::vector<std::string> agentIds;
    if (m_agentStore) {
        for (const auto& agent : m_agentStore->List()) {
            agentIds.push_back(agent.id);
        }
    }

    // Track loaded script names per agent to handle overrides
    // agentId -> set of script names already loaded
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
        loadedSources;  // agentId -> (name -> "db"|"fs")

    // Phase 1: Load DB scripts (if enabled)
    if (m_config.lua_load_from_db && m_scriptStore) {
        for (const auto& agentId : agentIds) {
            auto scripts = m_scriptStore->List(agentId);
            for (const auto& script : scripts) {
                if (!script.enabled) continue;

                EnsureLuaState(agentId);
                auto& state = *m_luaStates[agentId];

                try {
                    state.Eval(script.source, script.agent_id.empty() ? agentId : script.agent_id);
                    loadedSources[agentId][script.name] = "db";
                    ALOG_DEBUG("lua:startup", "Loaded DB script '" << script.name
                              << "' for agent='" << agentId << "'");
                } catch (const LuaException& e) {
                    ALOG_ERROR("lua:startup", "ERROR loading DB script '" << script.name
                              << "' for agent='" << agentId
                              << "': " << e.what());
                }
            }
        }
    }

    // Phase 2: Load filesystem scripts
    if (!m_config.lua_script_dir.empty()) {
        // #143: volume-heal-on-boot — reconcile image-baked _shared defaults
        // into the persisted volume BEFORE any discovery/loading happens.
        // Empty staging dir (dev/bare boot) disables the heal entirely.
        if (!m_config.lua_staging_dir.empty()) {
            ReconcileSharedScripts(m_config.lua_staging_dir,
                                   m_config.lua_script_dir);
        }
        if (!m_fsScriptLoader) {
            m_fsScriptLoader = std::make_unique<FilesystemScriptLoader>(m_config.lua_script_dir);
        }

        // Track which agents have shared scripts loaded
        std::unordered_set<std::string> sharedLoadedFor;

        for (const auto& agentId : agentIds) {
            auto fsScripts = m_fsScriptLoader->DiscoverForAgent(agentId);

            for (auto& script : fsScripts) {
                if (!script.enabled) continue;

                // Check for override conflict
                bool isShared = script.agent_id.empty();
                std::string effectiveAgentId = isShared ? agentId : script.agent_id;

                if (m_config.lua_filesystem_overrides_db) {
                    auto it = loadedSources.find(effectiveAgentId);
                    if (it != loadedSources.end()) {
                        auto nameIt = it->second.find(script.name);
                        if (nameIt != it->second.end() && nameIt->second == "db") {
                            // Filesystem overrides DB — log and skip DB version
                            // But we need to actually reload the Lua state fresh
                            // since the DB script already ran. For safety, we
                            // just eval the filesystem script on top.
                            ALOG_INFO("lua:startup", "Filesystem script '" << script.name
                                      << "' overrides DB script for agent='" << effectiveAgentId << "'");
                        }
                    }
                }

                EnsureLuaState(effectiveAgentId);
                auto& state = *m_luaStates[effectiveAgentId];

                try {
                    state.Eval(script.source, effectiveAgentId);
                    loadedSources[effectiveAgentId][script.name] = "fs";
                    ALOG_DEBUG("lua:startup", "Loaded FS script '" << script.name
                              << "' for agent='" << effectiveAgentId << "'");
                } catch (const LuaException& e) {
                    ALOG_ERROR("lua:startup", "ERROR loading FS script '" << script.name
                              << "' for agent='" << effectiveAgentId
                              << "': " << e.what());
                }
            }
        }
    }
}

// ============================================================================
// ComputePendingEmbeddings — chunk files and compute missing embeddings
// ============================================================================

void AgentKernel::ComputePendingEmbeddings() {
    if (!m_embeddingService || !m_embeddingService->IsAvailable()) return;
    if (!m_memoryFileStore) return;

    // #232: sweep extracted to memory/EmbeddingSweep (testable, injectable
    // embed callback). The old per-agent loop skipped global (agent_id=0)
    // files entirely - globals were never chunked or embedded while the
    // keyword fallback still surfaced them (#225's mode-dependent silent
    // failure, one layer earlier). The sweep includes globals now, and
    // no longer requires any agent to be registered.
    memory::SweepPendingEmbeddings(m_memoryFileStore,
        [this](const std::string& text) {
            return m_embeddingService->Embed(text);
        });
}

// ============================================================================
// ComputePendingReportEmbeddings — backfill missing session report embeddings
// ============================================================================

void AgentKernel::ComputePendingReportEmbeddings() {
    if (!m_embeddingService || !m_embeddingService->IsAvailable()) return;
    if (!m_sessionReportStore || !m_agentStore) return;

    auto agents = m_agentStore->List();
    int totalEmbedded = 0;

    for (const auto& agent : agents) {
        auto reports = m_sessionReportStore->ListWithoutEmbeddings(agent.id, 200);
        for (const auto& report : reports) {
            // Build text from all four fields for a rich embedding
            std::string embedText = report.summary + " " +
                                    report.past_events + " " +
                                    report.current_activity + " " +
                                    report.forward_look;
            // Truncate to embedding model's sweet spot
            if (embedText.size() > 700) {
                embedText = embedText.substr(0, 700);
            }
            if (embedText.empty()) continue;

            auto emb = m_embeddingService->Embed(embedText);
            if (emb.has_value()) {
                m_sessionReportStore->StoreEmbedding(report.id, *emb);
                totalEmbedded++;
            }
        }
    }

    if (totalEmbedded > 0) {
        ALOG_INFO("embedding", "Computed " << totalEmbedded
                  << " session report embeddings");
    }
}

} // namespace animus::kernel

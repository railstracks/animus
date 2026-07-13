#include "animus_kernel/AgentKernel.h"

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
#include "animus_kernel/tools/NodeTool.h"
#include "animus_kernel/tools/ShellExecTool.h"
#include "animus_kernel/tools/WebFetchTool.h"
#include "animus_kernel/tools/WebSearchTool.h"
#include "animus_kernel/tools/DiaryTool.h"
#include "animus_kernel/tools/ConsolidationTool.h"
#include "animus_kernel/tools/ScheduleTool.h"
#include "animus_kernel/tools/SessionsTool.h"
#include "animus_kernel/tools/ChannelsTool.h"
#include "animus_kernel/tools/EmailTool.h"
#include "animus_kernel/scheduler/Scheduler.h"
#include "animus_kernel/admin/DiaryManager.h"
#include "animus_kernel/SessionNotesStore.h"
#include "animus_kernel/SessionReportStore.h"
#include "animus_kernel/SessionTagsStore.h"
#include "animus_kernel/ContextProviderRegistry.h"
#include "animus_kernel/context/IdentityProvider.h"
#include "animus_kernel/context/SessionNotesProvider.h"
#include "animus_kernel/context/SessionReportProvider.h"
#include "animus_kernel/context/ActiveMemoryProvider.h"
#include "animus_kernel/context/ChannelContextProvider.h"

#include "animus_kernel/tools/DiceTool.h"
#include "animus_kernel/tools/CalculatorTool.h"
#include "animus_kernel/tools/ToolsTool.h"
#include "animus_kernel/tools/ImageTool.h"

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
    delete m_dataStore; m_dataStore = nullptr;
    delete m_providerThrottle; m_providerThrottle = nullptr;
    delete m_scheduler; m_scheduler = nullptr;
    delete m_sessionNotesStore; m_sessionNotesStore = nullptr;
    delete m_sessionReportStore; m_sessionReportStore = nullptr;
    delete m_contextRegistry; m_contextRegistry = nullptr;
    delete m_sessionTagsStore; m_sessionTagsStore = nullptr;

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
        std::cerr << "[module:" << lvl << "] "
                  << std::string(msg.data ? msg.data : "", msg.size)
                  << "\n";
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
                std::cerr << "[kernel] Embedding service in degraded mode — keyword fallback active\n";
            }
        }

        // --- Node manager (external node connections) ---
        m_nodeManager = new NodeManager(m_dataStore);
        m_chainRunner->SetNodeManager(m_nodeManager);

        // --- Agent store (SQLite-backed, after MemoryStore so observations table exists) ---
        m_agentStore = new AgentStore(m_dataStore);
        m_chainRunner->SetAgentStore(m_agentStore);
        m_adminServer->SetMemoryStore(m_memoryStore);
        m_adminServer->SetMemoryFileStore(m_memoryFileStore);
        m_adminServer->SetOntologyStore(m_ontologyStore);
        m_adminServer->SetAgentStore(m_agentStore);
        m_nodeManager->SetAgentStore(m_agentStore);
        m_adminServer->SetToolRegistry(&m_tools);
        m_adminServer->SetDataDir(m_config.dataDir);
        m_adminServer->SetNodeManager(m_nodeManager);
        m_adminServer->SetSearchConfig(&m_config.search);
        m_adminServer->SetHttpClient(&m_httpClient);

        // --- Diary store (SQLite-backed, per-agent private diary) ---
        m_diaryStore = new DiaryStore(m_dataStore);
        m_adminServer->SetDiaryStore(m_diaryStore);
        m_tools.Register(std::make_unique<DiaryTool>(&m_adminServer->GetDiaryManager()));

        // --- Consolidation Tool (session-gated: only available during consolidation sessions) ---
        m_tools.Register(std::make_unique<ConsolidationTool>(m_memoryStore, m_ontologyStore, m_sessionManager, m_memoryFileStore, m_agentStore, m_sessionReportStore, m_embeddingService));

        // --- Session Notes Store (SQLite-backed, per-session persistent bullet points) ---
        m_sessionNotesStore = new SessionNotesStore(m_dataStore);
        m_adminServer->SetSessionNotesStore(m_sessionNotesStore);

        // --- Session Report Store (temporal summaries for active memory) ---
        m_sessionReportStore = new SessionReportStore(m_dataStore);
        m_adminServer->SetSessionReportStore(m_sessionReportStore);

        // --- Session Tags Store (keywords for ontology retrieval) ---
        m_sessionTagsStore = new SessionTagsStore(m_dataStore);
        m_adminServer->SetSessionTagsStore(m_sessionTagsStore);

        // --- Context Provider Registry ---
        m_contextRegistry = new ContextProviderRegistry();
        m_contextRegistry->Register(std::make_unique<IdentityProvider>());
        m_contextRegistry->Register(
            std::make_unique<SessionNotesProvider>(m_sessionNotesStore));
        m_contextRegistry->Register(
            std::make_unique<SessionReportProvider>(
                m_sessionReportStore, m_embeddingService));
        m_contextRegistry->Register(
            std::make_unique<ActiveMemoryProvider>(
                m_memoryStore, m_diaryStore, m_ontologyStore, m_memoryFileStore,
                m_sessionTagsStore, m_sessionManager, m_scheduler, m_embeddingService));
        m_contextRegistry->Register(
            std::make_unique<ChannelContextProvider>());
        m_chainRunner->SetContextRegistry(m_contextRegistry);
        m_adminServer->SetContextRegistry(m_contextRegistry);

        // --- Sessions Tool (agent-facing session layer access) ---
        m_tools.Register(std::make_unique<SessionsTool>(
            m_sessionManager, m_sessionNotesStore, m_sessionTagsStore, m_compactionService));
        m_memorySearch = new memory::MemorySearch(
            m_memoryStore, m_ontologyStore, m_memoryFileStore, m_diaryStore);
        m_memorySearch->SetSessionManager(m_sessionManager);
        m_memorySearch->SetAgentStore(m_agentStore);

        // Verify FTS sync at startup — auto-rebuild if out of sync.
        {
            auto diaryStats = m_memorySearch->VerifyFtsSync("diary");
            if (diaryStats.source_count != diaryStats.fts_count) {
                std::cerr << "[kernel] Diary FTS out of sync ("
                          << diaryStats.fts_count << "/" << diaryStats.source_count
                          << "), rebuilding...\n";
                m_memorySearch->RebuildDiaryIndex();
            }
            auto obsStats = m_memorySearch->VerifyFtsSync("observations");
            if (obsStats.source_count != obsStats.fts_count) {
                std::cerr << "[kernel] Observations FTS out of sync ("
                          << obsStats.fts_count << "/" << obsStats.source_count
                          << "), running backfill...\n";
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
        m_adminServer->SetLuaScriptDir(m_config.lua_script_dir);
        m_configStore = new AgentConfigStore(m_dataStore);

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

        // ChannelsTool composite — must exist before Lua scripts load
        // so adapters can register into it during Phase 2
        m_channelsTool = new ChannelsTool();
        m_channelsTool->SetConfigStore(m_configStore);
        // Note: m_channelManager is null here — wired in Initialize()
        m_tools.Register(std::unique_ptr<IToolHandler>(m_channelsTool));
        m_adminServer->SetChannelsTool(m_channelsTool);
        m_adminServer->SetLuaStates(&m_luaStates);
        m_adminServer->SetConfigStore(m_configStore);

        m_tools.Register(std::make_unique<LuaTool>(m_luaStates, m_tools, m_httpClient, *m_scriptStore, m_configStore));

    // --- Register built-in tools ---
    RegisterBuiltinTools(m_config);

        // --- Scheduler (cron-like, fires IncomingEvent → session pipeline) ---
        m_scheduler = new Scheduler(m_dataStore);
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
                            // Fallback: try all layers (for 'system' or 'default' schedule agentId)
                            for (const auto& l : m_memoryStore->ListLayers()) {
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
                                return;
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
                        return;
                    }
                    sessionSubtype = "intake";
                    std::string layerHint;
                    std::optional<int64_t> targetLayerId;
                    if (message.find("intake:") == 0) {
                        const std::string layerName = message.substr(7);
                        layerHint = " for layer \"" + layerName + "\"";
                        // Resolve layer name to ID for automated intake
                        if (m_memoryStore) {
                            for (const auto& l : m_memoryStore->ListLayers()) {
                                if (l.name == layerName) {
                                    targetLayerId = l.id;
                                    break;
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

                        "7. For each session that had pending turns, update its session report by calling action \"sessions:report\" with params: {session_id, summary, past_events, current_activity, forward_look}. Use these guidelines:\n"
                        "   - summary: A stable one-sentence description of what this session is about (update only if it has changed).\n"
                        "   - past_events: Key developments that have transpired since the last report (compress if the session has a long history).\n"
                        "   - current_activity: What is happening right now in this session.\n"
                        "   - forward_look: What you expect to happen next in this session.\n"
                        "   Each field should be ~200 chars max. The report is upserted — it replaces any previous report for that session.\n"
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
                } else {
                    // Unknown consolidation message — skip
                    return;
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
                        SessionKey key{
                            "consolidation:" + sessionSubtype + ":" + curAgent,
                            ""
                        };

                        // Delete previous consolidation session for this key
                        // so each run starts fresh (not accumulating turns forever)
                        {
                            auto existing = m_sessionManager->GetOrCreate(key);
                            if (existing && !existing->Turns().empty()) {
                                m_sessionManager->DeleteById(existing->Id());
                            }
                        }

                        auto session = m_sessionManager->GetOrCreate(key);
                        if (!session) continue;

                        session->SetAgentId(curAgent);
                        session->SetSessionType("consolidation");

                        std::string providerId;
                        std::string model;
                        auto agent = m_agentStore->GetById(curAgent);
                        if (agent) {
                            if (!agent->default_provider.empty()) providerId = agent->default_provider;
                            if (!agent->default_model.empty()) model = agent->default_model;
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
                                if (result.success && m_sessionManager) {
                                    m_sessionManager->FlushSession(session->Id());
                                    if (result.triggered_compaction && m_compactionService) {
                                        m_compactionService->CompactIfNeeded(
                                            session->Id(), identity, registryKey, model, contextWindow);
                                    }
                                }
                            });
                    }
                }
                return;
            }

            SessionKey key{
                "scheduled:" + tag + ":" + agentId,
                ""  // no thread
            };
            auto session = m_sessionManager->GetOrCreate(key);
            if (!session) return;

            if (session->AgentId().empty()) {
                session->SetAgentId(agentId);
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
                [this, session, message, providerId, registryKey, model, contextWindow]() {
                    auto sessionAccess = SessionAccess(session, SessionAccessMode::ReadWrite);
                    auto result = m_chainRunner->ExecuteOnSession(
                        sessionAccess,
                        message,
                        m_config.agent.identity,
                        registryKey,
                        providerId,
                        model,
                        contextWindow);
                    if (result.success && m_sessionManager) {
                        m_sessionManager->FlushSession(session->Id());
                        if (result.triggered_compaction && m_compactionService) {
                            m_compactionService->CompactIfNeeded(
                                session->Id(), m_config.agent.identity, registryKey, model, contextWindow);
                        }
                    }
                });
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
                SessionKey key{"consolidation:intake:" + agentId, ""};

                // Delete previous session to avoid accumulating turns across runs
                {
                    auto existing = m_sessionManager->GetOrCreate(key);
                    if (existing && !existing->Turns().empty()) {
                        m_sessionManager->DeleteById(existing->Id());
                    }
                }

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

                if (result.success && m_sessionManager) {
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
        }
        {
            std::string consErr;
            if (!m_consolidation->Start(m_scheduler, &consErr)) {
                std::cerr << "[kernel] consolidation pipeline start failed: "
                          << consErr << std::endl;
            }
        }

        // Start scheduler AFTER consolidation has registered its cron schedules.
        // Old schedules are cleaned up during registration; starting the poll loop
        // before cleanup allows stale schedules to fire immediately on boot.
        {
            std::string schedErr;
            if (!m_scheduler->Start(&schedErr)) {
                std::cerr << "[kernel] scheduler start failed: " << schedErr << std::endl;
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
        auto dispatch = [this](const std::string& agentId,
                               const std::string& sessionKey,
                               const std::string& message,
                               const std::string& sessionType,
                               const ChannelManager::ReplyTarget& replyTarget) {
            std::cerr << "[channels:dispatch] agentId=" << agentId
                      << " sessionKey=" << sessionKey
                      << " type=" << sessionType << std::endl;
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
                std::cerr << "[kernel] session references unknown agent: "
                          << session->AgentId() << ", skipping dispatch" << std::endl;
            }

            // Resolve provider — same logic as IRC dispatch in AdminServer
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
                std::cerr << "[channels:dispatch] No provider resolved for agentId="
                          << agentId << " — dropping message" << std::endl;
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

            // Throttle
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
            m_jobs.EnqueueInLane(
                ::animus::jobs::JobLane::Cognition,
                [this, session, message, identity, registryKey, providerId, model, contextWindow, replyTarget]() {
                    auto sessionAccess = SessionAccess(session, SessionAccessMode::ReadWrite);
                    auto result = m_chainRunner->ExecuteOnSession(
                        sessionAccess,
                        message,
                        identity,
                        registryKey,
                        providerId,
                        model,
                        contextWindow);
                    if (result.success && m_sessionManager) {
                        m_sessionManager->FlushSession(session->Id());

                        // Trigger compaction if needed
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
                        std::cerr << "[channels:dispatch] LLM execution failed: "
                                  << result.error << std::endl;
                    }
                    if (result.success && !result.response.empty()) {
                        SendAutoReply(replyTarget, result.response);
                    }
                });
        };

        m_channelManager = new ChannelManager(
            m_httpClient, m_configStore, dispatch);

        // Wire ChannelManager into AdminServer for route handlers
        if (m_adminServer) {
            m_adminServer->SetChannelManager(m_channelManager);
        }

        // Wire ChannelManager into ChannelsTool for list action
        if (m_channelsTool) {
            m_channelsTool->SetChannelManager(m_channelManager);
        }

        m_channelManager->Initialize();
    }

    m_running = true;

    // Compute pending embeddings after all stores are initialized
    if (m_embeddingService && m_embeddingService->IsAvailable()) {
        m_jobs.Enqueue([this]() {
            ComputePendingEmbeddings();
        });
    }

    return true;
}

void AgentKernel::SendAutoReply(const ChannelManager::ReplyTarget& target,
                                   const std::string& text) {
    if (!m_channelManager) return;
    m_channelManager->SendReply(target, text);
}

std::size_t AgentKernel::ResolveContextWindow(const std::string& agentId,
                                             const std::string& providerId,
                                             const std::string& model) const {
    std::size_t contextWindow = 128000;

    // Start with provider's context window
    if (m_adminServer) {
        auto ps = m_adminServer->GetProvider(providerId);
        if (ps) {
            contextWindow = AdminServer::ResolveProviderContextWindow(
                *ps, model, static_cast<std::uint32_t>(contextWindow));
        }
    }

    // Clamp to agent's context_window if set (acts as upper bound)
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
    m_tools.Register(std::make_unique<WebFetchTool>(m_httpClient));
    m_tools.Register(std::make_unique<EmailTool>(m_httpClient, m_configStore));

    // Web Search tool (ticket 069) — Brave provider
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
        std::cerr << "[kernel] Web search tool registered (provider: brave)" << std::endl;
    } else {
        std::cerr << "[kernel] Web search tool NOT registered (no API key configured)" << std::endl;
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

    std::cerr << "[kernel] Registered " << m_tools.Size() << " built-in tools" << std::endl;
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
                    std::cerr << "[lua:startup] Loaded DB script '" << script.name
                              << "' for agent='" << agentId << "'\n";
                } catch (const LuaException& e) {
                    std::cerr << "[lua:startup] ERROR loading DB script '" << script.name
                              << "' for agent='" << agentId
                              << "': " << e.what() << "\n";
                }
            }
        }
    }

    // Phase 2: Load filesystem scripts
    if (!m_config.lua_script_dir.empty()) {
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
                            std::cerr << "[lua:startup] Filesystem script '" << script.name
                                      << "' overrides DB script for agent='" << effectiveAgentId << "'\n";
                        }
                    }
                }

                EnsureLuaState(effectiveAgentId);
                auto& state = *m_luaStates[effectiveAgentId];

                try {
                    state.Eval(script.source, effectiveAgentId);
                    loadedSources[effectiveAgentId][script.name] = "fs";
                    std::cerr << "[lua:startup] Loaded FS script '" << script.name
                              << "' for agent='" << effectiveAgentId << "'\n";
                } catch (const LuaException& e) {
                    std::cerr << "[lua:startup] ERROR loading FS script '" << script.name
                              << "' for agent='" << effectiveAgentId
                              << "': " << e.what() << "\n";
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
    if (!m_memoryFileStore || !m_agentStore) return;

    auto agents = m_agentStore->List();
    int totalChunked = 0, totalEmbedded = 0;

    for (const auto& agent : agents) {
        auto files = m_memoryFileStore->ListFiles(std::nullopt, 1000, 0);
        std::cerr << "[embedding] Agent " << agent.id << " (numeric=" << agent.numeric_id
                  << "): " << files.size() << " files total\n";

        for (const auto& file : files) {
            if (file.agent_id != agent.numeric_id) continue;
            if (file.superseded) continue;

            auto existingChunks = m_memoryFileStore->GetChunks(file.id);
            bool needsRechunk = existingChunks.empty();

            if (needsRechunk && !file.content.empty()) {
                std::vector<memory::MemoryFileChunk> newChunks;
                const std::string& content = file.content;
                std::size_t pos = 0;
                std::size_t lastHeader = 0;
                bool foundAnyHeader = false;
                int lineNum = 0;
                int headerLine = 0;

                while (pos < content.size()) {
                    std::size_t nl = content.find('\n', pos);
                    lineNum++;
                    std::size_t lineEnd = (nl == std::string::npos) ? content.size() : nl;
                    if (lineEnd > pos && content[pos] == '#') {
                        if (foundAnyHeader) {
                            memory::MemoryFileChunk chunk;
                            chunk.source_path = file.source_path;
                            chunk.content = content.substr(lastHeader, pos - lastHeader);
                            chunk.start_line = headerLine;
                            chunk.end_line = lineNum - 1;
                            newChunks.push_back(std::move(chunk));
                        }
                        lastHeader = pos;
                        headerLine = lineNum;
                        foundAnyHeader = true;
                    }
                    pos = (nl == std::string::npos) ? content.size() : nl + 1;
                }

                if (foundAnyHeader) {
                    memory::MemoryFileChunk chunk;
                    chunk.source_path = file.source_path;
                    chunk.content = content.substr(lastHeader);
                    chunk.start_line = headerLine;
                    chunk.end_line = lineNum;
                    newChunks.push_back(std::move(chunk));
                } else {
                    memory::MemoryFileChunk chunk;
                    chunk.source_path = file.source_path;
                    chunk.content = content;
                    chunk.start_line = 1;
                    chunk.end_line = lineNum;
                    newChunks.push_back(std::move(chunk));
                }

                m_memoryFileStore->ReplaceChunks(file.id, newChunks);
                existingChunks = m_memoryFileStore->GetChunks(file.id);
                totalChunked += static_cast<int>(newChunks.size());
            }

            for (const auto& chunk : existingChunks) {
                if (chunk.embedding_dim > 0 && !chunk.embedding.empty()) continue;
                if (chunk.content.empty()) continue;

                auto emb = m_embeddingService->Embed(chunk.content);
                if (emb.has_value()) {
                    m_memoryFileStore->UpdateChunkEmbedding(chunk.id, *emb);
                    totalEmbedded++;
                }
            }
        }
    }

    if (totalChunked > 0 || totalEmbedded > 0) {
        std::cerr << "[embedding] Computed " << totalChunked << " chunks, "
                  << totalEmbedded << " embeddings for " << agents.size() << " agents\n";
    }
}

} // namespace animus::kernel

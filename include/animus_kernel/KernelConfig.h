#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "animus/Jobs.h"

namespace animus::kernel {

// Forward declaration for log level
enum class PromptLogLevel;

// KernelConfig is the host-facing configuration surface for bootstrapping the kernel.
struct KernelConfig {
    struct AdminServerConfig {
        bool enabled{true};
        std::string host{"127.0.0.1"};
        std::uint16_t port{8080};
        bool allowUnauthenticatedLocalhost{true};
        std::string authToken{};
        std::uint32_t websocketMaxMessageBytes{65536};
    };

    struct AgentModelConfig {
        std::string provider{"openai"};
        std::string modelId{"gpt-4.1-mini"};
        std::uint32_t contextWindow{128000};
        std::string apiKey{};
    };

    struct AgentBudgetConfig {
        std::uint32_t maxChainSteps{200};
        std::uint32_t maxToolCallsPerChain{50};
        std::uint32_t consolidationToolBudget{100};  // Min tool calls for consolidation sessions
        std::uint32_t timeoutSeconds{1800};
        std::uint32_t tokenBudgetPerPrompt{200000};
        std::uint32_t episodicTokenBudget{10000};
        std::uint32_t semanticTokenBudget{10000};
        std::uint32_t perspectivesTokenBudget{3000};
        std::uint32_t memoryFileTokenBudget{2500};
        std::uint32_t ambientContextLimit{5000};
        std::uint32_t sessionReportTokenBudget{1500};
    };

    struct AgentRuntimeConfig {
        AgentModelConfig model{};
        double temperature{0.7};
        std::string identity{};
        AgentBudgetConfig budget{};

        // Thinking mode configuration (native provider thinking)
        // When reasoning_effort is non-empty, providers use native thinking.
        bool reasoningEnabled{false};
        std::string reasoning_effort{"high"}; // Effort level: "none"/"low"/"medium"/"high"/"xhigh"
        std::string reasoningInstruction; // Kept for API compat, no longer injected into prompts

    };

    struct AgentConfigStorage {
        bool persistToDisk{true};
        std::string filePath;  // resolved from dataDir
    };

    struct InterfaceConfigStorage {
        bool persistToDisk{true};
        std::string filePath;  // resolved from dataDir
    };

    struct InterfaceModuleInfo {
        std::string moduleId;
        std::string name;
        std::string type;
    };

    struct MemoryLayerConfig {
        std::string name;
        std::string description;
        std::string retentionPolicy;
        std::string horizon;
        std::string consolidationInterval;
        std::string perspectivePast;
        std::string perspectiveCurrent;
        std::string perspectiveFuture;
    };

    struct MemoryObservation {
        std::string id;
        std::string text;
        std::vector<std::string> tags;
        std::uint64_t timestampUnixMs{0};
        double weight{1.0};
        double decayRate{0.95};
        std::string layer;
        std::string createdInLayer;
        std::string demotionReason;
        std::uint64_t demotionTimestampUnixMs{0};
    };

    struct MemoryConfig {
        bool persistToDisk{true};
        std::string filePath;  // resolved from dataDir
        std::vector<MemoryLayerConfig> layers{{
            {"immediate", "Immediate observations from the current moment. Raw sensory intake.", "5m", "1h", "5m", "", "", ""},
            {"day", "Observations from the current day. Recent context and active priorities.", "48h", "1d", "1h", "Retrospective on the previous day.", "Current-day operational picture and active priorities.", "Expectations and risks for the next day."},
            {"week", "Observations relevant across a weekly timescale.", "14d", "1w", "6h", "Retrospective on the previous week and persistent themes.", "Current-week momentum, blockers, and execution posture.", "Expectations and strategy for the next week."},
            {"month", "Observations relevant across a monthly timescale.", "90d", "1mo", "1d", "Monthly review and trend identification.", "Current-month posture and trajectory assessment.", "Monthly outlook and strategic adjustments."},
            {"quarter", "Quarterly strategic observations.", "365d", "1q", "1w", "Quarterly retrospective and strategic alignment.", "Quarterly posture and initiative health.", "Quarterly outlook and course corrections."},
            {"semester", "Semi-annual observations.", "730d", "6mo", "2w", "Semi-annual strategic review.", "Semi-annual posture and identity alignment.", "Semi-annual strategic direction."},
            {"permanent", "Permanent observations. Core identity and constitutional memory.", "0", "0", "0", "", "", ""}
        }};
    };

    struct ConstitutionConfigStorage {
        bool persistToDisk{true};
        std::string filePath;  // resolved from dataDir
    };

    struct ProviderConfigStorage {
        bool persistToDisk{true};
        std::string providersFilePath;  // resolved from configDir
        std::string authFilePath;      // resolved from configDir
        std::string modelContextSizesPath;  // static fallback table (configDir/model_context_sizes.json)
    };

    struct WebConfig {
        // URL allowlist for HTTP tool (glob patterns).
        // If non-empty, only URLs matching these patterns are allowed.
        std::vector<std::string> urlAllowlist;

        // URL denylist for HTTP tool (glob patterns).
        // URLs matching these patterns are always blocked.
        std::vector<std::string> urlDenylist;

        // Maximum response body size in bytes (default 1MB).
        std::size_t maxResponseBytes{1024 * 1024};

        // Web fetch response cache TTL in seconds (default 300 = 5 min).
        int cacheTtlSeconds{300};

        // Whether to allow requests to private/internal addresses.
        // Default false — blocks SSRF attacks against internal services.
        bool allowPrivateAddresses{false};

        // Whether to verify TLS certificates.
        // Default true — secure by default. Set false only for development.
        bool tlsVerify{true};

        // Default timeout for web requests in seconds.
        int defaultTimeoutSeconds{30};
    };

    struct FileConfig {
        // File path allowlist (glob patterns). If non-empty, only matching paths are allowed.
        // Paths are resolved (weakly canonical) before matching.
        std::vector<std::string> pathAllowlist;

        // File path denylist (glob patterns). Matching paths are always blocked.
        // Evaluated after allowlist. Denylist wins over allowlist.
        std::vector<std::string> pathDenylist;

        // Whether to restrict file access to the workspace root.
        // Default true — agents can only access files under their workspace.
        // Set false to allow unrestricted file access (not recommended for production).
        bool restrictToWorkspace{true};
    };

    struct ShellConfig {
        // Whether shell execution is enabled at all.
        // Default true. Set false to completely disable command execution.
        bool enabled{true};

        // Command allowlist (glob patterns). If non-empty, only matching commands are allowed.
        // Patterns match against the full command string.
        std::vector<std::string> commandAllowlist;

        // Command denylist (glob patterns). Matching commands are always blocked.
        // Denylist wins over allowlist.
        std::vector<std::string> commandDenylist;

        // Maximum command timeout in seconds (default 120).
        // Agents cannot request a timeout longer than this.
        int maxTimeoutSeconds{120};

        // Whether to allow shell features (pipes, redirects, &&, ||, etc.).
        // Default true. Set false to restrict to single commands only.
        bool allowShellFeatures{true};
    };

    struct SearchConfig {
        std::string provider;  // "brave", "google", "searxng"
        std::string api_key;
        std::string endpoint;  // For self-hosted (SearXNG)
        int default_count{5};
        int rate_limit_per_minute{10};
    };

    // Job system
    jobs::JobSystemConfig jobSystemConfig{};

    // Module loading
    std::vector<std::string> modulePaths;
    std::vector<std::string> moduleDirs;

    // Allowlist policy
    std::vector<std::string> allowModuleIds;
    bool allowAllModulesByDefault{true};

    // Resolved paths (set by ResolvePaths in main.cpp, not by user directly)
    std::filesystem::path configDir;   // e.g. /home/user/.animus/config
    std::filesystem::path dataDir;     // e.g. /home/user/.animus/data
    bool legacyPaths{false};           // true when CWD-relative fallback is used

    // Embedded admin server
    AdminServerConfig admin{};
    AgentRuntimeConfig agent{};
    AgentConfigStorage agentStorage{};
    InterfaceConfigStorage interfaceStorage{};
    MemoryConfig memory{};
    ConstitutionConfigStorage constitutionStorage{};
    ProviderConfigStorage providerStorage{};
    WebConfig web{};
    FileConfig file{};
    ShellConfig shell{};
    SearchConfig search{};
    std::vector<InterfaceModuleInfo> interfaceModules{};

    // Storage backend configuration
    std::string storage_backend{"sqlite"};  // "sqlite" or "postgresql"
    std::string sqlite_path;  // resolved from dataDir / "memory.db"

    // PostgreSQL configuration (used when storage_backend == "postgresql")
    std::string pg_host{"localhost"};
    int pg_port{5432};
    std::string pg_database{"animus"};
    std::string pg_username{"animus"};
    std::string pg_password;
    int pg_pool_size{10};

    // Embedding configuration
    std::string embedding_model_path;  // resolved from dataDir / "models" / ...
    bool embedding_enabled{true};

    // Lua filesystem loading
    std::string lua_script_dir;  // resolved from dataDir / "lua"
    bool lua_load_from_db{true};               // also load from DB (backward compat)
    bool lua_filesystem_overrides_db{true};    // filesystem scripts override DB on name conflict

    // Prompt logging level: "none", "default", "full"
    // Default: metadata only (tokens, model, provider, latency)
    // Full: also stores prompt/response content and tool calls (EU AI Act compliance)
    std::string promptLogLevel{"default"};
};

} // namespace animus::kernel
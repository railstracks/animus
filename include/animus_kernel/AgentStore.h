#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace animus::kernel {

class IDataStore;

// ============================================================================
// Agent data model
// ============================================================================

struct AgentBudgetConfig {
    std::uint32_t maxChainSteps{200};
    std::uint32_t maxToolCallsPerChain{50};
    std::uint32_t consolidationToolBudget{100};  // Min tool calls for consolidation sessions
    std::uint32_t timeoutSeconds{1800};
    std::uint32_t tokenBudgetPerPrompt{200000};
    std::uint32_t episodicTokenBudget{10000}; // Max tokens for episodic observations in active memory
    std::uint32_t semanticTokenBudget{10000};   // Max tokens for semantic/ontology entities in active memory
    std::uint32_t perspectivesTokenBudget{3000}; // Max tokens for temporal perspectives in active memory
    std::uint32_t memoryFileTokenBudget{2500};  // Max tokens for MemoryFile context in active memory
    std::uint32_t ambientContextLimit{5000};     // Max tokens for ambient (Tier 2) MemoryFile context
    std::uint32_t sessionReportTokenBudget{1500}; // Max tokens for session reports in active memory
};

struct Agent {
    std::string id;                    // Server-generated agent fingerprint (MD5 hex)
    int64_t numeric_id{0};            // Auto-increment DB id, used for foreign-key joins with MemoryFiles etc.
    std::string name;                  // Display name
    std::string description;           // What this agent does
    std::string identity;               // Agent's identity document (core self-description)
    std::string avatar;               // URL or identifier for avatar

    // Model defaults
    std::string default_provider;      // e.g. "openai", "ollama", "zai"
    std::string default_model;         // e.g. "gpt-4.1-mini", "glm-5.1"
    std::string default_vision_model;  // e.g. "ollama/llava" — empty = no vision

    // Intake (agent-level, not per-layer)
    std::string intake_interval;       // cron expression, empty = no scheduled intake
    std::string intake_prompt;         // override for intake LLM prompt, empty = use default
    std::uint32_t context_window{128000}; // Legacy field; context is now resolved at provider/model level.
    double temperature{0.7};

    // Reasoning
    bool reasoning_enabled{false};
    std::string reasoning_effort{"high"};

    // Context padding — when true, fill unused context budget with next-best
    // (non-relevance-matched) items. When false, only relevance-matched items
    // surface. Useful for ambient awareness (on) vs laser-focused utility (off).
    bool pad_context{true};

    // Budget
    AgentBudgetConfig budget{};

    // Tool access (empty = all registered tools)
    std::vector<std::string> enabled_tools;
    std::string tool_configs_json{"{}"}; // Per-tool config object, keyed by tool name.

    // Node access (empty = all connected nodes allowed)
    std::vector<std::string> allowed_nodes;

    // Diary encryption key — generated at agent creation, never shown to admin
    std::string diary_secret;

    // Timestamps
    std::int64_t created_at_unix_ms{0};
    std::int64_t updated_at_unix_ms{0};
};

// ============================================================================
// AgentStore — SQLite-backed agent CRUD
// ============================================================================

class AgentStore {
public:
    // Takes a shared IDataStore (does not assume ownership).
    // Creates the agents table and migrates if needed.
    explicit AgentStore(IDataStore* dataStore);
    ~AgentStore();

    AgentStore(const AgentStore&) = delete;
    AgentStore& operator=(const AgentStore&) = delete;

    // CRUD operations
    std::vector<Agent> List();
    std::optional<Agent> GetById(const std::string& id);
    std::optional<Agent> GetByName(const std::string& name);
    Agent Create(const Agent& agent);
    bool Update(const Agent& agent);
    bool Delete(const std::string& id);

    // Check if an agent has active sessions (for delete safeguard)
    bool HasActiveSessions(const std::string& agentId);

    // Check if any agents exist (for first-run wizard detection)
    bool HasAnyAgent();

private:
    void EnsureSchema();
    Agent RowToAgent(int64_t id, const std::string& idStr,
                     const std::string& name, const std::string& description,
                     const std::string& identity, const std::string& avatar,
                     const std::string& defaultProvider, const std::string& defaultModel,
                     const std::string& defaultVisionModel,
                     const std::string& intakeInterval, const std::string& intakePrompt,
                     std::uint32_t contextWindow, double temperature,
                     bool reasoningEnabled, const std::string& reasoningEffort,
                     bool padContext,
                     std::uint32_t maxChainSteps, std::uint32_t maxToolCallsPerChain,
                     std::uint32_t timeoutSeconds, std::uint32_t tokenBudgetPerPrompt,
                     std::uint32_t episodicTokenBudget,
                     std::uint32_t semanticTokenBudget,
                     std::uint32_t perspectivesTokenBudget,
                     std::uint32_t consolidationToolBudget,
                     std::uint32_t memoryFileTokenBudget,
                     std::uint32_t ambientContextLimit,
                     const std::string& enabledToolsJson, const std::string& toolConfigsJson,
                     const std::string& allowedNodesJson,
                     std::uint32_t sessionReportTokenBudget,
                     const std::string& diarySecret,
                     std::int64_t createdAtUnixMs, std::int64_t updatedAtUnixMs);

    IDataStore* m_store;
};

} // namespace animus::kernel

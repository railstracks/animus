
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/IDataStore.h"

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <vector>

#include <json/json.h>

namespace animus::kernel {

// ============================================================================
// Helpers
// ============================================================================

static int64_t NowUnixMs() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

static std::uint32_t LeftRotate(std::uint32_t x, std::uint32_t c) {
    return (x << c) | (x >> (32U - c));
}

static std::string Md5Hex(const std::string& input) {
    static const std::uint32_t s[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

    static const std::uint32_t k[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
        0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
        0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
        0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

    std::vector<std::uint8_t> msg(input.begin(), input.end());
    const std::uint64_t bitLen = static_cast<std::uint64_t>(msg.size()) * 8ULL;
    msg.push_back(0x80U);
    while ((msg.size() % 64U) != 56U) {
        msg.push_back(0U);
    }
    for (int i = 0; i < 8; ++i) {
        msg.push_back(static_cast<std::uint8_t>((bitLen >> (8 * i)) & 0xFFU));
    }

    std::uint32_t a0 = 0x67452301U;
    std::uint32_t b0 = 0xefcdab89U;
    std::uint32_t c0 = 0x98badcfeU;
    std::uint32_t d0 = 0x10325476U;

    for (std::size_t offset = 0; offset < msg.size(); offset += 64U) {
        std::uint32_t m[16] = {};
        for (int i = 0; i < 16; ++i) {
            const std::size_t j = offset + static_cast<std::size_t>(i) * 4U;
            m[i] = static_cast<std::uint32_t>(msg[j])
                | (static_cast<std::uint32_t>(msg[j + 1]) << 8U)
                | (static_cast<std::uint32_t>(msg[j + 2]) << 16U)
                | (static_cast<std::uint32_t>(msg[j + 3]) << 24U);
        }

        std::uint32_t a = a0;
        std::uint32_t b = b0;
        std::uint32_t c = c0;
        std::uint32_t d = d0;

        for (int i = 0; i < 64; ++i) {
            std::uint32_t f = 0U;
            int g = 0;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }

            const std::uint32_t temp = d;
            d = c;
            c = b;
            b = b + LeftRotate(a + f + k[i] + m[g], s[i]);
            a = temp;
        }

        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }

    std::array<std::uint8_t, 16> digest{
        static_cast<std::uint8_t>(a0 & 0xFFU), static_cast<std::uint8_t>((a0 >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((a0 >> 16U) & 0xFFU), static_cast<std::uint8_t>((a0 >> 24U) & 0xFFU),
        static_cast<std::uint8_t>(b0 & 0xFFU), static_cast<std::uint8_t>((b0 >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((b0 >> 16U) & 0xFFU), static_cast<std::uint8_t>((b0 >> 24U) & 0xFFU),
        static_cast<std::uint8_t>(c0 & 0xFFU), static_cast<std::uint8_t>((c0 >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((c0 >> 16U) & 0xFFU), static_cast<std::uint8_t>((c0 >> 24U) & 0xFFU),
        static_cast<std::uint8_t>(d0 & 0xFFU), static_cast<std::uint8_t>((d0 >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((d0 >> 16U) & 0xFFU), static_cast<std::uint8_t>((d0 >> 24U) & 0xFFU)};

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::uint8_t byte : digest) {
        oss << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return oss.str();
}

static std::string GenerateRandomInputMaterial() {
    std::random_device rd;
    static std::mt19937_64 rng(rd());
    static std::atomic<std::uint64_t> counter{0};
    std::uniform_int_distribution<std::uint64_t> dist(
        0, std::numeric_limits<std::uint64_t>::max());

    std::ostringstream oss;
    oss << NowUnixMs() << ":" << counter.fetch_add(1) << ":";
    for (int i = 0; i < 4; ++i) {
        oss << std::hex << dist(rng);
    }
    return oss.str();
}

static std::string GenerateAgentId() {
    return Md5Hex(GenerateRandomInputMaterial());
}

static std::string GenerateDiarySecret() {
    // Generate a 64-char hex secret (256 bits of entropy) for diary encryption.
    // Uses the same randomness source as agent ID generation.
    auto material1 = GenerateRandomInputMaterial();
    auto material2 = GenerateRandomInputMaterial();
    return Md5Hex(material1) + Md5Hex(material2);
}

static std::string VectorToJsonArray(const std::vector<std::string>& vec) {
    Json::Value arr(Json::arrayValue);
    for (const auto& s : vec) arr.append(s);
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, arr);
}

static std::vector<std::string> JsonArrayToVector(const std::string& json) {
    std::vector<std::string> result;
    Json::Value root;
    Json::CharReaderBuilder rb;
    std::istringstream stream(json);
    std::string errors;
    if (Json::parseFromStream(rb, stream, &root, &errors)) {
        if (root.isArray()) {
            for (Json::ArrayIndex i = 0; i < root.size(); ++i) {
                if (root[i].isString()) {
                    result.push_back(root[i].asString());
                }
            }
        }
    }
    return result;
}

// ============================================================================
// Construction + schema
// ============================================================================

AgentStore::AgentStore(IDataStore* dataStore) : m_store(dataStore) {
    EnsureSchema();
}

AgentStore::~AgentStore() = default;

void AgentStore::EnsureSchema() {
    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS agents (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            agent_id TEXT NOT NULL UNIQUE,
            name TEXT NOT NULL,
            description TEXT NOT NULL DEFAULT '',
            identity TEXT NOT NULL DEFAULT 'You are Animus.',
            avatar TEXT NOT NULL DEFAULT '',
            default_provider TEXT NOT NULL DEFAULT 'openai',
            default_model TEXT NOT NULL DEFAULT 'gpt-4.1-mini',
            default_vision_model TEXT NOT NULL DEFAULT '',
            intake_interval TEXT NOT NULL DEFAULT '',
            intake_prompt TEXT NOT NULL DEFAULT '',
            context_window INTEGER NOT NULL DEFAULT 128000,
            temperature REAL NOT NULL DEFAULT 0.7,
            reasoning_enabled INTEGER NOT NULL DEFAULT 0,
            reasoning_effort TEXT NOT NULL DEFAULT 'high',
            max_chain_steps INTEGER NOT NULL DEFAULT 200,
            max_tool_calls_per_chain INTEGER NOT NULL DEFAULT 50,
            timeout_seconds INTEGER NOT NULL DEFAULT 1800,
            token_budget_per_prompt INTEGER NOT NULL DEFAULT 200000,
            episodic_token_budget INTEGER NOT NULL DEFAULT 10000,
            semantic_token_budget INTEGER NOT NULL DEFAULT 10000,
            perspectives_token_budget INTEGER NOT NULL DEFAULT 3000,
            consolidation_tool_budget INTEGER NOT NULL DEFAULT 100,
            enabled_tools TEXT NOT NULL DEFAULT '[]',
            tool_configs TEXT NOT NULL DEFAULT '{}',

            created_at_unix_ms INTEGER NOT NULL,
            updated_at_unix_ms INTEGER NOT NULL,
            diary_secret TEXT NOT NULL DEFAULT '',
            pad_context INTEGER NOT NULL DEFAULT 1
        );
    )");

    // Migration: add tool_configs column if missing.
    {
        if (!schema::ColumnExists(m_store, "agents", "tool_configs")) {
            m_store->Exec("ALTER TABLE agents ADD COLUMN tool_configs TEXT NOT NULL DEFAULT '{}'");
        }
    }

    // Migration: rename system_prompt → identity if the old column exists
    {
        if (schema::ColumnExists(m_store, "agents", "system_prompt") &&
            !schema::ColumnExists(m_store, "agents", "identity")) {
            m_store->Exec("ALTER TABLE agents RENAME COLUMN system_prompt TO identity");
        }
    }

    // Migration: add diary_secret column to agents if missing
    {
        if (!schema::ColumnExists(m_store, "agents", "diary_secret")) {
            m_store->Exec("ALTER TABLE agents ADD COLUMN diary_secret TEXT NOT NULL DEFAULT ''");
        }
    }

    // Migration: add agent_id column to sessions if missing
    {
        if (!schema::ColumnExists(m_store, "sessions", "agent_id")) {
            m_store->Exec("ALTER TABLE sessions ADD COLUMN agent_id TEXT NOT NULL DEFAULT 'default'");
        }
    }

    // Migration: add agent_id column to observations if missing.
    {
        if (schema::TableExists(m_store, "observations") &&
            !schema::ColumnExists(m_store, "observations", "agent_id")) {
            m_store->Exec("ALTER TABLE observations ADD COLUMN agent_id TEXT NOT NULL DEFAULT 'default'");
        }
    }

    // Migration: add token budget columns to agents
    {
        if (!schema::ColumnExists(m_store, "agents", "episodic_token_budget"))
            m_store->Exec("ALTER TABLE agents ADD COLUMN episodic_token_budget INTEGER NOT NULL DEFAULT 10000");
        if (!schema::ColumnExists(m_store, "agents", "semantic_token_budget"))
            m_store->Exec("ALTER TABLE agents ADD COLUMN semantic_token_budget INTEGER NOT NULL DEFAULT 10000");
        if (!schema::ColumnExists(m_store, "agents", "perspectives_token_budget"))
            m_store->Exec("ALTER TABLE agents ADD COLUMN perspectives_token_budget INTEGER NOT NULL DEFAULT 3000");
        if (!schema::ColumnExists(m_store, "agents", "consolidation_tool_budget"))
            m_store->Exec("ALTER TABLE agents ADD COLUMN consolidation_tool_budget INTEGER NOT NULL DEFAULT 100");
        if (!schema::ColumnExists(m_store, "agents", "pad_context"))
            m_store->Exec("ALTER TABLE agents ADD COLUMN pad_context INTEGER NOT NULL DEFAULT 1");
        if (!schema::ColumnExists(m_store, "agents", "memory_file_token_budget"))
            m_store->Exec("ALTER TABLE agents ADD COLUMN memory_file_token_budget INTEGER NOT NULL DEFAULT 2500");
        if (!schema::ColumnExists(m_store, "agents", "ambient_context_limit"))
            m_store->Exec("ALTER TABLE agents ADD COLUMN ambient_context_limit INTEGER NOT NULL DEFAULT 5000");
        if (!schema::ColumnExists(m_store, "agents", "allowed_nodes"))
            m_store->Exec("ALTER TABLE agents ADD COLUMN allowed_nodes TEXT NOT NULL DEFAULT '[]'");
        if (!schema::ColumnExists(m_store, "agents", "session_report_token_budget"))
            m_store->Exec("ALTER TABLE agents ADD COLUMN session_report_token_budget INTEGER NOT NULL DEFAULT 1500");
        if (!schema::ColumnExists(m_store, "agents", "default_vision_model"))
            m_store->Exec("ALTER TABLE agents ADD COLUMN default_vision_model TEXT NOT NULL DEFAULT ''");
        if (!schema::ColumnExists(m_store, "agents", "intake_interval")) {
            m_store->Exec("ALTER TABLE agents ADD COLUMN intake_interval TEXT NOT NULL DEFAULT ''");
            // Migrate: copy intake_interval from layer to agent
            m_store->Exec(
                "UPDATE agents SET intake_interval = ("
                "  SELECT intake_interval FROM memory_layers "
                "  WHERE agent_id = agents.id AND intake_interval IS NOT NULL "
                "  AND TRIM(intake_interval) <> '' LIMIT 1"
                ") WHERE intake_interval = ''");
        }
        if (!schema::ColumnExists(m_store, "agents", "intake_prompt")) {
            m_store->Exec("ALTER TABLE agents ADD COLUMN intake_prompt TEXT NOT NULL DEFAULT ''");
            // Migrate: copy consolidation_intake_prompt from layer to agent
            m_store->Exec(
                "UPDATE agents SET intake_prompt = ("
                "  SELECT consolidation_intake_prompt FROM memory_layers "
                "  WHERE agent_id = agents.id AND consolidation_intake_prompt <> '' LIMIT 1"
                ") WHERE intake_prompt = ''");
        }
    }
}

// ============================================================================
// Row mapping
// ============================================================================

Agent AgentStore::RowToAgent(
        int64_t id, const std::string& idStr,
        const std::string& name, const std::string& description,
        const std::string& identity, const std::string& avatar,
        const std::string& defaultProvider, const std::string& defaultModel,
        const std::string& defaultVisionModel,
        const std::string& intakeInterval,
        const std::string& intakePrompt,
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
        std::int64_t createdAtUnixMs, std::int64_t updatedAtUnixMs) {
    Agent a;
    a.id = idStr;
    a.numeric_id = id;
    a.name = name;
    a.description = description;
    a.identity = identity;
    a.avatar = avatar;
    a.default_provider = defaultProvider;
    a.default_model = defaultModel;
    a.default_vision_model = defaultVisionModel;
    a.intake_interval = intakeInterval;
    a.intake_prompt = intakePrompt;
    a.context_window = contextWindow;
    a.temperature = temperature;
    a.reasoning_enabled = reasoningEnabled;
    a.reasoning_effort = reasoningEffort;
    a.budget.maxChainSteps = maxChainSteps;
    a.budget.maxToolCallsPerChain = maxToolCallsPerChain;
    a.budget.timeoutSeconds = timeoutSeconds;
    a.budget.tokenBudgetPerPrompt = tokenBudgetPerPrompt;
    a.budget.episodicTokenBudget = episodicTokenBudget;
    a.budget.semanticTokenBudget = semanticTokenBudget;
    a.budget.perspectivesTokenBudget = perspectivesTokenBudget;
    a.budget.consolidationToolBudget = consolidationToolBudget;
    a.budget.memoryFileTokenBudget = memoryFileTokenBudget;
    a.budget.ambientContextLimit = ambientContextLimit;
    a.enabled_tools = JsonArrayToVector(enabledToolsJson);
    a.tool_configs_json = toolConfigsJson.empty() ? "{}" : toolConfigsJson;
    a.allowed_nodes = JsonArrayToVector(allowedNodesJson);
    a.budget.sessionReportTokenBudget = sessionReportTokenBudget;
    a.diary_secret = diarySecret;
    a.pad_context = padContext;
    a.created_at_unix_ms = createdAtUnixMs;
    a.updated_at_unix_ms = updatedAtUnixMs;
    return a;
}

// ============================================================================
// CRUD
// ============================================================================

std::vector<Agent> AgentStore::List() {
    std::vector<Agent> result;
    auto stmt = m_store->Prepare(
        "SELECT id, agent_id, name, description, identity, avatar, "
        "default_provider, default_model, default_vision_model, intake_interval, intake_prompt, context_window, temperature, "
        "reasoning_enabled, reasoning_effort, pad_context, "
        "max_chain_steps, max_tool_calls_per_chain, timeout_seconds, token_budget_per_prompt, episodic_token_budget, semantic_token_budget, perspectives_token_budget, consolidation_tool_budget, memory_file_token_budget, ambient_context_limit, "
        "enabled_tools, tool_configs, allowed_nodes, session_report_token_budget, "
        "created_at_unix_ms, updated_at_unix_ms, diary_secret "
        "FROM agents ORDER BY created_at_unix_ms ASC");
    if (!stmt) return result;

    while (stmt->Step()) {
        result.push_back(RowToAgent(
            stmt->ColumnInt64(0), stmt->ColumnText(1),
            stmt->ColumnText(2), stmt->ColumnText(3),
            stmt->ColumnText(4), stmt->ColumnText(5),
            stmt->ColumnText(6), stmt->ColumnText(7),
            stmt->ColumnText(8),
            stmt->ColumnText(9), stmt->ColumnText(10),
            static_cast<std::uint32_t>(stmt->ColumnInt64(11)),
            stmt->ColumnDouble(12),
            stmt->ColumnInt64(13) != 0,
            stmt->ColumnText(14),
            stmt->ColumnInt64(15) != 0,
            static_cast<std::uint32_t>(stmt->ColumnInt64(16)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(17)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(18)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(19)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(20)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(21)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(22)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(23)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(24)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(25)),
            stmt->ColumnText(26), stmt->ColumnText(27),
            stmt->ColumnText(28),
            static_cast<std::uint32_t>(stmt->ColumnInt64(29)),
            stmt->ColumnText(32),
            stmt->ColumnInt64(30), stmt->ColumnInt64(31)));
    }
    return result;
}

std::optional<Agent> AgentStore::GetById(const std::string& id) {
    auto stmt = m_store->Prepare(
        "SELECT id, agent_id, name, description, identity, avatar, "
        "default_provider, default_model, default_vision_model, intake_interval, intake_prompt, context_window, temperature, "
        "reasoning_enabled, reasoning_effort, pad_context, "
        "max_chain_steps, max_tool_calls_per_chain, timeout_seconds, token_budget_per_prompt, episodic_token_budget, semantic_token_budget, perspectives_token_budget, consolidation_tool_budget, memory_file_token_budget, ambient_context_limit, "
        "enabled_tools, tool_configs, allowed_nodes, session_report_token_budget, "
        "created_at_unix_ms, updated_at_unix_ms, diary_secret "
        "FROM agents WHERE agent_id=?");
    if (!stmt) return std::nullopt;
    stmt->BindText(1, id);

    if (stmt->Step()) {
        return RowToAgent(
            stmt->ColumnInt64(0), stmt->ColumnText(1),
            stmt->ColumnText(2), stmt->ColumnText(3),
            stmt->ColumnText(4), stmt->ColumnText(5),
            stmt->ColumnText(6), stmt->ColumnText(7),
            stmt->ColumnText(8),
            stmt->ColumnText(9), stmt->ColumnText(10),
            static_cast<std::uint32_t>(stmt->ColumnInt64(11)),
            stmt->ColumnDouble(12),
            stmt->ColumnInt64(13) != 0,
            stmt->ColumnText(14),
            stmt->ColumnInt64(15) != 0,
            static_cast<std::uint32_t>(stmt->ColumnInt64(16)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(17)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(18)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(19)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(20)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(21)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(22)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(23)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(24)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(25)),
            stmt->ColumnText(26), stmt->ColumnText(27),
            stmt->ColumnText(28),
            static_cast<std::uint32_t>(stmt->ColumnInt64(29)),
            stmt->ColumnText(32),
            stmt->ColumnInt64(30), stmt->ColumnInt64(31));
    }
    return std::nullopt;
}

std::optional<Agent> AgentStore::GetByName(const std::string& name) {
    auto stmt = m_store->Prepare(
        "SELECT id, agent_id, name, description, identity, avatar, "
        "default_provider, default_model, default_vision_model, intake_interval, intake_prompt, context_window, temperature, "
        "reasoning_enabled, reasoning_effort, pad_context, "
        "max_chain_steps, max_tool_calls_per_chain, timeout_seconds, token_budget_per_prompt, episodic_token_budget, semantic_token_budget, perspectives_token_budget, consolidation_tool_budget, memory_file_token_budget, ambient_context_limit, "
        "enabled_tools, tool_configs, allowed_nodes, session_report_token_budget, "
        "created_at_unix_ms, updated_at_unix_ms, diary_secret "
        "FROM agents WHERE name=?");
    if (!stmt) return std::nullopt;
    stmt->BindText(1, name);

    if (stmt->Step()) {
        return RowToAgent(
            stmt->ColumnInt64(0), stmt->ColumnText(1),
            stmt->ColumnText(2), stmt->ColumnText(3),
            stmt->ColumnText(4), stmt->ColumnText(5),
            stmt->ColumnText(6), stmt->ColumnText(7),
            stmt->ColumnText(8),
            stmt->ColumnText(9), stmt->ColumnText(10),
            static_cast<std::uint32_t>(stmt->ColumnInt64(11)),
            stmt->ColumnDouble(12),
            stmt->ColumnInt64(13) != 0,
            stmt->ColumnText(14),
            stmt->ColumnInt64(15) != 0,
            static_cast<std::uint32_t>(stmt->ColumnInt64(16)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(17)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(18)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(19)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(20)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(21)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(22)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(23)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(24)),
            static_cast<std::uint32_t>(stmt->ColumnInt64(25)),
            stmt->ColumnText(26), stmt->ColumnText(27),
            stmt->ColumnText(28),
            static_cast<std::uint32_t>(stmt->ColumnInt64(29)),
            stmt->ColumnText(32),
            stmt->ColumnInt64(30), stmt->ColumnInt64(31));
    }
    return std::nullopt;
}

Agent AgentStore::Create(const Agent& agent) {
    auto now = NowUnixMs();
    std::string id = agent.id;
    if (id.empty()) {
        constexpr int kMaxIdRetries = 8;
        for (int attempt = 0; attempt < kMaxIdRetries; ++attempt) {
            const std::string candidate = GenerateAgentId();
            if (!GetById(candidate).has_value()) {
                id = candidate;
                break;
            }
        }
        if (id.empty()) {
            std::cerr << "[agent-store] failed to generate unique agent id after retries" << std::endl;
            return {};
        }
    }

    auto stmt = m_store->Prepare(
        "INSERT INTO agents (agent_id, name, description, identity, avatar, "
        "default_provider, default_model, default_vision_model, intake_interval, intake_prompt, context_window, temperature, "
        "reasoning_enabled, reasoning_effort, pad_context, "
        "max_chain_steps, max_tool_calls_per_chain, timeout_seconds, token_budget_per_prompt, episodic_token_budget, semantic_token_budget, perspectives_token_budget, consolidation_tool_budget, memory_file_token_budget, ambient_context_limit, "
        "enabled_tools, tool_configs, allowed_nodes, session_report_token_budget, "
        "created_at_unix_ms, updated_at_unix_ms, diary_secret) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    if (!stmt) {
        std::cerr << "[agent-store] insert failed: " << m_store->ErrMsg() << std::endl;
        return {};
    }

    // Generate diary secret if not provided
    std::string diarySecret = agent.diary_secret.empty() ? GenerateDiarySecret() : agent.diary_secret;

    stmt->BindText(1, id);
    stmt->BindText(2, agent.name);
    stmt->BindText(3, agent.description);
    stmt->BindText(4, agent.identity);
    stmt->BindText(5, agent.avatar);
    stmt->BindText(6, agent.default_provider);
    stmt->BindText(7, agent.default_model);
    stmt->BindText(8, agent.default_vision_model);
    stmt->BindText(9, agent.intake_interval);
    stmt->BindText(10, agent.intake_prompt);
    stmt->BindInt64(11, static_cast<int64_t>(agent.context_window));
    stmt->BindDouble(12, agent.temperature);
    stmt->BindInt(13, agent.reasoning_enabled ? 1 : 0);
    stmt->BindText(14, agent.reasoning_effort);
    stmt->BindInt(15, agent.pad_context ? 1 : 0);
    stmt->BindInt64(16, static_cast<int64_t>(agent.budget.maxChainSteps));
    stmt->BindInt64(17, static_cast<int64_t>(agent.budget.maxToolCallsPerChain));
    stmt->BindInt64(18, static_cast<int64_t>(agent.budget.timeoutSeconds));
    stmt->BindInt64(19, static_cast<int64_t>(agent.budget.tokenBudgetPerPrompt));
    stmt->BindInt64(20, static_cast<int64_t>(agent.budget.episodicTokenBudget));
    stmt->BindInt64(21, static_cast<int64_t>(agent.budget.semanticTokenBudget));
    stmt->BindInt64(22, static_cast<int64_t>(agent.budget.perspectivesTokenBudget));
    stmt->BindInt64(23, static_cast<int64_t>(agent.budget.consolidationToolBudget));
    stmt->BindInt64(24, static_cast<int64_t>(agent.budget.memoryFileTokenBudget));
    stmt->BindInt64(25, static_cast<int64_t>(agent.budget.ambientContextLimit));
    stmt->BindText(26, VectorToJsonArray(agent.enabled_tools));
    stmt->BindText(27, agent.tool_configs_json.empty() ? "{}" : agent.tool_configs_json);
    stmt->BindText(28, VectorToJsonArray(agent.allowed_nodes));
    stmt->BindInt(29, agent.budget.sessionReportTokenBudget);
    stmt->BindInt64(30, now);
    stmt->BindInt64(31, now);
    stmt->BindText(32, diarySecret);

    // For non-SELECT statements, Step() returns false on SQLITE_DONE.
    // Treat the operation as successful if the row is now queryable.
    bool stepResult = stmt->Step();
    std::cerr << "[agent-store] Create: Step()=" << (stepResult ? "true" : "false")
              << " ErrMsg=" << m_store->ErrMsg() << std::endl;
    auto result = GetById(id);
    if (!result.has_value()) {
        std::cerr << "[agent-store] Create: GetById(" << id << ") returned nullopt" << std::endl;
    }
    return result.value_or(Agent{});
}

bool AgentStore::Update(const Agent& agent) {
    auto now = NowUnixMs();
    auto stmt = m_store->Prepare(
        "UPDATE agents SET name=?, description=?, identity=?, avatar=?, "
        "default_provider=?, default_model=?, default_vision_model=?, intake_interval=?, intake_prompt=?, context_window=?, temperature=?, "
        "reasoning_enabled=?, reasoning_effort=?, pad_context=?, "
        "max_chain_steps=?, max_tool_calls_per_chain=?, timeout_seconds=?, token_budget_per_prompt=?, episodic_token_budget=?, semantic_token_budget=?, perspectives_token_budget=?, consolidation_tool_budget=?, memory_file_token_budget=?, ambient_context_limit=?, "
        "enabled_tools=?, tool_configs=?, allowed_nodes=?, session_report_token_budget=?, updated_at_unix_ms=? "
        "WHERE agent_id=?");
    if (!stmt) return false;

    stmt->BindText(1, agent.name);
    stmt->BindText(2, agent.description);
    stmt->BindText(3, agent.identity);
    stmt->BindText(4, agent.avatar);
    stmt->BindText(5, agent.default_provider);
    stmt->BindText(6, agent.default_model);
    stmt->BindText(7, agent.default_vision_model);
    stmt->BindText(8, agent.intake_interval);
    stmt->BindText(9, agent.intake_prompt);
    stmt->BindInt64(10, static_cast<int64_t>(agent.context_window));
    stmt->BindDouble(11, agent.temperature);
    stmt->BindInt(12, agent.reasoning_enabled ? 1 : 0);
    stmt->BindText(13, agent.reasoning_effort);
    stmt->BindInt(14, agent.pad_context ? 1 : 0);
    stmt->BindInt64(15, static_cast<int64_t>(agent.budget.maxChainSteps));
    stmt->BindInt64(16, static_cast<int64_t>(agent.budget.maxToolCallsPerChain));
    stmt->BindInt64(17, static_cast<int64_t>(agent.budget.timeoutSeconds));
    stmt->BindInt64(18, static_cast<int64_t>(agent.budget.tokenBudgetPerPrompt));
    stmt->BindInt64(19, static_cast<int64_t>(agent.budget.episodicTokenBudget));
    stmt->BindInt64(20, static_cast<int64_t>(agent.budget.semanticTokenBudget));
    stmt->BindInt64(21, static_cast<int64_t>(agent.budget.perspectivesTokenBudget));
    stmt->BindInt64(22, static_cast<int64_t>(agent.budget.consolidationToolBudget));
    stmt->BindInt64(23, static_cast<int64_t>(agent.budget.memoryFileTokenBudget));
    stmt->BindInt64(24, static_cast<int64_t>(agent.budget.ambientContextLimit));
    stmt->BindText(25, VectorToJsonArray(agent.enabled_tools));
    stmt->BindText(26, agent.tool_configs_json.empty() ? "{}" : agent.tool_configs_json);
    stmt->BindText(27, VectorToJsonArray(agent.allowed_nodes));
    stmt->BindInt(28, agent.budget.sessionReportTokenBudget);
    stmt->BindInt64(29, now);
    stmt->BindText(30, agent.id);

    // For non-SELECT statements, Step() returns false on SQLITE_DONE.
    // Verify success by checking that exactly one row was affected.
    stmt->Step();
    return m_store->Changes() > 0;
}

bool AgentStore::Delete(const std::string& id) {
    auto stmt = m_store->Prepare("DELETE FROM agents WHERE agent_id=?");
    if (!stmt) return false;
    stmt->BindText(1, id);
    // For non-SELECT statements, Step() returns false on SQLITE_DONE.
    stmt->Step();
    return m_store->Changes() > 0;
}

bool AgentStore::HasActiveSessions(const std::string& agentId) {
    auto stmt = m_store->Prepare(
        "SELECT COUNT(*) FROM sessions WHERE agent_id=? AND terminated=0");
    if (!stmt) return false;
    stmt->BindText(1, agentId);
    if (stmt->Step()) {
        return stmt->ColumnInt64(0) > 0;
    }
    return false;
}

bool AgentStore::HasAnyAgent() {
    auto stmt = m_store->Prepare("SELECT COUNT(*) FROM agents");
    if (stmt && stmt->Step()) {
        return stmt->ColumnInt64(0) > 0;
    }
    return false;
}

} // namespace animus::kernel

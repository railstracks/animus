#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "animus_kernel/IDataStore.h"

namespace animus::kernel {

// ============================================================================
// PromptLogLevel — controls what gets logged per LLM call
// ============================================================================

enum class PromptLogLevel {
    None,     // No logging
    Default,  // Metadata only: tokens, model, provider, latency, chain_step
    Full      // Metadata + full prompt/response content + tool calls/results
};

/// Parse a log level string ("none", "default", "full") into enum.
/// Returns Default for unrecognized values.
PromptLogLevel ParsePromptLogLevel(const std::string& s);

/// Convert log level to string.
std::string PromptLogLevelToString(PromptLogLevel level);

// ============================================================================
// PromptLogEntry — a single logged LLM interaction
// ============================================================================

struct PromptLogEntry {
    int64_t id{0};
    std::string agent_id;
    int64_t session_id{0};
    std::string provider;
    std::string model;
    int prompt_tokens{0};
    int completion_tokens{0};
    int total_tokens{0};
    int latency_ms{0};
    int chain_step{0};
    std::string created_at;

    // Only populated at 'full' level
    std::string prompt_content;
    std::string response_content;
    std::string tool_calls;       // JSON array
    std::string tool_results;     // JSON array
};

// ============================================================================
// PromptLogStore — persistent storage for LLM call logs
// ============================================================================

class PromptLogStore {
public:
    explicit PromptLogStore(IDataStore* store);

    /// Create the prompt_logs table if it doesn't exist.
    void EnsureSchema();

    /// Log a single LLM call. At Default level, content fields are ignored.
    /// At None level, this is a no-op.
    void Log(PromptLogLevel level,
             const std::string& agent_id,
             int64_t session_id,
             const std::string& provider,
             const std::string& model,
             int prompt_tokens,
             int completion_tokens,
             int latency_ms,
             int chain_step,
             const std::string& prompt_content,
             const std::string& response_content,
             const std::string& tool_calls_json,
             const std::string& tool_results_json);

    /// Query logs for a specific agent within a time range.
    /// Returns entries newest-first. If limit is 0, returns all.
    std::vector<PromptLogEntry> QueryByAgent(
        const std::string& agent_id,
        const std::string& from_iso,
        const std::string& to_iso,
        int limit = 100);

    /// Get aggregate token usage for an agent in a time range.
    struct TokenUsageSummary {
        int total_calls{0};
        int total_prompt_tokens{0};
        int total_completion_tokens{0};
        int total_tokens{0};
        double avg_latency_ms{0.0};
    };

    TokenUsageSummary GetUsageSummary(
        const std::string& agent_id,
        const std::string& from_iso,
        const std::string& to_iso);

private:
    IDataStore* m_store;
};

} // namespace animus::kernel

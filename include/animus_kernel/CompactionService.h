#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <functional>

#include "animus_kernel/PromptAssembler.h"
#include "animus_kernel/Session.h"

namespace animus::kernel {

class IDataStore;
class SessionManager;

// PromptAssembler is included below (needed as member, not just forward-declared)

// ============================================================================
// Compaction — compresses older session turns into a summary to keep context
// within model limits. Each compaction replaces a range of turns with a single
// summary turn. Cascading compactions absorb previous summaries.
// ============================================================================

struct CompactionEntry {
    std::uint64_t id{0};
    SessionId session_id{0};
    SessionTurnId message_id{0};    // Last turn included in this compaction
    std::string summary;             // LLM-generated summary text
    std::uint64_t token_count{0};    // Estimated tokens in the summary
    std::string model;               // Model used to generate this compaction
    std::uint64_t created_at_unix_ms{0};
};

struct CompactionResult {
    bool success{false};
    std::string error;
    CompactionEntry entry;
    std::size_t turns_compacted{0};  // How many turns were compacted
};

// Callback for LLM-based summary generation.
// Receives the messages to summarize, returns the summary text.
using SummaryGenerator = std::function<std::string(
    const std::string& systemPrompt,
    const std::vector<SessionTurn>& turns,
    const std::string& model)>;

class CompactionService {
public:
    struct CompactionConfig {
        float threshold_percent{80.0f};        // Trigger compaction at this % of context
        std::size_t min_messages_before_compact{6};   // Don't compact very short sessions
        std::string model_override;            // Use different model for summaries (empty = same)
        std::string prompt_template;           // Custom compaction prompt (empty = default)
    };

    explicit CompactionService(
        IDataStore* dataStore,
        SessionManager* sessions,
        const CompactionConfig& config);

    // Check if a session needs compaction and perform it if so.
    // Returns the result (success or failure). If no compaction needed,
    // result.success is true with turns_compacted = 0.
    CompactionResult CompactIfNeeded(
        SessionId sessionId,
        const std::string& systemPrompt,
        const std::string& providerId,
        const std::string& model,
        std::size_t contextWindowTokens,
        SummaryGenerator generator = nullptr);

    // Force compaction of a session regardless of threshold.
    CompactionResult CompactNow(
        SessionId sessionId,
        const std::string& systemPrompt,
        const std::string& providerId,
        const std::string& model,
        std::size_t contextWindowTokens,
        SummaryGenerator generator = nullptr);

    // Retrieve compaction history for a session.
    std::vector<CompactionEntry> GetCompactions(SessionId sessionId);

    // Get the most recent compaction for a session.
    std::optional<CompactionEntry> GetLatestCompaction(SessionId sessionId);

    // Delete all compactions for a session (e.g. when deleting the session).
    void DeleteCompactions(SessionId sessionId);

    // Estimate tokens for a text string.
    static std::size_t EstimateTokens(const std::string& text);

private:
    CompactionResult DoCompact(
        SessionAccess& session,
        const std::string& systemPrompt,
        const std::string& providerId,
        const std::string& model,
        std::size_t contextWindowTokens,
        bool force,
        SummaryGenerator generator);

    std::string BuildCompactionPrompt(
        const std::vector<SessionTurn>& turns,
        const SessionTurn* previousSummary) const;

    std::string DefaultCompactionPrompt() const;

    void StoreCompaction(const CompactionEntry& entry);
    void CreateSchema();

    IDataStore* m_store;
    SessionManager* m_sessions;
    CompactionConfig m_config;
    PromptAssembler m_assembler;
    bool m_schemaReady{false};
};

} // namespace animus::kernel

#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/CompactionService.h"
#include "animus_kernel/ISessionStore.h"
#include "animus_kernel/SessionManager.h"
#include "animus_kernel/TokenEstimate.h"
#include <iostream>
#include "animus_kernel/PromptAssembler.h"
#include "animus_kernel/IDataStore.h"

#include <chrono>
#include <sstream>

namespace animus::kernel {

// ============================================================================
// Token estimation
// ============================================================================

std::size_t CompactionService::EstimateTokens(const std::string& text) {
    return TokenEstimate::Estimate(text);
}

// ============================================================================
// Construction
// ============================================================================

CompactionService::CompactionService(
    IDataStore* dataStore,
    SessionManager* sessions,
    const CompactionConfig& config)
    : m_store(dataStore)
    , m_sessions(sessions)
    , m_config(config)
    , m_schemaReady(false)
{
}

// ============================================================================
// Schema
// ============================================================================

void CompactionService::CreateSchema() {
    if (m_schemaReady) return;

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS session_compactions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
            message_id INTEGER NOT NULL,
            summary TEXT NOT NULL,
            token_count INTEGER NOT NULL DEFAULT 0,
            model TEXT NOT NULL DEFAULT '',
            created_at_unix_ms INTEGER NOT NULL DEFAULT 0
        );
    )");

    m_store->Exec(R"(
        CREATE INDEX IF NOT EXISTS idx_compactions_session
        ON session_compactions(session_id, message_id);
    )");

    m_schemaReady = true;
}

// ============================================================================
// CompactIfNeeded
// ============================================================================

CompactionResult CompactionService::CompactIfNeeded(
    SessionId sessionId,
    const std::string& systemPrompt,
    const std::string& providerId,
    const std::string& model,
    std::size_t contextWindowTokens,
    SummaryGenerator generator)
{
    CreateSchema();

    auto session = m_sessions->GetStore().GetById(sessionId);
    if (!session) {
        return {false, "Session not found", {}, 0};
    }

    SessionAccess access(session, SessionAccessMode::ReadWrite);

    // Don't compact very short sessions
    if (access.Turns().size() < m_config.min_messages_before_compact) {
        return {true, "", {}, 0};
    }

    // The compaction decision was already made by PromptAssembler's
    // token estimate (which includes context providers, active memory,
    // session reports, etc.). DoCompact's own estimate only sees raw
    // turn content + systemPrompt, which underestimates total tokens.
    // Pass force=true to skip the redundant budget check.
    return DoCompact(access, systemPrompt, providerId, model,
                     contextWindowTokens, true, generator);
}

// ============================================================================
// CompactNow
// ============================================================================

CompactionResult CompactionService::CompactNow(
    SessionId sessionId,
    const std::string& systemPrompt,
    const std::string& providerId,
    const std::string& model,
    std::size_t contextWindowTokens,
    SummaryGenerator generator)
{
    CreateSchema();

    auto session = m_sessions->GetStore().GetById(sessionId);
    if (!session) {
        return {false, "Session not found", {}, 0};
    }

    SessionAccess access(session, SessionAccessMode::ReadWrite);
    return DoCompact(access, systemPrompt, providerId, model,
                     contextWindowTokens, true, generator);
}

// ============================================================================
// DoCompact — core compaction logic
// ============================================================================

CompactionResult CompactionService::DoCompact(
    SessionAccess& session,
    const std::string& systemPrompt,
    const std::string& providerId,
    const std::string& model,
    std::size_t contextWindowTokens,
    bool force,
    SummaryGenerator generator)
{
    const auto& turns = session.Turns();
    if (turns.empty()) {
        return {true, "", {}, 0};
    }

    // Check if compaction is needed
    if (!force) {
        const std::size_t budget = static_cast<std::size_t>(
            static_cast<float>(contextWindowTokens) * m_config.threshold_percent / 100.0f);

        std::size_t totalTokens = EstimateTokens(systemPrompt);
        for (const auto& turn : turns) {
            if (turn.is_summary) continue;
            totalTokens += EstimateTokens(turn.content) + 4;
        }

        if (totalTokens <= budget) {
            return {true, "", {}, 0};  // No compaction needed
        }
    }

    // Find the most recent compaction for cascading
    auto previousCompaction = GetLatestCompaction(session.Id());

    // Determine which turns to compact:
    // Keep the last N turns un-compacted (they're recent context).
    // Compact everything before that, including any previous compaction summary.
    const std::size_t keepRecent = 10;  // Keep last 10 turns as-is
    std::size_t compactEnd = turns.size() > keepRecent ? turns.size() - keepRecent : turns.size();

    // Don't compact if there's nothing to compact
    if (compactEnd == 0) {
        return {true, "", {}, 0};
    }

    // Gather turns to compact (skip summary turns and already-compacted turns)
    std::vector<SessionTurn> turnsToCompact;
    SessionTurnId lastTurnId = 0;
    for (std::size_t i = 0; i < compactEnd; ++i) {
        const auto& turn = turns[i];
        if (turn.is_summary || turn.is_compacted) continue;
        turnsToCompact.push_back(turn);
        lastTurnId = turn.turn_id;
    }

    if (turnsToCompact.empty()) {
        return {true, "", {}, 0};
    }

    // Generate the summary
    const std::string& summaryModel = m_config.model_override.empty()
        ? model : m_config.model_override;

    std::string summaryText;
    if (generator) {
        summaryText = generator(systemPrompt, turnsToCompact, summaryModel);
    } else {
        // Without a generator, create a structural summary (no LLM call)
        std::ostringstream ss;
        ss << "[Compaction of " << turnsToCompact.size() << " turns, ending at turn "
           << lastTurnId << "]\n\n";

        // Include key points from first/last few turns
        std::size_t showCount = std::min(turnsToCompact.size(), static_cast<std::size_t>(6));
        for (std::size_t i = 0; i < showCount; ++i) {
            const auto& t = turnsToCompact[i];
            std::string preview = t.content.substr(0, 200);
            if (t.content.size() > 200) preview += "...";
            ss << "[" << t.role << "] " << preview << "\n";
        }
        if (turnsToCompact.size() > 6) {
            ss << "... (" << (turnsToCompact.size() - 6) << " turns omitted) ...\n";
            // Show last turn for recency
            const auto& last = turnsToCompact.back();
            std::string preview = last.content.substr(0, 200);
            if (last.content.size() > 200) preview += "...";
            ss << "[" << last.role << "] " << preview << "\n";
        }

        summaryText = ss.str();
    }

    // Build and store the compaction entry
    CompactionEntry entry;
    entry.session_id = session.Id();
    entry.message_id = lastTurnId;
    entry.summary = summaryText;
    entry.token_count = EstimateTokens(summaryText);
    entry.model = summaryModel;
    entry.created_at_unix_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    StoreCompaction(entry);

    // Build the compaction summary turn.
    // compacted_from records which original turns were folded in.
    SessionTurn summaryTurn;
    summaryTurn.role = "system";
    summaryTurn.content = summaryText;
    summaryTurn.is_summary = true;
    summaryTurn.unix_ms = entry.created_at_unix_ms;
    for (std::size_t i = 0; i < compactEnd; ++i) {
        if (!turns[i].is_summary && !turns[i].is_compacted) {
            summaryTurn.compacted_from.push_back(turns[i].turn_id);
        }
    }

    // Mark the compacted turns so PromptAssembler skips them.
    // We keep them in the session (and DB) so consolidation can still process them.
    for (std::size_t i = 0; i < compactEnd; ++i) {
        if (!turns[i].is_summary && !turns[i].is_compacted) {
            session.MarkTurnCompacted(turns[i].turn_id);
        }
    }

    // Set the compaction summary on the session object
    session.SetCompactionSummary(summaryTurn);

    // Persist the session (including compaction summary + compacted flags)
    m_sessions->GetStore().FlushSession(session.Id());

    return {true, "", entry, turnsToCompact.size()};
}

// ============================================================================
// Compaction prompt
// ============================================================================

std::string CompactionService::BuildCompactionPrompt(
    const std::vector<SessionTurn>& turns,
    const SessionTurn* previousSummary) const
{
    std::ostringstream prompt;
    prompt << DefaultCompactionPrompt();

    if (previousSummary) {
        prompt << "\n\nPrevious compaction summary:\n";
        prompt << previousSummary->content << "\n";
        prompt << "\nNew messages to incorporate:\n";
    }

    return prompt.str();
}

std::string CompactionService::DefaultCompactionPrompt() const {
    if (!m_config.prompt_template.empty()) {
        return m_config.prompt_template;
    }

    return R"(You are summarizing a conversation session for context compression.
Produce a structured summary preserving:

1. **Decisions made** — what was decided and why
2. **Actions taken** — files modified, commands run, their outcomes
3. **Current state** — what exists now (files, config, running processes)
4. **Open issues** — unresolved problems, blocked tasks, pending questions
5. **Key context** — names, IDs, paths, credentials, versions that are referenced later
6. **Emotional/narrative state** — any ongoing interpersonal dynamics worth preserving

Be specific. Include exact file paths, commit hashes, error messages.
A future reader must be able to resume work from this summary without
access to the original messages.

Format as a concise, structured summary.)";
}

// ============================================================================
// Storage
// ============================================================================

void CompactionService::StoreCompaction(const CompactionEntry& entry) {
    CreateSchema();

    auto stmt = m_store->Prepare(
        "INSERT INTO session_compactions "
        "(session_id, message_id, summary, token_count, model, created_at_unix_ms) "
        "VALUES (?,?,?,?,?,?)");
    if (!stmt) return;

    stmt->BindInt64(1, entry.session_id);
    stmt->BindInt64(2, entry.message_id);
    stmt->BindText(3, entry.summary);
    stmt->BindInt64(4, entry.token_count);
    stmt->BindText(5, entry.model);
    stmt->BindInt64(6, entry.created_at_unix_ms);
    stmt->Step();
}

std::vector<CompactionEntry> CompactionService::GetCompactions(SessionId sessionId) {
    CreateSchema();

    std::vector<CompactionEntry> entries;

    auto stmt = m_store->Prepare(
        "SELECT id, session_id, message_id, summary, token_count, model, created_at_unix_ms "
        "FROM session_compactions WHERE session_id=? ORDER BY message_id ASC");
    if (!stmt) return entries;

    stmt->BindInt64(1, sessionId);
    while (stmt->Step()) {
        CompactionEntry e;
        e.id = stmt->ColumnInt64(0);
        e.session_id = stmt->ColumnInt64(1);
        e.message_id = stmt->ColumnInt64(2);
        e.summary = stmt->ColumnText(3);
        e.token_count = stmt->ColumnInt64(4);
        e.model = stmt->ColumnText(5);
        e.created_at_unix_ms = stmt->ColumnInt64(6);
        entries.push_back(std::move(e));
    }

    return entries;
}

std::optional<CompactionEntry> CompactionService::GetLatestCompaction(SessionId sessionId) {
    CreateSchema();

    auto stmt = m_store->Prepare(
        "SELECT id, session_id, message_id, summary, token_count, model, created_at_unix_ms "
        "FROM session_compactions WHERE session_id=? ORDER BY message_id DESC LIMIT 1");
    if (!stmt) return std::nullopt;

    stmt->BindInt64(1, sessionId);
    if (stmt->Step()) {
        CompactionEntry e;
        e.id = stmt->ColumnInt64(0);
        e.session_id = stmt->ColumnInt64(1);
        e.message_id = stmt->ColumnInt64(2);
        e.summary = stmt->ColumnText(3);
        e.token_count = stmt->ColumnInt64(4);
        e.model = stmt->ColumnText(5);
        e.created_at_unix_ms = stmt->ColumnInt64(6);
        return e;
    }

    return std::nullopt;
}

void CompactionService::DeleteCompactions(SessionId sessionId) {
    CreateSchema();

    auto stmt = m_store->Prepare(
        "DELETE FROM session_compactions WHERE session_id=?");
    if (!stmt) return;
    stmt->BindInt64(1, sessionId);
    stmt->Step();
}

} // namespace animus::kernel
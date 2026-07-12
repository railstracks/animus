#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "animus_kernel/IDataStore.h"

namespace animus::kernel {

// ============================================================================
// SessionReport — temporal structured summary of a session
// ============================================================================

struct SessionReport {
    int64_t id{0};
    int64_t session_id{0};
    std::string agent_id;
    std::string summary;           // Stable identity — what kind of session this is
    std::string past_events;       // Trajectory — what has transpired
    std::string current_activity;  // Snapshot — what's happening now
    std::string forward_look;      // Expectation — what's expected next
    int64_t created_at_unix_ms{0};
    int64_t updated_at_unix_ms{0};
};

// SessionReport with optional embedding vector (Ticket 094)
struct SessionReportWithEmbedding : SessionReport {
    std::vector<float> embedding;
    bool has_embedding{false};
};

// ============================================================================
// SessionReportStore — CRUD for session reports backed by SQLite
// ============================================================================

class SessionReportStore {
public:
    explicit SessionReportStore(IDataStore* store);

    void EnsureSchema();

    // Upsert (create or replace) a report for a session+agent.
    // Returns the report with id and timestamps populated.
    SessionReport Upsert(int64_t sessionId,
                         const std::string& agentId,
                         const std::string& summary,
                         const std::string& pastEvents,
                         const std::string& currentActivity,
                         const std::string& forwardLook);

    // Get the report for a specific session. Returns nullopt if none exists.
    std::optional<SessionReport> GetForSession(int64_t sessionId,
                                                const std::string& agentId) const;

    // List most recent report per distinct session, ordered by updated_at DESC.
    // Used for active memory injection — fills until token budget exhausted.
    std::vector<SessionReport> ListRecent(const std::string& agentId,
                                           int limit = 20) const;

    // Search across all report fields for an agent.
    std::vector<SessionReport> Search(const std::string& agentId,
                                       const std::string& query,
                                       int limit = 20) const;

    // Delete a report. Returns true on success.
    bool Delete(int64_t sessionId, const std::string& agentId);

    // --- Embedding support (Ticket 094) ---

    // Store an embedding vector for a report (for relevance-based retrieval).
    bool StoreEmbedding(int64_t reportId, const std::vector<float>& embedding);

    // List recent reports with embedding vectors attached.
    // Reports without embeddings have has_embedding = false.
    std::vector<SessionReportWithEmbedding> ListRecentWithEmbeddings(
        const std::string& agentId, int limit = 20) const;

private:
    static int64_t NowUnixMs();
    IDataStore* m_store;
};

} // namespace animus::kernel

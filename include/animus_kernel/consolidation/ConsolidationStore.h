#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

namespace animus::kernel {

class IDataStore;

// ============================================================================
// Consolidation watermarks — track what's been processed per source per agent
// ============================================================================

struct ConsolidationWatermark {
    std::string agent_id;
    std::string source;           // "session_turns", "diary_entries", "study_reports"
    int64_t last_processed_id{0};
    int64_t last_run_unix_ms{0};
};

// ============================================================================
// Consolidation run log — records each pipeline execution
// ============================================================================

struct ConsolidationRun {
    int64_t id{0};
    std::string agent_id;
    std::string phase;            // "intake", "layer_consolidation", "perspective_revision"
    int64_t started_unix_ms{0};
    int64_t finished_unix_ms{0};
    std::string status;           // "completed", "failed"
    std::string summary_json;     // JSON with counts, observations, etc.
    std::string error;
};

// ============================================================================
// ConsolidationStore — SQLite-backed consolidation state
// ============================================================================

class ConsolidationStore {
public:
    explicit ConsolidationStore(IDataStore* dataStore);
    ~ConsolidationStore();

    ConsolidationStore(const ConsolidationStore&) = delete;
    ConsolidationStore& operator=(const ConsolidationStore&) = delete;

    // Watermarks
    std::optional<ConsolidationWatermark> GetWatermark(
        const std::string& agentId, const std::string& source) const;
    void SetWatermark(const ConsolidationWatermark& wm);
    std::vector<ConsolidationWatermark> ListWatermarks(
        const std::string& agentId) const;

    // Run log
    int64_t CreateRun(const ConsolidationRun& run);
    bool FinishRun(int64_t runId, const std::string& status,
                   const std::string& summaryJson, const std::string& error);
    std::vector<ConsolidationRun> ListRuns(
        const std::string& agentId, int limit = 20) const;
    std::optional<ConsolidationRun> GetLatestRun(
        const std::string& agentId, const std::string& phase) const;

    // Schema
    void EnsureSchema();

private:
    IDataStore* m_store;
};

} // namespace animus::kernel

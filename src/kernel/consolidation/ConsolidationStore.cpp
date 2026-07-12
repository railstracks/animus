
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/consolidation/ConsolidationStore.h"
#include "animus_kernel/IDataStore.h"

#include <chrono>

namespace animus::kernel {

namespace {

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // anonymous namespace

ConsolidationStore::ConsolidationStore(IDataStore* dataStore) : m_store(dataStore) {
    EnsureSchema();
}

ConsolidationStore::~ConsolidationStore() = default;

void ConsolidationStore::EnsureSchema() {
    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS consolidation_watermarks (
            agent_id TEXT NOT NULL,
            source TEXT NOT NULL,
            last_processed_id INTEGER NOT NULL DEFAULT 0,
            last_run_unix_ms INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (agent_id, source)
        );
    )");

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS consolidation_runs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            agent_id TEXT NOT NULL,
            phase TEXT NOT NULL,
            started_unix_ms INTEGER NOT NULL,
            finished_unix_ms INTEGER NOT NULL DEFAULT 0,
            status TEXT NOT NULL DEFAULT 'running',
            summary_json TEXT NOT NULL DEFAULT '{}',
            error TEXT NOT NULL DEFAULT ''
        );
    )");

    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_consolidation_runs_agent "
        "ON consolidation_runs(agent_id, phase, started_unix_ms DESC);");
}

// ============================================================================
// Watermarks
// ============================================================================

std::optional<ConsolidationWatermark> ConsolidationStore::GetWatermark(
        const std::string& agentId, const std::string& source) const {
    auto stmt = m_store->Prepare(
        "SELECT agent_id, source, last_processed_id, last_run_unix_ms "
        "FROM consolidation_watermarks WHERE agent_id=? AND source=?");
    if (!stmt) return std::nullopt;
    stmt->BindText(1, agentId);
    stmt->BindText(2, source);

    if (stmt->Step()) {
        ConsolidationWatermark wm;
        wm.agent_id = stmt->ColumnText(0);
        wm.source = stmt->ColumnText(1);
        wm.last_processed_id = stmt->ColumnInt64(2);
        wm.last_run_unix_ms = stmt->ColumnInt64(3);
        return wm;
    }
    return std::nullopt;
}

void ConsolidationStore::SetWatermark(const ConsolidationWatermark& wm) {
    std::string sql = schema::UpsertSql(m_store,
        "consolidation_watermarks",
        "agent_id, source, last_processed_id, last_run_unix_ms",
        "agent_id, source",
        "last_processed_id = EXCLUDED.last_processed_id, last_run_unix_ms = EXCLUDED.last_run_unix_ms");
    auto stmt = m_store->Prepare(sql);
    if (!stmt) return;
    stmt->BindText(1, wm.agent_id);
    stmt->BindText(2, wm.source);
    stmt->BindInt64(3, wm.last_processed_id);
    stmt->BindInt64(4, wm.last_run_unix_ms);
    stmt->Step();
}

std::vector<ConsolidationWatermark> ConsolidationStore::ListWatermarks(
        const std::string& agentId) const {
    std::vector<ConsolidationWatermark> result;
    auto stmt = m_store->Prepare(
        "SELECT agent_id, source, last_processed_id, last_run_unix_ms "
        "FROM consolidation_watermarks WHERE agent_id=?");
    if (!stmt) return result;
    stmt->BindText(1, agentId);

    while (stmt->Step()) {
        ConsolidationWatermark wm;
        wm.agent_id = stmt->ColumnText(0);
        wm.source = stmt->ColumnText(1);
        wm.last_processed_id = stmt->ColumnInt64(2);
        wm.last_run_unix_ms = stmt->ColumnInt64(3);
        result.push_back(wm);
    }
    return result;
}

// ============================================================================
// Run log
// ============================================================================

int64_t ConsolidationStore::CreateRun(const ConsolidationRun& run) {
    auto stmt = m_store->Prepare(
        "INSERT INTO consolidation_runs "
        "(agent_id, phase, started_unix_ms, status, summary_json, error) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    if (!stmt) return 0;
    stmt->BindText(1, run.agent_id);
    stmt->BindText(2, run.phase);
    stmt->BindInt64(3, run.started_unix_ms);
    stmt->BindText(4, run.status.empty() ? std::string("running") : run.status);
    stmt->BindText(5, run.summary_json.empty() ? std::string("{}") : run.summary_json);
    stmt->BindText(6, run.error);
    stmt->Step();

    return m_store->LastInsertRowId();
}

bool ConsolidationStore::FinishRun(int64_t runId, const std::string& status,
                                    const std::string& summaryJson,
                                    const std::string& error) {
    auto stmt = m_store->Prepare(
        "UPDATE consolidation_runs SET finished_unix_ms=?, status=?, "
        "summary_json=?, error=? WHERE id=?");
    if (!stmt) return false;
    stmt->BindInt64(1, NowMs());
    stmt->BindText(2, status);
    stmt->BindText(3, summaryJson);
    stmt->BindText(4, error);
    stmt->BindInt64(5, runId);
    stmt->Step();
    return true;
}

std::vector<ConsolidationRun> ConsolidationStore::ListRuns(
        const std::string& agentId, int limit) const {
    std::vector<ConsolidationRun> result;
    auto stmt = m_store->Prepare(
        "SELECT id, agent_id, phase, started_unix_ms, finished_unix_ms, "
        "status, summary_json, error "
        "FROM consolidation_runs WHERE agent_id=? "
        "ORDER BY started_unix_ms DESC LIMIT ?");
    if (!stmt) return result;
    stmt->BindText(1, agentId);
    stmt->BindInt(2, limit);

    while (stmt->Step()) {
        ConsolidationRun r;
        r.id = stmt->ColumnInt64(0);
        r.agent_id = stmt->ColumnText(1);
        r.phase = stmt->ColumnText(2);
        r.started_unix_ms = stmt->ColumnInt64(3);
        r.finished_unix_ms = stmt->ColumnInt64(4);
        r.status = stmt->ColumnText(5);
        r.summary_json = stmt->ColumnText(6);
        r.error = stmt->ColumnText(7);
        result.push_back(r);
    }
    return result;
}

std::optional<ConsolidationRun> ConsolidationStore::GetLatestRun(
        const std::string& agentId, const std::string& phase) const {
    auto stmt = m_store->Prepare(
        "SELECT id, agent_id, phase, started_unix_ms, finished_unix_ms, "
        "status, summary_json, error "
        "FROM consolidation_runs WHERE agent_id=? AND phase=? "
        "ORDER BY started_unix_ms DESC LIMIT 1");
    if (!stmt) return std::nullopt;
    stmt->BindText(1, agentId);
    stmt->BindText(2, phase);

    if (stmt->Step()) {
        ConsolidationRun r;
        r.id = stmt->ColumnInt64(0);
        r.agent_id = stmt->ColumnText(1);
        r.phase = stmt->ColumnText(2);
        r.started_unix_ms = stmt->ColumnInt64(3);
        r.finished_unix_ms = stmt->ColumnInt64(4);
        r.status = stmt->ColumnText(5);
        r.summary_json = stmt->ColumnText(6);
        r.error = stmt->ColumnText(7);
        return r;
    }
    return std::nullopt;
}

} // namespace animus::kernel

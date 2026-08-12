#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/SessionReportStore.h"

#include <chrono>
#include <cstring>

namespace {

std::vector<float> DeserializeEmbedding(const std::vector<uint8_t>& blob, int32_t dim) {
    std::vector<float> result;
    if (blob.empty() || dim <= 0) return result;
    result.resize(dim);
    size_t copySize = std::min(blob.size(), static_cast<size_t>(dim * sizeof(float)));
    std::memcpy(result.data(), blob.data(), copySize);
    return result;
}

std::vector<uint8_t> SerializeEmbedding(const std::vector<float>& embedding) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(embedding.data());
    size_t size = embedding.size() * sizeof(float);
    return std::vector<uint8_t>(bytes, bytes + size);
}

} // anonymous namespace

namespace animus::kernel {

SessionReportStore::SessionReportStore(IDataStore* store)
    : m_store(store) {
    EnsureSchema();
}

void SessionReportStore::EnsureSchema() {
    if (!m_store) return;

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS session_reports (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id INTEGER NOT NULL,
            agent_id TEXT NOT NULL,
            summary TEXT NOT NULL DEFAULT '',
            past_events TEXT NOT NULL DEFAULT '',
            current_activity TEXT NOT NULL DEFAULT '',
            forward_look TEXT NOT NULL DEFAULT '',
            created_at_unix_ms INTEGER NOT NULL,
            updated_at_unix_ms INTEGER NOT NULL,
            UNIQUE(session_id, agent_id)
        );
    )");

    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_session_reports_agent "
        "ON session_reports (agent_id, updated_at_unix_ms DESC)");

    // Migration: add embedding columns for relevance-based retrieval (Ticket 094)
    // Use BYTEA for PostgreSQL, BLOB for SQLite
    std::string blobType = (m_store->Dialect() == DataStoreDialect::PostgreSQL) ? "BYTEA" : "BLOB";
    if (!schema::ColumnExists(m_store, "session_reports", "embedding")) {
        m_store->Exec("ALTER TABLE session_reports ADD COLUMN embedding " + blobType);
    }
    if (!schema::ColumnExists(m_store, "session_reports", "embedding_dim")) {
        m_store->Exec("ALTER TABLE session_reports ADD COLUMN embedding_dim INTEGER DEFAULT 0");
    }
}

SessionReport SessionReportStore::Upsert(int64_t sessionId,
                                          const std::string& agentId,
                                          const std::string& summary,
                                          const std::string& pastEvents,
                                          const std::string& currentActivity,
                                          const std::string& forwardLook) {
    const int64_t now = NowUnixMs();

    auto stmt = m_store->Prepare(
        "INSERT INTO session_reports "
        "(session_id, agent_id, summary, past_events, current_activity, forward_look, "
        "created_at_unix_ms, updated_at_unix_ms) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(session_id, agent_id) DO UPDATE SET "
        "summary = excluded.summary, "
        "past_events = excluded.past_events, "
        "current_activity = excluded.current_activity, "
        "forward_look = excluded.forward_look, "
        "updated_at_unix_ms = excluded.updated_at_unix_ms");
    if (!stmt) return {};

    stmt->BindInt64(1, sessionId);
    stmt->BindText(2, agentId);
    stmt->BindText(3, summary);
    stmt->BindText(4, pastEvents);
    stmt->BindText(5, currentActivity);
    stmt->BindText(6, forwardLook);
    stmt->BindInt64(7, now);
    stmt->BindInt64(8, now);
    stmt->ExecDML();
    stmt->Finalize();

    // Return the upserted row
    auto report = GetForSession(sessionId, agentId);
    return report.value_or(SessionReport{});
}

std::optional<SessionReport> SessionReportStore::GetForSession(
        int64_t sessionId, const std::string& agentId) const {
    auto stmt = m_store->Prepare(
        "SELECT id, session_id, agent_id, summary, past_events, "
        "current_activity, forward_look, created_at_unix_ms, updated_at_unix_ms "
        "FROM session_reports WHERE session_id = ? AND agent_id = ?");
    if (!stmt) return std::nullopt;

    stmt->BindInt64(1, sessionId);
    stmt->BindText(2, agentId);

    if (stmt->Step()) {
        SessionReport r;
        r.id = stmt->ColumnInt64(0);
        r.session_id = stmt->ColumnInt64(1);
        r.agent_id = stmt->ColumnText(2);
        r.summary = stmt->ColumnText(3);
        r.past_events = stmt->ColumnText(4);
        r.current_activity = stmt->ColumnText(5);
        r.forward_look = stmt->ColumnText(6);
        r.created_at_unix_ms = stmt->ColumnInt64(7);
        r.updated_at_unix_ms = stmt->ColumnInt64(8);
        return r;
    }
    return std::nullopt;
}

std::vector<SessionReport> SessionReportStore::ListRecent(
        const std::string& agentId, int limit) const {
    auto stmt = m_store->Prepare(
        "SELECT id, session_id, agent_id, summary, past_events, "
        "current_activity, forward_look, created_at_unix_ms, updated_at_unix_ms "
        "FROM session_reports WHERE agent_id = ? "
        "ORDER BY updated_at_unix_ms DESC LIMIT ?");
    if (!stmt) return {};

    stmt->BindText(1, agentId);
    stmt->BindInt(2, limit);

    std::vector<SessionReport> reports;
    while (stmt->Step()) {
        SessionReport r;
        r.id = stmt->ColumnInt64(0);
        r.session_id = stmt->ColumnInt64(1);
        r.agent_id = stmt->ColumnText(2);
        r.summary = stmt->ColumnText(3);
        r.past_events = stmt->ColumnText(4);
        r.current_activity = stmt->ColumnText(5);
        r.forward_look = stmt->ColumnText(6);
        r.created_at_unix_ms = stmt->ColumnInt64(7);
        r.updated_at_unix_ms = stmt->ColumnInt64(8);
        reports.push_back(std::move(r));
    }
    return reports;
}

std::vector<SessionReport> SessionReportStore::Search(
        const std::string& agentId, const std::string& query, int limit) const {
    // Use LIKE for simple substring matching across all four text fields
    auto stmt = m_store->Prepare(
        "SELECT id, session_id, agent_id, summary, past_events, "
        "current_activity, forward_look, created_at_unix_ms, updated_at_unix_ms "
        "FROM session_reports WHERE agent_id = ? AND ("
        "  summary LIKE ? OR past_events LIKE ? OR "
        "  current_activity LIKE ? OR forward_look LIKE ?"
        ") ORDER BY updated_at_unix_ms DESC LIMIT ?");
    if (!stmt) return {};

    const std::string pattern = "%" + query + "%";
    stmt->BindText(1, agentId);
    stmt->BindText(2, pattern);
    stmt->BindText(3, pattern);
    stmt->BindText(4, pattern);
    stmt->BindText(5, pattern);
    stmt->BindInt(6, limit);

    std::vector<SessionReport> reports;
    while (stmt->Step()) {
        SessionReport r;
        r.id = stmt->ColumnInt64(0);
        r.session_id = stmt->ColumnInt64(1);
        r.agent_id = stmt->ColumnText(2);
        r.summary = stmt->ColumnText(3);
        r.past_events = stmt->ColumnText(4);
        r.current_activity = stmt->ColumnText(5);
        r.forward_look = stmt->ColumnText(6);
        r.created_at_unix_ms = stmt->ColumnInt64(7);
        r.updated_at_unix_ms = stmt->ColumnInt64(8);
        reports.push_back(std::move(r));
    }
    return reports;
}

bool SessionReportStore::Delete(int64_t sessionId, const std::string& agentId) {
    auto stmt = m_store->Prepare(
        "DELETE FROM session_reports WHERE session_id = ? AND agent_id = ?");
    if (!stmt) return false;

    stmt->BindInt64(1, sessionId);
    stmt->BindText(2, agentId);
    stmt->ExecDML();

    return m_store->Changes() > 0;
}

// ---------------------------------------------------------------------------
// Embedding support (Ticket 094)
// ---------------------------------------------------------------------------

bool SessionReportStore::StoreEmbedding(int64_t reportId,
                                         const std::vector<float>& embedding) {
    if (embedding.empty()) return false;

    auto blob = SerializeEmbedding(embedding);
    auto stmt = m_store->Prepare(
        "UPDATE session_reports SET embedding = ?, embedding_dim = ? "
        "WHERE id = ?");
    if (!stmt) return false;

    stmt->BindBlob(1, blob.data(), blob.size());
    stmt->BindInt(2, static_cast<int>(embedding.size()));
    stmt->BindInt64(3, reportId);
    stmt->ExecDML();

    return m_store->Changes() > 0;
}

std::vector<SessionReportWithEmbedding> SessionReportStore::ListWithoutEmbeddings(
        const std::string& agentId, int limit) const {
    auto stmt = m_store->Prepare(
        "SELECT id, session_id, agent_id, summary, past_events, "
        "current_activity, forward_look, created_at_unix_ms, updated_at_unix_ms, "
        "embedding, embedding_dim "
        "FROM session_reports WHERE agent_id = ? "
        "AND (embedding_dim IS NULL OR embedding_dim = 0 OR embedding IS NULL) "
        "ORDER BY updated_at_unix_ms DESC LIMIT ?");
    if (!stmt) return {};

    stmt->BindText(1, agentId);
    stmt->BindInt(2, limit);

    std::vector<SessionReportWithEmbedding> reports;
    while (stmt->Step()) {
        SessionReportWithEmbedding r;
        r.id = stmt->ColumnInt64(0);
        r.session_id = stmt->ColumnInt64(1);
        r.agent_id = stmt->ColumnText(2);
        r.summary = stmt->ColumnText(3);
        r.past_events = stmt->ColumnText(4);
        r.current_activity = stmt->ColumnText(5);
        r.forward_look = stmt->ColumnText(6);
        r.created_at_unix_ms = stmt->ColumnInt64(7);
        r.updated_at_unix_ms = stmt->ColumnInt64(8);
        r.has_embedding = false;
        reports.push_back(std::move(r));
    }
    return reports;
}

std::vector<SessionReportWithEmbedding> SessionReportStore::ListRecentWithEmbeddings(
        const std::string& agentId, int limit) const {
    auto stmt = m_store->Prepare(
        "SELECT id, session_id, agent_id, summary, past_events, "
        "current_activity, forward_look, created_at_unix_ms, updated_at_unix_ms, "
        "embedding, embedding_dim "
        "FROM session_reports WHERE agent_id = ? "
        "ORDER BY updated_at_unix_ms DESC LIMIT ?");
    if (!stmt) return {};

    stmt->BindText(1, agentId);
    stmt->BindInt(2, limit);

    std::vector<SessionReportWithEmbedding> reports;
    while (stmt->Step()) {
        SessionReportWithEmbedding r;
        r.id = stmt->ColumnInt64(0);
        r.session_id = stmt->ColumnInt64(1);
        r.agent_id = stmt->ColumnText(2);
        r.summary = stmt->ColumnText(3);
        r.past_events = stmt->ColumnText(4);
        r.current_activity = stmt->ColumnText(5);
        r.forward_look = stmt->ColumnText(6);
        r.created_at_unix_ms = stmt->ColumnInt64(7);
        r.updated_at_unix_ms = stmt->ColumnInt64(8);

        auto blob = stmt->ColumnBlob(9);
        int32_t dim = static_cast<int32_t>(stmt->ColumnInt64(10));
        if (!blob.empty() && dim > 0) {
            r.embedding = DeserializeEmbedding(blob, dim);
            r.has_embedding = true;
        }

        reports.push_back(std::move(r));
    }
    return reports;
}

std::unordered_map<int64_t, int64_t>
SessionReportStore::GetLastReportTimePerSession(const std::string& agentId) const {
    std::unordered_map<int64_t, int64_t> result;

    // Get the latest updated_at_unix_ms per session_id for this agent.
    // Since Upsert uses ON CONFLICT ... DO UPDATE, there's at most one row
    // per (session_id, agent_id), so a simple SELECT gives us what we need.
    auto stmt = m_store->Prepare(
        "SELECT session_id, updated_at_unix_ms "
        "FROM session_reports WHERE agent_id = ?");
    if (!stmt) return result;

    stmt->BindText(1, agentId);
    while (stmt->Step()) {
        int64_t sessionId = stmt->ColumnInt64(0);
        int64_t updatedAt  = stmt->ColumnInt64(1);
        result[sessionId] = updatedAt;
    }
    return result;
}

int64_t SessionReportStore::NowUnixMs() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

bool SessionReportStore::HasSessionsNeedingReport(const std::string& agentId) const {
    // Find sessions that have at least one turn newer than the session's
    // last report (or no report at all). Excludes consolidation sessions.
    //
    // Query logic:
    //   1. For each session matching agentId, get max(turn.unix_ms)
    //   2. LEFT JOIN session_reports to get the report watermark
    //   3. Keep sessions where max_turn > COALESCE(report.updated_at, 0)

    auto stmt = m_store->Prepare(
        "SELECT s.id, MAX(st.unix_ms), "
        "       COALESCE(sr.updated_at_unix_ms, 0) "
        "FROM sessions s "
        "JOIN session_turns st ON st.session_id = s.id "
        "LEFT JOIN session_reports sr ON sr.session_id = s.id AND sr.agent_id = ? "
        "WHERE s.agent_id = ? "
        "  AND s.session_type NOT LIKE 'consolidation%' "
        "GROUP BY s.id, sr.updated_at_unix_ms "
        "HAVING MAX(st.unix_ms) > COALESCE(sr.updated_at_unix_ms, 0) "
        "LIMIT 1");
    if (!stmt) return false;

    stmt->BindText(1, agentId);
    stmt->BindText(2, agentId);

    return stmt->Step(); // true if at least one row matches
}

std::vector<ISessionStore::UnprocessedTurn> SessionReportStore::GetTurnsNeedingReport(
        const std::string& agentId, int limit) const {
    std::vector<ISessionStore::UnprocessedTurn> result;

    // Select turns from non-consolidation sessions where the turn's unix_ms
    // is newer than the session's last report (or the session has no report).
    auto stmt = m_store->Prepare(
        "SELECT st.session_id, st.turn_id, st.role, st.content, st.token_count, st.unix_ms "
        "FROM session_turns st "
        "JOIN sessions s ON s.id = st.session_id "
        "LEFT JOIN session_reports sr ON sr.session_id = s.id AND sr.agent_id = ? "
        "WHERE s.agent_id = ? "
        "  AND s.session_type NOT LIKE 'consolidation%' "
        "  AND st.content != '' "
        "  AND st.unix_ms > COALESCE(sr.updated_at_unix_ms, 0) "
        "ORDER BY st.unix_ms ASC LIMIT ?");
    if (!stmt) return result;

    stmt->BindText(1, agentId);
    stmt->BindText(2, agentId);
    stmt->BindInt(3, limit);

    while (stmt->Step()) {
        ISessionStore::UnprocessedTurn t;
        t.session_id = stmt->ColumnInt64(0);
        t.turn_id = stmt->ColumnInt64(1);
        t.role = stmt->ColumnText(2);
        t.content = stmt->ColumnText(3);
        t.token_count = !stmt->IsColumnNull(4)
            ? static_cast<std::size_t>(stmt->ColumnInt64(4))
            : 0;
        t.unix_ms = stmt->ColumnInt64(5);
        result.push_back(std::move(t));
    }

    return result;
}

} // namespace animus::kernel

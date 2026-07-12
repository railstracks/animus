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
    stmt->Step();
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
    stmt->Step();

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
    stmt->Step();

    return m_store->Changes() > 0;
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

int64_t SessionReportStore::NowUnixMs() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace animus::kernel

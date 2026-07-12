
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/GallivantingStore.h"

#include "animus_kernel/IDataStore.h"

#include <json/json.h>

#include <algorithm>
#include <chrono>
#include <sstream>

namespace animus::kernel {

namespace {
Json::Value ParseJson(const std::string& json) {
    Json::Value root;
    if (json.empty()) return root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(json);
    std::string errors;
    Json::parseFromStream(builder, stream, &root, &errors);
    return root;
}

std::string SerializeJson(const Json::Value& val) {
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, val);
}

int64_t NowMs() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}
} // anonymous namespace

// ============================================================================
// Construction / Schema
// ============================================================================

GallivantingStore::GallivantingStore(IDataStore* dataStore)
    : m_store(dataStore) {
    EnsureSchema();
}

GallivantingStore::~GallivantingStore() = default;

void GallivantingStore::EnsureSchema() {
    if (!m_store) return;

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS gallivanting_threads (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            agent_id        TEXT NOT NULL DEFAULT 'default',
            name            TEXT NOT NULL,
            description     TEXT NOT NULL DEFAULT '',
            sdt_tags        TEXT NOT NULL DEFAULT '{}',
            prompt_template TEXT NOT NULL DEFAULT '',
            enabled         INTEGER NOT NULL DEFAULT 1,
            created_at_unix_ms INTEGER NOT NULL DEFAULT 0,
            updated_at_unix_ms INTEGER NOT NULL DEFAULT 0
        );
    )");
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_gallivanting_threads_agent "
        "ON gallivanting_threads(agent_id)");

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS gallivanting_sessions (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            thread_id       INTEGER NOT NULL REFERENCES gallivanting_threads(id) ON DELETE CASCADE,
            agent_id        TEXT NOT NULL DEFAULT 'default',
            started_at_unix_ms INTEGER NOT NULL DEFAULT 0,
            duration_ms     INTEGER NOT NULL DEFAULT 0,
            summary         TEXT NOT NULL DEFAULT '',
            outcome         TEXT NOT NULL DEFAULT '',
            sdt_scores      TEXT NOT NULL DEFAULT '{}',
            tools_used      TEXT NOT NULL DEFAULT '[]',
            created_at_unix_ms INTEGER NOT NULL DEFAULT 0
        );
    )");
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_gallivanting_sessions_thread "
        "ON gallivanting_sessions(thread_id)");
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_gallivanting_sessions_agent "
        "ON gallivanting_sessions(agent_id, created_at_unix_ms DESC)");
}

// ============================================================================
// Threads
// ============================================================================

std::vector<GallivantingThread> GallivantingStore::ListThreads(const std::string& agent_id) {
    std::vector<GallivantingThread> result;
    if (!m_store) return result;

    auto stmt = m_store->Prepare(
        "SELECT id, agent_id, name, description, sdt_tags, prompt_template, "
        "enabled, created_at_unix_ms, updated_at_unix_ms "
        "FROM gallivanting_threads WHERE agent_id = ? ORDER BY name");
    if (!stmt) return result;
    stmt->BindText(1, agent_id);

    while (stmt->Step()) {
        GallivantingThread t;
        t.id = stmt->ColumnInt64(0);
        t.agent_id = stmt->ColumnText(1);
        t.name = stmt->ColumnText(2);
        t.description = stmt->ColumnText(3);
        t.sdt_tags_json = stmt->ColumnText(4);
        t.prompt_template = stmt->ColumnText(5);
        t.enabled = stmt->ColumnInt64(6) != 0;
        t.created_at_unix_ms = stmt->ColumnInt64(7);
        t.updated_at_unix_ms = stmt->ColumnInt64(8);
        result.push_back(std::move(t));
    }
    return result;
}

std::optional<GallivantingThread> GallivantingStore::GetThread(int64_t id) {
    if (!m_store) return std::nullopt;

    auto stmt = m_store->Prepare(
        "SELECT id, agent_id, name, description, sdt_tags, prompt_template, "
        "enabled, created_at_unix_ms, updated_at_unix_ms "
        "FROM gallivanting_threads WHERE id = ?");
    if (!stmt) return std::nullopt;
    stmt->BindInt64(1, id);

    if (stmt->Step()) {
        GallivantingThread t;
        t.id = stmt->ColumnInt64(0);
        t.agent_id = stmt->ColumnText(1);
        t.name = stmt->ColumnText(2);
        t.description = stmt->ColumnText(3);
        t.sdt_tags_json = stmt->ColumnText(4);
        t.prompt_template = stmt->ColumnText(5);
        t.enabled = stmt->ColumnInt64(6) != 0;
        t.created_at_unix_ms = stmt->ColumnInt64(7);
        t.updated_at_unix_ms = stmt->ColumnInt64(8);
        return t;
    }
    return std::nullopt;
}

GallivantingThread GallivantingStore::CreateThread(const GallivantingThread& thread) {
    if (!m_store) return {};

    const int64_t now = NowMs();
    auto stmt = m_store->Prepare(
        "INSERT INTO gallivanting_threads "
        "(agent_id, name, description, sdt_tags, prompt_template, enabled, created_at_unix_ms, updated_at_unix_ms) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    if (!stmt) return {};

    stmt->BindText(1, thread.agent_id);
    stmt->BindText(2, thread.name);
    stmt->BindText(3, thread.description);
    stmt->BindText(4, thread.sdt_tags_json.empty() ? std::string("[]") : thread.sdt_tags_json);
    stmt->BindText(5, thread.prompt_template);
    stmt->BindInt64(6, thread.enabled ? 1 : 0);
    stmt->BindInt64(7, now);
    stmt->BindInt64(8, now);
    stmt->Step();

    auto created = GetThread(m_store->LastInsertRowId());
    return created.value_or(GallivantingThread{});
}

bool GallivantingStore::UpdateThread(const GallivantingThread& thread) {
    if (!m_store) return false;

    const int64_t now = NowMs();
    auto stmt = m_store->Prepare(
        "UPDATE gallivanting_threads SET "
        "name = ?, description = ?, sdt_tags = ?, prompt_template = ?, "
        "enabled = ?, updated_at_unix_ms = ? "
        "WHERE id = ?");
    if (!stmt) return false;

    stmt->BindText(1, thread.name);
    stmt->BindText(2, thread.description);
    stmt->BindText(3, thread.sdt_tags_json);
    stmt->BindText(4, thread.prompt_template);
    stmt->BindInt64(5, thread.enabled ? 1 : 0);
    stmt->BindInt64(6, now);
    stmt->BindInt64(7, thread.id);
    return stmt->Step();
}

bool GallivantingStore::DeleteThread(int64_t id) {
    if (!m_store) return false;

    // Delete sessions first (even though CASCADE should handle it)
    DeleteSessionsForThread(id);

    auto stmt = m_store->Prepare("DELETE FROM gallivanting_threads WHERE id = ?");
    if (!stmt) return false;
    stmt->BindInt64(1, id);
    return stmt->Step();
}

// ============================================================================
// Sessions
// ============================================================================

std::vector<GallivantingSession> GallivantingStore::ListSessionsForThread(
        int64_t thread_id, int64_t limit) {
    std::vector<GallivantingSession> result;
    if (!m_store) return result;

    auto stmt = m_store->Prepare(
        "SELECT id, thread_id, agent_id, started_at_unix_ms, duration_ms, "
        "summary, outcome, sdt_scores, tools_used, created_at_unix_ms "
        "FROM gallivanting_sessions WHERE thread_id = ? "
        "ORDER BY created_at_unix_ms DESC LIMIT ?");
    if (!stmt) return result;
    stmt->BindInt64(1, thread_id);
    stmt->BindInt64(2, limit);

    while (stmt->Step()) {
        GallivantingSession s;
        s.id = stmt->ColumnInt64(0);
        s.thread_id = stmt->ColumnInt64(1);
        s.agent_id = stmt->ColumnText(2);
        s.started_at_unix_ms = stmt->ColumnInt64(3);
        s.duration_ms = stmt->ColumnInt64(4);
        s.summary = stmt->ColumnText(5);
        s.outcome = stmt->ColumnText(6);
        s.sdt_scores_json = stmt->ColumnText(7);
        s.tools_used_json = stmt->ColumnText(8);
        s.created_at_unix_ms = stmt->ColumnInt64(9);
        result.push_back(std::move(s));
    }
    return result;
}

std::vector<GallivantingSession> GallivantingStore::ListRecentSessions(
        const std::string& agent_id, int64_t limit) {
    std::vector<GallivantingSession> result;
    if (!m_store) return result;

    auto stmt = m_store->Prepare(
        "SELECT id, thread_id, agent_id, started_at_unix_ms, duration_ms, "
        "summary, outcome, sdt_scores, tools_used, created_at_unix_ms "
        "FROM gallivanting_sessions WHERE agent_id = ? "
        "ORDER BY created_at_unix_ms DESC LIMIT ?");
    if (!stmt) return result;
    stmt->BindText(1, agent_id);
    stmt->BindInt64(2, limit);

    while (stmt->Step()) {
        GallivantingSession s;
        s.id = stmt->ColumnInt64(0);
        s.thread_id = stmt->ColumnInt64(1);
        s.agent_id = stmt->ColumnText(2);
        s.started_at_unix_ms = stmt->ColumnInt64(3);
        s.duration_ms = stmt->ColumnInt64(4);
        s.summary = stmt->ColumnText(5);
        s.outcome = stmt->ColumnText(6);
        s.sdt_scores_json = stmt->ColumnText(7);
        s.tools_used_json = stmt->ColumnText(8);
        s.created_at_unix_ms = stmt->ColumnInt64(9);
        result.push_back(std::move(s));
    }
    return result;
}

GallivantingSession GallivantingStore::CreateSession(const GallivantingSession& session) {
    if (!m_store) return {};

    const int64_t now = NowMs();
    auto stmt = m_store->Prepare(
        "INSERT INTO gallivanting_sessions "
        "(thread_id, agent_id, started_at_unix_ms, duration_ms, "
        "summary, outcome, sdt_scores, tools_used, created_at_unix_ms) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!stmt) return {};

    stmt->BindInt64(1, session.thread_id);
    stmt->BindText(2, session.agent_id);
    stmt->BindInt64(3, session.started_at_unix_ms);
    stmt->BindInt64(4, session.duration_ms);
    stmt->BindText(5, session.summary);
    stmt->BindText(6, session.outcome);
    stmt->BindText(7, session.sdt_scores_json.empty() ? std::string("{}") : session.sdt_scores_json);
    stmt->BindText(8, session.tools_used_json.empty() ? std::string("[]") : session.tools_used_json);
    stmt->BindInt64(9, now);
    stmt->Step();

    GallivantingSession created = session;
    created.id = m_store->LastInsertRowId();
    created.created_at_unix_ms = now;
    return created;
}

bool GallivantingStore::DeleteSessionsForThread(int64_t thread_id) {
    if (!m_store) return false;

    auto stmt = m_store->Prepare("DELETE FROM gallivanting_sessions WHERE thread_id = ?");
    if (!stmt) return false;
    stmt->BindInt64(1, thread_id);
    return stmt->Step();
}

// ============================================================================
// SDT Balance
// ============================================================================

SdtBalance GallivantingStore::GetSdtBalance(const std::string& agent_id, int64_t since_unix_ms) {
    SdtBalance balance;
    if (!m_store) return balance;

    auto stmt = m_store->Prepare(
        "SELECT sdt_scores FROM gallivanting_sessions "
        "WHERE agent_id = ? AND created_at_unix_ms >= ?");
    if (!stmt) return balance;
    stmt->BindText(1, agent_id);
    stmt->BindInt64(2, since_unix_ms);

    while (stmt->Step()) {
        const std::string json = stmt->ColumnText(0);
        const auto scores = ParseJson(json);
        if (!scores.isObject()) continue;

        balance.autonomy += scores.get("autonomy", 0.0).asDouble();
        balance.competence += scores.get("competence", 0.0).asDouble();
        balance.relatedness += scores.get("relatedness", 0.0).asDouble();
        balance.personal_development += scores.get("personal_development", 0.0).asDouble();
        balance.relaxation += scores.get("relaxation", 0.0).asDouble();
        balance.meaning += scores.get("meaning", 0.0).asDouble();
        balance.session_count++;
    }

    if (balance.session_count > 0) {
        const double n = static_cast<double>(balance.session_count);
        balance.autonomy /= n;
        balance.competence /= n;
        balance.relatedness /= n;
        balance.personal_development /= n;
        balance.relaxation /= n;
        balance.meaning /= n;
    }

    return balance;
}

} // namespace animus::kernel

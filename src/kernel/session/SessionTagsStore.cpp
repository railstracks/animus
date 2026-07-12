
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/SessionTagsStore.h"

#include <chrono>

namespace animus::kernel {

SessionTagsStore::SessionTagsStore(IDataStore* store)
    : m_store(store) {
    EnsureSchema();
}

void SessionTagsStore::EnsureSchema() {
    if (!m_store) return;

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS session_tags (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_key TEXT NOT NULL,
            agent_id TEXT NOT NULL,
            tag TEXT NOT NULL,
            source TEXT NOT NULL DEFAULT 'agent',
            created_at_unix_ms INTEGER NOT NULL
        );
    )");

    m_store->Exec(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_session_tags_unique "
        "ON session_tags (session_key, agent_id, tag, source)");

    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_session_tags_session "
        "ON session_tags (session_key)");

    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_session_tags_tag "
        "ON session_tags (tag)");
}

SessionTag SessionTagsStore::Add(const std::string& sessionKey,
                                  const std::string& agentId,
                                  const std::string& tag,
                                  const std::string& source) {
    const int64_t now = NowUnixMs();

    // Check if it already exists
    auto check = m_store->Prepare(
        "SELECT id, session_key, agent_id, tag, source, created_at_unix_ms "
        "FROM session_tags WHERE session_key = ? AND agent_id = ? AND tag = ? AND source = ?");
    if (check) {
        check->BindText(1, sessionKey);
        check->BindText(2, agentId);
        check->BindText(3, tag);
        check->BindText(4, source);
        if (check->Step()) {
            SessionTag existing;
            existing.id = check->ColumnInt64(0);
            existing.session_key = check->ColumnText(1);
            existing.tag = check->ColumnText(3);
            existing.source = check->ColumnText(4);
            existing.created_at_unix_ms = check->ColumnInt64(5);
            return existing;
        }
    }

    auto stmt = m_store->Prepare(
        "INSERT INTO session_tags (session_key, agent_id, tag, source, created_at_unix_ms) "
        "VALUES (?, ?, ?, ?, ?)");
    if (!stmt) return {};

    stmt->BindText(1, sessionKey);
    stmt->BindText(2, agentId);
    stmt->BindText(3, tag);
    stmt->BindText(4, source);
    stmt->BindInt64(5, now);
    stmt->Step();

    SessionTag result;
    result.id = m_store->LastInsertRowId();
    result.session_key = sessionKey;
    result.tag = tag;
    result.source = source;
    result.created_at_unix_ms = now;
    return result;
}

bool SessionTagsStore::Remove(const std::string& sessionKey,
                               const std::string& agentId,
                               const std::string& tag) {
    auto stmt = m_store->Prepare(
        "DELETE FROM session_tags WHERE session_key = ? AND agent_id = ? AND tag = ?");
    if (!stmt) return false;

    stmt->BindText(1, sessionKey);
    stmt->BindText(2, agentId);
    stmt->BindText(3, tag);
    stmt->Step();

    return m_store->Changes() > 0;
}

std::vector<SessionTag> SessionTagsStore::ListForSession(
        const std::string& sessionKey) const {
    auto stmt = m_store->Prepare(
        "SELECT id, session_key, agent_id, tag, source, created_at_unix_ms "
        "FROM session_tags WHERE session_key = ? "
        "ORDER BY created_at_unix_ms ASC");
    if (!stmt) return {};

    stmt->BindText(1, sessionKey);

    std::vector<SessionTag> tags;
    while (stmt->Step()) {
        SessionTag t;
        t.id = stmt->ColumnInt64(0);
        t.session_key = stmt->ColumnText(1);
        t.tag = stmt->ColumnText(3);
        t.source = stmt->ColumnText(4);
        t.created_at_unix_ms = stmt->ColumnInt64(5);
        tags.push_back(std::move(t));
    }
    return tags;
}

std::vector<SessionTag> SessionTagsStore::ListForSessionBySource(
        const std::string& sessionKey,
        const std::string& source) const {
    auto stmt = m_store->Prepare(
        "SELECT id, session_key, agent_id, tag, source, created_at_unix_ms "
        "FROM session_tags WHERE session_key = ? AND source = ? "
        "ORDER BY created_at_unix_ms ASC");
    if (!stmt) return {};

    stmt->BindText(1, sessionKey);
    stmt->BindText(2, source);

    std::vector<SessionTag> tags;
    while (stmt->Step()) {
        SessionTag t;
        t.id = stmt->ColumnInt64(0);
        t.session_key = stmt->ColumnText(1);
        t.tag = stmt->ColumnText(3);
        t.source = stmt->ColumnText(4);
        t.created_at_unix_ms = stmt->ColumnInt64(5);
        tags.push_back(std::move(t));
    }
    return tags;
}

bool SessionTagsStore::HasTag(const std::string& sessionKey,
                               const std::string& tag) const {
    auto stmt = m_store->Prepare(
        "SELECT COUNT(*) FROM session_tags WHERE session_key = ? AND tag = ?");
    if (!stmt) return false;

    stmt->BindText(1, sessionKey);
    stmt->BindText(2, tag);
    if (stmt->Step()) {
        return stmt->ColumnInt64(0) > 0;
    }
    return false;
}

std::vector<std::string> SessionTagsStore::FindSessionsWithTags(
        const std::vector<std::string>& tags) const {
    if (tags.empty()) return {};

    // Build dynamic IN clause
    std::string placeholders;
    for (size_t i = 0; i < tags.size(); ++i) {
        if (i > 0) placeholders += ",";
        placeholders += "?";
    }

    auto stmt = m_store->Prepare(
        "SELECT DISTINCT session_key FROM session_tags WHERE tag IN (" +
        placeholders + ")");
    if (!stmt) return {};

    for (size_t i = 0; i < tags.size(); ++i) {
        stmt->BindText(static_cast<int>(i + 1), tags[i]);
    }

    std::vector<std::string> keys;
    while (stmt->Step()) {
        keys.push_back(stmt->ColumnText(0));
    }
    return keys;
}

std::size_t SessionTagsStore::RemoveAutoTags(const std::string& sessionKey) {
    auto stmt = m_store->Prepare(
        "DELETE FROM session_tags WHERE session_key = ? AND source = 'auto'");
    if (!stmt) return 0;

    stmt->BindText(1, sessionKey);
    stmt->Step();

    return static_cast<std::size_t>(m_store->Changes());
}

int64_t SessionTagsStore::NowUnixMs() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace animus::kernel

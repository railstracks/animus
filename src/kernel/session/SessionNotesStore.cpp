
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/SessionNotesStore.h"

#include <chrono>

namespace animus::kernel {

SessionNotesStore::SessionNotesStore(IDataStore* store)
    : m_store(store) {
    EnsureSchema();
}

void SessionNotesStore::EnsureSchema() {
    if (!m_store) return;

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS session_notes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_key TEXT NOT NULL,
            agent_id TEXT NOT NULL,
            bullet TEXT NOT NULL,
            sort_order INTEGER NOT NULL DEFAULT 0,
            created_at_unix_ms INTEGER NOT NULL,
            updated_at_unix_ms INTEGER NOT NULL
        );
    )");

    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_session_notes_session "
        "ON session_notes (session_key, agent_id, sort_order)");
}

SessionNote SessionNotesStore::Create(const std::string& sessionKey,
                                       const std::string& agentId,
                                       const std::string& bullet,
                                       int sortOrder) {
    const int64_t now = NowUnixMs();

    auto stmt = m_store->Prepare(
        "INSERT INTO session_notes (session_key, agent_id, bullet, sort_order, "
        "created_at_unix_ms, updated_at_unix_ms) VALUES (?, ?, ?, ?, ?, ?)");
    if (!stmt) return {};

    stmt->BindText(1, sessionKey);
    stmt->BindText(2, agentId);
    stmt->BindText(3, bullet);
    stmt->BindInt(4, sortOrder);
    stmt->BindInt64(5, now);
    stmt->BindInt64(6, now);
    stmt->Step();
    stmt->Finalize();

    SessionNote note;
    note.id = m_store->LastInsertRowId();
    note.session_key = sessionKey;
    note.agent_id = agentId;
    note.bullet = bullet;
    note.sort_order = sortOrder;
    note.created_at_unix_ms = now;
    note.updated_at_unix_ms = now;
    return note;
}

std::vector<SessionNote> SessionNotesStore::ListForSession(
        const std::string& sessionKey, const std::string& agentId) const {
    auto stmt = m_store->Prepare(
        "SELECT id, session_key, agent_id, bullet, sort_order, "
        "created_at_unix_ms, updated_at_unix_ms "
        "FROM session_notes "
        "WHERE session_key = ? AND agent_id = ? "
        "ORDER BY sort_order ASC");
    if (!stmt) return {};

    stmt->BindText(1, sessionKey);
    stmt->BindText(2, agentId);

    std::vector<SessionNote> notes;
    while (stmt->Step()) {
        SessionNote n;
        n.id = stmt->ColumnInt64(0);
        n.session_key = stmt->ColumnText(1);
        n.agent_id = stmt->ColumnText(2);
        n.bullet = stmt->ColumnText(3);
        n.sort_order = static_cast<int>(stmt->ColumnInt64(4));
        n.created_at_unix_ms = stmt->ColumnInt64(5);
        n.updated_at_unix_ms = stmt->ColumnInt64(6);
        notes.push_back(std::move(n));
    }
    return notes;
}

std::optional<SessionNote> SessionNotesStore::GetById(int64_t noteId,
                                                       const std::string& agentId) const {
    auto stmt = m_store->Prepare(
        "SELECT id, session_key, agent_id, bullet, sort_order, "
        "created_at_unix_ms, updated_at_unix_ms "
        "FROM session_notes WHERE id = ? AND agent_id = ?");
    if (!stmt) return std::nullopt;

    stmt->BindInt64(1, noteId);
    stmt->BindText(2, agentId);

    if (stmt->Step()) {
        SessionNote n;
        n.id = stmt->ColumnInt64(0);
        n.session_key = stmt->ColumnText(1);
        n.agent_id = stmt->ColumnText(2);
        n.bullet = stmt->ColumnText(3);
        n.sort_order = static_cast<int>(stmt->ColumnInt64(4));
        n.created_at_unix_ms = stmt->ColumnInt64(5);
        n.updated_at_unix_ms = stmt->ColumnInt64(6);
        return n;
    }
    return std::nullopt;
}

bool SessionNotesStore::Update(int64_t noteId, const std::string& agentId,
                                const std::string& newBullet) {
    auto stmt = m_store->Prepare(
        "UPDATE session_notes SET bullet = ?, updated_at_unix_ms = ? "
        "WHERE id = ? AND agent_id = ?");
    if (!stmt) return false;

    stmt->BindText(1, newBullet);
    stmt->BindInt64(2, NowUnixMs());
    stmt->BindInt64(3, noteId);
    stmt->BindText(4, agentId);
    stmt->Step();

    // Check rows affected
    return m_store->Changes() > 0;
}

bool SessionNotesStore::Delete(int64_t noteId, const std::string& agentId) {
    auto stmt = m_store->Prepare(
        "DELETE FROM session_notes WHERE id = ? AND agent_id = ?");
    if (!stmt) return false;

    stmt->BindInt64(1, noteId);
    stmt->BindText(2, agentId);
    stmt->Step();

    return m_store->Changes() > 0;
}

bool SessionNotesStore::Reorder(const std::vector<int64_t>& noteIds,
                                 const std::string& agentId) {
    for (size_t i = 0; i < noteIds.size(); ++i) {
        auto stmt = m_store->Prepare(
            "UPDATE session_notes SET sort_order = ?, updated_at_unix_ms = ? "
            "WHERE id = ? AND agent_id = ?");
        if (!stmt) return false;

        stmt->BindInt(1, static_cast<int>(i));
        stmt->BindInt64(2, NowUnixMs());
        stmt->BindInt64(3, noteIds[i]);
        stmt->BindText(4, agentId);
        stmt->Step();
    }
    return true;
}

int64_t SessionNotesStore::CountForSession(const std::string& sessionKey,
                                            const std::string& agentId) const {
    auto stmt = m_store->Prepare(
        "SELECT COUNT(*) FROM session_notes WHERE session_key = ? AND agent_id = ?");
    if (!stmt) return 0;

    stmt->BindText(1, sessionKey);
    stmt->BindText(2, agentId);
    if (stmt->Step()) {
        return stmt->ColumnInt64(0);
    }
    return 0;
}

int SessionNotesStore::NextSortOrder(const std::string& sessionKey,
                                      const std::string& agentId) const {
    auto stmt = m_store->Prepare(
        "SELECT COALESCE(MAX(sort_order), -1) + 1 FROM session_notes "
        "WHERE session_key = ? AND agent_id = ?");
    if (!stmt) return 0;

    stmt->BindText(1, sessionKey);
    stmt->BindText(2, agentId);
    if (stmt->Step()) {
        return static_cast<int>(stmt->ColumnInt64(0));
    }
    return 0;
}

int64_t SessionNotesStore::NowUnixMs() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace animus::kernel

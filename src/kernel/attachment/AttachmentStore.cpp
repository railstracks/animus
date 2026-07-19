#include "animus_kernel/AttachmentStore.h"
#include "animus_kernel/SchemaHelpers.h"

#include <json/json.h>
#include <sstream>
#include <iostream>

namespace animus::kernel {

AttachmentStore::AttachmentStore(IDataStore* store)
    : m_store(store) {
    EnsureSchema();
}

void AttachmentStore::EnsureSchema() {
    if (!m_store) return;

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS session_attachments (
            id TEXT PRIMARY KEY,
            turn_id INTEGER NOT NULL,
            session_key TEXT NOT NULL,
            filename TEXT NOT NULL,
            mime_type TEXT NOT NULL,
            filepath TEXT NOT NULL DEFAULT '',
            data_b64 TEXT NOT NULL DEFAULT '',
            size_bytes INTEGER NOT NULL DEFAULT 0,
            created_at INTEGER NOT NULL DEFAULT 0
        );
    )");

    m_store->Exec("CREATE INDEX IF NOT EXISTS idx_attachments_turn ON session_attachments(turn_id)");
    m_store->Exec("CREATE INDEX IF NOT EXISTS idx_attachments_session ON session_attachments(session_key)");
}

bool AttachmentStore::Save(const Attachment& att) {
    if (!m_store) return false;

    auto stmt = m_store->Prepare(
        "INSERT INTO session_attachments "
        "(id, turn_id, session_key, filename, mime_type, filepath, data_b64, size_bytes, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");

    if (!stmt) return false;

    stmt->BindText(1, att.id);
    stmt->BindInt64(2, static_cast<int64_t>(att.turn_id));
    stmt->BindText(3, att.session_key);
    stmt->BindText(4, att.filename);
    stmt->BindText(5, att.mime_type);
    stmt->BindText(6, att.filepath);
    stmt->BindText(7, att.data_b64);
    stmt->BindInt64(8, att.size_bytes);
    stmt->BindInt64(9, att.created_at);

    stmt->Step();
    return schema::DidAffectRows(m_store);
}

std::optional<Attachment> AttachmentStore::GetById(const std::string& id) {
    if (!m_store) return std::nullopt;

    auto stmt = m_store->Prepare(
        "SELECT id, turn_id, session_key, filename, mime_type, filepath, data_b64, size_bytes, created_at "
        "FROM session_attachments WHERE id=?");
    if (!stmt) return std::nullopt;

    stmt->BindText(1, id);
    if (!stmt->Step()) return std::nullopt;

    Attachment att;
    att.id = stmt->ColumnText(0);
    att.turn_id = static_cast<std::uint64_t>(stmt->ColumnInt64(1));
    att.session_key = stmt->ColumnText(2);
    att.filename = stmt->ColumnText(3);
    att.mime_type = stmt->ColumnText(4);
    att.filepath = stmt->ColumnText(5);
    att.data_b64 = stmt->ColumnText(6);
    att.size_bytes = stmt->ColumnInt64(7);
    att.created_at = stmt->ColumnInt64(8);
    return att;
}

std::vector<Attachment> AttachmentStore::GetForTurn(std::uint64_t turnId) {
    std::vector<Attachment> results;
    if (!m_store) return results;

    auto stmt = m_store->Prepare(
        "SELECT id, turn_id, session_key, filename, mime_type, filepath, data_b64, size_bytes, created_at "
        "FROM session_attachments WHERE turn_id=? ORDER BY created_at ASC");
    if (!stmt) return results;

    stmt->BindInt64(1, static_cast<int64_t>(turnId));
    while (stmt->Step()) {
        Attachment att;
        att.id = stmt->ColumnText(0);
        att.turn_id = static_cast<std::uint64_t>(stmt->ColumnInt64(1));
        att.session_key = stmt->ColumnText(2);
        att.filename = stmt->ColumnText(3);
        att.mime_type = stmt->ColumnText(4);
        att.filepath = stmt->ColumnText(5);
        att.data_b64 = stmt->ColumnText(6);
        att.size_bytes = stmt->ColumnInt64(7);
        att.created_at = stmt->ColumnInt64(8);
        results.push_back(std::move(att));
    }
    return results;
}

std::vector<Attachment> AttachmentStore::GetForSession(const std::string& sessionKey) {
    std::vector<Attachment> results;
    if (!m_store) return results;

    auto stmt = m_store->Prepare(
        "SELECT id, turn_id, session_key, filename, mime_type, filepath, data_b64, size_bytes, created_at "
        "FROM session_attachments WHERE session_key=? ORDER BY created_at ASC");
    if (!stmt) return results;

    stmt->BindText(1, sessionKey);
    while (stmt->Step()) {
        Attachment att;
        att.id = stmt->ColumnText(0);
        att.turn_id = static_cast<std::uint64_t>(stmt->ColumnInt64(1));
        att.session_key = stmt->ColumnText(2);
        att.filename = stmt->ColumnText(3);
        att.mime_type = stmt->ColumnText(4);
        att.filepath = stmt->ColumnText(5);
        att.data_b64 = stmt->ColumnText(6);
        att.size_bytes = stmt->ColumnInt64(7);
        att.created_at = stmt->ColumnInt64(8);
        results.push_back(std::move(att));
    }
    return results;
}

bool AttachmentStore::DeleteForTurn(std::uint64_t turnId) {
    if (!m_store) return false;
    auto stmt = m_store->Prepare("DELETE FROM session_attachments WHERE turn_id=?");
    if (!stmt) return false;
    stmt->BindInt64(1, static_cast<int64_t>(turnId));
    stmt->Step();
    return schema::DidAffectRows(m_store);
}

} // namespace animus::kernel
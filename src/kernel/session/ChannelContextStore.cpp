#include "animus_kernel/ChannelContextStore.h"
#include "animus_kernel/SchemaHelpers.h"

#include <chrono>

namespace animus::kernel {

ChannelContextStore::ChannelContextStore(IDataStore* store)
    : m_store(store) {
    EnsureSchema();
}

void ChannelContextStore::EnsureSchema() {
    if (!m_store) return;

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS channel_arrivals (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_key TEXT NOT NULL,
            agent_id TEXT NOT NULL,
            channel_type TEXT NOT NULL DEFAULT '',
            channel_name TEXT NOT NULL DEFAULT '',
            platform_id TEXT NOT NULL DEFAULT '',
            message_type TEXT NOT NULL DEFAULT '',
            author_id TEXT NOT NULL DEFAULT '',
            author_handle TEXT NOT NULL DEFAULT '',
            delivery TEXT NOT NULL DEFAULT '',
            peer_id TEXT NOT NULL DEFAULT '',
            post_id TEXT NOT NULL DEFAULT '',
            group_id TEXT NOT NULL DEFAULT '',
            email_thread_id TEXT NOT NULL DEFAULT '',
            source_message_id TEXT NOT NULL DEFAULT '',
            reply_parent_id TEXT NOT NULL DEFAULT '',
            thread_root_id TEXT NOT NULL DEFAULT '',
            created_at_unix_ms INTEGER NOT NULL,
            consumed INTEGER NOT NULL DEFAULT 0
        );
    )");

    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_channel_arrivals_session "
        "ON channel_arrivals (session_key, agent_id, id)");

    m_store->Exec(
        "CREATE TABLE IF NOT EXISTS channel_seen_uris ("
        "session_key TEXT NOT NULL, "
        "agent_id TEXT NOT NULL, "
        "uri TEXT NOT NULL, "
        "created_at_unix_ms INTEGER NOT NULL, "
        "PRIMARY KEY (session_key, agent_id, uri))");
}

std::string ChannelContextStore::NormalizeSessionKey(const std::string& key) {
    // Raw dispatch keys: "channel:chat:discord:1". SessionKey::ToString():
    // "channel:chat:discord:1||" (connector|conversation|thread with empty
    // trailing components). Strip trailing '|' so both forms canonicalize
    // identically; non-empty trailing components are unaffected.
    std::string k = key;
    while (!k.empty() && k.back() == '|') k.pop_back();
    return k;
}

ChannelArrival ChannelContextStore::AddArrival(const ChannelArrival& arrival) {
    const int64_t now = NowUnixMs();

    auto stmt = m_store->Prepare(
        "INSERT INTO channel_arrivals (session_key, agent_id, channel_type, "
        "channel_name, platform_id, message_type, author_id, author_handle, "
        "delivery, peer_id, post_id, group_id, email_thread_id, "
        "source_message_id, reply_parent_id, thread_root_id, "
        "created_at_unix_ms, consumed) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)");
    if (!stmt) return {};

    stmt->BindText(1, NormalizeSessionKey(arrival.session_key));
    stmt->BindText(2, arrival.agent_id);
    stmt->BindText(3, arrival.channel_type);
    stmt->BindText(4, arrival.channel_name);
    stmt->BindText(5, arrival.platform_id);
    stmt->BindText(6, arrival.message_type);
    stmt->BindText(7, arrival.author_id);
    stmt->BindText(8, arrival.author_handle);
    stmt->BindText(9, arrival.delivery);
    stmt->BindText(10, arrival.peer_id);
    stmt->BindText(11, arrival.post_id);
    stmt->BindText(12, arrival.group_id);
    stmt->BindText(13, arrival.email_thread_id);
    stmt->BindText(14, arrival.source_message_id);
    stmt->BindText(15, arrival.reply_parent_id);
    stmt->BindText(16, arrival.thread_root_id);
    stmt->BindInt64(17, now);
    stmt->ExecDML();
    stmt->Finalize();

    ChannelArrival stored = arrival;
    stored.id = m_store->LastInsertRowId();
    stored.created_at_unix_ms = now;
    stored.consumed = false;
    return stored;
}

std::vector<ChannelArrival> ChannelContextStore::PendingArrivals(
        const std::string& sessionKey, const std::string& agentId) const {
    auto stmt = m_store->Prepare(
        "SELECT id, session_key, agent_id, channel_type, channel_name, "
        "platform_id, message_type, author_id, author_handle, delivery, "
        "peer_id, post_id, group_id, email_thread_id, source_message_id, "
        "reply_parent_id, thread_root_id, created_at_unix_ms, consumed "
        "FROM channel_arrivals "
        "WHERE session_key = ? AND agent_id = ? AND consumed = 0 "
        "ORDER BY id ASC");
    if (!stmt) return {};

    stmt->BindText(1, NormalizeSessionKey(sessionKey));
    stmt->BindText(2, agentId);

    std::vector<ChannelArrival> arrivals;
    while (stmt->Step()) {
        ChannelArrival a;
        a.id = stmt->ColumnInt64(0);
        a.session_key = stmt->ColumnText(1);
        a.agent_id = stmt->ColumnText(2);
        a.channel_type = stmt->ColumnText(3);
        a.channel_name = stmt->ColumnText(4);
        a.platform_id = stmt->ColumnText(5);
        a.message_type = stmt->ColumnText(6);
        a.author_id = stmt->ColumnText(7);
        a.author_handle = stmt->ColumnText(8);
        a.delivery = stmt->ColumnText(9);
        a.peer_id = stmt->ColumnText(10);
        a.post_id = stmt->ColumnText(11);
        a.group_id = stmt->ColumnText(12);
        a.email_thread_id = stmt->ColumnText(13);
        a.source_message_id = stmt->ColumnText(14);
        a.reply_parent_id = stmt->ColumnText(15);
        a.thread_root_id = stmt->ColumnText(16);
        a.created_at_unix_ms = stmt->ColumnInt64(17);
        a.consumed = stmt->ColumnInt64(18) != 0;
        arrivals.push_back(std::move(a));
    }
    return arrivals;
}

std::optional<ChannelArrival> ChannelContextStore::LatestArrival(
        const std::string& sessionKey, const std::string& agentId) const {
    auto stmt = m_store->Prepare(
        "SELECT id, session_key, agent_id, channel_type, channel_name, "
        "platform_id, message_type, author_id, author_handle, delivery, "
        "peer_id, post_id, group_id, email_thread_id, source_message_id, "
        "reply_parent_id, thread_root_id, created_at_unix_ms, consumed "
        "FROM channel_arrivals "
        "WHERE session_key = ? AND agent_id = ? "
        "ORDER BY id DESC LIMIT 1");
    if (!stmt) return std::nullopt;

    stmt->BindText(1, NormalizeSessionKey(sessionKey));
    stmt->BindText(2, agentId);

    if (!stmt->Step()) return std::nullopt;

    ChannelArrival a;
    a.id = stmt->ColumnInt64(0);
    a.session_key = stmt->ColumnText(1);
    a.agent_id = stmt->ColumnText(2);
    a.channel_type = stmt->ColumnText(3);
    a.channel_name = stmt->ColumnText(4);
    a.platform_id = stmt->ColumnText(5);
    a.message_type = stmt->ColumnText(6);
    a.author_id = stmt->ColumnText(7);
    a.author_handle = stmt->ColumnText(8);
    a.delivery = stmt->ColumnText(9);
    a.peer_id = stmt->ColumnText(10);
    a.post_id = stmt->ColumnText(11);
    a.group_id = stmt->ColumnText(12);
    a.email_thread_id = stmt->ColumnText(13);
    a.source_message_id = stmt->ColumnText(14);
    a.reply_parent_id = stmt->ColumnText(15);
    a.thread_root_id = stmt->ColumnText(16);
    a.created_at_unix_ms = stmt->ColumnInt64(17);
    a.consumed = stmt->ColumnInt64(18) != 0;
    return a;
}

std::vector<ChannelArrival> ChannelContextStore::RecentArrivals(
        const std::string& sessionKey, const std::string& agentId,
        int limit) const {
    auto stmt = m_store->Prepare(
        "SELECT id, session_key, agent_id, channel_type, channel_name, "
        "platform_id, message_type, author_id, author_handle, delivery, "
        "peer_id, post_id, group_id, email_thread_id, source_message_id, "
        "reply_parent_id, thread_root_id, created_at_unix_ms, consumed "
        "FROM channel_arrivals "
        "WHERE session_key = ? AND agent_id = ? "
        "ORDER BY id DESC LIMIT ?");
    if (!stmt) return {};

    stmt->BindText(1, NormalizeSessionKey(sessionKey));
    stmt->BindText(2, agentId);
    stmt->BindInt64(3, limit);

    std::vector<ChannelArrival> arrivals;
    while (stmt->Step()) {
        ChannelArrival a;
        a.id = stmt->ColumnInt64(0);
        a.session_key = stmt->ColumnText(1);
        a.agent_id = stmt->ColumnText(2);
        a.channel_type = stmt->ColumnText(3);
        a.channel_name = stmt->ColumnText(4);
        a.platform_id = stmt->ColumnText(5);
        a.message_type = stmt->ColumnText(6);
        a.author_id = stmt->ColumnText(7);
        a.author_handle = stmt->ColumnText(8);
        a.delivery = stmt->ColumnText(9);
        a.peer_id = stmt->ColumnText(10);
        a.post_id = stmt->ColumnText(11);
        a.group_id = stmt->ColumnText(12);
        a.email_thread_id = stmt->ColumnText(13);
        a.source_message_id = stmt->ColumnText(14);
        a.reply_parent_id = stmt->ColumnText(15);
        a.thread_root_id = stmt->ColumnText(16);
        a.created_at_unix_ms = stmt->ColumnInt64(17);
        a.consumed = stmt->ColumnInt64(18) != 0;
        arrivals.push_back(std::move(a));
    }
    return arrivals;
}

bool ChannelContextStore::MarkConsumed(int64_t arrivalId) {
    auto stmt = m_store->Prepare(
        "UPDATE channel_arrivals SET consumed = 1 WHERE id = ?");
    if (!stmt) return false;

    stmt->BindInt64(1, arrivalId);
    stmt->ExecDML();
    return m_store->Changes() > 0;
}

int ChannelContextStore::MarkAllConsumed(const std::string& sessionKey,
                                          const std::string& agentId) {
    auto stmt = m_store->Prepare(
        "UPDATE channel_arrivals SET consumed = 1 "
        "WHERE session_key = ? AND agent_id = ? AND consumed = 0");
    if (!stmt) return 0;

    stmt->BindText(1, NormalizeSessionKey(sessionKey));
    stmt->BindText(2, agentId);
    stmt->ExecDML();
    return static_cast<int>(m_store->Changes());
}

int ChannelContextStore::Prune(const std::string& sessionKey,
                                const std::string& agentId, int keepLast) {
    auto stmt = m_store->Prepare(
        "DELETE FROM channel_arrivals WHERE id IN ("
        "SELECT id FROM channel_arrivals "
        "WHERE session_key = ? AND agent_id = ? "
        "ORDER BY id DESC LIMIT -1 OFFSET ?)");
    if (!stmt) return 0;

    stmt->BindText(1, NormalizeSessionKey(sessionKey));
    stmt->BindText(2, agentId);
    stmt->BindInt64(3, keepLast);
    stmt->ExecDML();
    const int removed = static_cast<int>(m_store->Changes());

    // Opportunistic seen-uri cap: keep the set bounded per session.
    if (removed > 0) {
        auto uriStmt = m_store->Prepare(
            "DELETE FROM channel_seen_uris WHERE (session_key, agent_id, uri) IN ("
            "SELECT session_key, agent_id, uri FROM channel_seen_uris "
            "WHERE session_key = ? AND agent_id = ? "
            "ORDER BY created_at_unix_ms DESC LIMIT -1 OFFSET 500)");
        if (uriStmt) {
            uriStmt->BindText(1, NormalizeSessionKey(sessionKey));
            uriStmt->BindText(2, agentId);
            uriStmt->ExecDML();
        }
    }
    return removed;
}

bool ChannelContextStore::AddSeenUri(const std::string& sessionKey,
                                      const std::string& agentId,
                                      const std::string& uri) {
    auto stmt = m_store->Prepare(
        "INSERT OR IGNORE INTO channel_seen_uris "
        "(session_key, agent_id, uri, created_at_unix_ms) VALUES (?, ?, ?, ?)");
    if (!stmt) return false;

    stmt->BindText(1, NormalizeSessionKey(sessionKey));
    stmt->BindText(2, agentId);
    stmt->BindText(3, uri);
    stmt->BindInt64(4, NowUnixMs());
    stmt->ExecDML();
    return m_store->Changes() > 0;  // false ⇒ already seen
}

bool ChannelContextStore::HasSeenUri(const std::string& sessionKey,
                                      const std::string& agentId,
                                      const std::string& uri) const {
    auto stmt = m_store->Prepare(
        "SELECT 1 FROM channel_seen_uris "
        "WHERE session_key = ? AND agent_id = ? AND uri = ?");
    if (!stmt) return false;

    stmt->BindText(1, NormalizeSessionKey(sessionKey));
    stmt->BindText(2, agentId);
    stmt->BindText(3, uri);
    const bool seen = stmt->Step();
    stmt->Finalize();
    return seen;
}

int64_t ChannelContextStore::NowUnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace animus::kernel

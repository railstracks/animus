#include "animus_kernel/SqliteSessionStore.h"
#include "animus_kernel/IDataStore.h"
#include "animus_kernel/SchemaHelpers.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <json/json.h>

namespace animus::kernel {

// ============================================================================
// Helpers: Session ↔ SQLite row
// ============================================================================

static void BindSessionKey(IStatement& stmt, int startIdx, const SessionKey& key) {
    stmt.BindText(startIdx,     key.connector);
    stmt.BindText(startIdx + 1, key.conversation_id);
    stmt.BindText(startIdx + 2, key.thread_id);
}

static void WriteTurns(IDataStore& store, SessionId sessionId, const std::vector<SessionTurn>& turns) {
    for (const auto& t : turns) {
        auto stmt = store.Prepare(
            "INSERT INTO session_turns "
            "(session_id, turn_id, role, content, unix_ms, is_summary, compacted_from, "
            " thinking_content, tool_calls, tool_call_id, tool_name, "
            " intake_processed, intake_processed_at_unix_ms, token_count) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        if (!stmt) continue;
        stmt->BindInt64(1, sessionId);
        stmt->BindInt64(2, t.turn_id);
        stmt->BindText(3, t.role);
        stmt->BindText(4, t.content);
        stmt->BindInt64(5, t.unix_ms);
        stmt->BindInt(6, t.is_summary ? 1 : 0);

        // compacted_from as JSON array
        Json::Value fromArr(Json::arrayValue);
        for (auto id : t.compacted_from) {
            fromArr.append(static_cast<Json::UInt64>(id));
        }
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        stmt->BindText(7, Json::writeString(wb, fromArr));

        // thinking_content
        stmt->BindText(8, t.thinking_content);

        // tool_calls as JSON array
        Json::Value tcArr(Json::arrayValue);
        for (const auto& tc : t.tool_calls) {
            Json::Value tcObj(Json::objectValue);
            tcObj["id"] = tc.id;
            tcObj["name"] = tc.name;
            tcObj["arguments"] = tc.arguments;
            tcArr.append(tcObj);
        }
        stmt->BindText(9, Json::writeString(wb, tcArr));

        // tool_call_id, tool_name
        stmt->BindText(10, t.tool_call_id);
        stmt->BindText(11, t.tool_name);
        stmt->BindInt(12, t.intake_processed ? 1 : 0);
        stmt->BindInt64(13, static_cast<int64_t>(t.intake_processed_at_unix_ms));
        stmt->BindInt64(14, static_cast<int64_t>(t.token_count));

        stmt->Step();
    }
}

static void LoadTurns(IDataStore& store, Session& session) {
    auto stmt = store.Prepare(
        "SELECT turn_id, role, content, unix_ms, is_summary, compacted_from, "
        "       thinking_content, tool_calls, tool_call_id, tool_name, "
        "       intake_processed, intake_processed_at_unix_ms, token_count "
        "FROM session_turns WHERE session_id=? ORDER BY turn_id ASC");
    if (!stmt) return;
    stmt->BindInt64(1, session.Id());

    while (stmt->Step()) {
        SessionTurn turn;
        turn.turn_id  = stmt->ColumnInt64(0);
        turn.role     = stmt->ColumnText(1);
        turn.content  = stmt->ColumnText(2);
        turn.unix_ms  = stmt->ColumnInt64(3);
        turn.is_summary = stmt->ColumnInt64(4) != 0;

        // Parse compacted_from JSON
        std::string fromJson = stmt->ColumnText(5);
        if (!fromJson.empty() && fromJson != "[]") {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::string err;
            std::istringstream iss(fromJson);
            if (Json::parseFromStream(builder, iss, &root, &err) && root.isArray()) {
                for (const auto& v : root) {
                    turn.compacted_from.push_back(v.asUInt64());
                }
            }
        }

        // New columns (with safe defaults for old rows)
        turn.thinking_content = stmt->ColumnText(6);

        std::string tcJson = stmt->ColumnText(7);
        if (!tcJson.empty() && tcJson != "[]") {
            Json::Value tcRoot;
            Json::CharReaderBuilder tcBuilder;
            std::string tcErr;
            std::istringstream tcIss(tcJson);
            if (Json::parseFromStream(tcBuilder, tcIss, &tcRoot, &tcErr) && tcRoot.isArray()) {
                for (const auto& tcv : tcRoot) {
                    ToolCall tc;
                    tc.id = tcv.get("id", "").asString();
                    tc.name = tcv.get("name", "").asString();
                    tc.arguments = tcv.get("arguments", "").asString();
                    turn.tool_calls.push_back(std::move(tc));
                }
            }
        }

        turn.tool_call_id = stmt->ColumnText(8);
        turn.tool_name    = stmt->ColumnText(9);
        turn.intake_processed = !stmt->IsColumnNull(10) && stmt->ColumnInt64(10) != 0;
        turn.intake_processed_at_unix_ms = stmt->IsColumnNull(11)
            ? 0
            : static_cast<std::uint64_t>(stmt->ColumnInt64(11));
        turn.token_count = stmt->IsColumnNull(12)
            ? 0
            : static_cast<std::size_t>(stmt->ColumnInt64(12));

        session.AddTurn(std::move(turn));
    }
}

static void LoadCompactionSummary(IDataStore& store, Session& session) {
    auto stmt = store.Prepare(
        "SELECT turn_id, role, content, unix_ms, is_summary "
        "FROM session_compaction_summaries WHERE session_id=?");
    if (!stmt) return;
    stmt->BindInt64(1, session.Id());

    if (stmt->Step()) {
        SessionTurn cs;
        cs.turn_id    = stmt->ColumnInt64(0);
        cs.role       = stmt->ColumnText(1);
        cs.content    = stmt->ColumnText(2);
        cs.unix_ms    = stmt->ColumnInt64(3);
        cs.is_summary = stmt->ColumnInt64(4) != 0;
        session.SetCompactionSummary(std::move(cs));
    }
}

// ============================================================================
// JSON migration helpers (reuses FileSessionStore serialization format)
// ============================================================================

static std::shared_ptr<Session> SessionFromJson(const Json::Value& root) {
    SessionKey key;
    key.connector       = root["key"]["connector"].asString();
    key.conversation_id = root["key"]["conversation_id"].asString();
    key.thread_id       = root["key"]["thread_id"].asString();

    SessionId id = root["id"].asUInt64();
    auto session = std::make_shared<Session>(id, key);

    if (root.isMember("provider_id"))
        session->SetProviderId(root["provider_id"].asString());
    if (root.isMember("summary") && root["summary"].isString())
        session->SetSummary(root["summary"].asString());
    if (root.isMember("agent_id") && root["agent_id"].isString())
        session->SetAgentId(root["agent_id"].asString());
    if (root.isMember("session_type") && root["session_type"].isString())
        session->SetSessionType(root["session_type"].asString());
    if (root.get("terminated", false).asBool())
        session->MarkTerminated();

    if (root.isMember("turns") && root["turns"].isArray()) {
        for (const auto& tv : root["turns"]) {
            SessionTurn turn;
            turn.turn_id  = tv["turn_id"].asUInt64();
            turn.role     = tv["role"].asString();
            turn.content  = tv["content"].asString();
            turn.unix_ms  = tv["unix_ms"].asUInt64();
            turn.is_summary = tv.get("is_summary", false).asBool();
            turn.thinking_content = tv.get("thinking_content", "").asString();
            turn.tool_call_id = tv.get("tool_call_id", "").asString();
            turn.tool_name    = tv.get("tool_name", "").asString();
            turn.intake_processed = tv.get("intake_processed", false).asBool();
            if (tv.isMember("intake_processed_at_unix_ms")) {
                turn.intake_processed_at_unix_ms =
                    static_cast<std::uint64_t>(tv["intake_processed_at_unix_ms"].asUInt64());
            }
            if (tv.isMember("compacted_from") && tv["compacted_from"].isArray()) {
                for (const auto& idv : tv["compacted_from"]) {
                    turn.compacted_from.push_back(idv.asUInt64());
                }
            }
            if (tv.isMember("tool_calls") && tv["tool_calls"].isArray()) {
                for (const auto& tcv : tv["tool_calls"]) {
                    ToolCall tc;
                    tc.id = tcv.get("id", "").asString();
                    tc.name = tcv.get("name", "").asString();
                    tc.arguments = tcv.get("arguments", "").asString();
                    turn.tool_calls.push_back(std::move(tc));
                }
            }
            session->AddTurn(std::move(turn));
        }
    }

    if (root.isMember("compaction_summary")) {
        const auto& csVal = root["compaction_summary"];
        SessionTurn cs;
        cs.turn_id    = csVal["turn_id"].asUInt64();
        cs.role       = csVal["role"].asString();
        cs.content    = csVal["content"].asString();
        cs.unix_ms    = csVal["unix_ms"].asUInt64();
        cs.is_summary = csVal["is_summary"].asBool();
        session->SetCompactionSummary(std::move(cs));
    }

    return session;
}

// ============================================================================
// Construction
// ============================================================================

SqliteSessionStore::SqliteSessionStore(IDataStore* dataStore,
                                       const std::string& legacyJsonPath)
    : m_store(dataStore) {
    EnsureSchema();
    LoadFromDb();

    // One-time migration from sessions.json if DB was empty
    if (m_entries.empty() && !legacyJsonPath.empty()) {
        MigrateFromJson(legacyJsonPath);
    }

    std::cerr << "[session] store ready, " << m_entries.size() << " sessions loaded" << std::endl;
}

// ============================================================================
// Schema
// ============================================================================

void SqliteSessionStore::EnsureSchema() {
    m_store->Exec(R"(
        CREATE TABLE IF NOT EXISTS sessions (
            id INTEGER PRIMARY KEY,
            connector TEXT NOT NULL,
            conversation_id TEXT NOT NULL,
            thread_id TEXT NOT NULL,
            provider_id TEXT NOT NULL DEFAULT '',
            summary TEXT NOT NULL DEFAULT '',
            created_at_unix_ms BIGINT NOT NULL,
            last_active_unix_ms BIGINT NOT NULL,
            terminated INTEGER NOT NULL DEFAULT 0,
            session_type TEXT NOT NULL DEFAULT ''
        );
    )");

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS session_turns (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
            turn_id INTEGER NOT NULL,
            role TEXT NOT NULL,
            content TEXT NOT NULL,
            unix_ms INTEGER NOT NULL,
            is_summary INTEGER NOT NULL DEFAULT 0,
            compacted_from TEXT NOT NULL DEFAULT '[]',
            thinking_content TEXT NOT NULL DEFAULT '',
            tool_calls TEXT NOT NULL DEFAULT '[]',
            tool_call_id TEXT NOT NULL DEFAULT '',
            tool_name TEXT NOT NULL DEFAULT '',
            intake_processed INTEGER NOT NULL DEFAULT 0,
            intake_processed_at_unix_ms INTEGER NOT NULL DEFAULT 0
        );
    )");

    // Migration v1: add thinking/tool_call columns to session_turns.
    // SQLite ALTER TABLE ADD COLUMN fails with "duplicate column name" if column
    // already exists, so we check column existence first.
    // On PostgreSQL, ALTER TABLE ADD COLUMN IF NOT EXISTS handles this natively,
    // but we use the same column-existence check for consistency.
    auto HasColumn = [&](const std::string& table, const std::string& col) -> bool {
        return schema::ColumnExists(m_store, table, col);
    };
    if (!HasColumn("session_turns", "thinking_content"))
        m_store->Exec("ALTER TABLE session_turns ADD COLUMN thinking_content TEXT NOT NULL DEFAULT ''");
    if (!HasColumn("session_turns", "tool_calls"))
        m_store->Exec("ALTER TABLE session_turns ADD COLUMN tool_calls TEXT NOT NULL DEFAULT '[]'");
    if (!HasColumn("session_turns", "tool_call_id"))
        m_store->Exec("ALTER TABLE session_turns ADD COLUMN tool_call_id TEXT NOT NULL DEFAULT ''");
    if (!HasColumn("session_turns", "tool_name"))
        m_store->Exec("ALTER TABLE session_turns ADD COLUMN tool_name TEXT NOT NULL DEFAULT ''");
    if (!HasColumn("session_turns", "intake_processed"))
        m_store->Exec("ALTER TABLE session_turns ADD COLUMN intake_processed INTEGER NOT NULL DEFAULT 0");
    if (!HasColumn("session_turns", "intake_processed_at_unix_ms"))
        m_store->Exec("ALTER TABLE session_turns ADD COLUMN intake_processed_at_unix_ms INTEGER NOT NULL DEFAULT 0");
    // Migration v2: add agent_id column to sessions table.
    // Ownership: sessions table is owned by SqliteSessionStore, so the migration
    // lives here regardless of which component originally introduced agent_id.
    if (!HasColumn("sessions", "agent_id"))
        m_store->Exec("ALTER TABLE sessions ADD COLUMN agent_id TEXT NOT NULL DEFAULT 'default'");
    // Migration v3: add session_type column to sessions table.
    if (!HasColumn("sessions", "session_type"))
        m_store->Exec("ALTER TABLE sessions ADD COLUMN session_type TEXT NOT NULL DEFAULT ''");
    // Migration v4: add token_count column to session_turns for caching.
    if (!HasColumn("session_turns", "token_count"))
        m_store->Exec("ALTER TABLE session_turns ADD COLUMN token_count INTEGER NOT NULL DEFAULT 0");
    // Migration v5: fix timestamp columns on PostgreSQL (INTEGER → BIGINT).
    // SQLite stores INTEGER as 64-bit natively, so no migration needed there.
    if (m_store->Dialect() == DataStoreDialect::PostgreSQL) {
        m_store->Exec("ALTER TABLE sessions ALTER COLUMN created_at_unix_ms TYPE BIGINT");
        m_store->Exec("ALTER TABLE sessions ALTER COLUMN last_active_unix_ms TYPE BIGINT");
    }
    m_store->Exec(R"(
        CREATE TABLE IF NOT EXISTS session_compaction_summaries (
            session_id INTEGER NOT NULL UNIQUE REFERENCES sessions(id) ON DELETE CASCADE,
            turn_id INTEGER NOT NULL,
            role TEXT NOT NULL,
            content TEXT NOT NULL,
            unix_ms INTEGER NOT NULL,
            is_summary INTEGER NOT NULL DEFAULT 1
        );
    )");
}

// ============================================================================
// Load from DB
// ============================================================================

void SqliteSessionStore::LoadFromDb() {
    auto stmt = m_store->Prepare(
        "SELECT id, connector, conversation_id, thread_id, provider_id, summary, "
        "       agent_id, created_at_unix_ms, last_active_unix_ms, terminated, session_type "
        "FROM sessions ORDER BY id ASC");
    if (!stmt) return;

    SessionId maxId = 0;
    while (stmt->Step()) {
        SessionKey key;
        SessionId id      = stmt->ColumnInt64(0);
        key.connector       = stmt->ColumnText(1);
        key.conversation_id = stmt->ColumnText(2);
        key.thread_id       = stmt->ColumnText(3);

        auto session = std::make_shared<Session>(id, key);
        session->SetProviderId(stmt->ColumnText(4));
        session->SetSummary(stmt->ColumnText(5));
        session->SetAgentId(stmt->ColumnText(6));
        session->SetCreatedUnixMs(static_cast<std::uint64_t>(stmt->ColumnInt64(7)));
        session->SetLastActiveUnixMs(static_cast<std::uint64_t>(stmt->ColumnInt64(8)));
        if (stmt->ColumnInt64(9) != 0) {
            session->MarkTerminated();
        }
        session->SetSessionType(stmt->ColumnText(10));

        LoadTurns(*m_store, *session);
        LoadCompactionSummary(*m_store, *session);

        if (id >= maxId) maxId = id + 1;
        m_entries.push_back({session});
    }
    m_nextId = maxId > 0 ? maxId : 1;
    std::cerr << "[session-store] LoadFromDb: loaded " << m_entries.size()
              << " sessions, nextId=" << m_nextId << std::endl;
}

// ============================================================================
// JSON migration
// ============================================================================

void SqliteSessionStore::MigrateFromJson(const std::string& jsonPath) {
    if (!std::filesystem::exists(jsonPath)) return;

    std::cerr << "[session] migrating from " << jsonPath << " ..." << std::endl;

    std::ifstream in(jsonPath);
    if (!in.is_open()) return;

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string err;
    if (!Json::parseFromStream(builder, in, &root, &err)) {
        std::cerr << "[session] failed to parse " << jsonPath << ": " << err << std::endl;
        return;
    }

    if (!root.isMember("sessions") || !root["sessions"].isArray()) return;

    SessionId maxId = m_nextId;
    for (const auto& sv : root["sessions"]) {
        auto session = SessionFromJson(sv);
        if (session->Id() >= maxId) maxId = session->Id() + 1;
        PersistSession(*session);
        m_entries.push_back({session});
    }
    m_nextId = maxId;

    std::cerr << "[session] migrated " << m_entries.size() << " sessions from JSON" << std::endl;

    // Rename the old file to mark migration complete
    std::string migratedPath = jsonPath + ".migrated";
    std::filesystem::rename(jsonPath, migratedPath);
    std::cerr << "[session] renamed " << jsonPath << " -> " << migratedPath << std::endl;
}

// ============================================================================
// Persistence
// ============================================================================

void SqliteSessionStore::PersistSession(const Session& s) {
    // Upsert the session row
    std::string sql = schema::UpsertSql(m_store,
        "sessions",
        "id, connector, conversation_id, thread_id, provider_id, summary, "
        "agent_id, created_at_unix_ms, last_active_unix_ms, terminated, session_type",
        "id",
        "connector = EXCLUDED.connector, conversation_id = EXCLUDED.conversation_id, "
        "thread_id = EXCLUDED.thread_id, provider_id = EXCLUDED.provider_id, "
        "summary = EXCLUDED.summary, agent_id = EXCLUDED.agent_id, "
        "last_active_unix_ms = EXCLUDED.last_active_unix_ms, terminated = EXCLUDED.terminated, "
        "session_type = EXCLUDED.session_type");
    auto stmt = m_store->Prepare(sql);
    if (!stmt) return;

    stmt->BindInt64(1, s.Id());
    stmt->BindText(2, s.Key().connector);
    stmt->BindText(3, s.Key().conversation_id);
    stmt->BindText(4, s.Key().thread_id);
    stmt->BindText(5, s.ProviderId());
    stmt->BindText(6, s.Summary());
    stmt->BindText(7, s.AgentId());
    stmt->BindInt64(8, s.CreatedUnixMs());
    stmt->BindInt64(9, s.LastActiveUnixMs());
    stmt->BindInt64(10, s.IsTerminated() ? 1 : 0);
    stmt->BindText(11, s.SessionType());
    if (!stmt->Step()) {
        std::cerr << "[session-store] PersistSession: Step() failed for session " << s.Id()
                  << ": " << m_store->ErrMsg() << std::endl;
    }

    // Delete old turns + compaction summary, then re-insert
    {
        auto delTurns = m_store->Prepare("DELETE FROM session_turns WHERE session_id=?");
        if (delTurns) { delTurns->BindInt64(1, s.Id()); delTurns->Step(); }
    }
    {
        auto delSummary = m_store->Prepare("DELETE FROM session_compaction_summaries WHERE session_id=?");
        if (delSummary) { delSummary->BindInt64(1, s.Id()); delSummary->Step(); }
    }

    WriteTurns(*m_store, s.Id(), s.Turns());

    // Compaction summary
    const auto* cs = s.GetCompactionSummary();
    if (cs) {
        auto csStmt = m_store->Prepare(
            "INSERT INTO session_compaction_summaries "
            "(session_id, turn_id, role, content, unix_ms, is_summary) VALUES (?,?,?,?,?,?)");
        if (csStmt) {
            csStmt->BindInt64(1, s.Id());
            csStmt->BindInt64(2, cs->turn_id);
            csStmt->BindText(3, cs->role);
            csStmt->BindText(4, cs->content);
            csStmt->BindInt64(5, cs->unix_ms);
            csStmt->BindInt64(6, cs->is_summary ? 1 : 0);
            csStmt->Step();
        }
    }
}

void SqliteSessionStore::DeleteSessionFromDb(SessionId id) {
    auto stmt = m_store->Prepare("DELETE FROM sessions WHERE id=?");
    if (stmt) {
        stmt->BindInt64(1, id);
        stmt->Step();
    }
    // ON DELETE CASCADE handles turns + compaction summaries
}

// ============================================================================
// ISessionStore interface
// ============================================================================

std::shared_ptr<Session> SqliteSessionStore::GetOrCreate(const SessionKey& key) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& entry : m_entries) {
        const auto& k = entry.session->Key();
        if (k.connector == key.connector &&
            k.conversation_id == key.conversation_id &&
            k.thread_id == key.thread_id) {
            return entry.session;
        }
    }

    auto session = std::make_shared<Session>(m_nextId++, key);
    PersistSession(*session);
    m_entries.push_back({session});
    return session;
}

std::shared_ptr<Session> SqliteSessionStore::GetById(SessionId id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& entry : m_entries) {
        if (entry.session->Id() == id) return entry.session;
    }
    return nullptr;
}

std::vector<std::shared_ptr<Session>> SqliteSessionStore::List() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::shared_ptr<Session>> result;
    result.reserve(m_entries.size());
    for (auto& entry : m_entries) {
        result.push_back(entry.session);
    }
    return result;
}

// ---------------------------------------------------------------------------
// ListPaginated — server-side pagination + content search
// ---------------------------------------------------------------------------

ISessionStore::ListPage SqliteSessionStore::ListPaginated(
        std::size_t offset, std::size_t limit, const std::string& search) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ListPage page;

    // Build the base WHERE and query.
    // When search is non-empty, join with session_turns and filter on content.
    std::string sql;
    std::string countSql;

    if (search.empty()) {
        sql =
            "SELECT s.id, s.connector, s.conversation_id, s.thread_id, s.provider_id, s.summary, "
            "       s.agent_id, s.created_at_unix_ms, s.last_active_unix_ms, s.terminated, s.session_type, "
            "       (SELECT COUNT(*) FROM session_turns t WHERE t.session_id = s.id) AS turn_count "
            "FROM sessions s ORDER BY s.last_active_unix_ms DESC LIMIT ? OFFSET ?";
        countSql = "SELECT COUNT(*) FROM sessions";
    } else {
        // DISTINCT because multiple turns in one session may match.
        sql =
            "SELECT DISTINCT s.id, s.connector, s.conversation_id, s.thread_id, "
            "       s.provider_id, s.summary, s.agent_id, "
            "       s.created_at_unix_ms, s.last_active_unix_ms, s.terminated, s.session_type, "
            "       (SELECT COUNT(*) FROM session_turns t WHERE t.session_id = s.id) AS turn_count "
            "FROM sessions s "
            "LEFT JOIN session_turns t ON s.id = t.session_id "
            "WHERE t.content LIKE ? "
            "ORDER BY s.last_active_unix_ms DESC LIMIT ? OFFSET ?";
        countSql =
            "SELECT COUNT(DISTINCT s.id) FROM sessions s "
            "LEFT JOIN session_turns t ON s.id = t.session_id "
            "WHERE t.content LIKE ?";
    }

    // Count query
    {
        auto countStmt = m_store->Prepare(countSql);
        if (!countStmt) return page;
        if (!search.empty()) {
            countStmt->BindText(1, "%" + search + "%");
        }
        if (countStmt->Step()) {
            page.total = static_cast<std::size_t>(countStmt->ColumnInt64(0));
        }
    }

    // Page query
    {
        auto stmt = m_store->Prepare(sql);
        if (!stmt) return page;

        int bindIdx = 1;
        if (!search.empty()) {
            stmt->BindText(bindIdx++, "%" + search + "%");
        }
        stmt->BindInt64(bindIdx++, static_cast<std::int64_t>(limit));
        stmt->BindInt64(bindIdx++, static_cast<std::int64_t>(offset));

        while (stmt->Step()) {
            SessionKey key;
            SessionId id = stmt->ColumnInt64(0);
            key.connector       = stmt->ColumnText(1);
            key.conversation_id = stmt->ColumnText(2);
            key.thread_id       = stmt->ColumnText(3);

            auto session = std::make_shared<Session>(id, key);
            session->SetProviderId(stmt->ColumnText(4));
            session->SetSummary(stmt->ColumnText(5));
            session->SetAgentId(stmt->ColumnText(6));
            session->SetCreatedUnixMs(static_cast<std::uint64_t>(stmt->ColumnInt64(7)));
            session->SetLastActiveUnixMs(static_cast<std::uint64_t>(stmt->ColumnInt64(8)));
            if (stmt->ColumnInt64(9) != 0) {
                session->MarkTerminated();
            }
            session->SetSessionType(stmt->ColumnText(10));
            session->SetMessageCountOverride(static_cast<std::size_t>(stmt->ColumnInt64(11)));

            page.sessions.push_back(session);
        }
    }

    return page;
}

bool SqliteSessionStore::DeleteById(SessionId id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->session->Id() == id) {
            DeleteSessionFromDb(id);
            m_entries.erase(it);
            return true;
        }
    }
    return false;
}

void SqliteSessionStore::FlushSession(SessionId id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& entry : m_entries) {
        if (entry.session->Id() == id) {
            PersistSession(*entry.session);
            return;
        }
    }
    std::cerr << "[session-store] FlushSession: session " << id << " not found in " << m_entries.size() << " entries\n";
}


// ============================================================================
// DB-level queries for consolidation intake (bypasses in-memory cache)
// ============================================================================

std::vector<ISessionStore::UnprocessedTurn> SqliteSessionStore::GetUnprocessedTurns(
        const std::string& agentId, int limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<UnprocessedTurn> result;

    std::string sql;
    if (agentId.empty()) {
        sql = "SELECT st.session_id, st.turn_id, st.role, st.content "
              "FROM session_turns st "
              "JOIN sessions s ON s.id = st.session_id "
              "WHERE st.intake_processed = 0 AND st.content != '' "
              "AND (s.session_type IS NULL OR s.session_type != 'consolidation') "
              "ORDER BY st.unix_ms ASC LIMIT ?";
    } else {
        sql = "SELECT st.session_id, st.turn_id, st.role, st.content "
              "FROM session_turns st "
              "JOIN sessions s ON s.id = st.session_id "
              "WHERE st.intake_processed = 0 AND st.content != '' "
              "AND s.agent_id = ? "
              "AND (s.session_type IS NULL OR s.session_type != 'consolidation') "
              "ORDER BY st.unix_ms ASC LIMIT ?";
    }

    auto stmt = m_store->Prepare(sql);
    if (agentId.empty()) {
        stmt->BindInt(1, limit);
    } else {
        stmt->BindText(1, agentId);
        stmt->BindInt(2, limit);
    }

    int rawRows = 0;
    while (stmt->Step()) {
        ++rawRows;
        UnprocessedTurn t;
        t.session_id = stmt->ColumnInt64(0);
        t.turn_id = stmt->ColumnInt64(1);
        t.role = stmt->ColumnText(2);
        t.content = stmt->ColumnText(3);
        result.push_back(std::move(t));
    }

    std::cerr << "[session-store] GetUnprocessedTurns: agent=\"" << agentId
              << "\" limit=" << limit << " raw_rows=" << rawRows
              << " returned=" << result.size() << std::endl;

    return result;
}

void SqliteSessionStore::MarkTurnsProcessed(const std::vector<SessionTurnId>& turnIds) {
    if (turnIds.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);

    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (const auto& tid : turnIds) {
        m_store->Exec(
            "UPDATE session_turns SET intake_processed = 1, intake_processed_at_unix_ms = "
            + std::to_string(now) + " WHERE turn_id = " + std::to_string(tid));
    }
}

} // namespace animus::kernel

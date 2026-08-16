#include "animus_kernel/AgendaStore.h"
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/IDataStore.h"

#include <chrono>

namespace animus::kernel {

namespace {
int64_t NowUnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
}

AgendaStore::AgendaStore(IDataStore* store)
    : m_store(store) {
    EnsureSchema();
}

void AgendaStore::EnsureSchema() {
    if (!m_store) return;

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS agenda_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            agent_id TEXT NOT NULL,
            title TEXT NOT NULL,
            description TEXT NOT NULL DEFAULT '',
            start_time TEXT NOT NULL,
            end_time TEXT NOT NULL DEFAULT '',
            timezone TEXT NOT NULL DEFAULT 'UTC',
            recurrence TEXT NOT NULL DEFAULT 'none',
            completed INTEGER NOT NULL DEFAULT 0,
            created_at_unix_ms INTEGER NOT NULL,
            updated_at_unix_ms INTEGER NOT NULL
        );
    )");

    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_agenda_agent_start "
        "ON agenda_events (agent_id, start_time)");
}

AgendaEvent AgendaStore::Create(const std::string& agentId,
                                 const std::string& title,
                                 const std::string& startTime,
                                 const std::string& timezone,
                                 const std::string& description,
                                 const std::string& endTime,
                                 const std::string& recurrence) {
    if (!m_store) return {};

    const int64_t now = NowUnixMs();

    auto stmt = m_store->Prepare(
        "INSERT INTO agenda_events "
        "(agent_id, title, description, start_time, end_time, timezone, "
        "recurrence, completed, created_at_unix_ms, updated_at_unix_ms) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 0, ?, ?)");
    if (!stmt) return {};

    stmt->BindText(1, agentId);
    stmt->BindText(2, title);
    stmt->BindText(3, description);
    stmt->BindText(4, startTime);
    stmt->BindText(5, endTime);
    stmt->BindText(6, timezone);
    stmt->BindText(7, recurrence);
    stmt->BindInt64(8, now);
    stmt->BindInt64(9, now);
    stmt->ExecDML();
    stmt->Finalize();

    AgendaEvent ev;
    ev.id = m_store->LastInsertRowId();
    ev.agent_id = agentId;
    ev.title = title;
    ev.description = description;
    ev.start_time = startTime;
    ev.end_time = endTime;
    ev.timezone = timezone;
    ev.recurrence = recurrence;
    ev.completed = false;
    ev.created_at_unix_ms = now;
    ev.updated_at_unix_ms = now;
    return ev;
}

std::optional<AgendaEvent> AgendaStore::Update(int64_t id,
                                                const std::string& agentId,
                                                const std::string& title,
                                                const std::string& startTime,
                                                const std::string& timezone,
                                                const std::string& description,
                                                const std::string& endTime,
                                                const std::string& recurrence) {
    if (!m_store) return std::nullopt;

    const int64_t now = NowUnixMs();

    auto stmt = m_store->Prepare(
        "UPDATE agenda_events SET title = ?, start_time = ?, timezone = ?, "
        "description = ?, end_time = ?, recurrence = ?, updated_at_unix_ms = ? "
        "WHERE id = ? AND agent_id = ?");
    if (!stmt) return std::nullopt;

    stmt->BindText(1, title);
    stmt->BindText(2, startTime);
    stmt->BindText(3, timezone);
    stmt->BindText(4, description);
    stmt->BindText(5, endTime);
    stmt->BindText(6, recurrence);
    stmt->BindInt64(7, now);
    stmt->BindInt64(8, id);
    stmt->BindText(9, agentId);
    stmt->ExecDML();
    stmt->Finalize();

    return GetById(id, agentId);
}

bool AgendaStore::SetCompleted(int64_t id, const std::string& agentId, bool completed) {
    if (!m_store) return false;

    auto stmt = m_store->Prepare(
        "UPDATE agenda_events SET completed = ?, updated_at_unix_ms = ? "
        "WHERE id = ? AND agent_id = ?");
    if (!stmt) return false;

    stmt->BindInt(1, completed ? 1 : 0);
    stmt->BindInt64(2, NowUnixMs());
    stmt->BindInt64(3, id);
    stmt->BindText(4, agentId);
    stmt->ExecDML();
    stmt->Finalize();

    return m_store->Changes() > 0;
}

bool AgendaStore::Delete(int64_t id, const std::string& agentId) {
    if (!m_store) return false;

    auto stmt = m_store->Prepare(
        "DELETE FROM agenda_events WHERE id = ? AND agent_id = ?");
    if (!stmt) return false;

    stmt->BindInt64(1, id);
    stmt->BindText(2, agentId);
    stmt->ExecDML();
    stmt->Finalize();

    return m_store->Changes() > 0;
}

std::optional<AgendaEvent> AgendaStore::GetById(int64_t id, const std::string& agentId) const {
    if (!m_store) return std::nullopt;

    auto stmt = m_store->Prepare(
        "SELECT id, agent_id, title, description, start_time, end_time, "
        "timezone, recurrence, completed, created_at_unix_ms, updated_at_unix_ms "
        "FROM agenda_events WHERE id = ? AND agent_id = ?");
    if (!stmt) return std::nullopt;

    stmt->BindInt64(1, id);
    stmt->BindText(2, agentId);

    std::optional<AgendaEvent> result;
    if (stmt->Step()) {
        result = RowToEvent(*stmt);
    }
    stmt->Finalize();
    return result;
}

std::vector<AgendaEvent> AgendaStore::ListForAgent(const std::string& agentId) const {
    if (!m_store) return {};

    auto stmt = m_store->Prepare(
        "SELECT id, agent_id, title, description, start_time, end_time, "
        "timezone, recurrence, completed, created_at_unix_ms, updated_at_unix_ms "
        "FROM agenda_events WHERE agent_id = ? ORDER BY start_time ASC");
    if (!stmt) return {};

    stmt->BindText(1, agentId);

    std::vector<AgendaEvent> events;
    while (stmt->Step()) {
        events.push_back(RowToEvent(*stmt));
    }
    stmt->Finalize();
    return events;
}

std::vector<AgendaEvent> AgendaStore::ListUpcoming(const std::string& agentId,
                                                    int maxCount) const {
    if (!m_store) return {};

    // Get current time in ISO-8601 format for string comparison.
    // Since start_time is stored as ISO-8601, lexicographic comparison works
    // for same-format timestamps.
    auto now = std::chrono::system_clock::now();
    std::time_t nowT = std::chrono::system_clock::to_time_t(now);
    std::tm tmUtc{};
    gmtime_r(&nowT, &tmUtc);
    char nowBuf[32];
    std::strftime(nowBuf, sizeof(nowBuf), "%Y-%m-%dT%H:%M:%S", &tmUtc);
    std::string nowStr(nowBuf);

    auto stmt = m_store->Prepare(
        "SELECT id, agent_id, title, description, start_time, end_time, "
        "timezone, recurrence, completed, created_at_unix_ms, updated_at_unix_ms "
        "FROM agenda_events WHERE agent_id = ? AND completed = 0 "
        "AND start_time >= ? ORDER BY start_time ASC LIMIT ?");
    if (!stmt) return {};

    stmt->BindText(1, agentId);
    stmt->BindText(2, nowStr);
    stmt->BindInt(3, maxCount);

    std::vector<AgendaEvent> events;
    while (stmt->Step()) {
        events.push_back(RowToEvent(*stmt));
    }
    stmt->Finalize();
    return events;
}

AgendaEvent AgendaStore::RowToEvent(IStatement& stmt) const {
    AgendaEvent ev;
    ev.id = stmt.ColumnInt64(0);
    ev.agent_id = stmt.ColumnText(1);
    ev.title = stmt.ColumnText(2);
    ev.description = stmt.ColumnText(3);
    ev.start_time = stmt.ColumnText(4);
    ev.end_time = stmt.ColumnText(5);
    ev.timezone = stmt.ColumnText(6);
    ev.recurrence = stmt.ColumnText(7);
    ev.completed = stmt.ColumnInt64(8) != 0;
    ev.created_at_unix_ms = stmt.ColumnInt64(9);
    ev.updated_at_unix_ms = stmt.ColumnInt64(10);
    return ev;
}

} // namespace animus::kernel
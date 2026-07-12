
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/scheduler/ScheduleStore.h"
#include "animus_kernel/IDataStore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

namespace animus::kernel {

namespace {

std::string NowUnixMsStr() {
    return std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

// ISO-8601 timestamp from epoch ms
std::string IsoFromUnixMs(int64_t ms) {
    const auto tp = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(ms));
    const auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    // Use gmtime for UTC consistency; timezone display is handled by metadata
    gmtime_r(&tt, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string GenerateId() {
    static thread_local std::mt19937_64 rng(
        static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    static std::atomic<std::uint64_t> counter{0};
    std::uniform_int_distribution<std::uint64_t> dist;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << dist(rng)
        << std::setw(16) << counter.fetch_add(1);
    return oss.str();
}

ScheduleType ParseType(const std::string& s) {
    if (s == "recurring") return ScheduleType::Recurring;
    return ScheduleType::OneShot;
}

std::string TypeToString(ScheduleType t) {
    return t == ScheduleType::Recurring ? "recurring" : "one_shot";
}

} // anonymous namespace

// ============================================================================
// Construction + schema
// ============================================================================

ScheduleStore::ScheduleStore(IDataStore* dataStore) : m_store(dataStore) {
    EnsureSchema();
}

ScheduleStore::~ScheduleStore() = default;

void ScheduleStore::EnsureSchema() {
    m_store->Exec(R"(
        CREATE TABLE IF NOT EXISTS schedules (
            id       TEXT PRIMARY KEY,
            agent_id TEXT NOT NULL,
            tag      TEXT NOT NULL DEFAULT '',
            type     TEXT NOT NULL DEFAULT 'one_shot',
            next_fire TEXT NOT NULL,
            cron_expr TEXT,
            timezone TEXT NOT NULL DEFAULT 'UTC',
            message  TEXT NOT NULL,
            enabled  INTEGER NOT NULL DEFAULT 1,
            created_at TEXT NOT NULL,
            last_fire  TEXT,
            fire_count INTEGER NOT NULL DEFAULT 0,
            max_fires  INTEGER
        );
    )");

    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_schedules_agent ON schedules(agent_id, enabled, next_fire);");
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_schedules_tag ON schedules(agent_id, tag);");

    // Migration: add max_fires and fire_count if missing (from older schema)
    if (m_store->Dialect() == DataStoreDialect::SQLite) {
        if (!schema::ColumnExists(m_store, "schedules", "fire_count")) {
            m_store->Exec("ALTER TABLE schedules ADD COLUMN fire_count INTEGER NOT NULL DEFAULT 0");
        }
        if (!schema::ColumnExists(m_store, "schedules", "max_fires")) {
            m_store->Exec("ALTER TABLE schedules ADD COLUMN max_fires INTEGER");
        }
    }
}

// ============================================================================
// Row mapping
// ============================================================================

ScheduleDescriptor ScheduleStore::RowToDescriptor(
        const std::string& id, const std::string& agentId,
        const std::string& tag, const std::string& typeStr,
        const std::string& nextFire, const std::string& cronExpr,
        const std::string& tz, const std::string& message,
        bool enabled, const std::string& createdAt,
        const std::string& lastFire, std::int32_t fireCount,
        std::int32_t maxFires) const {
    ScheduleDescriptor s;
    s.id = id;
    s.agent_id = agentId;
    s.tag = tag;
    s.type = ParseType(typeStr);
    s.next_fire = nextFire;
    s.cron_expr = cronExpr;
    s.timezone = tz;
    s.message = message;
    s.enabled = enabled;
    s.created_at = createdAt;
    s.last_fire = lastFire;
    s.fire_count = fireCount;
    s.max_fires = maxFires;
    return s;
}

// ============================================================================
// CRUD
// ============================================================================

bool ScheduleStore::Create(const ScheduleDescriptor& sched, std::string* error) {
    std::string id = sched.id.empty() ? GenerateId() : sched.id;

    auto stmt = m_store->Prepare(
        "INSERT INTO schedules (id, agent_id, tag, type, next_fire, cron_expr, timezone, "
        "message, enabled, created_at, max_fires) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?)");
    if (!stmt) {
        if (error) *error = "failed to prepare schedule insert: " + m_store->ErrMsg();
        return false;
    }

    const std::string now = sched.created_at.empty() ? IsoFromUnixMs(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count())
        : sched.created_at;

    stmt->BindText(1, id);
    stmt->BindText(2, sched.agent_id);
    stmt->BindText(3, sched.tag);
    stmt->BindText(4, TypeToString(sched.type));
    stmt->BindText(5, sched.next_fire);
    stmt->BindText(6, sched.cron_expr);
    stmt->BindText(7, sched.timezone);
    stmt->BindText(8, sched.message);
    stmt->BindInt(9, sched.enabled ? 1 : 0);
    stmt->BindText(10, now);
    if (sched.max_fires <= 0) {
        stmt->BindNull(11);
    } else {
        stmt->BindInt(11, sched.max_fires);
    }

    stmt->Step();
    return true;
}

std::string ScheduleStore::CreateAndReturnId(ScheduleDescriptor& sched, std::string* error) {
    if (sched.id.empty()) sched.id = GenerateId();
    if (!Create(sched, error)) return "";
    return sched.id;
}

std::optional<ScheduleDescriptor> ScheduleStore::Get(const std::string& id) const {
    auto stmt = m_store->Prepare(
        "SELECT id, agent_id, tag, type, next_fire, cron_expr, timezone, "
        "message, enabled, created_at, last_fire, fire_count, max_fires "
        "FROM schedules WHERE id=?");
    if (!stmt) return std::nullopt;
    stmt->BindText(1, id);

    if (stmt->Step()) {
        return RowToDescriptor(
            stmt->ColumnText(0), stmt->ColumnText(1),
            stmt->ColumnText(2), stmt->ColumnText(3),
            stmt->ColumnText(4), stmt->IsColumnNull(5) ? "" : stmt->ColumnText(5),
            stmt->ColumnText(6), stmt->ColumnText(7),
            stmt->ColumnInt64(8) != 0, stmt->ColumnText(9),
            stmt->IsColumnNull(10) ? "" : stmt->ColumnText(10),
            stmt->IsColumnNull(11) ? 0 : static_cast<std::int32_t>(stmt->ColumnInt64(11)),
            stmt->IsColumnNull(12) ? 0 : static_cast<std::int32_t>(stmt->ColumnInt64(12)));
    }
    return std::nullopt;
}

std::vector<ScheduleDescriptor> ScheduleStore::List(
        const std::string& agentId, const std::string& tag) const {
    std::vector<ScheduleDescriptor> result;

    std::string sql =
        "SELECT id, agent_id, tag, type, next_fire, cron_expr, timezone, "
        "message, enabled, created_at, last_fire, fire_count, max_fires "
        "FROM schedules WHERE agent_id=?";
    if (!tag.empty()) sql += " AND tag=?";
    sql += " ORDER BY next_fire ASC";

    auto stmt = m_store->Prepare(sql);
    if (!stmt) return result;

    stmt->BindText(1, agentId);
    if (!tag.empty()) stmt->BindText(2, tag);

    while (stmt->Step()) {
        result.push_back(RowToDescriptor(
            stmt->ColumnText(0), stmt->ColumnText(1),
            stmt->ColumnText(2), stmt->ColumnText(3),
            stmt->ColumnText(4), stmt->IsColumnNull(5) ? "" : stmt->ColumnText(5),
            stmt->ColumnText(6), stmt->ColumnText(7),
            stmt->ColumnInt64(8) != 0, stmt->ColumnText(9),
            stmt->IsColumnNull(10) ? "" : stmt->ColumnText(10),
            stmt->IsColumnNull(11) ? 0 : static_cast<std::int32_t>(stmt->ColumnInt64(11)),
            stmt->IsColumnNull(12) ? 0 : static_cast<std::int32_t>(stmt->ColumnInt64(12))));
    }
    return result;
}

bool ScheduleStore::Update(const ScheduleDescriptor& sched, std::string* error) {
    auto stmt = m_store->Prepare(
        "UPDATE schedules SET agent_id=?, tag=?, type=?, next_fire=?, "
        "cron_expr=?, timezone=?, message=?, enabled=?, last_fire=?, "
        "fire_count=?, max_fires=? WHERE id=?");
    if (!stmt) {
        if (error) *error = "failed to prepare schedule update: " + m_store->ErrMsg();
        return false;
    }

    stmt->BindText(1, sched.agent_id);
    stmt->BindText(2, sched.tag);
    stmt->BindText(3, TypeToString(sched.type));
    stmt->BindText(4, sched.next_fire);
    stmt->BindText(5, sched.cron_expr);
    stmt->BindText(6, sched.timezone);
    stmt->BindText(7, sched.message);
    stmt->BindInt(8, sched.enabled ? 1 : 0);
    if (sched.last_fire.empty()) {
        stmt->BindNull(9);
    } else {
        stmt->BindText(9, sched.last_fire);
    }
    stmt->BindInt(10, sched.fire_count);
    if (sched.max_fires <= 0) {
        stmt->BindNull(11);
    } else {
        stmt->BindInt(11, sched.max_fires);
    }
    stmt->BindText(12, sched.id);

    // Trust Step() result, not Changes(). m_lastChanges is a shared atomic
    // across all connections/threads — concurrent queries overwrite it
    // between Execute() and Changes(), causing false negatives.
    stmt->Step();
    return true;
}

bool ScheduleStore::Delete(const std::string& id, std::string* error) {
    auto stmt = m_store->Prepare("DELETE FROM schedules WHERE id=?");
    if (!stmt) {
        if (error) *error = "failed to prepare schedule delete: " + m_store->ErrMsg();
        return false;
    }
    stmt->BindText(1, id);
    stmt->Step();
    return true;
}

std::vector<ScheduleDescriptor> ScheduleStore::GetDueSchedules(
        const std::string& nextFireUpperBound, int limit) const {
    std::vector<ScheduleDescriptor> result;

    auto stmt = m_store->Prepare(
        "SELECT id, agent_id, tag, type, next_fire, cron_expr, timezone, "
        "message, enabled, created_at, last_fire, fire_count, max_fires "
        "FROM schedules "
        "WHERE enabled = 1 AND next_fire <= ? "
        "ORDER BY next_fire ASC LIMIT ?");
    if (!stmt) return result;

    stmt->BindText(1, nextFireUpperBound);
    stmt->BindInt(2, limit);

    while (stmt->Step()) {
        result.push_back(RowToDescriptor(
            stmt->ColumnText(0), stmt->ColumnText(1),
            stmt->ColumnText(2), stmt->ColumnText(3),
            stmt->ColumnText(4), stmt->IsColumnNull(5) ? "" : stmt->ColumnText(5),
            stmt->ColumnText(6), stmt->ColumnText(7),
            stmt->ColumnInt64(8) != 0, stmt->ColumnText(9),
            stmt->IsColumnNull(10) ? "" : stmt->ColumnText(10),
            stmt->IsColumnNull(11) ? 0 : static_cast<std::int32_t>(stmt->ColumnInt64(11)),
            stmt->IsColumnNull(12) ? 0 : static_cast<std::int32_t>(stmt->ColumnInt64(12))));
    }
    return result;
}

int ScheduleStore::CountByAgent(const std::string& agentId) const {
    auto stmt = m_store->Prepare(
        "SELECT COUNT(*) FROM schedules WHERE agent_id=? AND enabled=1");
    if (!stmt) return 0;
    stmt->BindText(1, agentId);
    if (stmt->Step()) {
        return static_cast<int>(stmt->ColumnInt64(0));
    }
    return 0;
}

} // namespace animus::kernel

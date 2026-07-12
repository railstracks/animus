
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/lua/ScriptStore.h"
#include "animus_kernel/IDataStore.h"

#include <atomic>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace animus::kernel {

namespace {

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

std::string NowISO8601() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

} // anonymous namespace

// ============================================================================
// Construction
// ============================================================================

ScriptStore::ScriptStore(IDataStore* dataStore)
    : m_store(dataStore)
{
    EnsureSchema();
}

ScriptStore::~ScriptStore() = default;

// ============================================================================
// Schema
// ============================================================================

void ScriptStore::EnsureSchema() {
    m_store->Exec(
        "CREATE TABLE IF NOT EXISTS lua_scripts ("
        "  id TEXT PRIMARY KEY,"
        "  agent_id TEXT NOT NULL,"
        "  name TEXT NOT NULL,"
        "  source TEXT NOT NULL,"
        "  description TEXT DEFAULT '',"
        "  enabled INTEGER DEFAULT 1,"
        "  created_at TEXT NOT NULL,"
        "  updated_at TEXT NOT NULL"
        ")"
    );
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_lua_scripts_agent "
        "ON lua_scripts(agent_id)"
    );
    m_store->Exec(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_lua_scripts_agent_name "
        "ON lua_scripts(agent_id, name)"
    );
}

// ============================================================================
// Row mapping
// ============================================================================

ScriptDescriptor ScriptStore::RowToDescriptor(
    const std::string& id, const std::string& agentId,
    const std::string& name, const std::string& source,
    const std::string& description, bool enabled,
    const std::string& createdAt, const std::string& updatedAt) const
{
    ScriptDescriptor d;
    d.id = id;
    d.agent_id = agentId;
    d.name = name;
    d.source = source;
    d.description = description;
    d.enabled = enabled;
    d.created_at = createdAt;
    d.updated_at = updatedAt;
    return d;
}

// ============================================================================
// CRUD
// ============================================================================

bool ScriptStore::Create(const ScriptDescriptor& script, std::string* error) {
    auto stmt = m_store->Prepare(
        "INSERT INTO lua_scripts (id, agent_id, name, source, description, "
        "enabled, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"
    );
    if (!stmt) {
        if (error) *error = "failed to prepare insert: " + m_store->ErrMsg();
        return false;
    }

    stmt->BindText(1, script.id);
    stmt->BindText(2, script.agent_id);
    stmt->BindText(3, script.name);
    stmt->BindText(4, script.source);
    stmt->BindText(5, script.description);
    stmt->BindInt(6, script.enabled ? 1 : 0);
    stmt->BindText(7, script.created_at);
    stmt->BindText(8, script.updated_at);

    stmt->Step();
    return true;
}

std::string ScriptStore::CreateAndReturnId(ScriptDescriptor& script, std::string* error) {
    script.id = GenerateId();
    script.created_at = NowISO8601();
    script.updated_at = script.created_at;
    if (!Create(script, error)) {
        return "";
    }
    return script.id;
}

std::optional<ScriptDescriptor> ScriptStore::Get(const std::string& id) const {
    auto stmt = m_store->Prepare(
        "SELECT id, agent_id, name, source, description, enabled, "
        "created_at, updated_at FROM lua_scripts WHERE id = ?"
    );
    if (!stmt) return std::nullopt;

    stmt->BindText(1, id);
    if (!stmt->Step()) return std::nullopt;

    auto desc = RowToDescriptor(
        stmt->ColumnText(0), stmt->ColumnText(1), stmt->ColumnText(2),
        stmt->ColumnText(3), stmt->ColumnText(4),
        stmt->ColumnInt64(5) != 0,
        stmt->ColumnText(6), stmt->ColumnText(7)
    );
    return desc;
}

std::optional<ScriptDescriptor> ScriptStore::GetByName(
    const std::string& agentId, const std::string& name) const
{
    auto stmt = m_store->Prepare(
        "SELECT id, agent_id, name, source, description, enabled, "
        "created_at, updated_at FROM lua_scripts "
        "WHERE agent_id = ? AND name = ?"
    );
    if (!stmt) return std::nullopt;

    stmt->BindText(1, agentId);
    stmt->BindText(2, name);
    if (!stmt->Step()) return std::nullopt;

    auto desc = RowToDescriptor(
        stmt->ColumnText(0), stmt->ColumnText(1), stmt->ColumnText(2),
        stmt->ColumnText(3), stmt->ColumnText(4),
        stmt->ColumnInt64(5) != 0,
        stmt->ColumnText(6), stmt->ColumnText(7)
    );
    return desc;
}

std::vector<ScriptDescriptor> ScriptStore::List(const std::string& agentId) const {
    std::vector<ScriptDescriptor> results;

    auto stmt = m_store->Prepare(
        "SELECT id, agent_id, name, source, description, enabled, "
        "created_at, updated_at FROM lua_scripts "
        "WHERE agent_id = ? ORDER BY created_at"
    );
    if (!stmt) return results;

    stmt->BindText(1, agentId);
    while (stmt->Step()) {
        results.push_back(RowToDescriptor(
            stmt->ColumnText(0), stmt->ColumnText(1), stmt->ColumnText(2),
            stmt->ColumnText(3), stmt->ColumnText(4),
            stmt->ColumnInt64(5) != 0,
            stmt->ColumnText(6), stmt->ColumnText(7)
        ));
    }
    return results;
}

bool ScriptStore::Update(const ScriptDescriptor& script, std::string* error) {
    auto stmt = m_store->Prepare(
        "UPDATE lua_scripts SET name = ?, source = ?, description = ?, "
        "enabled = ?, updated_at = ? WHERE id = ?"
    );
    if (!stmt) {
        if (error) *error = "failed to prepare update: " + m_store->ErrMsg();
        return false;
    }

    stmt->BindText(1, script.name);
    stmt->BindText(2, script.source);
    stmt->BindText(3, script.description);
    stmt->BindInt(4, script.enabled ? 1 : 0);
    stmt->BindText(5, NowISO8601());
    stmt->BindText(6, script.id);

    stmt->Step();
    return true;
}

bool ScriptStore::Delete(const std::string& id, std::string* error) {
    auto stmt = m_store->Prepare(
        "DELETE FROM lua_scripts WHERE id = ?"
    );
    if (!stmt) {
        if (error) *error = "failed to prepare delete: " + m_store->ErrMsg();
        return false;
    }

    stmt->BindText(1, id);
    stmt->Step();
    return true;
}

int ScriptStore::CountByAgent(const std::string& agentId) const {
    auto stmt = m_store->Prepare(
        "SELECT COUNT(*) FROM lua_scripts WHERE agent_id = ?"
    );
    if (!stmt) return 0;

    stmt->BindText(1, agentId);
    if (!stmt->Step()) return 0;

    return static_cast<int>(stmt->ColumnInt64(0));
}

} // namespace animus::kernel
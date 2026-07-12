#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/IDataStore.h"
#include "animus_kernel/SchemaHelpers.h"

#include <iostream>
#include <sstream>

namespace animus::kernel {

// Schema: agent_config table
// DDL is dialect-dependent — datetime('now') is SQLite-specific.
// PostgreSQL uses now() or CURRENT_TIMESTAMP.

// ============================================================================
// Construction
// ============================================================================

AgentConfigStore::AgentConfigStore(IDataStore* dataStore)
    : m_store(dataStore)
{
    EnsureSchema();
}

void AgentConfigStore::EnsureSchema() {
    if (!m_store) return;
    if (m_store->Dialect() == DataStoreDialect::PostgreSQL) {
        schema::CreateTable(m_store, R"(
            CREATE TABLE IF NOT EXISTS agent_config (
              agent_id  TEXT NOT NULL,
              key       TEXT NOT NULL,
              value     TEXT NOT NULL DEFAULT '',
              updated_at TEXT NOT NULL DEFAULT (now()::text),
              PRIMARY KEY (agent_id, key)
            );
        )");
    } else {
        schema::CreateTable(m_store, R"(
            CREATE TABLE IF NOT EXISTS agent_config (
              agent_id  TEXT NOT NULL,
              key       TEXT NOT NULL,
              value     TEXT NOT NULL DEFAULT '',
              updated_at TEXT NOT NULL DEFAULT (datetime('now')),
              PRIMARY KEY (agent_id, key)
            );
        )");
    }
}

// ============================================================================
// Single key operations
// ============================================================================

std::string AgentConfigStore::Get(const std::string& agentId,
                                   const std::string& key) const {
    // Check cache first
    auto agentIt = m_cache.find(agentId);
    if (agentIt != m_cache.end()) {
        auto keyIt = agentIt->second.find(key);
        if (keyIt != agentIt->second.end()) {
            return keyIt->second;
        }
        // Key not in cache means it doesn't exist
        return "";
    }

    // Cache not warmed for this agent — query DB and warm it
    if (!m_store) return "";

    auto stmt = m_store->Prepare(
        "SELECT value FROM agent_config WHERE agent_id = ? AND key = ?");
    if (!stmt) return "";

    stmt->BindText(1, agentId);
    stmt->BindText(2, key);

    std::string value;
    if (stmt->Step()) {
        value = stmt->ColumnText(0);
    }
    stmt->Finalize();
    return value;
}

void AgentConfigStore::Set(const std::string& agentId,
                            const std::string& key,
                            const std::string& value) {
    // Write to DB first
    if (m_store) {
        const char* nowFunc = (m_store->Dialect() == DataStoreDialect::PostgreSQL)
            ? "now()::text" : "datetime('now')";
        std::string sql = std::string(
            "INSERT OR REPLACE INTO agent_config (agent_id, key, value, updated_at) "
            "VALUES (?, ?, ?, ") + nowFunc + ")";

        // PostgreSQL doesn't support OR REPLACE — use UPSERT instead
        if (m_store->Dialect() == DataStoreDialect::PostgreSQL) {
            sql = std::string(
                "INSERT INTO agent_config (agent_id, key, value, updated_at) "
                "VALUES (?, ?, ?, ") + nowFunc + ") "
                "ON CONFLICT (agent_id, key) DO UPDATE SET value = EXCLUDED.value, updated_at = EXCLUDED.updated_at";
        }

        auto stmt = m_store->Prepare(sql);
        if (stmt) {
            stmt->BindText(1, agentId);
            stmt->BindText(2, key);
            stmt->BindText(3, value);
            stmt->Step();
            stmt->Finalize();
        }
    }

    // Update cache
    m_cache[agentId][key] = value;
    m_cacheWarmed[agentId] = true;
}

void AgentConfigStore::Delete(const std::string& agentId,
                               const std::string& key) {
    if (m_store) {
        auto stmt = m_store->Prepare(
            "DELETE FROM agent_config WHERE agent_id = ? AND key = ?");
        if (stmt) {
            stmt->BindText(1, agentId);
            stmt->BindText(2, key);
            stmt->Step();
            stmt->Finalize();
        }
    }

    // Remove from cache
    auto agentIt = m_cache.find(agentId);
    if (agentIt != m_cache.end()) {
        agentIt->second.erase(key);
    }
}

// ============================================================================
// Bulk operations
// ============================================================================

std::unordered_map<std::string, std::string>
AgentConfigStore::GetAll(const std::string& agentId) const {
    // Return from cache if warmed
    auto warmedIt = m_cacheWarmed.find(agentId);
    if (warmedIt != m_cacheWarmed.end() && warmedIt->second) {
        auto agentIt = m_cache.find(agentId);
        if (agentIt != m_cache.end()) {
            return agentIt->second;
        }
        return {};
    }

    // Load from DB
    if (!m_store) { std::cerr << "[config-store] GetAll: no store!" << std::endl; return {}; }

    auto stmt = m_store->Prepare(
        "SELECT key, value FROM agent_config WHERE agent_id = ?");
    if (!stmt) { std::cerr << "[config-store] GetAll: prepare failed" << std::endl; return {}; }

    std::unordered_map<std::string, std::string> result;
    stmt->BindText(1, agentId);
    while (stmt->Step()) {
        result[stmt->ColumnText(0)] = stmt->ColumnText(1);
    }
    stmt->Finalize();

    std::cerr << "[config-store] GetAll(agentId='" << agentId << "'): " << result.size() << " entries" << std::endl;

    // Cache it
    m_cache[agentId] = result;
    m_cacheWarmed[agentId] = true;
    return result;
}

std::vector<std::string>
AgentConfigStore::ListKeys(const std::string& agentId) const {
    auto all = GetAll(agentId);
    std::vector<std::string> keys;
    keys.reserve(all.size());
    for (const auto& [k, _] : all) {
        keys.push_back(k);
    }
    return keys;
}

void AgentConfigStore::DeleteByPrefix(const std::string& agentId,
                                       const std::string& prefix) {
    if (m_store) {
        auto stmt = m_store->Prepare(
            "DELETE FROM agent_config WHERE agent_id = ? AND key LIKE ?");
        if (stmt) {
            stmt->BindText(1, agentId);
            stmt->BindText(2, prefix + "%");
            stmt->Step();
            stmt->Finalize();
        }
    }

    // Remove from cache
    auto agentIt = m_cache.find(agentId);
    if (agentIt != m_cache.end()) {
        auto& map = agentIt->second;
        for (auto it = map.begin(); it != map.end(); ) {
            if (it->first.substr(0, prefix.size()) == prefix) {
                it = map.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void AgentConfigStore::WarmCache(const std::string& agentId) {
    // Load all from DB into cache
    (void)GetAll(agentId);
}

void AgentConfigStore::Flush() {
    // All writes are immediate (INSERT OR REPLACE).
    // This exists for symmetry with other stores and future batching.
}

} // namespace animus::kernel

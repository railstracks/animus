#pragma once

#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

namespace animus::kernel {

class IDataStore;

// ============================================================================
// AgentConfigStore — persistent key-value config for Lua agents
//
// Backed by SQLite via IDataStore. Each key is scoped to an agent_id.
// Used by LuaState's config.get/set for values that must survive restarts
// (e.g. social credentials, API tokens).
//
// Read operations use an in-memory cache; writes go to SQLite first, then
// update the cache. This avoids DB hits on every Lua config.get call while
// maintaining consistency.
// ============================================================================

class AgentConfigStore {
public:
    explicit AgentConfigStore(IDataStore* dataStore);
    ~AgentConfigStore() = default;

    AgentConfigStore(const AgentConfigStore&) = delete;
    AgentConfigStore& operator=(const AgentConfigStore&) = delete;

    // --- Single key operations ---

    std::string Get(const std::string& agentId, const std::string& key) const;
    void Set(const std::string& agentId, const std::string& key, const std::string& value);
    void Delete(const std::string& agentId, const std::string& key);

    // --- Bulk operations ---

    /// Get all config entries for an agent as key→value pairs.
    std::unordered_map<std::string, std::string> GetAll(const std::string& agentId) const;

    /// Get all keys for an agent.
    std::vector<std::string> ListKeys(const std::string& agentId) const;

    /// Delete all keys matching a prefix for a given agent.
    /// e.g. DeleteByPrefix("default", "social.") removes all social config.
    void DeleteByPrefix(const std::string& agentId, const std::string& prefix);

    /// Load all values for an agent from SQLite into the in-memory cache.
    /// Called at startup or when a new agent's state is initialized.
    void WarmCache(const std::string& agentId);

    /// Flush any pending writes (called during graceful shutdown).
    void Flush();

private:
    void EnsureSchema();

    IDataStore* m_store;

    // Cache: agent_id → (key → value)
    // Mutable because Get operations are logically const but may need cache access.
    mutable std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_cache;

    // Track which agents have been warmed.
    mutable std::unordered_map<std::string, bool> m_cacheWarmed;
};

} // namespace animus::kernel

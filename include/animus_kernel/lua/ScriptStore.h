#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace animus::kernel {

class IDataStore;

// ============================================================================
// Script data model
// ============================================================================

struct ScriptDescriptor {
    std::string id;              // server-generated (hex)
    std::string agent_id;        // owning agent
    std::string name;            // human-readable script name
    std::string source;          // Lua source code
    std::string description;     // optional description
    std::string created_at;      // ISO-8601
    std::string updated_at;      // ISO-8601
    bool enabled{true};
};

// ============================================================================
// ScriptStore — SQLite-backed Lua script persistence
//
// Scripts are stored per-agent and survive restarts. The store handles
// creation, retrieval, update, and deletion of Lua scripts.
// ============================================================================

class ScriptStore {
public:
    explicit ScriptStore(IDataStore* dataStore);
    ~ScriptStore();

    ScriptStore(const ScriptStore&) = delete;
    ScriptStore& operator=(const ScriptStore&) = delete;

    // CRUD
    bool Create(const ScriptDescriptor& script, std::string* error);
    std::string CreateAndReturnId(ScriptDescriptor& script, std::string* error);
    std::optional<ScriptDescriptor> Get(const std::string& id) const;
    std::optional<ScriptDescriptor> GetByName(const std::string& agentId,
                                                const std::string& name) const;
    std::vector<ScriptDescriptor> List(const std::string& agentId) const;
    bool Update(const ScriptDescriptor& script, std::string* error);
    bool Delete(const std::string& id, std::string* error);

    // Count scripts per agent (for limit enforcement)
    int CountByAgent(const std::string& agentId) const;

private:
    void EnsureSchema();
    ScriptDescriptor RowToDescriptor(
        const std::string& id, const std::string& agentId,
        const std::string& name, const std::string& source,
        const std::string& description, bool enabled,
        const std::string& createdAt, const std::string& updatedAt) const;

    IDataStore* m_store;
};

} // namespace animus::kernel
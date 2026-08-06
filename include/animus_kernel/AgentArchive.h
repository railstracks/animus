#pragma once

#include "animus_kernel/IDataStore.h"

#include <json/json.h>

#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace animus::kernel {

// ============================================================================
// Agent Archive — composite export/import for agent portability
//
// The archive is a tar.gz containing JSONL files for each table. Internal
// auto-increment IDs are remapped to stable export IDs for cross-instance
// portability. See Ticket 139 for full design.
// ============================================================================

struct AgentArchiveComponentFlags {
    bool agent         = true;   // always
    bool memory        = true;   // always
    bool memoryFiles   = true;   // always
    bool ontology      = true;
    bool schedules     = true;
    bool gallivanting  = true;
    bool gallivantingHistory = false;
    bool diary         = true;
    bool sessions      = false;
    bool reports       = false;
    bool attachments   = false;
    bool luaScripts    = true;
    bool promptLogs    = false;
    bool authTokens    = false;
    bool projects      = false;
    bool embeddings    = true;   // include embedding blobs in memory_files
};

// ────────────────────────────────────────────────────────────────────────────
// Writer — exports an agent to a .agent tar.gz
// ────────────────────────────────────────────────────────────────────────────

class AgentArchiveWriter {
public:
    explicit AgentArchiveWriter(IDataStore* store);

    // Write the agent archive to the given path.
    // Returns empty string on success, error message on failure.
    std::string Write(const std::string& agentId,
                      const AgentArchiveComponentFlags& flags,
                      const std::string& outputPath);

private:
    IDataStore* m_store;

    // Helper: dump a table's rows for an agent to a JSONL string.
    // whereClause is appended to "SELECT * FROM <table>" (may be empty).
    // If agentColumn is non-empty, filters by agent_id.
    std::string DumpTableJSONL(const std::string& table,
                                const std::string& agentId,
                                const std::string& agentColumn = "agent_id",
                                const std::string& extraWhere = "");

    // Helper: dump sessions + dependent tables.
    std::string DumpSessionsJSONL(const std::string& agentId);
    std::string DumpSessionTurnsJSONL(const std::string& agentId);
    std::string DumpSessionDependentJSONL(const std::string& table,
                                           const std::string& agentId,
                                           const std::string& agentColumn);

    // Helper: dump attachments as binary files + index.
    std::string DumpAttachmentsIndexJSONL(const std::string& agentId);

    // Build manifest.json content.
    std::string BuildManifest(const std::string& agentId,
                               const AgentArchiveComponentFlags& flags);
};

// ────────────────────────────────────────────────────────────────────────────
// Reader — imports an agent from a .agent tar.gz
// ────────────────────────────────────────────────────────────────────────────

enum class ImportMode {
    New,      // create new agent, error if exists
    Merge,    // merge into existing agent
    Replace   // drop existing agent data, replace with archive
};

class AgentArchiveReader {
public:
    explicit AgentArchiveReader(IDataStore* store);

    // Import an agent archive.
    // Returns empty string on success, error message on failure.
    std::string Read(const std::string& archivePath,
                     ImportMode mode,
                     const std::string& targetAgentId = "");

    // Inspect an archive without importing. Returns manifest as JSON string.
    static std::string Inspect(const std::string& archivePath);

private:
    IDataStore* m_store;

    // ID remapping: export_id → new internal id
    std::unordered_map<std::string, int64_t> m_idMap;

    // Remap a foreign key reference using the id map.
    // Returns the new id, or -1 if not found.
    int64_t RemapId(const std::string& table, int64_t exportId) const;

    // Parse JSONL lines into a vector of JSON objects.
    std::vector<Json::Value> ParseJSONL(const std::string& content);

    // Import a JSONL into a table, remapping IDs.
    std::string ImportJSONL(const std::string& tableName,
                             const std::vector<Json::Value>& rows,
                             const std::string& idColumn = "id",
                             bool remapId = true);
};

} // namespace animus::kernel

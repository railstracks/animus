#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

namespace animus::kernel {

class IDataStore;

// ============================================================================
// Diary entry data model
// ============================================================================

struct DiaryEntry {
    std::string id;                 // UUID-style identifier
    std::string agent_id;           // Owner agent (multi-tenant scoping)
    std::int64_t timestamp_unix_ms{0};
    std::string layer;              // "reflection", "observation", "intention"
    std::string content;            // Freeform text
    std::string tags_json;          // JSON array of optional tags
    std::string session_id;         // Optional session context (nullable → empty string)
};

// ============================================================================
// DiaryStore — SQLite-backed diary CRUD
// ============================================================================

class DiaryStore {
public:
    explicit DiaryStore(IDataStore* dataStore);
    ~DiaryStore();

    DiaryStore(const DiaryStore&) = delete;
    DiaryStore& operator=(const DiaryStore&) = delete;

    // Schema
    void EnsureSchema();

    // CRUD
    DiaryEntry Create(const DiaryEntry& entry);
    std::optional<DiaryEntry> GetById(const std::string& id);
    std::vector<DiaryEntry> ListByAgent(const std::string& agentId,
                                         int64_t sinceUnixMs = 0,
                                         int64_t untilUnixMs = 0,
                                         const std::string& layer = "",
                                         int limit = 100,
                                         int offset = 0);
    std::vector<DiaryEntry> SearchByAgent(const std::string& agentId,
                                            const std::string& query,
                                            int limit = 50,
                                            int offset = 0);
    bool Delete(const std::string& id);
    std::int64_t CountByAgent(const std::string& agentId, const std::string& layer = "");
    std::optional<std::int64_t> LastEntryTimestampByAgent(const std::string& agentId);

private:
    IDataStore* m_store;
};

// ============================================================================
// DiaryManager — domain logic and API-facing operations
// ============================================================================

class DiaryManager {
public:
    struct OperationResult {
        int httpStatusCode{200};
        Json::Value body{Json::objectValue};
    };

    void Configure(DiaryStore* diaryStore);

    // Write a new entry (agent-facing)
    OperationResult WriteEntry(const std::string& agentId,
                                const Json::Value& body);

    // List entries with pagination (agent-facing, browse-oriented)
    OperationResult ListEntries(const std::string& agentId,
                                 const std::string& layer,
                                 int page,
                                 int limit);

    // Read entries by range/filter (agent-facing)
    OperationResult ReadEntries(const std::string& agentId,
                                 const std::string& layer,
                                 std::int64_t sinceUnixMs,
                                 std::int64_t untilUnixMs,
                                 int limit);

    // Full-text search within agent's diary (agent-facing)
    OperationResult SearchEntries(const std::string& agentId,
                                    const std::string& query,
                                    int page,
                                    int limit);

    // Delete an entry by id (agent-facing)
    OperationResult DeleteEntry(const std::string& agentId,
                                  const std::string& entryId);

    // Admin metadata — no content exposed
    OperationResult GetMetadata(const std::string& agentId);

    // Export diary as JSON (for .aidiary encryption layer)
    Json::Value ExportEntriesJson(const std::string& agentId);

    // Import entries from JSON (for .aidiary import)
    OperationResult ImportEntriesJson(const std::string& agentId,
                                       const Json::Value& entriesArray,
                                       const std::string& mergeStrategy);

private:
    static Json::Value EntryToJson(const DiaryEntry& entry);
    static std::string GenerateEntryId();

    DiaryStore* m_diaryStore{nullptr};
    mutable std::mutex m_diaryMutex;
};

} // namespace animus::kernel
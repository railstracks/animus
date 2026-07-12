
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/admin/DiaryManager.h"
#include "animus_kernel/IDataStore.h"

#include <chrono>
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>

#include <json/json.h>
#include <json/reader.h>
#include <json/writer.h>

namespace animus::kernel {

// ============================================================================
// Helpers
// ============================================================================

static int64_t NowUnixMs() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

static std::string GenerateUUID() {
    // Version 4 UUID (random)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    uint32_t a = dist(gen);
    uint32_t b = dist(gen);
    uint32_t c = dist(gen);
    uint32_t d = dist(gen);

    // Set version (4) and variant bits
    b = (b & 0xFFFF0FFF) | 0x00004000;
    c = (c & 0x3FFFFFFF) | 0x80000000;

    char buf[37];
    std::snprintf(buf, sizeof(buf),
        "%08x-%04x-%04x-%04x-%08x%04x",
        a,
        (b >> 16) & 0xFFFF,
        b & 0xFFFF,
        (c >> 16) & 0xFFFF,
        c & 0xFFFF,
        d & 0xFFFF);
    return std::string(buf);
}

// ============================================================================
// DiaryStore implementation
// ============================================================================

DiaryStore::DiaryStore(IDataStore* dataStore)
    : m_store(dataStore) {
    EnsureSchema();
}

DiaryStore::~DiaryStore() = default;

void DiaryStore::EnsureSchema() {
    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS diary_entries (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            entry_id TEXT NOT NULL UNIQUE,
            agent_id TEXT NOT NULL,
            timestamp_unix_ms INTEGER NOT NULL,
            layer TEXT NOT NULL DEFAULT 'reflection',
            content TEXT NOT NULL,
            tags TEXT NOT NULL DEFAULT '[]',
            session_id TEXT NOT NULL DEFAULT ''
        );
    )");

    // Index for agent-scoped queries
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_diary_agent_time "
        "ON diary_entries (agent_id, timestamp_unix_ms)");

    // Index for full-text search (LIKE-based for v1; FTS5 can be added later)
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_diary_agent_content "
        "ON diary_entries (agent_id, content)");

    // Index for layer filtering
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_diary_agent_layer "
        "ON diary_entries (agent_id, layer)");
}

DiaryEntry DiaryStore::Create(const DiaryEntry& entry) {
    auto stmt = m_store->Prepare(
        "INSERT INTO diary_entries "
        "(entry_id, agent_id, timestamp_unix_ms, layer, content, tags, session_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    if (!stmt) return {};

    stmt->BindText(1, entry.id);
    stmt->BindText(2, entry.agent_id);
    stmt->BindInt64(3, entry.timestamp_unix_ms);
    stmt->BindText(4, entry.layer);
    stmt->BindText(5, entry.content);
    stmt->BindText(6, entry.tags_json);
    stmt->BindText(7, entry.session_id);

    if (!stmt->Step()) {
        // INSERT should not return rows; Step() returning false is normal for successful INSERT
        // Error check via ErrMsg if needed
    }
    stmt->Finalize();

    return entry;
}

std::optional<DiaryEntry> DiaryStore::GetById(const std::string& id) {
    auto stmt = m_store->Prepare(
        "SELECT entry_id, agent_id, timestamp_unix_ms, layer, content, tags, session_id "
        "FROM diary_entries WHERE entry_id = ?");
    if (!stmt) return std::nullopt;

    stmt->BindText(1, id);
    if (!stmt->Step()) {
        stmt->Finalize();
        return std::nullopt;
    }

    DiaryEntry entry;
    entry.id = stmt->ColumnText(0);
    entry.agent_id = stmt->ColumnText(1);
    entry.timestamp_unix_ms = stmt->ColumnInt64(2);
    entry.layer = stmt->ColumnText(3);
    entry.content = stmt->ColumnText(4);
    entry.tags_json = stmt->ColumnText(5);
    entry.session_id = stmt->ColumnText(6);
    stmt->Finalize();

    return entry;
}

std::vector<DiaryEntry> DiaryStore::ListByAgent(
        const std::string& agentId,
        int64_t sinceUnixMs,
        int64_t untilUnixMs,
        const std::string& layer,
        int limit,
        int offset) {
    std::string sql =
        "SELECT entry_id, agent_id, timestamp_unix_ms, layer, content, tags, session_id "
        "FROM diary_entries WHERE agent_id = ?";
    if (sinceUnixMs > 0) sql += " AND timestamp_unix_ms >= ?";
    if (untilUnixMs > 0) sql += " AND timestamp_unix_ms <= ?";
    if (!layer.empty()) sql += " AND layer = ?";
    sql += " ORDER BY timestamp_unix_ms DESC LIMIT ? OFFSET ?";

    auto stmt = m_store->Prepare(sql);
    if (!stmt) return {};

    int idx = 1;
    stmt->BindText(idx++, agentId);
    if (sinceUnixMs > 0) stmt->BindInt64(idx++, sinceUnixMs);
    if (untilUnixMs > 0) stmt->BindInt64(idx++, untilUnixMs);
    if (!layer.empty()) stmt->BindText(idx++, layer);
    stmt->BindInt(idx++, limit);
    stmt->BindInt(idx, offset);

    std::vector<DiaryEntry> entries;
    while (stmt->Step()) {
        DiaryEntry entry;
        entry.id = stmt->ColumnText(0);
        entry.agent_id = stmt->ColumnText(1);
        entry.timestamp_unix_ms = stmt->ColumnInt64(2);
        entry.layer = stmt->ColumnText(3);
        entry.content = stmt->ColumnText(4);
        entry.tags_json = stmt->ColumnText(5);
        entry.session_id = stmt->ColumnText(6);
        entries.push_back(std::move(entry));
    }
    stmt->Finalize();
    return entries;
}

std::vector<DiaryEntry> DiaryStore::SearchByAgent(
        const std::string& agentId,
        const std::string& query,
        int limit,
        int offset) {
    // LIKE-based search for v1. FTS5 can replace this for better performance.
    std::string sql =
        "SELECT entry_id, agent_id, timestamp_unix_ms, layer, content, tags, session_id "
        "FROM diary_entries WHERE agent_id = ? AND content LIKE ? "
        "ORDER BY timestamp_unix_ms DESC LIMIT ? OFFSET ?";

    auto stmt = m_store->Prepare(sql);
    if (!stmt) return {};

    stmt->BindText(1, agentId);
    stmt->BindText(2, "%" + query + "%");
    stmt->BindInt(3, limit);
    stmt->BindInt(4, offset);

    std::vector<DiaryEntry> entries;
    while (stmt->Step()) {
        DiaryEntry entry;
        entry.id = stmt->ColumnText(0);
        entry.agent_id = stmt->ColumnText(1);
        entry.timestamp_unix_ms = stmt->ColumnInt64(2);
        entry.layer = stmt->ColumnText(3);
        entry.content = stmt->ColumnText(4);
        entry.tags_json = stmt->ColumnText(5);
        entry.session_id = stmt->ColumnText(6);
        entries.push_back(std::move(entry));
    }
    stmt->Finalize();
    return entries;
}

bool DiaryStore::Delete(const std::string& id) {
    auto stmt = m_store->Prepare("DELETE FROM diary_entries WHERE entry_id = ?");
    if (!stmt) return false;

    stmt->BindText(1, id);
    stmt->Step();
    stmt->Finalize();

    // Check rows affected
    int64_t affected = m_store->Changes();

    return affected > 0;
}

std::int64_t DiaryStore::CountByAgent(const std::string& agentId, const std::string& layer) {
    std::string sql = "SELECT COUNT(*) FROM diary_entries WHERE agent_id = ?";
    if (!layer.empty()) sql += " AND layer = ?";

    auto stmt = m_store->Prepare(sql);
    if (!stmt) return 0;

    stmt->BindText(1, agentId);
    if (!layer.empty()) stmt->BindText(2, layer);

    int64_t count = 0;
    if (stmt->Step()) {
        count = stmt->ColumnInt64(0);
    }
    stmt->Finalize();
    return count;
}

std::optional<std::int64_t> DiaryStore::LastEntryTimestampByAgent(const std::string& agentId) {
    auto stmt = m_store->Prepare(
        "SELECT MAX(timestamp_unix_ms) FROM diary_entries WHERE agent_id = ?");
    if (!stmt) return std::nullopt;

    stmt->BindText(1, agentId);
    std::optional<std::int64_t> result;
    if (stmt->Step()) {
        if (!stmt->IsColumnNull(0)) {
            result = stmt->ColumnInt64(0);
        }
    }
    stmt->Finalize();
    return result;
}

// ============================================================================
// DiaryManager implementation
// ============================================================================

void DiaryManager::Configure(DiaryStore* diaryStore) {
    std::lock_guard<std::mutex> lock(m_diaryMutex);
    m_diaryStore = diaryStore;
}

DiaryManager::OperationResult DiaryManager::WriteEntry(
        const std::string& agentId,
        const Json::Value& body) {
    OperationResult result;
    std::lock_guard<std::mutex> lock(m_diaryMutex);

    if (!m_diaryStore) {
        result.httpStatusCode = 500;
        result.body["error"] = "diary store not available";
        return result;
    }

    // Validate required fields
    if (!body.isMember("content") || !body["content"].isString() ||
        body["content"].asString().empty()) {
        result.httpStatusCode = 400;
        result.body["error"] = "content is required";
        return result;
    }

    DiaryEntry entry;
    entry.id = GenerateEntryId();
    entry.agent_id = agentId;
    entry.timestamp_unix_ms = NowUnixMs();

    // Layer (optional, defaults to "reflection")
    if (body.isMember("layer") && body["layer"].isString()) {
        std::string layer = body["layer"].asString();
        if (layer == "reflection" || layer == "observation" || layer == "intention") {
            entry.layer = layer;
        } else {
            entry.layer = "reflection";
        }
    } else {
        entry.layer = "reflection";
    }

    entry.content = body["content"].asString();

    // Tags (optional, JSON array)
    if (body.isMember("tags") && body["tags"].isArray()) {
        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";
        entry.tags_json = Json::writeString(wb, body["tags"]);
    } else {
        entry.tags_json = "[]";
    }

    // Session context (optional)
    if (body.isMember("session_id") && body["session_id"].isString()) {
        entry.session_id = body["session_id"].asString();
    }

    auto created = m_diaryStore->Create(entry);
    if (created.id.empty()) {
        result.httpStatusCode = 500;
        result.body["error"] = "failed to create diary entry";
        return result;
    }

    result.httpStatusCode = 201;
    result.body = EntryToJson(created);
    return result;
}

DiaryManager::OperationResult DiaryManager::ListEntries(
        const std::string& agentId,
        const std::string& layer,
        int page,
        int limit) {
    OperationResult result;
    std::lock_guard<std::mutex> lock(m_diaryMutex);

    if (!m_diaryStore) {
        result.httpStatusCode = 500;
        result.body["error"] = "diary store not available";
        return result;
    }

    if (limit <= 0 || limit > 100) limit = 20;
    if (page < 1) page = 1;

    int offset = (page - 1) * limit;
    auto totalCount = m_diaryStore->CountByAgent(agentId, layer);

    auto entries = m_diaryStore->ListByAgent(agentId, 0, 0, layer, limit, offset);

    result.body["entries"] = Json::Value(Json::arrayValue);
    for (const auto& entry : entries) {
        result.body["entries"].append(EntryToJson(entry));
    }
    result.body["count"] = static_cast<Json::Int>(entries.size());
    result.body["total"] = static_cast<Json::Int64>(totalCount);
    result.body["page"] = page;
    result.body["limit"] = limit;
    result.body["total_pages"] = static_cast<Json::Int>((totalCount + limit - 1) / limit);
    return result;
}

DiaryManager::OperationResult DiaryManager::ReadEntries(
        const std::string& agentId,
        const std::string& layer,
        std::int64_t sinceUnixMs,
        std::int64_t untilUnixMs,
        int limit) {
    OperationResult result;
    std::lock_guard<std::mutex> lock(m_diaryMutex);

    if (!m_diaryStore) {
        result.httpStatusCode = 500;
        result.body["error"] = "diary store not available";
        return result;
    }

    if (limit <= 0 || limit > 1000) limit = 100;

    auto entries = m_diaryStore->ListByAgent(
        agentId, sinceUnixMs, untilUnixMs, layer, limit);

    result.body["entries"] = Json::Value(Json::arrayValue);
    for (const auto& entry : entries) {
        result.body["entries"].append(EntryToJson(entry));
    }
    result.body["count"] = static_cast<Json::Int>(entries.size());
    return result;
}

DiaryManager::OperationResult DiaryManager::SearchEntries(
        const std::string& agentId,
        const std::string& query,
        int page,
        int limit) {
    OperationResult result;
    std::lock_guard<std::mutex> lock(m_diaryMutex);

    if (!m_diaryStore) {
        result.httpStatusCode = 500;
        result.body["error"] = "diary store not available";
        return result;
    }

    if (query.empty()) {
        result.httpStatusCode = 400;
        result.body["error"] = "query is required";
        return result;
    }

    if (limit <= 0 || limit > 100) limit = 20;
    if (page < 1) page = 1;

    int offset = (page - 1) * limit;

    auto entries = m_diaryStore->SearchByAgent(agentId, query, limit, offset);

    result.body["entries"] = Json::Value(Json::arrayValue);
    for (const auto& entry : entries) {
        result.body["entries"].append(EntryToJson(entry));
    }
    result.body["count"] = static_cast<Json::Int>(entries.size());
    result.body["page"] = page;
    result.body["limit"] = limit;
    result.body["query"] = query;
    return result;
}

DiaryManager::OperationResult DiaryManager::DeleteEntry(
        const std::string& agentId,
        const std::string& entryId) {
    OperationResult result;
    std::lock_guard<std::mutex> lock(m_diaryMutex);

    if (!m_diaryStore) {
        result.httpStatusCode = 500;
        result.body["error"] = "diary store not available";
        return result;
    }

    // Verify entry belongs to this agent
    auto entry = m_diaryStore->GetById(entryId);
    if (!entry.has_value()) {
        result.httpStatusCode = 404;
        result.body["error"] = "diary entry not found";
        return result;
    }

    if (entry->agent_id != agentId) {
        result.httpStatusCode = 403;
        result.body["error"] = "diary entry does not belong to this agent";
        return result;
    }

    if (!m_diaryStore->Delete(entryId)) {
        result.httpStatusCode = 500;
        result.body["error"] = "failed to delete diary entry";
        return result;
    }

    result.body["deleted"] = entryId;
    return result;
}

DiaryManager::OperationResult DiaryManager::GetMetadata(const std::string& agentId) {
    OperationResult result;
    std::lock_guard<std::mutex> lock(m_diaryMutex);

    if (!m_diaryStore) {
        result.httpStatusCode = 500;
        result.body["error"] = "diary store not available";
        return result;
    }

    auto count = m_diaryStore->CountByAgent(agentId);
    auto lastTs = m_diaryStore->LastEntryTimestampByAgent(agentId);

    result.body["exists"] = count > 0;
    result.body["entry_count"] = static_cast<Json::Int>(count);
    if (lastTs.has_value()) {
        result.body["last_entry_date"] = static_cast<Json::Int64>(*lastTs);
    }
    return result;
}

Json::Value DiaryManager::ExportEntriesJson(const std::string& agentId) {
    std::lock_guard<std::mutex> lock(m_diaryMutex);

    if (!m_diaryStore) return {};

    // Export all entries for this agent (no limit for export)
    auto entries = m_diaryStore->ListByAgent(agentId, 0, 0, "", 1000000);

    Json::Value entriesArray(Json::arrayValue);
    for (const auto& entry : entries) {
        entriesArray.append(EntryToJson(entry));
    }
    return entriesArray;
}

DiaryManager::OperationResult DiaryManager::ImportEntriesJson(
        const std::string& agentId,
        const Json::Value& entriesArray,
        const std::string& mergeStrategy) {
    OperationResult result;
    std::lock_guard<std::mutex> lock(m_diaryMutex);

    if (!m_diaryStore) {
        result.httpStatusCode = 500;
        result.body["error"] = "diary store not available";
        return result;
    }

    if (!entriesArray.isArray()) {
        result.httpStatusCode = 400;
        result.body["error"] = "entries must be a JSON array";
        return result;
    }

    if (mergeStrategy != "merge" && mergeStrategy != "replace") {
        result.httpStatusCode = 400;
        result.body["error"] = "merge_strategy must be 'merge' or 'replace'";
        return result;
    }

    // Replace strategy: delete all existing entries for this agent
    if (mergeStrategy == "replace") {
        auto existing = m_diaryStore->ListByAgent(agentId, 0, 0, "", 1000000);
        for (const auto& e : existing) {
            m_diaryStore->Delete(e.id);
        }
    }

    int imported = 0;
    int skipped = 0;
    for (const auto& item : entriesArray) {
        // For merge strategy, skip entries that already exist for this agent
        if (mergeStrategy == "merge" && item.isMember("id") && item["id"].isString()) {
            auto existing = m_diaryStore->GetById(item["id"].asString());
            if (existing.has_value() && existing->agent_id == agentId) {
                skipped++;
                continue;
            }
        }

        DiaryEntry entry;
        if (item.isMember("id") && item["id"].isString()) {
            entry.id = item["id"].asString();
            // If this entry_id already exists (from another agent), generate a new one
            if (m_diaryStore->GetById(entry.id).has_value()) {
                entry.id = GenerateEntryId();
            }
        } else {
            entry.id = GenerateEntryId();
        }
        entry.agent_id = agentId;

        if (item.isMember("timestamp") && item["timestamp"].isInt64()) {
            entry.timestamp_unix_ms = item["timestamp"].asInt64();
        } else if (item.isMember("timestamp_unix_ms") && item["timestamp_unix_ms"].isInt64()) {
            entry.timestamp_unix_ms = item["timestamp_unix_ms"].asInt64();
        } else {
            entry.timestamp_unix_ms = NowUnixMs();
        }

        if (item.isMember("layer") && item["layer"].isString()) {
            entry.layer = item["layer"].asString();
        } else {
            entry.layer = "reflection";
        }

        if (item.isMember("content") && item["content"].isString()) {
            entry.content = item["content"].asString();
        } else {
            skipped++;
            continue;  // Content is required
        }

        if (item.isMember("tags")) {
            if (item["tags"].isArray()) {
                Json::StreamWriterBuilder wb;
                wb.settings_["indentation"] = "";
                entry.tags_json = Json::writeString(wb, item["tags"]);
            }
        } else {
            entry.tags_json = "[]";
        }

        if (item.isMember("session_id") && item["session_id"].isString()) {
            entry.session_id = item["session_id"].asString();
        }

        m_diaryStore->Create(entry);
        imported++;
    }

    result.body["imported"] = imported;
    result.body["skipped"] = skipped;
    result.body["strategy"] = mergeStrategy;
    return result;
}

// ============================================================================
// Private helpers
// ============================================================================

Json::Value DiaryManager::EntryToJson(const DiaryEntry& entry) {
    Json::Value json(Json::objectValue);
    json["id"] = entry.id;
    json["agent_id"] = entry.agent_id;
    json["timestamp"] = static_cast<Json::Int64>(entry.timestamp_unix_ms);
    json["layer"] = entry.layer;
    json["content"] = entry.content;
    json["session_id"] = entry.session_id;

    // Parse tags JSON back to Json::Value
    Json::Value tags;
    Json::CharReaderBuilder rb;
    std::istringstream tagsStream(entry.tags_json);
    std::string parseErr;
    if (!Json::parseFromStream(rb, tagsStream, &tags, &parseErr)) {
        tags = Json::Value(Json::arrayValue);
    }
    json["tags"] = tags;

    return json;
}

std::string DiaryManager::GenerateEntryId() {
    return "diary_" + GenerateUUID();
}

} // namespace animus::kernel
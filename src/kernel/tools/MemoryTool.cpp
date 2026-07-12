#include "animus_kernel/tools/MemoryTool.h"

#include "animus_kernel/MemorySearch.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/SessionManager.h"
#include "animus_kernel/AgentStore.h"

#include <chrono>
#include <json/json.h>
#include <json/writer.h>
#include <regex>
#include <sstream>

namespace animus::kernel {

namespace {

int64_t AgentIdToNumeric(const std::string& agentId) {
    if (agentId.empty()) return 0;
    try { return std::stoll(agentId); }
    catch (...) { return 0; }
}

Json::Value ParseArgs(const std::string& args) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(args);
    std::string errors;
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        return {};
    }
    return root;
}

std::string GetStringField(const Json::Value& args, const std::string& key,
                             const std::string& defaultVal = "") {
    if (args.isMember(key) && args[key].isString()) {
        return args[key].asString();
    }
    return defaultVal;
}

int GetIntField(const Json::Value& args, const std::string& key, int defaultVal = 0) {
    if (args.isMember(key) && args[key].isInt()) {
        return args[key].asInt();
    }
    return defaultVal;
}

double GetDoubleField(const Json::Value& args, const std::string& key, double defaultVal = 0.0) {
    if (args.isMember(key) && args[key].isDouble()) {
        return args[key].asDouble();
    }
    return defaultVal;
}

std::string ResultToJsonString(const Json::Value& body) {
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, body);
}

std::string ErrorJson(const std::string& error) {
    Json::Value root(Json::objectValue);
    root["error"] = error;
    return ResultToJsonString(root);
}

std::vector<std::string> GetStringArray(const Json::Value& args, const std::string& key) {
    std::vector<std::string> result;
    if (args.isMember(key) && args[key].isArray()) {
        for (const auto& item : args[key]) {
            if (item.isString()) {
                result.push_back(item.asString());
            }
        }
    }
    return result;
}
} // anonymous namespace

// ============================================================================
// Construction
// ============================================================================

MemoryTool::MemoryTool(memory::MemorySearch* search,
                       memory::MemoryStore* store,
                       SessionManager* sessions,
                       memory::MemoryFileStore* fileStore,
                       AgentStore* agentStore)
    : m_search(search)
    , m_store(store)
    , m_sessions(sessions)
    , m_fileStore(fileStore)
    , m_agentStore(agentStore) {}

// ============================================================================
// Tool definition
// ============================================================================

ToolDefinition MemoryTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "memory";
    def.description =
        "Access your memory system. Search across all your accumulated knowledge — "
        "distilled memories, structured facts, stored documents, private reflections, "
        "and session history. Recall relevant observations, pin important items for "
        "consolidation attention, inspect your memory layer state, or manage "
        "your memory files (list, read, write, delete).";
    def.resultMode = ToolResultMode::deliver_to_model;

    def.parameters.push_back({
        "action", "string",
        "Memory operation to perform",
        true, "", {"search", "recall", "pin", "inspect", "file_list", "file_browse", "file_read", "file_search", "file_headings", "file_write", "file_delete"}
    });

    // search parameters
    def.parameters.push_back({
        "query", "string",
        "Search terms or natural language query (required for search and recall)",
        false
    });
    def.parameters.push_back({
        "domains", "array",
        "Domains to search: episodic, semantic, files, diary, sessions. Default: all.",
        false
    });
    def.parameters.push_back({
        "limit", "integer",
        "Max results (default: 20, max: 50)",
        false
    });
    def.parameters.push_back({
        "page", "integer",
        "Pagination offset (default: 0)",
        false
    });

    // recall parameters
    def.parameters.push_back({
        "count", "integer",
        "Max results for recall (default: 5, max: 20)",
        false
    });
    def.parameters.push_back({
        "layers", "array",
        "Restrict recall to specific layers (array of layer names). Default: all active.",
        false
    });
    def.parameters.push_back({
        "min_weight", "number",
        "Minimum observation weight for recall (0.0-1.0)",
        false
    });

    // pin parameters
    def.parameters.push_back({
        "observation_id", "string",
        "ID of the observation to pin (required for pin)",
        false
    });
    def.parameters.push_back({
        "reason", "string",
        "Why this observation matters for consolidation (required for pin)",
        false
    });

    // inspect parameters
    def.parameters.push_back({
        "layer", "string",
        "Layer name to inspect. Empty or omitted = summary of all layers.",
        false
    });

    // file_read / file_write / file_delete parameters
    def.parameters.push_back({
        "source_path", "string",
        "Memory file path (for file_read, file_write, file_delete, file_headings)",
        false
    });
    def.parameters.push_back({
        "content", "string",
        "Full file content (for file_write)",
        false
    });
    def.parameters.push_back({
        "file_id", "integer",
        "Memory file ID (alternative to source_path for file_read, file_delete, file_headings)",
        false
    });
    def.parameters.push_back({
        "section", "string",
        "Section title to extract (file_read). Case-insensitive substring match.",
        false
    });
    def.parameters.push_back({
        "section_number", "integer",
        "Section number from headings list (file_read). Alternative to section name.",
        false
    });
    def.parameters.push_back({
        "page", "integer",
        "Page number for paginated reading (default: 1)",
        false
    });
    def.parameters.push_back({
        "page_size", "integer",
        "Lines per page for paginated reading (default: 200, max: 1000)",
        false
    });
    def.parameters.push_back({
        "lines", "string",
        "Line range for file_read, e.g. \"50-80\". Alternative to page/page_size.",
        false
    });
    def.parameters.push_back({
        "regex", "string",
        "Regex pattern for file_search (searches across all memory files)",
        false
    });
    def.parameters.push_back({
        "results_per_page", "integer",
        "Items per page for file_browse (default: 10, max: 50)",
        false
    });

    return def;
}

// ============================================================================
// Execute
// ============================================================================

ToolResult MemoryTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    auto args = ParseArgs(call.arguments);
    if (args.isNull()) {
        result.success = false;
        result.error = "failed to parse arguments";
        return result;
    }

    std::string agentId = GetStringField(args, "__agent_id", "");
    std::string action = GetStringField(args, "action", "");

    if (action.empty()) {
        result.success = false;
        result.error = "action is required (search, recall, pin, inspect, file_list, file_browse, file_read, file_search, file_headings, file_write, file_delete)";
        return result;
    }

    if (action == "search") {
        return HandleSearch(agentId, call.arguments);
    } else if (action == "recall") {
        return HandleRecall(agentId, call.arguments);
    } else if (action == "pin") {
        return HandlePin(agentId, call.arguments);
    } else if (action == "inspect") {
        return HandleInspect(agentId, call.arguments);
    } else if (action == "file_list" || action == "file_browse") {
        return HandleFileBrowse(agentId, call.arguments);
    } else if (action == "file_read") {
        return HandleFileRead(agentId, call.arguments);
    } else if (action == "file_search") {
        return HandleFileSearch(agentId, call.arguments);
    } else if (action == "file_headings") {
        return HandleFileHeadings(agentId, call.arguments);
    } else if (action == "file_write") {
        return HandleFileWrite(agentId, call.arguments);
    } else if (action == "file_delete") {
        return HandleFileDelete(agentId, call.arguments);
    } else {
        result.success = false;
        result.error = "unknown action: " + action;
        return result;
    }
}

// ============================================================================
// search — FTS5 across all memory domains
// ============================================================================

ToolResult MemoryTool::HandleSearch(const std::string& agentId,
                                     const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    std::string query = GetStringField(args, "query", "");
    if (query.empty()) {
        result.success = false;
        result.error = "query is required for search";
        return result;
    }

    int limit = GetIntField(args, "limit", 20);
    if (limit < 1) limit = 20;
    if (limit > 50) limit = 50;

    // Parse domain toggles
    auto domainList = GetStringArray(args, "domains");
    memory::MemorySearchDomain domains;
    if (!domainList.empty()) {
        // Explicit domains specified — disable all, then enable listed
        domains.observations = false;
        domains.ontology = false;
        domains.raw_files = false;
        domains.diary = false;
        domains.sessions = false;
        for (const auto& d : domainList) {
            if (d == "episodic") domains.observations = true;
            else if (d == "semantic") domains.ontology = true;
            else if (d == "files") domains.raw_files = true;
            else if (d == "diary") domains.diary = true;
            else if (d == "sessions") domains.sessions = true;
        }
    }

    if (!m_search) {
        result.success = false;
        result.error = "memory search is not available";
        return result;
    }

    // Agent_id for MemorySearch is numeric — default agent is 0
    auto results = m_search->Search(query, agentId, domains, limit);

    Json::Value body(Json::objectValue);
    body["total"] = static_cast<Json::Int>(results.size());
    Json::Value items(Json::arrayValue);
    for (const auto& r : results) {
        Json::Value item(Json::objectValue);
        item["domain"] = r.domain;
        item["id"] = static_cast<Json::Int64>(r.id);
        item["text"] = r.text;
        item["relevance"] = r.relevance;
        item["status"] = r.status;
        if (r.created_at_unix_ms > 0)
            item["created_at"] = static_cast<Json::Int64>(r.created_at_unix_ms);
        if (r.updated_at_unix_ms > 0)
            item["updated_at"] = static_cast<Json::Int64>(r.updated_at_unix_ms);
        if (r.layer_id.has_value()) {
            item["layer_id"] = static_cast<Json::Int64>(*r.layer_id);
        }
        if (r.entity_path.has_value()) {
            item["entity_path"] = *r.entity_path;
        }
        if (r.source_path.has_value()) {
            item["source_path"] = *r.source_path;
        }
        if (r.layer.has_value()) {
            item["layer"] = *r.layer;
        }
        if (r.session_key.has_value()) {
            item["session_key"] = *r.session_key;
        }
        items.append(item);
    }
    body["results"] = items;

    result.success = true;
    result.output = ResultToJsonString(body);
    return result;
}

// ============================================================================
// recall — weighted retrieval from episodic memory layers
// ============================================================================

ToolResult MemoryTool::HandleRecall(const std::string& agentId,
                                     const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    std::string query = GetStringField(args, "query", "");
    if (query.empty()) {
        result.success = false;
        result.error = "query is required for recall";
        return result;
    }

    int count = GetIntField(args, "count", 5);
    if (count < 1) count = 5;
    if (count > 20) count = 20;

    double minWeight = GetDoubleField(args, "min_weight", 0.0);

    auto layerFilter = GetStringArray(args, "layers");

    if (!m_store) {
        result.success = false;
        result.error = "memory store is not available";
        return result;
    }

    // Search observations via FTS5 in episodic layers
    // Use MemorySearch with only observations domain enabled
    if (m_search) {
        memory::MemorySearchDomain domains;
        domains.observations = true;
        domains.ontology = false;
        domains.raw_files = false;
        domains.diary = false;
        domains.sessions = false;

        auto results = m_search->Search(query, agentId, domains, count * 3);

        // Filter by layer names if specified
        Json::Value items(Json::arrayValue);
        int emitted = 0;
        for (const auto& r : results) {
            if (emitted >= count) break;
            if (r.relevance < minWeight) continue;

            // If layer filter is specified, check the observation's layer
            if (!layerFilter.empty() && r.layer_id.has_value()) {
                auto layer = m_store->GetLayer(*r.layer_id);
                if (!layer) continue;
                bool match = false;
                for (const auto& name : layerFilter) {
                    if (layer->name == name) { match = true; break; }
                }
                if (!match) continue;
            }

            Json::Value item(Json::objectValue);
            item["id"] = static_cast<Json::Int64>(r.id);
            item["content"] = r.text;
            item["relevance"] = r.relevance;
            item["status"] = r.status;
            if (r.created_at_unix_ms > 0)
                item["created_at"] = static_cast<Json::Int64>(r.created_at_unix_ms);
            if (r.updated_at_unix_ms > 0)
                item["updated_at"] = static_cast<Json::Int64>(r.updated_at_unix_ms);
            if (r.layer_id.has_value()) {
                auto layer = m_store->GetLayer(*r.layer_id);
                if (layer) {
                    item["layer"] = layer->name;
                }
                item["layer_id"] = static_cast<Json::Int64>(*r.layer_id);
            }
            items.append(item);
            emitted++;
        }

        Json::Value body(Json::objectValue);
        body["total"] = static_cast<Json::Int>(emitted);
        body["results"] = items;

        result.success = true;
        result.output = ResultToJsonString(body);
    } else {
        result.success = false;
        result.error = "memory search is not available";
    }
    return result;
}

// ============================================================================
// pin — annotate an observation for consolidation attention
// ============================================================================

ToolResult MemoryTool::HandlePin(const std::string& agentId,
                                  const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    std::string observationId = GetStringField(args, "observation_id", "");
    std::string reason = GetStringField(args, "reason", "");

    if (observationId.empty()) {
        result.success = false;
        result.error = "observation_id is required for pin";
        return result;
    }
    if (reason.empty()) {
        result.success = false;
        result.error = "reason is required for pin";
        return result;
    }

    if (!m_store) {
        result.success = false;
        result.error = "memory store is not available";
        return result;
    }

    int64_t obsId = AgentIdToNumeric(observationId);
    auto obs = m_store->GetObservation(obsId);
    if (!obs) {
        result.success = false;
        result.error = "observation not found: " + observationId;
        return result;
    }

    // Verify agent scope
    if (obs->agent_id != agentId) {
        result.success = false;
        result.error = "observation does not belong to this agent";
        return result;
    }

    // Append pinned_reason to tags_json
    Json::Value tags;
    if (!obs->tags_json.empty()) {
        Json::CharReaderBuilder builder;
        std::istringstream stream(obs->tags_json);
        std::string errors;
        Json::parseFromStream(builder, stream, &tags, &errors);
    }
    if (!tags.isArray()) {
        tags = Json::Value(Json::arrayValue);
    }

    // Store pin as a special tag entry
    Json::Value pinEntry(Json::objectValue);
    pinEntry["pinned_reason"] = reason;
    pinEntry["pinned_at_ms"] = static_cast<Json::Int64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    tags.append(pinEntry);

    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    obs->tags_json = Json::writeString(wb, tags);
    obs->updated_at_unix_ms = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    m_store->UpdateObservation(*obs);

    Json::Value body(Json::objectValue);
    body["success"] = true;
    body["observation_id"] = observationId;
    body["pin_timestamp_unix_ms"] = static_cast<Json::Int64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    result.success = true;
    result.output = ResultToJsonString(body);
    return result;
}

// ============================================================================
// inspect — view memory layer state and perspectives
// ============================================================================

ToolResult MemoryTool::HandleInspect(const std::string& agentId,
                                      const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";

    auto args = ParseArgs(rawArgs);
    if (!m_store) {
        result.success = false;
        result.error = "memory store is not available";
        return result;
    }

    std::string layerName = GetStringField(args, "layer", "");

    auto layers = m_store->ListLayers();
    Json::Value items(Json::arrayValue);

    for (const auto& layer : layers) {
        if (!layerName.empty() && layer.name != layerName) continue;

        Json::Value item(Json::objectValue);
        item["name"] = layer.name;
        item["horizon"] = layer.horizon;
        item["enabled"] = layer.enabled;
        item["evaluation_interval_seconds"] = static_cast<Json::Int64>(layer.evaluation_interval_seconds);
        item["cron_expr"] = layer.cron_expr;

        // Observation count for this agent in this layer
        auto observations = m_store->ListObservationsForLayer(layer.id);
        item["observation_count"] = static_cast<Json::Int>(observations.size());

        // Perspectives
        auto perspective = m_store->GetPerspective(layer.id);
        if (perspective) {
            Json::Value perspectives(Json::objectValue);
            perspectives["retrospective"] = perspective->retrospective;
            perspectives["retrospective_valence"] = perspective->retrospective_valence;
            perspectives["current"] = perspective->current_perspective;
            perspectives["current_valence"] = perspective->current_valence;
            perspectives["future"] = perspective->future_perspective;
            perspectives["future_valence"] = perspective->future_valence;
            item["perspectives"] = perspectives;
        } else {
            item["perspectives"] = Json::nullValue;
        }

        items.append(item);
    }

    Json::Value body(Json::objectValue);
    body["layers"] = items;

    result.success = true;
    result.output = ResultToJsonString(body);
    return result;
}

// ============================================================================
// MemoryFile management handlers
// ============================================================================

int64_t MemoryTool::AgentIdToNumeric(const std::string& agentId) const {
    // Resolve hex agent ID to numeric DB ID via AgentStore
    if (m_agentStore && !agentId.empty()) {
        auto agent = m_agentStore->GetById(agentId);
        if (agent) return agent->numeric_id;
    }
    // Fallback: try direct numeric parse
    if (agentId.empty()) return 0;
    try { return std::stoll(agentId); }
    catch (...) { return 0; }
}

ToolResult MemoryTool::HandleFileBrowse(const std::string& agentId, const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";
    if (!m_fileStore) {
        result.success = false;
        result.error = "memory file store not available";
        return result;
    }

    auto args = ParseArgs(rawArgs);
    int64_t agentNum = AgentIdToNumeric(agentId);

    // Pagination parameters
    int page = args.get("page", 1).asInt();
    if (page < 1) page = 1;
    int resultsPerPage = args.get("results_per_page", 10).asInt();
    if (resultsPerPage < 1) resultsPerPage = 10;
    if (resultsPerPage > 50) resultsPerPage = 50;

    // Fetch all files, filter by agent
    auto allFiles = m_fileStore->ListFiles(std::nullopt, 1000, 0);
    std::vector<const memory::MemoryFile*> agentFiles;
    for (const auto& f : allFiles) {
        if (f.agent_id == agentNum || f.agent_id == 0) {
            agentFiles.push_back(&f);
        }
    }

    int totalFiles = static_cast<int>(agentFiles.size());
    int totalPages = (totalFiles + resultsPerPage - 1) / resultsPerPage;
    if (totalPages == 0) totalPages = 1;

    int startIdx = (page - 1) * resultsPerPage;
    int endIdx = std::min(startIdx + resultsPerPage, totalFiles);

    Json::Value out(Json::objectValue);
    out["total_files"] = totalFiles;
    out["total_pages"] = totalPages;
    out["current_page"] = page;
    out["results_per_page"] = resultsPerPage;

    Json::Value filesArr(Json::arrayValue);
    for (int i = startIdx; i < endIdx; ++i) {
        const auto* f = agentFiles[i];
        int lineCount = static_cast<int>(
            std::count(f->content.begin(), f->content.end(), '\n') + 1);

        Json::Value item(Json::objectValue);
        item["id"] = static_cast<Json::Int64>(f->id);
        item["source_path"] = f->source_path;
        item["file_type"] = memory::MemoryFileStore::FileTypeToString(f->file_type);
        item["content_mutable"] = f->content_mutable;
        item["superseded"] = f->superseded;
        item["status"] = static_cast<int>(f->status) == 1 ? "processed" : "unprocessed";
        item["size_bytes"] = static_cast<Json::UInt64>(f->content.size());
        item["lines"] = lineCount;
        filesArr.append(item);
    }
    out["files"] = filesArr;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "  ";
    result.success = true;
    result.output = Json::writeString(wb, out);
    return result;
}

// ---------------------------------------------------------------------------
// file_search — regex search across memory files (Ticket 095)
// ---------------------------------------------------------------------------

ToolResult MemoryTool::HandleFileSearch(const std::string& agentId, const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";
    if (!m_fileStore) {
        result.success = false;
        result.error = "memory file store not available";
        return result;
    }

    auto args = ParseArgs(rawArgs);
    std::string pattern = GetStringField(args, "regex", "");
    if (pattern.empty()) {
        pattern = GetStringField(args, "pattern", "");
    }
    if (pattern.empty()) {
        result.success = false;
        result.error = "regex pattern is required for file_search";
        return result;
    }

    // Optional: limit to specific file
    std::string specificPath = GetStringField(args, "source_path", "");

    int maxResults = args.get("limit", 20).asInt();
    if (maxResults < 1) maxResults = 20;
    if (maxResults > 100) maxResults = 100;

    // Compile regex
    std::regex re;
    try {
        re = std::regex(pattern, std::regex::ECMAScript | std::regex::icase);
    } catch (const std::regex_error& e) {
        result.success = false;
        result.error = std::string("invalid regex pattern: ") + e.what();
        return result;
    }

    int64_t agentNum = AgentIdToNumeric(agentId);
    auto allFiles = m_fileStore->ListFiles(std::nullopt, 1000, 0);

    Json::Value matches(Json::arrayValue);
    int matchCount = 0;

    for (const auto& f : allFiles) {
        if (f.agent_id != agentNum && f.agent_id != 0) continue;
        if (!specificPath.empty() && f.source_path != specificPath) continue;
        if (f.superseded) continue;

        std::istringstream stream(f.content);
        std::string line;
        int lineNum = 0;
        while (std::getline(stream, line)) {
            lineNum++;
            if (std::regex_search(line, re)) {
                Json::Value m(Json::objectValue);
                m["source_path"] = f.source_path;
                m["line"] = lineNum;
                m["text"] = line;
                matches.append(m);
                ++matchCount;
                if (matchCount >= maxResults) goto done;
            }
        }
    }
    done:;

    Json::Value out(Json::objectValue);
    out["total"] = matchCount;
    out["pattern"] = pattern;
    out["results"] = matches;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "  ";
    result.success = true;
    result.output = Json::writeString(wb, out);
    return result;
}

// Extract headings from markdown content. Returns vector of (level, title, line_number).
static std::vector<std::tuple<int, std::string, int>> ExtractHeadings(const std::string& content) {
    std::vector<std::tuple<int, std::string, int>> headings;
    std::istringstream stream(content);
    std::string line;
    int lineNum = 0;
    while (std::getline(stream, line)) {
        lineNum++;
        // Count leading # characters
        int level = 0;
        while (level < static_cast<int>(line.size()) && line[level] == '#') level++;
        if (level > 0 && level <= 6 && level < static_cast<int>(line.size()) && line[level] == ' ') {
            std::string title = line.substr(level + 1);
            // Trim trailing whitespace
            while (!title.empty() && (title.back() == ' ' || title.back() == '\r')) title.pop_back();
            headings.push_back({level, title, lineNum});
        }
    }
    return headings;
}

ToolResult MemoryTool::HandleFileHeadings(const std::string& agentId, const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";
    if (!m_fileStore) {
        result.success = false;
        result.error = "memory file store not available";
        return result;
    }

    auto args = ParseArgs(rawArgs);
    int64_t agentNum = AgentIdToNumeric(agentId);

    // Resolve file
    int64_t fileId = args.get("file_id", 0).asInt64();
    std::string path = args.get("source_path", "").asString();
    std::optional<memory::MemoryFile> file;

    if (fileId > 0) {
        file = m_fileStore->GetFile(fileId);
    } else if (!path.empty()) {
        auto files = m_fileStore->ListFiles(std::nullopt, 1000, 0);
        for (const auto& f : files) {
            if (f.source_path == path && f.agent_id == agentNum) { file = f; break; }
        }
    }

    if (!file) {
        result.success = false;
        result.error = "File not found";
        return result;
    }
    if (file->agent_id != agentNum) {
        result.success = false;
        result.error = "File does not belong to this agent";
        return result;
    }

    auto headings = ExtractHeadings(file->content);

    Json::Value out(Json::objectValue);
    out["id"] = static_cast<Json::Int64>(file->id);
    out["source_path"] = file->source_path;
    Json::Value headingsArr(Json::arrayValue);
    for (size_t i = 0; i < headings.size(); ++i) {
        auto& [level, title, lineNum] = headings[i];
        Json::Value h(Json::objectValue);
        h["number"] = static_cast<Json::UInt64>(i + 1);
        h["level"] = level;
        h["title"] = title;
        h["line"] = lineNum;
        headingsArr.append(h);
    }
    out["headings"] = headingsArr;
    out["total_headings"] = static_cast<Json::UInt64>(headings.size());

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.success = true;
    result.output = Json::writeString(wb, out);
    return result;
}

ToolResult MemoryTool::HandleFileRead(const std::string& agentId, const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";
    if (!m_fileStore) {
        result.success = false;
        result.error = "memory file store not available";
        return result;
    }

    auto args = ParseArgs(rawArgs);
    int64_t agentNum = AgentIdToNumeric(agentId);

    // Resolve file by file_id or source_path
    int64_t fileId = args.get("file_id", 0).asInt64();
    std::string path = args.get("source_path", "").asString();
    std::optional<memory::MemoryFile> file;

    if (fileId > 0) {
        file = m_fileStore->GetFile(fileId);
    } else if (!path.empty()) {
        auto files = m_fileStore->ListFiles(std::nullopt, 1000, 0);
        for (const auto& f : files) {
            if (f.source_path == path && f.agent_id == agentNum) { file = f; break; }
        }
    }

    if (!file) {
        result.success = false;
        result.error = fileId > 0 ? "File not found (id=" + std::to_string(fileId) + ")"
                                  : "File not found: " + path;
        return result;
    }
    if (file->agent_id != agentNum) {
        result.success = false;
        result.error = "File does not belong to this agent";
        return result;
    }

    // Parse read options
    std::string section = args.get("section", "").asString();
    int sectionNumber = args.get("section_number", 0).asInt();
    int page = args.get("page", 1).asInt();
    if (page < 1) page = 1;
    int pageSize = args.get("page_size", 200).asInt();
    if (pageSize < 1) pageSize = 200;
    if (pageSize > 1000) pageSize = 1000;

    // Always include headings index for navigation
    auto headings = ExtractHeadings(file->content);

    Json::Value out(Json::objectValue);
    out["id"] = static_cast<Json::Int64>(file->id);
    out["source_path"] = file->source_path;
    out["file_type"] = memory::MemoryFileStore::FileTypeToString(file->file_type);
    out["content_mutable"] = file->content_mutable;
    out["superseded"] = file->superseded;
    out["total_lines"] = static_cast<Json::UInt64>(
        std::count(file->content.begin(), file->content.end(), '\n') + 1);

    // Headings index
    Json::Value headingsArr(Json::arrayValue);
    for (size_t i = 0; i < headings.size(); ++i) {
        auto& [level, title, lineNum] = headings[i];
        Json::Value h(Json::objectValue);
        h["number"] = static_cast<Json::UInt64>(i + 1);
        h["level"] = level;
        h["title"] = title;
        h["line"] = lineNum;
        headingsArr.append(h);
    }
    out["headings"] = headingsArr;

    // Determine content to return
    std::string contentOut;
    int startLine = 1, endLine = 0;

    if (!section.empty() || sectionNumber > 0) {
        // Section extraction mode
        int targetIdx = -1;
        if (sectionNumber > 0 && sectionNumber <= static_cast<int>(headings.size())) {
            targetIdx = sectionNumber - 1;
        } else if (!section.empty()) {
            // Find by name (case-insensitive substring match)
            std::string sectionLower = section;
            std::transform(sectionLower.begin(), sectionLower.end(), sectionLower.begin(), ::tolower);
            for (size_t i = 0; i < headings.size(); ++i) {
                std::string titleLower = std::get<1>(headings[i]);
                std::transform(titleLower.begin(), titleLower.end(), titleLower.begin(), ::tolower);
                if (titleLower.find(sectionLower) != std::string::npos) {
                    targetIdx = static_cast<int>(i);
                    break;
                }
            }
        }

        if (targetIdx >= 0) {
            auto& [level, title, lineNum] = headings[targetIdx];
            startLine = lineNum;
            // Find end: next heading at same or higher level (<= level)
            endLine = static_cast<int>(out["total_lines"].asUInt64());
            for (int i = targetIdx + 1; i < static_cast<int>(headings.size()); ++i) {
                if (std::get<0>(headings[i]) <= level) {
                    endLine = std::get<2>(headings[i]) - 1;
                    break;
                }
            }
            out["section"] = title;
            out["section_number"] = static_cast<Json::UInt64>(targetIdx + 1);
        } else {
            result.success = false;
            result.error = "Section not found: " + section;
            return result;
        }
    } else {
        // Check for explicit line range: "lines" = "50-80" syntax
        std::string linesParam = args.get("lines", "").asString();
        int totalLines = static_cast<int>(out["total_lines"].asUInt64());

        if (!linesParam.empty() && linesParam.find('-') != std::string::npos) {
            // Parse "start-end" range
            auto dashPos = linesParam.find('-');
            try {
                startLine = std::stoi(linesParam.substr(0, dashPos));
                endLine = std::stoi(linesParam.substr(dashPos + 1));
                if (startLine < 1) startLine = 1;
                if (endLine > totalLines) endLine = totalLines;
                if (startLine > endLine) std::swap(startLine, endLine);
                out["lines_range"] = linesParam;
            } catch (...) {
                result.success = false;
                result.error = "invalid lines range format. Use \"start-end\", e.g. \"50-80\"";
                return result;
            }
        } else {
            // Standard pagination mode
            int totalPages = (totalLines + pageSize - 1) / pageSize;
            if (totalPages == 0) totalPages = 1;
            if (page > totalPages) page = totalPages;
            startLine = (page - 1) * pageSize + 1;
            endLine = std::min(page * pageSize, totalLines);
            out["page"] = page;
            out["page_size"] = pageSize;
            out["total_pages"] = totalPages;
        }
    }

    out["start_line"] = startLine;
    out["end_line"] = endLine;

    // Extract the requested lines
    std::istringstream stream(file->content);
    std::string line;
    int currentLine = 0;
    while (std::getline(stream, line)) {
        currentLine++;
        if (currentLine >= startLine && currentLine <= endLine) {
            contentOut += line + "\n";
        }
        if (currentLine > endLine) break;
    }

    out["content"] = contentOut;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.success = true;
    result.output = Json::writeString(wb, out);
    return result;
}

ToolResult MemoryTool::HandleFileWrite(const std::string& agentId, const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";
    if (!m_fileStore) {
        result.success = false;
        result.error = "memory file store not available";
        return result;
    }

    auto args = ParseArgs(rawArgs);
    std::string sourcePath = args.get("source_path", "").asString();
    std::string content = args.get("content", "").asString();
    if (sourcePath.empty()) {
        result.success = false;
        result.error = "Required: source_path and content";
        return result;
    }

    int64_t agentNum = AgentIdToNumeric(agentId);

    // Check if file already exists (by path + agent)
    auto files = m_fileStore->ListFiles(std::nullopt, 1000, 0);
    int64_t existingId = 0;
    for (const auto& f : files) {
        if (f.source_path == sourcePath && (f.agent_id == agentNum)) {
            existingId = f.id;
            break;
        }
    }

    if (existingId > 0) {
        // Update existing file
        auto file = m_fileStore->GetFile(existingId);
        if (!file) {
            result.success = false;
            result.error = "File disappeared during update";
            return result;
        }
        if (!file->content_mutable) {
            result.success = false;
            result.error = "File is not mutable";
            return result;
        }
        file->content = content;
        if (!m_fileStore->UpdateFile(*file)) {
            result.success = false;
            result.error = "Failed to update file";
            return result;
        }
        Json::Value out(Json::objectValue);
        out["id"] = static_cast<Json::Int64>(existingId);
        out["source_path"] = sourcePath;
        out["action"] = "updated";
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        result.success = true;
        result.output = Json::writeString(wb, out);
    } else {
        // Create new file
        memory::MemoryFile file;
        file.source_path = sourcePath;
        file.content = content;
        file.agent_id = agentNum;
        file.content_mutable = true;
        file.file_type = memory::MemoryFileType::ExpandedMemory;
        file.status = memory::MemoryFileStatus::Unprocessed;

        auto created = m_fileStore->CreateFile(file);
        if (created.id <= 0) {
            result.success = false;
            result.error = "Failed to create file";
            return result;
        }
        Json::Value out(Json::objectValue);
        out["id"] = static_cast<Json::Int64>(created.id);
        out["source_path"] = sourcePath;
        out["action"] = "created";
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        result.success = true;
        result.output = Json::writeString(wb, out);
    }
    return result;
}

ToolResult MemoryTool::HandleFileDelete(const std::string& agentId, const std::string& rawArgs) {
    ToolResult result;
    result.call_id = "";
    if (!m_fileStore) {
        result.success = false;
        result.error = "memory file store not available";
        return result;
    }

    auto args = ParseArgs(rawArgs);
    int64_t fileId = args.get("file_id", 0).asInt64();
    if (fileId == 0) {
        result.success = false;
        result.error = "Required: file_id";
        return result;
    }

    auto file = m_fileStore->GetFile(fileId);
    if (!file) {
        result.success = false;
        result.error = "File not found";
        return result;
    }

    // Ownership check
    int64_t agentNum = AgentIdToNumeric(agentId);
    if (file->agent_id != agentNum) {
        result.success = false;
        result.error = "File does not belong to this agent";
        return result;
    }

    if (m_fileStore->DeleteFile(fileId)) {
        Json::Value out(Json::objectValue);
        out["id"] = static_cast<Json::Int64>(fileId);
        out["action"] = "deleted";
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        result.success = true;
        result.output = Json::writeString(wb, out);
    } else {
        result.success = false;
        result.error = "Failed to delete file";
    }
    return result;
}

} // namespace animus::kernel

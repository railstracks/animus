#include "animus_kernel/tools/ConsolidationTool.h"
#include "animus_kernel/EmbeddingService.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/OntologyStore.h"
#include "animus_kernel/SessionManager.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/MemoryFileStore.h"

#include <json/json.h>
#include <json/reader.h>
#include <json/writer.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>
#include <sstream>

namespace animus::kernel {

ConsolidationTool::ConsolidationTool(memory::MemoryStore* memoryStore,
                                     ontology::OntologyStore* ontologyStore,
                                     SessionManager* sessionManager,
                                     memory::MemoryFileStore* memoryFileStore,
                                     AgentStore* agentStore,
                                     SessionReportStore* sessionReportStore,
                                     const EmbeddingService* embeddingService)
    : m_memoryStore(memoryStore)
    , m_ontologyStore(ontologyStore)
    , m_sessionManager(sessionManager)
    , m_memoryFileStore(memoryFileStore)
    , m_agentStore(agentStore)
    , m_sessionReportStore(sessionReportStore)
    , m_embeddingService(embeddingService) {}

int64_t ConsolidationTool::ResolveAgentId(const std::string& agentId) const {
    if (m_agentStore && !agentId.empty()) {
        auto agent = m_agentStore->GetById(agentId);
        if (agent) return agent->numeric_id;
    }
    try { return std::stoll(agentId); } catch (...) { return 0; }
}

ToolDefinition ConsolidationTool::GetDefinition() const {
    ToolDefinition def;
    def.name = "consolidation";
    def.description =
        "Review, restructure, and curate your episodic memory during consolidation cycles. "
        "Available actions: create, fetch_pending, review, promote, merge, revise, "
        "retire, tag, history, perspective:generate, perspective:review, ontology:upsert, "
        "memory_file:fetch_pending, memory_file:mark_processed, summary.";
    def.resultMode = ToolResultMode::deliver_to_model;
    def.session_types = {"consolidation"};

    def.parameters.push_back({
        "action", "string",
        "The consolidation action to perform: "
        "create, fetch_pending, review, promote, merge, revise, retire, tag, history, "
        "perspective:generate, perspective:review, ontology:upsert, summary",
        true,
        "",
        {"create", "fetch_pending", "review", "promote", "merge", "revise", "retire", "tag", "history",
         "perspective:generate", "perspective:review", "ontology:upsert", "summary"}
    });

    // All other parameters are action-specific and passed via a JSON "params" object
    def.parameters.push_back({
        "params", "object",
        "Action-specific parameters. See each action's documentation for required fields.",
        false
    });

    return def;
}

ToolResult ConsolidationTool::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    // Parse arguments
    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    Json::CharReader* reader = builder.newCharReader();
    if (!reader->parse(call.arguments.c_str(),
                       call.arguments.c_str() + call.arguments.size(),
                       &args, &parseErr)) {
        delete reader;
        result.success = false;
        result.error = "Failed to parse arguments: " + parseErr;
        return result;
    }
    delete reader;

    // Agent ID is always from __agent_id (ChainRunner-injected), never from params.
    // Agents must not be able to override their own identity.
    const std::string agentId = args.get("__agent_id", "").asString();
    if (agentId.empty()) {
        result.success = false;
        result.error = "Agent ID is required (injected by ChainRunner)";
        return result;
    }

    const std::string sessionKey = args.get("__session_key", "").asString();
    const std::string action = args.get("action", "").asString();

    // Determine session mode from session key
    // Format: "consolidation:intake:<agent_id>" or "consolidation:review:<agent_id>"
    bool isIntake = sessionKey.find("consolidation:intake:") != std::string::npos;
    bool isReview = sessionKey.find("consolidation:review:") != std::string::npos;

    // Action whitelist per session type
    static const std::set<std::string> intakeActions = {
        "fetch_pending", "create", "ontology:upsert",
        "perspective:generate", "summary", "sessions:report",
        "memory_file:fetch_pending", "memory_file:mark_processed"
    };
    static const std::set<std::string> reviewActions = {
        "review", "promote", "merge", "revise", "retire", "tag",
        "history", "perspective:review", "perspective:generate", "summary"
    };

    if (isIntake && intakeActions.find(action) == intakeActions.end()) {
        result.success = false;
        result.error = "Action '" + action + "' is not available in intake sessions. "
                      "Intake actions: fetch_pending, create, ontology:upsert, "
                      "perspective:generate, sessions:report, memory_file:fetch_pending, "
                      "memory_file:mark_processed, summary.";
        return result;
    }
    if (isReview && reviewActions.find(action) == reviewActions.end()) {
        result.success = false;
        result.error = "Action '" + action + "' is not available in review sessions. "
                      "Review actions: review, promote, merge, revise, retire, "
                      "tag, history, perspective:review, perspective:generate, summary.";
        return result;
    }

    if (action == "create") {
        return HandleCreate(call.arguments, agentId);
    } else if (action == "fetch_pending") {
        return HandleFetchPending(call.arguments, agentId);
    } else if (action == "review") {
        return HandleReview(call.arguments, agentId);
    } else if (action == "promote") {
        return HandlePromote(call.arguments, agentId);
    } else if (action == "merge") {
        return HandleMerge(call.arguments, agentId);
    } else if (action == "revise") {
        return HandleRevise(call.arguments, agentId);
    } else if (action == "history") {
        return HandleHistory(call.arguments, agentId);
    } else if (action == "retire") {
        return HandleRetire(call.arguments, agentId);
    } else if (action == "tag") {
        return HandleTag(call.arguments, agentId);
    } else if (action == "perspective:generate") {
        return HandlePerspectiveGenerate(call.arguments, agentId);
    } else if (action == "perspective:review") {
        return HandlePerspectiveReview(call.arguments, agentId);
    } else if (action == "summary") {
        return HandleSummary(call.arguments, agentId);
    } else if (action == "ontology:upsert") {
        return HandleOntologyUpsert(call.arguments, agentId);
    } else if (action == "memory_file:fetch_pending") {
        return HandleMemoryFileFetch(call.arguments, agentId);
    } else if (action == "memory_file:mark_processed") {
        return HandleMemoryFileMarkProcessed(call.arguments, agentId);
    } else if (action == "sessions:report") {
        return HandleSessionReport(call.arguments, agentId);
    } else {
        result.success = false;
        result.error = "Unknown action: " + action;
        return result;
    }
}

// ============================================================================
// Helpers
// ============================================================================

bool ConsolidationTool::VerifyOwnership(const memory::Observation& obs, const std::string& agentId) const {
    // Only allow agents to operate on their own observations
    if (obs.agent_id != agentId) {
        return false;
    }
    return true;
}

std::optional<memory::MemoryLayer> ConsolidationTool::ResolveLayer(
    const std::string& layerName, const std::string& agentId) const {
    if (!m_memoryStore) return std::nullopt;
    auto layers = m_memoryStore->ListLayersForAgent(agentId);
    for (const auto& l : layers) {
        if (l.name == layerName) return l;
    }
    return std::nullopt;
}

std::string ConsolidationTool::ObservationsToJson(
    const std::vector<memory::Observation>& obs) const {
    Json::Value arr(Json::arrayValue);
    for (const auto& o : obs) {
        Json::Value obj(Json::objectValue);
        obj["id"] = static_cast<Json::Int64>(o.id);
        obj["text"] = o.text;
        obj["weight"] = o.weight;
        obj["tags"] = o.tags_json;
        obj["source"] = o.source;
        obj["layer_id"] = static_cast<Json::Int64>(o.layer_id);
        obj["agent_id"] = o.agent_id;
        obj["created_at_unix_ms"] = static_cast<Json::Int64>(o.created_at_unix_ms);
        obj["memory_state"] = static_cast<int>(o.memory_state);

        // Compute age in hours
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        double ageHours = static_cast<double>(now - o.created_at_unix_ms) / (1000.0 * 3600.0);
        obj["age_hours"] = std::to_string(static_cast<int>(ageHours));

        // Compute overdue status
        bool overdue = (o.next_review_at_ms > 0 && o.next_review_at_ms <= now);
        obj["overdue"] = overdue;

        arr.append(obj);
    }
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, arr);
}

// ============================================================================
// Actions
// ============================================================================

ToolResult ConsolidationTool::HandleCreate(const std::string& arguments, const std::string& agentId) {
    ToolResult result;
    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    auto* reader = builder.newCharReader();
    reader->parse(arguments.c_str(), arguments.c_str() + arguments.size(), &args, &parseErr);
    delete reader;

    const auto params = args.get("params", Json::objectValue);
    const std::string text = params.get("text", "").asString();
    if (text.empty()) {
        result.success = false;
        result.error = "Missing required param: text";
        return result;
    }

    const double weight = params.get("weight", 0.8).asDouble();
    const std::string source = params.get("source", "consolidation:intake").asString();
    const std::string layerName = params.get("layer", "day").asString();

    // Resolve target layer
    auto layer = ResolveLayer(layerName, agentId);
    if (!layer) {
        result.success = false;
        result.error = "Layer not found: " + layerName;
        return result;
    }

    // Extract tags
    std::string tagsJson = "[]";
    if (params.isMember("tags") && params["tags"].isArray()) {
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        tagsJson = Json::writeString(wb, params["tags"]);
    }

    // Create the observation
    memory::Observation obs;
    obs.layer_id = layer->id;
    obs.agent_id = agentId;
    obs.text = text;
    obs.weight = weight;
    obs.tags_json = tagsJson;
    obs.source = source;
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    obs.created_at_unix_ms = now;
    obs.updated_at_unix_ms = now;

    auto created = m_memoryStore->CreateObservationForAgent(agentId, obs);

    Json::Value out(Json::objectValue);
    out["success"] = created.id > 0;
    out["observation_id"] = static_cast<Json::Int64>(created.id);
    out["layer"] = layerName;
    out["weight"] = weight;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.success = created.id > 0;
    result.output = Json::writeString(wb, out);
    if (!result.success) {
        result.error = "Failed to create observation in layer '" + layerName +
                       "' (db insert returned id=0)";
    }
    return result;
}

ToolResult ConsolidationTool::HandleFetchPending(const std::string& arguments, const std::string& agentId) {
    ToolResult result;

    if (!m_sessionManager) {
        result.success = false;
        result.error = "SessionManager not available";
        return result;
    }

    // Parse arguments for optional agent_id and limit
    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(arguments.c_str(), arguments.c_str() + arguments.size(), &args, &parseErr)) {
        result.success = false;
        result.error = "Failed to parse arguments: " + parseErr;
        return result;
    }

    const auto params = args.get("params", Json::objectValue);
    int limit = params.get("limit", 50).asInt();
    if (limit <= 0) limit = 50;

    auto pending = m_sessionManager->GetUnprocessedTurns(agentId, limit);

    std::cerr << "[consolidation] fetch_pending: agent=" << agentId
              << " limit=" << limit << " result_count=" << pending.size() << std::endl;
    for (size_t i = 0; i < pending.size() && i < 3; ++i) {
        std::cerr << "[consolidation] fetch_pending: turn[" << i
                  << "] session=" << pending[i].session_id
                  << " role=" << pending[i].role
                  << " content_len=" << pending[i].content.size() << std::endl;
    }

    // NOTE: Do NOT mark turns as processed here. Turns remain pending until
    // the consolidation cycle completes via action "summary". This ensures
    // that if observation creation fails (agent error, crash, timeout),
    // the turns are still available for the next consolidation cycle.

    // Build response — only session turns, not diary entries.
    // Diary-to-observation is handled by IntakeFromDiary in the
    // LLM-callback path. Surfacing diary entries here creates a
    // recursion loop (memories of memories).
    Json::Value root(Json::objectValue);
    root["total"] = static_cast<Json::Int>(pending.size());

    Json::Value turns(Json::arrayValue);
    for (const auto& t : pending) {
        Json::Value turn(Json::objectValue);
        turn["role"] = t.role;
        turn["content"] = t.content;
        turns.append(turn);
    }
    root["turns"] = turns;

    Json::FastWriter writer;
    result.output = writer.write(root);
    result.success = true;
    return result;
}

ToolResult ConsolidationTool::HandleReview(const std::string& arguments, const std::string& agentId) {
    ToolResult result;
    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    auto* reader = builder.newCharReader();
    reader->parse(arguments.c_str(), arguments.c_str() + arguments.size(), &args, &parseErr);
    delete reader;

    const auto params = args.get("params", Json::objectValue);
    const std::string layerName = params.get("layer", "").asString();
    const std::string filter = params.get("filter", "all").asString();
    int limit = params.get("limit", 20).asInt();
    if (limit <= 0) limit = 20;

    if (layerName.empty()) {
        result.success = false;
        result.error = "Missing required param: layer";
        return result;
    }

    auto layer = ResolveLayer(layerName, agentId);
    if (!layer) {
        result.success = false;
        result.error = "Layer not found: " + layerName;
        return result;
    }

    auto allObs = m_memoryStore->ListObservationsForLayer(layer->id);
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::vector<memory::Observation> filtered;
    for (const auto& o : allObs) {
        if (o.memory_state != memory::MemoryState::Current &&
            o.memory_state != memory::MemoryState::New) continue;

        if (filter == "overdue") {
            if (o.next_review_at_ms > 0 && o.next_review_at_ms <= now)
                filtered.push_back(o);
        } else if (filter == "stale") {
            // Past evaluation interval
            double ageSec = static_cast<double>(now - o.last_evaluated_at_ms) / 1000.0;
            if (o.last_evaluated_at_ms == 0 ||
                ageSec > layer->evaluation_interval_seconds)
                filtered.push_back(o);
        } else if (filter == "high_weight") {
            if (o.weight >= 0.7) filtered.push_back(o);
        } else if (filter == "all") {
            filtered.push_back(o);
        }
    }

    // Apply limit
    if (static_cast<int>(filtered.size()) > limit)
        filtered.resize(limit);

    result.success = true;
    result.output = ObservationsToJson(filtered);
    return result;
}

ToolResult ConsolidationTool::HandlePromote(const std::string& arguments, const std::string& agentId) {
    ToolResult result;
    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    auto* reader = builder.newCharReader();
    reader->parse(arguments.c_str(), arguments.c_str() + arguments.size(), &args, &parseErr);
    delete reader;

    const auto params = args.get("params", Json::objectValue);
    int64_t obsId = params.get("observation_id", 0).asInt64();
    const std::string targetLayerName = params.get("target_layer", "").asString();

    if (obsId == 0 || targetLayerName.empty()) {
        result.success = false;
        result.error = "Missing required params: observation_id, target_layer";
        return result;
    }

    auto targetLayer = ResolveLayer(targetLayerName, agentId);
    if (!targetLayer) {
        result.success = false;
        result.error = "Target layer not found: " + targetLayerName;
        return result;
    }

    auto obs = m_memoryStore->GetObservation(obsId);
    if (!obs) {
        result.success = false;
        result.error = "Observation not found: " + std::to_string(obsId);
        return result;
    }

    // Ownership check: agents can only promote their own observations
    if (!VerifyOwnership(*obs, agentId)) {
        result.success = false;
        result.error = "Cannot promote observation owned by another agent";
        return result;
    }

    // Allow skipping layers — the agent decides significance
    std::string motivation = "Agent-initiated promote to " + targetLayerName;
    bool ok = m_memoryStore->MoveObservation(obsId, targetLayer->id, motivation);

    Json::Value out(Json::objectValue);
    out["success"] = ok;
    out["observation_id"] = static_cast<Json::Int64>(obsId);
    out["from_layer_id"] = static_cast<Json::Int64>(obs->layer_id);
    out["to_layer_id"] = static_cast<Json::Int64>(targetLayer->id);
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.success = ok;
    result.output = Json::writeString(wb, out);
    return result;
}

ToolResult ConsolidationTool::HandleMerge(const std::string& arguments, const std::string& agentId) {
    ToolResult result;
    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    auto* reader = builder.newCharReader();
    reader->parse(arguments.c_str(), arguments.c_str() + arguments.size(), &args, &parseErr);
    delete reader;

    const auto params = args.get("params", Json::objectValue);
    const std::string mergedText = params.get("merged_text", "").asString();
    const std::string strategy = params.get("strategy", "max_weight").asString();

    if (mergedText.empty()) {
        result.success = false;
        result.error = "Missing required param: merged_text";
        return result;
    }

    const auto ids = params["observation_ids"];
    if (!ids.isArray() || ids.size() < 2) {
        result.success = false;
        result.error = "observation_ids must be an array of 2+ IDs";
        return result;
    }

    // Collect source observations
    std::vector<memory::Observation> sources;
    for (Json::ArrayIndex i = 0; i < ids.size(); ++i) {
        auto obs = m_memoryStore->GetObservation(ids[i].asInt64());
        if (obs) sources.push_back(*obs);
    }
    if (sources.size() < 2) {
        result.success = false;
        result.error = "Could not find at least 2 valid observations to merge";
        return result;
    }

    // Ownership check: agents can only merge their own observations
    for (const auto& s : sources) {
        if (!VerifyOwnership(s, agentId)) {
            result.success = false;
            result.error = "Cannot merge observations owned by another agent";
            return result;
        }
    }

    // Determine weight from strategy
    double mergedWeight = sources[0].weight;
    if (strategy == "max_weight") {
        for (const auto& s : sources)
            mergedWeight = std::max(mergedWeight, s.weight);
    } else if (strategy == "average_weight") {
        double sum = 0;
        for (const auto& s : sources) sum += s.weight;
        mergedWeight = sum / sources.size();
    } else if (strategy == "keep_weights") {
        mergedWeight = sources[0].weight; // use first source's weight
    }

    // Collect tags union (parse each source's tags_json)
    std::set<std::string> tagUnion;
    for (const auto& s : sources) {
        Json::Value srcTags;
        Json::CharReaderBuilder rb;
        auto* r = rb.newCharReader();
        r->parse(s.tags_json.c_str(), s.tags_json.c_str() + s.tags_json.size(), &srcTags, nullptr);
        delete r;
        if (srcTags.isArray()) {
            for (Json::ArrayIndex i = 0; i < srcTags.size(); ++i)
                tagUnion.insert(srcTags[i].asString());
        }
    }
    Json::Value mergedTags(Json::arrayValue);
    for (const auto& t : tagUnion) mergedTags.append(t);
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string mergedTagsJson = Json::writeString(wb, mergedTags);

    // Use first source's layer_id and agent_id for the merged observation
    auto& primary = sources[0];
    memory::Observation merged;
    merged.layer_id = primary.layer_id;
    merged.agent_id = primary.agent_id;
    merged.text = mergedText;
    merged.weight = mergedWeight;
    merged.tags_json = mergedTagsJson;
    merged.source = "consolidation:merge";
    merged.created_at_unix_ms = primary.created_at_unix_ms; // preserve original timestamp

    auto created = m_memoryStore->CreateObservation(merged);

    // Deprecate (not delete) source observations
    for (const auto& s : sources) {
        m_memoryStore->SetObservationMemoryState(
            s.id, memory::MemoryState::Deprecated,
            "Merged into observation " + std::to_string(created.id));
    }

    Json::Value out(Json::objectValue);
    out["success"] = true;
    out["merged_observation_id"] = static_cast<Json::Int64>(created.id);
    out["deprecated_source_ids"] = Json::Value(Json::arrayValue);
    for (const auto& s : sources)
        out["deprecated_source_ids"].append(static_cast<Json::Int64>(s.id));
    result.success = true;
    result.output = Json::writeString(wb, out);
    return result;
}

ToolResult ConsolidationTool::HandleRevise(const std::string& arguments, const std::string& agentId) {
    ToolResult result;
    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    auto* reader = builder.newCharReader();
    reader->parse(arguments.c_str(), arguments.c_str() + arguments.size(), &args, &parseErr);
    delete reader;

    const auto params = args.get("params", Json::objectValue);
    int64_t obsId = params.get("observation_id", 0).asInt64();

    if (obsId == 0) {
        result.success = false;
        result.error = "Missing required param: observation_id";
        return result;
    }

    auto obs = m_memoryStore->GetObservation(obsId);
    if (!obs) {
        result.success = false;
        result.error = "Observation not found: " + std::to_string(obsId);
        return result;
    }

    // Ownership check: agents can only revise their own observations
    if (!VerifyOwnership(*obs, agentId)) {
        result.success = false;
        result.error = "Cannot revise observation owned by another agent";
        return result;
    }

    // Copy-on-write: create new version, preserve old as snapshot
    std::string newText = params.isMember("text") ? params["text"].asString() : obs->text;
    double newWeight = params.isMember("weight") ? params["weight"].asDouble() : obs->weight;
    std::string newTags = params.isMember("tags") ? params["tags"].asString() : "";

    auto revised = m_memoryStore->ReviseObservation(obsId, newText, newWeight, newTags);
    if (revised.id != 0) {
        m_memoryStore->TouchEvaluation(revised.id);

        Json::Value body(Json::objectValue);
        body["revised"] = true;
        body["new_id"] = static_cast<Json::Int64>(revised.id);
        body["old_id"] = static_cast<Json::Int64>(obsId);
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        result.output = Json::writeString(wb, body);
    } else {
        result.output = "{\"revised\":false}";
    }
    result.success = (revised.id != 0);
    return result;
}

ToolResult ConsolidationTool::HandleHistory(const std::string& arguments, const std::string& agentId) {
    ToolResult result;
    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    auto* reader = builder.newCharReader();
    reader->parse(arguments.c_str(), arguments.c_str() + arguments.size(), &args, &parseErr);
    delete reader;

    const auto params = args.get("params", Json::objectValue);
    int64_t obsId = params.get("observation_id", 0).asInt64();

    if (obsId == 0) {
        // Also accept top-level observation_id for convenience
        obsId = args.get("observation_id", 0).asInt64();
    }

    if (obsId == 0) {
        result.success = false;
        result.error = "Missing required param: observation_id";
        return result;
    }

    auto versions = m_memoryStore->GetObservationHistory(obsId);
    if (versions.empty()) {
        result.success = false;
        result.error = "Observation not found: " + std::to_string(obsId);
        return result;
    }

    // Ownership check: agents can only view history of their own observations
    if (!versions.empty() && !VerifyOwnership(versions[0], agentId)) {
        result.success = false;
        result.error = "Cannot view history of observation owned by another agent";
        return result;
    }

    Json::Value items(Json::arrayValue);
    for (const auto& v : versions) {
        Json::Value item(Json::objectValue);
        item["id"] = static_cast<Json::Int64>(v.id);
        item["text"] = v.text;
        item["weight"] = v.weight;
        item["created_at"] = static_cast<Json::Int64>(v.created_at_unix_ms);
        item["updated_at"] = static_cast<Json::Int64>(v.updated_at_unix_ms);
        item["superseded_by"] = static_cast<Json::Int64>(v.superseded_by);
        item["is_current"] = (v.superseded_by == 0);
        items.append(item);
    }

    Json::Value body(Json::objectValue);
    body["total"] = static_cast<Json::Int>(versions.size());
    body["versions"] = items;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.output = Json::writeString(wb, body);
    result.success = true;
    return result;
}

ToolResult ConsolidationTool::HandleRetire(const std::string& arguments, const std::string& agentId) {
    ToolResult result;
    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    auto* reader = builder.newCharReader();
    reader->parse(arguments.c_str(), arguments.c_str() + arguments.size(), &args, &parseErr);
    delete reader;

    const auto params = args.get("params", Json::objectValue);
    int64_t obsId = params.get("observation_id", 0).asInt64();
    const std::string reason = params.get("reason", "Agent-initiated retire").asString();

    if (obsId == 0) {
        result.success = false;
        result.error = "Missing required param: observation_id";
        return result;
    }

    // Ownership check: agents can only retire their own observations
    auto obs = m_memoryStore->GetObservation(obsId);
    if (!obs) {
        result.success = false;
        result.error = "Observation not found: " + std::to_string(obsId);
        return result;
    }
    if (!VerifyOwnership(*obs, agentId)) {
        result.success = false;
        result.error = "Cannot retire observation owned by another agent";
        return result;
    }

    bool ok = m_memoryStore->SetObservationMemoryState(
        obsId, memory::MemoryState::Deprecated, reason);

    result.success = ok;
    result.output = ok ? "{\"retired\":true}" : "{\"retired\":false}";
    return result;
}

ToolResult ConsolidationTool::HandleTag(const std::string& arguments, const std::string& agentId) {
    ToolResult result;
    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    auto* reader = builder.newCharReader();
    reader->parse(arguments.c_str(), arguments.c_str() + arguments.size(), &args, &parseErr);
    delete reader;

    const auto params = args.get("params", Json::objectValue);
    int64_t obsId = params.get("observation_id", 0).asInt64();
    const std::string mode = params.get("mode", "append").asString();
    const auto tagsArr = params["tags"];

    if (obsId == 0) {
        result.success = false;
        result.error = "Missing required param: observation_id";
        return result;
    }

    auto obs = m_memoryStore->GetObservation(obsId);
    if (!obs) {
        result.success = false;
        result.error = "Observation not found: " + std::to_string(obsId);
        return result;
    }

    // Ownership check: agents can only tag their own observations
    if (!VerifyOwnership(*obs, agentId)) {
        result.success = false;
        result.error = "Cannot tag observation owned by another agent";
        return result;
    }

    // Parse existing tags
    Json::Value existingTags;
    Json::CharReaderBuilder rb;
    auto* r = rb.newCharReader();
    r->parse(obs->tags_json.c_str(), obs->tags_json.c_str() + obs->tags_json.size(),
             &existingTags, nullptr);
    delete r;

    // Parse new tags
    std::vector<std::string> newTags;
    if (tagsArr.isArray()) {
        for (Json::ArrayIndex i = 0; i < tagsArr.size(); ++i)
            newTags.push_back(tagsArr[i].asString());
    }

    Json::Value resultTags(Json::arrayValue);

    if (mode == "replace") {
        for (const auto& t : newTags) resultTags.append(t);
    } else if (mode == "append") {
        // Copy existing, then add new (no duplicates)
        std::set<std::string> seen;
        if (existingTags.isArray()) {
            for (Json::ArrayIndex i = 0; i < existingTags.size(); ++i) {
                const std::string t = existingTags[i].asString();
                if (seen.insert(t).second) resultTags.append(t);
            }
        }
        for (const auto& t : newTags) {
            if (seen.insert(t).second) resultTags.append(t);
        }
    } else if (mode == "remove") {
        std::set<std::string> toRemove(newTags.begin(), newTags.end());
        if (existingTags.isArray()) {
            for (Json::ArrayIndex i = 0; i < existingTags.size(); ++i) {
                const std::string t = existingTags[i].asString();
                if (toRemove.find(t) == toRemove.end())
                    resultTags.append(t);
            }
        }
    }

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    obs->tags_json = Json::writeString(wb, resultTags);
    bool ok = m_memoryStore->UpdateObservation(*obs);

    result.success = ok;
    result.output = ok ? obs->tags_json : "{\"updated\":false}";
    return result;
}

ToolResult ConsolidationTool::HandlePerspectiveGenerate(const std::string& arguments, const std::string& agentId) {
    ToolResult result;
    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    auto* reader = builder.newCharReader();
    reader->parse(arguments.c_str(), arguments.c_str() + arguments.size(), &args, &parseErr);
    delete reader;

    const auto params = args.get("params", Json::objectValue);
    const std::string layerName = params.get("layer", "").asString();
    const std::string pov = params.get("pov", "").asString(); // retrospective, current, future
    const std::string text = params.get("text", "").asString();
    const std::string valence = params.get("valence", "neutral").asString();

    if (layerName.empty() || pov.empty() || text.empty()) {
        result.success = false;
        result.error = "Missing required params: layer, pov, text";
        return result;
    }

    auto layer = ResolveLayer(layerName, agentId);
    if (!layer) {
        result.success = false;
        result.error = "Layer not found: " + layerName;
        return result;
    }

    // Get existing perspective or create new one
    auto existing = m_memoryStore->GetPerspective(layer->id);
    memory::LayerPerspective p;
    if (existing) p = *existing;
    p.layer_id = layer->id;

    if (pov == "retrospective") {
        p.retrospective = text;
        p.retrospective_valence = valence;
    } else if (pov == "current") {
        p.current_perspective = text;
        p.current_valence = valence;
    } else if (pov == "future") {
        p.future_perspective = text;
        p.future_valence = valence;
    } else {
        result.success = false;
        result.error = "Invalid pov: " + pov + ". Must be retrospective, current, or future";
        return result;
    }

    m_memoryStore->SetPerspective(p);

    result.success = true;
    result.output = "{\"generated\":true}";
    return result;
}

ToolResult ConsolidationTool::HandlePerspectiveReview(const std::string& arguments, const std::string& agentId) {
    ToolResult result;
    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    auto* reader = builder.newCharReader();
    reader->parse(arguments.c_str(), arguments.c_str() + arguments.size(), &args, &parseErr);
    delete reader;

    const auto params = args.get("params", Json::objectValue);
    const std::string layerName = params.get("layer", "").asString();

    if (layerName.empty()) {
        result.success = false;
        result.error = "Missing required param: layer";
        return result;
    }

    auto layer = ResolveLayer(layerName, agentId);
    if (!layer) {
        result.success = false;
        result.error = "Layer not found: " + layerName;
        return result;
    }

    auto perspective = m_memoryStore->GetPerspective(layer->id);
    if (!perspective) {
        result.success = true;
        result.output = "{\"perspectives\":null}";
        return result;
    }

    Json::Value out(Json::objectValue);
    Json::Value perspectives(Json::objectValue);

    Json::Value retro(Json::objectValue);
    retro["text"] = perspective->retrospective;
    retro["valence"] = perspective->retrospective_valence;
    perspectives["retrospective"] = retro;

    Json::Value current(Json::objectValue);
    current["text"] = perspective->current_perspective;
    current["valence"] = perspective->current_valence;
    perspectives["current"] = current;

    Json::Value future(Json::objectValue);
    future["text"] = perspective->future_perspective;
    future["valence"] = perspective->future_valence;
    perspectives["future"] = future;

    out["perspectives"] = perspectives;
    out["layer"] = layerName;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.success = true;
    result.output = Json::writeString(wb, out);
    return result;
}

ToolResult ConsolidationTool::HandleOntologyUpsert(const std::string& arguments, const std::string& agentId) {
    ToolResult result;
    if (!m_ontologyStore) {
        result.success = false;
        result.error = "ontology store not available";
        return result;
    }

    Json::Value args;
    Json::CharReaderBuilder rb;
    std::istringstream stream(arguments);
    std::string parseErr;
    if (!Json::parseFromStream(rb, stream, &args, &parseErr)) {
        result.success = false;
        result.error = "Failed to parse arguments: " + parseErr;
        return result;
    }

    // Extract params sub-object (agents nest args under "params"),
    // falling back to top-level keys for direct callers.
    const auto params = args.get("params", Json::objectValue);
    const std::string rootCategory = params.get("root_category",
        args.get("root_category", "concept")).asString();
    const std::string path = params.get("path",
        args.get("path", "")).asString();
    // agentId comes from ChainRunner-injected __agent_id, not params
    const std::string motivation = params.get("motivation",
        args.get("motivation", "")).asString();

    if (path.empty()) {
        result.success = false;
        result.error = "path is required for ontology:upsert";
        return result;
    }

    auto category = ontology::OntologyStore::RootCategoryFromString(rootCategory);
    if (!category.has_value()) {
        result.success = false;
        result.error = "Invalid root_category: " + rootCategory +
            ". Valid: persons, concepts, procedures, events, locations, organizations, projects";
        return result;
    }

    // Strip root category prefix from path if present.
    // e.g. path="persons/Kestrel" with root_category="persons" → "Kestrel"
    std::string relativePath = path;
    const std::string categoryPrefix = ontology::OntologyStore::RootCategoryToString(*category) + "/";
    if (relativePath.find(categoryPrefix) == 0) {
        relativePath = relativePath.substr(categoryPrefix.size());
    } else if (relativePath == ontology::OntologyStore::RootCategoryToString(*category)) {
        // Path is just the root category name — create the root itself
        relativePath = "";
    }

    // Ensure the entity path exists (creates intermediates as needed)
    auto entityOpt = m_ontologyStore->EnsureEntityPath(
        *category, relativePath, motivation, agentId);

    if (!entityOpt.has_value()) {
        result.success = false;
        result.error = "Failed to create entity path: " + path;
        return result;
    }

    auto entity = entityOpt.value();

    Json::Value out(Json::objectValue);
    out["id"] = static_cast<Json::Int64>(entity.id);
    out["root_category"] = rootCategory;
    out["path"] = entity.full_path;
    out["name"] = entity.name;

    // Upsert any properties provided
    const Json::Value& props = params.get("properties",
        args.get("properties", Json::objectValue));
    if (props.isObject() && props.size() > 0) {
        Json::Value upsertedProps(Json::arrayValue);
        for (const auto& key : props.getMemberNames()) {
            const Json::Value& pv = props[key];
            const std::string value = pv.get("value", "").asString();
            const std::string valueType = pv.get("value_type", "text").asString();
            const std::string propMotivation = pv.get("motivation", motivation).asString();

            auto propType = ontology::OntologyStore::PropertyTypeFromString(valueType);
            if (!propType.has_value()) {
                continue; // skip invalid type
            }

            ontology::OntologyProperty prop;
            prop.entity_id = entity.id;
            prop.key = key;
            prop.value = value;
            prop.value_type = *propType;

            m_ontologyStore->SetProperty(prop, propMotivation);
            upsertedProps.append(key);
        }
        out["properties_set"] = upsertedProps;
    }

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.success = true;
    result.output = Json::writeString(wb, out);
    return result;
}

ToolResult ConsolidationTool::HandleSummary(const std::string& arguments, const std::string& agentId) {
    // Summary marks the consolidation cycle as complete. We mark all remaining
    // unprocessed turns for this agent as processed, since the agent has finished.
    // If observation creation failed for some turns, those turns are still marked
    // — but the next cycle will pick up any NEW turns created after this point.
    if (m_sessionManager) {
        // Mark all remaining unprocessed turns as processed
        auto remaining = m_sessionManager->GetUnprocessedTurns(agentId, 10000);
        if (!remaining.empty()) {
            std::vector<SessionTurnId> ids;
            ids.reserve(remaining.size());
            for (const auto& t : remaining) {
                ids.push_back(t.turn_id);
            }
            m_sessionManager->MarkTurnsProcessed(ids);
        }
    }

    ToolResult result;
    Json::Value out(Json::objectValue);

    if (m_memoryStore) {
        auto layers = m_memoryStore->ListLayersForAgent(agentId);
        Json::Value layerStats(Json::arrayValue);
        for (const auto& l : layers) {
            Json::Value ls(Json::objectValue);
            ls["name"] = l.name;
            ls["id"] = static_cast<Json::Int64>(l.id);
            auto obs = m_memoryStore->ListObservations(l.id);
            ls["observation_count"] = static_cast<Json::UInt>(obs.size());
            layerStats.append(ls);
        }
        out["layers"] = layerStats;
    }

    out["hint"] = "Generate a consolidation summary covering what you reviewed, "
                  "promoted, merged, retired, and any perspectives generated. "
                  "Write this to your diary as a consolidation record.";

    // Auto-mark all remaining unprocessed MemoryFiles as processed.
    // The agent may have read files via file_read rather than fetch_pending,
    // but if the consolidation session is finishing, the files have been
    // available for processing. This prevents files from being stuck
    // in unprocessed state indefinitely.
    if (m_memoryFileStore) {
        int64_t agentNum = ResolveAgentId(agentId);
        auto unprocessed = m_memoryFileStore->ListUnprocessedFiles(agentNum, 1000);
        int markedCount = 0;
        for (const auto& f : unprocessed) {
            if (m_memoryFileStore->MarkProcessed(f.id)) markedCount++;
        }
        if (markedCount > 0) {
            std::cerr << "[consolidation] Auto-marked " << markedCount
                      << " MemoryFiles as processed for agent " << agentNum << std::endl;
        }
    }

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    result.success = true;
    result.output = Json::writeString(wb, out);
    return result;
}

// ============================================================================
// MemoryFile handlers — process unprocessed files during consolidation
// ============================================================================

ToolResult ConsolidationTool::HandleMemoryFileFetch(const std::string& arguments, const std::string& agentId) {
    ToolResult result;
    if (!m_memoryFileStore) {
        result.success = false;
        result.error = "MemoryFileStore not available";
        return result;
    }

    // Parse limit from arguments
    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
    if (!reader->parse(arguments.c_str(), arguments.c_str() + arguments.size(), &args, &parseErr)) {
        result.success = false;
        result.error = "Failed to parse arguments: " + parseErr;
        return result;
    }

    const auto params = args.get("params", Json::objectValue);
    int limit = params.get("limit", 10).asInt();
    if (limit <= 0) limit = 10;

    // Parse agent ID to numeric
    int64_t agentNum = 0;
    agentNum = ResolveAgentId(agentId);

    std::cerr << "[consolidation] memory_file:fetch_pending agentId=" << agentId
              << " resolved=" << agentNum << std::endl;

    auto files = m_memoryFileStore->ListUnprocessedFiles(agentNum, limit);

    std::cerr << "[consolidation] memory_file:fetch_pending found " << files.size()
              << " unprocessed files for agent " << agentNum << std::endl;

    // Also count total unprocessed across all agents for diagnostics
    int64_t totalAll = m_memoryFileStore->CountUnprocessed(agentNum);
    std::cerr << "[consolidation] CountUnprocessed(" << agentNum << ")=" << totalAll << std::endl;

    Json::Value root(Json::objectValue);
    root["total"] = static_cast<Json::Int>(files.size());

    Json::Value filesArr(Json::arrayValue);
    for (const auto& f : files) {
        Json::Value fileObj(Json::objectValue);
        fileObj["id"] = static_cast<Json::Int64>(f.id);
        fileObj["source_path"] = f.source_path;
        fileObj["file_type"] = memory::MemoryFileStore::FileTypeToString(f.file_type);
        fileObj["content"] = f.content;
        filesArr.append(fileObj);
    }
    root["files"] = filesArr;

    Json::FastWriter writer;
    result.output = writer.write(root);
    result.success = true;
    return result;
}

ToolResult ConsolidationTool::HandleMemoryFileMarkProcessed(const std::string& arguments, const std::string& agentId) {
    ToolResult result;
    if (!m_memoryFileStore) {
        result.success = false;
        result.error = "MemoryFileStore not available";
        return result;
    }

    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
    if (!reader->parse(arguments.c_str(), arguments.c_str() + arguments.size(), &args, &parseErr)) {
        result.success = false;
        result.error = "Failed to parse arguments: " + parseErr;
        return result;
    }

    const auto params = args.get("params", Json::objectValue);
    int64_t fileId = params.get("id", 0).asInt64();
    if (fileId == 0) {
        result.success = false;
        result.error = "Missing required param: id";
        return result;
    }

    // Verify ownership: file must belong to this agent or be global (agent_id = 0)
    auto file = m_memoryFileStore->GetFile(fileId);
    if (!file) {
        result.success = false;
        result.error = "File not found";
        return result;
    }

    int64_t agentNum = 0;
    agentNum = ResolveAgentId(agentId);
    if (file->agent_id != 0 && file->agent_id != agentNum) {
        result.success = false;
        result.error = "File does not belong to this agent";
        return result;
    }

    if (m_memoryFileStore->MarkProcessed(fileId)) {
        result.success = true;
        Json::Value out; out["status"] = "processed"; out["id"] = static_cast<Json::Int64>(fileId); Json::FastWriter w; result.output = w.write(out);
    } else {
        result.success = false;
        result.error = "Failed to mark file as processed";
    }
    return result;
}

ToolResult ConsolidationTool::HandleSessionReport(const std::string& arguments, const std::string& agentId) {
    ToolResult result;
    result.call_id = "";

    if (!m_sessionReportStore) {
        result.success = false;
        result.error = "Session report store not available";
        return result;
    }

    Json::Value args;
    Json::CharReaderBuilder builder;
    std::string parseErr;
    Json::CharReader* reader = builder.newCharReader();
    if (!reader->parse(arguments.c_str(), arguments.c_str() + arguments.size(), &args, &parseErr)) {
        delete reader;
        result.success = false;
        result.error = "Failed to parse arguments: " + parseErr;
        return result;
    }
    delete reader;

    // session_id is required
    Json::Value& params = args["params"];
    if (!params.isObject() || !params.isMember("session_id")) {
        result.success = false;
        result.error = "params.session_id is required";
        return result;
    }

    const int64_t sessionId = params["session_id"].asInt64();
    const std::string summary = params.get("summary", "").asString();
    const std::string pastEvents = params.get("past_events", "").asString();
    const std::string currentActivity = params.get("current_activity", "").asString();
    const std::string forwardLook = params.get("forward_look", "").asString();

    if (summary.empty() && pastEvents.empty() && currentActivity.empty() && forwardLook.empty()) {
        result.success = false;
        result.error = "At least one of summary, past_events, current_activity, forward_look must be provided";
        return result;
    }

    auto report = m_sessionReportStore->Upsert(
        sessionId, agentId, summary, pastEvents, currentActivity, forwardLook);

    // Compute and store embedding for relevance-based retrieval (Ticket 094)
    if (m_embeddingService && m_embeddingService->IsAvailable() && report.id != 0) {
        // Build text from all four fields for a rich embedding
        std::string embedText = summary + " " + pastEvents + " " +
                                currentActivity + " " + forwardLook;
        // Truncate to embedding model's sweet spot
        if (embedText.size() > 700) {
            embedText = embedText.substr(0, 700);
        }
        if (!embedText.empty()) {
            auto emb = m_embeddingService->Embed(embedText);
            if (emb.has_value()) {
                m_sessionReportStore->StoreEmbedding(report.id, *emb);
            }
        }
    }

    Json::Value resp(Json::objectValue);
    resp["success"] = true;
    resp["report"]["id"] = report.id;
    resp["report"]["session_id"] = report.session_id;
    resp["report"]["agent_id"] = report.agent_id;
    resp["report"]["summary"] = report.summary;
    resp["report"]["past_events"] = report.past_events;
    resp["report"]["current_activity"] = report.current_activity;
    resp["report"]["forward_look"] = report.forward_look;
    resp["report"]["updated_at_unix_ms"] = report.updated_at_unix_ms;

    Json::StreamWriterBuilder wb;
    result.output = Json::writeString(wb, resp);
    result.success = true;
    return result;
}

} // namespace animus::kernel
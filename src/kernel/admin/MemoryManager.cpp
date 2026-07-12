#include "animus_kernel/admin/MemoryManager.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>

#include <json/json.h>

#include "animus_kernel/MemoryStore.h"

namespace animus::kernel {
namespace {

std::optional<int64_t> ParseInt64PathId(const std::string& idStr) {
    try {
        std::size_t pos = 0;
        const long long value = std::stoll(idStr, &pos);
        if (pos != idStr.size()) return std::nullopt;
        return static_cast<int64_t>(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::string MemoryStateToString(memory::MemoryState state) {
    switch (state) {
        case memory::MemoryState::Current: return "current";
        case memory::MemoryState::Deprecated: return "deprecated";
        case memory::MemoryState::New:
        default:
            return "new";
    }
}

memory::MemoryState ParseMemoryState(
        const Json::Value& body, memory::MemoryState fallback) {
    if (body.isMember("memory_state")) {
        const Json::Value& stateNode = body["memory_state"];
        if (stateNode.isInt() || stateNode.isUInt() || stateNode.isInt64() || stateNode.isUInt64()) {
            const auto value = stateNode.asInt64();
            if (value == 1) return memory::MemoryState::Current;
            if (value == 2) return memory::MemoryState::Deprecated;
            return memory::MemoryState::New;
        }
        if (stateNode.isString()) {
            const std::string value = stateNode.asString();
            if (value == "current") return memory::MemoryState::Current;
            if (value == "deprecated") return memory::MemoryState::Deprecated;
            if (value == "new") return memory::MemoryState::New;
        }
    }
    return fallback;
}

Json::Value BuildSqlObservationJson(const memory::Observation& obs) {
    Json::Value out(Json::objectValue);
    out["id"] = static_cast<Json::UInt64>(obs.id);
    out["layer_id"] = static_cast<Json::UInt64>(obs.layer_id);
    out["agent_id"] = obs.agent_id;
    out["text"] = obs.text;
    out["content"] = obs.text;
    out["weight"] = obs.weight;
    out["decay_rate"] = obs.decay_rate;
    out["tags"] = obs.tags_json;
    out["source"] = obs.source;
    out["created_at_unix_ms"] = static_cast<Json::UInt64>(obs.created_at_unix_ms);
    out["updated_at_unix_ms"] = static_cast<Json::UInt64>(obs.updated_at_unix_ms);
    out["last_evaluated_at_ms"] = static_cast<Json::UInt64>(obs.last_evaluated_at_ms);
    out["next_review_at_ms"] = static_cast<Json::UInt64>(obs.next_review_at_ms);
    out["memory_state"] = MemoryStateToString(obs.memory_state);
    return out;
}


} // namespace

void MemoryManager::Configure(
    const KernelConfig::MemoryConfig& memoryConfig,
    std::atomic<std::uint64_t>* nextObservationId) {
    std::lock_guard<std::mutex> lock(m_memoryMutex);
    m_memoryConfig = memoryConfig;
    m_nextObservationId = nextObservationId;
}

bool MemoryManager::LoadFromDisk(std::string* error) {
    std::lock_guard<std::mutex> lock(m_memoryMutex);
    m_memoryLayers = m_memoryConfig.layers;
    if (m_memoryLayers.empty()) {
        m_memoryLayers = KernelConfig::MemoryConfig{}.layers;
    }
    m_observationsById.clear();
    m_consolidationState = ConsolidationState{};

    if (!m_memoryConfig.persistToDisk) {
        return true;
    }
    if (m_memoryConfig.filePath.empty()) {
        if (error) {
            *error = "memory file path must not be empty when persistence is enabled";
        }
        return false;
    }

    const std::filesystem::path path(m_memoryConfig.filePath);
    if (!std::filesystem::exists(path)) {
        return true;
    }

    std::ifstream in(path);
    if (!in.is_open()) {
        if (error) {
            *error = "failed to open memory file for reading: " + path.string();
        }
        return false;
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string parseErrors;
    if (!Json::parseFromStream(builder, in, &root, &parseErrors)) {
        if (error) {
            *error = "failed to parse memory file: " + parseErrors;
        }
        return false;
    }

    if (root.isMember("layers") && root["layers"].isArray()) {
        std::unordered_map<std::string, KernelConfig::MemoryLayerConfig> defaultsByName;
        for (const auto& base : m_memoryConfig.layers) {
            defaultsByName[base.name] = base;
        }

        std::vector<KernelConfig::MemoryLayerConfig> loadedLayers;
        for (Json::ArrayIndex i = 0; i < root["layers"].size(); ++i) {
            const Json::Value& layer = root["layers"][i];
            if (!layer.isObject() || !layer["name"].isString()) {
                continue;
            }
            KernelConfig::MemoryLayerConfig cfg{};
            cfg.name = layer["name"].asString();
            auto defaultIt = defaultsByName.find(cfg.name);
            if (defaultIt != defaultsByName.end()) {
                cfg = defaultIt->second;
                cfg.name = layer["name"].asString();
            }
            cfg.description = layer.get("description", cfg.description).asString();
            cfg.retentionPolicy = layer.get("retention_policy", cfg.retentionPolicy).asString();
            cfg.horizon = layer.get("horizon", cfg.horizon).asString();
            cfg.consolidationInterval =
                layer.get("consolidation_interval", cfg.consolidationInterval).asString();
            cfg.perspectivePast = layer.get("perspective_past", cfg.perspectivePast).asString();
            cfg.perspectiveCurrent =
                layer.get("perspective_current", cfg.perspectiveCurrent).asString();
            cfg.perspectiveFuture =
                layer.get("perspective_future", cfg.perspectiveFuture).asString();
            loadedLayers.push_back(std::move(cfg));
        }
        if (!loadedLayers.empty()) {
            m_memoryLayers = std::move(loadedLayers);
        }
    }

    std::uint64_t maxCaptureObservationId = 0;
    if (root.isMember("observations") && root["observations"].isArray()) {
        for (Json::ArrayIndex i = 0; i < root["observations"].size(); ++i) {
            const Json::Value& item = root["observations"][i];
            if (!item.isObject()) {
                continue;
            }
            KernelConfig::MemoryObservation obs{};
            obs.id = item.get("id", "").asString();
            if (obs.id.empty()) {
                continue;
            }
            obs.text = item.get("text", "").asString();
            if (item.isMember("tags") && item["tags"].isArray()) {
                for (Json::ArrayIndex t = 0; t < item["tags"].size(); ++t) {
                    if (item["tags"][t].isString()) {
                        obs.tags.push_back(item["tags"][t].asString());
                    }
                }
            }
            if (item.isMember("timestamp_unix_ms")) {
                obs.timestampUnixMs = item.get("timestamp_unix_ms", 0).asUInt64();
            } else {
                obs.timestampUnixMs = item.get("timestamp", 0).asUInt64();
            }
            obs.weight = item.get("weight", 1.0).asDouble();
            obs.decayRate = item.get("decay_rate", 0.95).asDouble();
            obs.layer = item.get("layer", DefaultFirstLayerNameLocked()).asString();
            obs.createdInLayer = item.get("created_in_layer", obs.layer).asString();
            obs.demotionReason = item.get("demotion_reason", "").asString();
            if (item.isMember("demotion_timestamp_unix_ms")) {
                obs.demotionTimestampUnixMs = item.get("demotion_timestamp_unix_ms", 0).asUInt64();
            } else {
                obs.demotionTimestampUnixMs = item.get("demotion_timestamp", 0).asUInt64();
            }

            std::uint64_t parsedCaptureId = 0;
            if (ParsePrefixedUInt64(obs.id, "obs_capture_", &parsedCaptureId)) {
                maxCaptureObservationId = std::max(maxCaptureObservationId, parsedCaptureId);
            }
            m_observationsById.emplace(obs.id, std::move(obs));
        }
    }

    if (root.isMember("consolidation") && root["consolidation"].isObject()) {
        const Json::Value& c = root["consolidation"];
        m_consolidationState.lastRunUnixMs = c.get("last_run_unix_ms", 0).asUInt64();
        m_consolidationState.lastJobId = c.get("last_job_id", 0).asUInt64();
        m_consolidationState.lastError = c.get("last_error", "").asString();
        if (c.isMember("last_summary") && c["last_summary"].isObject()) {
            const Json::Value& s = c["last_summary"];
            m_consolidationState.lastSummary.promoted = s.get("promoted", 0).asUInt64();
            m_consolidationState.lastSummary.demoted = s.get("demoted", 0).asUInt64();
            m_consolidationState.lastSummary.archived = s.get("archived", 0).asUInt64();
        }
    }

    const std::uint64_t nextJobId = m_consolidationState.lastJobId + 1;
    m_nextMemoryConsolidationJobId.store(nextJobId == 0 ? 1 : nextJobId);
    if (m_nextObservationId) {
        m_nextObservationId->store(maxCaptureObservationId + 1);
    }

    return true;
}

bool MemoryManager::SaveToDisk(std::string* error) const {
    std::lock_guard<std::mutex> lock(m_memoryMutex);
    if (!m_memoryConfig.persistToDisk) {
        return true;
    }
    if (m_memoryConfig.filePath.empty()) {
        if (error) {
            *error = "memory file path must not be empty when persistence is enabled";
        }
        return false;
    }

    const std::filesystem::path path(m_memoryConfig.filePath);
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            if (error) {
                *error = "failed to create memory directory: " + parent.string();
            }
            return false;
        }
    }

    Json::Value root(Json::objectValue);
    Json::Value layers(Json::arrayValue);
    for (const auto& layer : m_memoryLayers) {
        layers.append(SerializeLayer(layer));
    }
    root["layers"] = layers;

    Json::Value observations(Json::arrayValue);
    for (const auto& pair : m_observationsById) {
        observations.append(SerializeObservation(pair.second));
    }
    root["observations"] = observations;

    Json::Value c(Json::objectValue);
    c["running"] = m_consolidationState.running;
    c["active_job_id"] = static_cast<Json::UInt64>(m_consolidationState.activeJobId);
    c["last_run_unix_ms"] = static_cast<Json::UInt64>(m_consolidationState.lastRunUnixMs);
    c["last_job_id"] = static_cast<Json::UInt64>(m_consolidationState.lastJobId);
    c["last_error"] = m_consolidationState.lastError;
    Json::Value summary(Json::objectValue);
    summary["promoted"] = static_cast<Json::UInt64>(m_consolidationState.lastSummary.promoted);
    summary["demoted"] = static_cast<Json::UInt64>(m_consolidationState.lastSummary.demoted);
    summary["archived"] = static_cast<Json::UInt64>(m_consolidationState.lastSummary.archived);
    c["last_summary"] = summary;
    root["consolidation"] = c;

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "  ";

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        if (error) {
            *error = "failed to open memory file for writing: " + path.string();
        }
        return false;
    }

    std::unique_ptr<Json::StreamWriter> writer(writerBuilder.newStreamWriter());
    writer->write(root, &out);
    out << "\n";
    if (!out.good()) {
        if (error) {
            *error = "failed to write memory file: " + path.string();
        }
        return false;
    }
    return true;
}

KernelConfig::MemoryObservation MemoryManager::CaptureObservation(
    const std::string& text,
    const std::vector<std::string>& tags,
    const std::string& idPrefix) {
    std::lock_guard<std::mutex> lock(m_memoryMutex);
    KernelConfig::MemoryObservation observation;
    const std::uint64_t nextId =
        m_nextObservationId ? m_nextObservationId->fetch_add(1) : 1;
    observation.id = idPrefix + std::to_string(nextId);
    observation.text = text;
    observation.tags = tags;
    observation.timestampUnixMs = NowUnixMs();
    observation.weight = 1.0;
    observation.decayRate = 0.95;
    observation.layer = DefaultFirstLayerNameLocked();
    observation.createdInLayer = observation.layer;
    m_observationsById[observation.id] = observation;
    return observation;
}

std::vector<std::string> MemoryManager::ListLayerNames() const {
    std::lock_guard<std::mutex> lock(m_memoryMutex);
    std::vector<std::string> names;
    names.reserve(m_memoryLayers.size());
    for (const auto& layer : m_memoryLayers) {
        names.push_back(layer.name);
    }
    return names;
}

std::vector<MemoryManager::LayerWithCount> MemoryManager::ListLayersWithCounts() const {
    std::lock_guard<std::mutex> lock(m_memoryMutex);
    std::vector<LayerWithCount> results;
    results.reserve(m_memoryLayers.size());
    for (const auto& layer : m_memoryLayers) {
        LayerWithCount item;
        item.layer = layer;
        item.observationCount = 0;
        for (const auto& pair : m_observationsById) {
            if (pair.second.layer == layer.name) {
                item.observationCount += 1;
            }
        }
        results.push_back(std::move(item));
    }
    return results;
}

Json::Value MemoryManager::BuildLayersResponse() const {
    const auto layersWithCounts = ListLayersWithCounts();
    Json::Value out(Json::objectValue);
    Json::Value layers(Json::arrayValue);
    for (const auto& item : layersWithCounts) {
        Json::Value layer(Json::objectValue);
        layer["name"] = item.layer.name;
        layer["description"] = item.layer.description;
        layer["retention_policy"] = item.layer.retentionPolicy;
        layer["horizon"] = item.layer.horizon;
        layer["consolidation_interval"] = item.layer.consolidationInterval;
        layer["perspective_past"] = item.layer.perspectivePast;
        layer["perspective_current"] = item.layer.perspectiveCurrent;
        layer["perspective_future"] = item.layer.perspectiveFuture;
        layer["observation_count"] = static_cast<Json::UInt64>(item.observationCount);
        layers.append(layer);
    }
    out["layers"] = layers;
    return out;
}

std::vector<KernelConfig::MemoryLayerConfig> MemoryManager::ListLayers() const {
    std::lock_guard<std::mutex> lock(m_memoryMutex);
    return m_memoryLayers;
}

std::vector<KernelConfig::MemoryObservation> MemoryManager::ListAllObservations() const {
    std::lock_guard<std::mutex> lock(m_memoryMutex);
    std::vector<KernelConfig::MemoryObservation> observations;
    observations.reserve(m_observationsById.size());
    for (const auto& pair : m_observationsById) {
        observations.push_back(pair.second);
    }
    return observations;
}

std::vector<KernelConfig::MemoryObservation> MemoryManager::ListLayerObservations(
    const std::string& layerName,
    bool* layerExists) const {
    std::lock_guard<std::mutex> lock(m_memoryMutex);
    bool exists = false;
    for (const auto& layer : m_memoryLayers) {
        if (layer.name == layerName) {
            exists = true;
            break;
        }
    }
    if (layerExists) {
        *layerExists = exists;
    }
    if (!exists) {
        return {};
    }

    std::vector<KernelConfig::MemoryObservation> observations;
    for (const auto& pair : m_observationsById) {
        if (pair.second.layer == layerName) {
            observations.push_back(pair.second);
        }
    }
    return observations;
}

MemoryManager::LayerObservationQueryResult MemoryManager::QueryLayerObservations(
    const std::string& layerName,
    const std::string& sort,
    bool ascending,
    std::uint32_t page,
    std::uint32_t limit) const {
    LayerObservationQueryResult result;
    result.sort = (sort == "age") ? "age" : "weight";
    result.ascending = ascending;
    if (page == 0) {
        page = 1;
    }
    if (limit == 0) {
        limit = 1;
    }

    std::vector<KernelConfig::MemoryObservation> observations;
    {
        std::lock_guard<std::mutex> lock(m_memoryMutex);
        for (const auto& layer : m_memoryLayers) {
            if (layer.name == layerName) {
                result.layerExists = true;
                break;
            }
        }
        if (!result.layerExists) {
            return result;
        }
        observations.reserve(m_observationsById.size());
        for (const auto& pair : m_observationsById) {
            if (pair.second.layer == layerName) {
                observations.push_back(pair.second);
            }
        }
    }

    const std::uint64_t nowUnixMs = NowUnixMs();
    std::sort(
        observations.begin(),
        observations.end(),
        [&](const KernelConfig::MemoryObservation& a, const KernelConfig::MemoryObservation& b) {
            if (result.sort == "age") {
                const double aAge = ObservationAgeSeconds(a, nowUnixMs);
                const double bAge = ObservationAgeSeconds(b, nowUnixMs);
                return result.ascending ? (aAge < bAge) : (aAge > bAge);
            }
            const double aWeight = a.weight;
            const double bWeight = b.weight;
            return result.ascending ? (aWeight < bWeight) : (aWeight > bWeight);
        });

    result.total = static_cast<std::uint64_t>(observations.size());
    const std::size_t offset = static_cast<std::size_t>(page - 1) * limit;
    if (offset >= observations.size()) {
        return result;
    }
    const std::size_t maxItems =
        std::min<std::size_t>(limit, observations.size() - offset);
    result.items.reserve(maxItems);
    for (std::size_t i = 0; i < maxItems; ++i) {
        result.items.push_back(observations[offset + i]);
    }
    return result;
}

MemoryManager::OperationResult MemoryManager::BuildLayerObservationsResponse(
    const std::string& layerName,
    const std::string& sort,
    bool ascending,
    std::uint32_t page,
    std::uint32_t limit) const {
    OperationResult out;
    const auto queryResult = QueryLayerObservations(layerName, sort, ascending, page, limit);
    if (!queryResult.layerExists) {
        out.httpStatusCode = 404;
        out.body["error"] = "memory layer not found";
        return out;
    }

    Json::Value items(Json::arrayValue);
    for (const auto& observation : queryResult.items) {
        items.append(BuildObservationApiJson(observation));
    }

    out.httpStatusCode = 200;
    out.body["layer"] = layerName;
    out.body["page"] = page;
    out.body["limit"] = limit;
    out.body["total"] = static_cast<Json::UInt64>(queryResult.total);
    out.body["sort"] = queryResult.sort;
    out.body["order"] = queryResult.ascending ? "asc" : "desc";
    out.body["observations"] = items;
    return out;
}

std::optional<KernelConfig::MemoryObservation> MemoryManager::GetObservation(
    const std::string& observationId) const {
    std::lock_guard<std::mutex> lock(m_memoryMutex);
    auto it = m_observationsById.find(observationId);
    if (it == m_observationsById.end()) {
        return std::nullopt;
    }
    return it->second;
}

MemoryManager::OperationResult MemoryManager::BuildObservationResponse(
    const std::string& observationId) const {
    OperationResult out;
    const auto observation = GetObservation(observationId);
    if (!observation.has_value()) {
        out.httpStatusCode = 404;
        out.body["error"] = "observation not found";
        return out;
    }
    out.httpStatusCode = 200;
    out.body = BuildObservationApiJson(*observation);
    return out;
}

bool MemoryManager::UpdateObservation(
    const std::string& observationId,
    const Json::Value& patch,
    KernelConfig::MemoryObservation* updated,
    std::string* error) {
    std::lock_guard<std::mutex> lock(m_memoryMutex);
    auto it = m_observationsById.find(observationId);
    if (it == m_observationsById.end()) {
        if (error) {
            *error = "observation not found";
        }
        return false;
    }

    KernelConfig::MemoryObservation next = it->second;
    if (!ParseObservationPatch(patch, &next, error)) {
        return false;
    }
    it->second = next;
    if (updated) {
        *updated = next;
    }
    return true;
}

MemoryManager::OperationResult MemoryManager::PatchObservationFromBody(
    const std::string& observationId,
    const Json::Value& body) {
    OperationResult out;
    if (!body.isObject()) {
        out.httpStatusCode = 400;
        out.body["error"] = "request body must be a JSON object";
        return out;
    }

    KernelConfig::MemoryObservation updated;
    std::string patchError;
    if (!UpdateObservation(observationId, body, &updated, &patchError)) {
        if (patchError == "observation not found") {
            out.httpStatusCode = 404;
            out.body["error"] = patchError;
        } else {
            out.httpStatusCode = 400;
            out.body["error"] = patchError;
        }
        return out;
    }

    out.httpStatusCode = 200;
    out.body = BuildObservationApiJson(updated);
    return out;
}

bool MemoryManager::ArchiveObservation(
    const std::string& observationId,
    KernelConfig::MemoryObservation* archived,
    std::string* error) {
    std::lock_guard<std::mutex> lock(m_memoryMutex);
    auto it = m_observationsById.find(observationId);
    if (it == m_observationsById.end()) {
        if (error) {
            *error = "observation not found";
        }
        return false;
    }

    KernelConfig::MemoryObservation observation = it->second;
    observation.layer = "archive";
    observation.demotionReason = "removed_via_api";
    observation.demotionTimestampUnixMs = NowUnixMs();
    it->second = observation;

    bool archiveLayerExists = false;
    for (const auto& layer : m_memoryLayers) {
        if (layer.name == "archive") {
            archiveLayerExists = true;
            break;
        }
    }
    if (!archiveLayerExists) {
        m_memoryLayers.push_back(
            KernelConfig::MemoryLayerConfig{
                "archive",
                "Cold storage for observations removed from active layers.",
                "permanent",
                "archive",
                "never",
                "Historical archive of superseded observations.",
                "Inactive archival layer.",
                "No forward horizon; archived data is retrospective only."});
    }
    if (archived) {
        *archived = observation;
    }
    return true;
}

MemoryManager::OperationResult MemoryManager::ArchiveObservationById(
    const std::string& observationId) {
    OperationResult out;
    std::string archiveError;
    if (!ArchiveObservation(observationId, nullptr, &archiveError)) {
        out.httpStatusCode = 404;
        out.body["error"] = archiveError;
        return out;
    }

    out.httpStatusCode = 200;
    out.body["status"] = "archived";
    out.body["observation_id"] = observationId;
    return out;
}

bool MemoryManager::BeginConsolidation(
    std::uint64_t* jobId,
    std::uint64_t* activeJobId,
    std::string* error) {
    std::lock_guard<std::mutex> lock(m_memoryMutex);
    if (m_consolidationState.running) {
        if (error) {
            *error = "consolidation already running";
        }
        if (activeJobId) {
            *activeJobId = m_consolidationState.activeJobId;
        }
        return false;
    }

    const std::uint64_t nextJobId = m_nextMemoryConsolidationJobId.fetch_add(1);
    m_consolidationState.running = true;
    m_consolidationState.activeJobId = nextJobId;
    m_consolidationState.lastError.clear();
    if (activeJobId) {
        *activeJobId = nextJobId;
    }
    if (jobId) {
        *jobId = nextJobId;
    }
    return true;
}

MemoryManager::OperationResult MemoryManager::BeginConsolidationRequest() {
    OperationResult out;
    std::uint64_t jobId = 0;
    std::uint64_t activeJobId = 0;
    std::string startError;
    if (!BeginConsolidation(&jobId, &activeJobId, &startError)) {
        out.httpStatusCode = 409;
        out.body["error"] = startError;
        out.body["active_job_id"] = static_cast<Json::UInt64>(activeJobId);
        return out;
    }

    out.httpStatusCode = 200;
    out.body["job_id"] = static_cast<Json::UInt64>(jobId);
    out.body["status"] = "started";
    return out;
}

void MemoryManager::RunConsolidationJob(std::uint64_t jobId) {
    ConsolidationSummary summary{};
    {
        std::lock_guard<std::mutex> lock(m_memoryMutex);
        const std::string firstLayerName = DefaultFirstLayerNameLocked();
        for (auto& pair : m_observationsById) {
            auto& observation = pair.second;
            if (observation.layer.empty()) {
                observation.layer = firstLayerName;
                observation.createdInLayer = firstLayerName;
                summary.demoted += 1;
            }
        }
    }

    std::string persistError;
    const bool persisted = SaveToDisk(&persistError);

    std::lock_guard<std::mutex> lock(m_memoryMutex);
    m_consolidationState.running = false;
    m_consolidationState.activeJobId = 0;
    m_consolidationState.lastJobId = jobId;
    m_consolidationState.lastRunUnixMs = NowUnixMs();
    m_consolidationState.lastSummary = summary;
    m_consolidationState.lastError = persisted ? "" : persistError;
}

ConsolidationState MemoryManager::GetConsolidationState() const {
    std::lock_guard<std::mutex> lock(m_memoryMutex);
    return m_consolidationState;
}

Json::Value MemoryManager::BuildConsolidationStatusResponse() const {
    const ConsolidationState state = GetConsolidationState();
    Json::Value out(Json::objectValue);
    out["state"] = state.running ? "running" : "idle";
    out["active_job_id"] = static_cast<Json::UInt64>(state.activeJobId);
    out["last_run_unix_ms"] = static_cast<Json::UInt64>(state.lastRunUnixMs);
    out["last_job_id"] = static_cast<Json::UInt64>(state.lastJobId);
    Json::Value summary(Json::objectValue);
    summary["promoted"] = static_cast<Json::UInt64>(state.lastSummary.promoted);
    summary["demoted"] = static_cast<Json::UInt64>(state.lastSummary.demoted);
    summary["archived"] = static_cast<Json::UInt64>(state.lastSummary.archived);
    out["last_summary"] = summary;
    if (state.lastError.empty()) {
        out["last_error"] = Json::nullValue;
    } else {
        out["last_error"] = state.lastError;
    }
    return out;
}

std::uint64_t MemoryManager::NowUnixMs() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch());
    return static_cast<std::uint64_t>(ms.count());
}

double MemoryManager::ObservationAgeSeconds(
    const KernelConfig::MemoryObservation& observation,
    std::uint64_t nowUnixMs) {
    if (nowUnixMs <= observation.timestampUnixMs) {
        return 0.0;
    }
    return static_cast<double>(nowUnixMs - observation.timestampUnixMs) / 1000.0;
}

bool MemoryManager::ParsePrefixedUInt64(
    const std::string& value,
    const std::string& prefix,
    std::uint64_t* out) {
    if (!out) {
        return false;
    }
    if (value.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    const std::string suffix = value.substr(prefix.size());
    if (suffix.empty()) {
        return false;
    }
    try {
        std::size_t parsed = 0;
        const std::uint64_t parsedValue = std::stoull(suffix, &parsed);
        if (parsed != suffix.size()) {
            return false;
        }
        *out = parsedValue;
        return true;
    } catch (...) {
        return false;
    }
}

Json::Value MemoryManager::SerializeLayer(const KernelConfig::MemoryLayerConfig& layer) {
    Json::Value item(Json::objectValue);
    item["name"] = layer.name;
    item["description"] = layer.description;
    item["retention_policy"] = layer.retentionPolicy;
    item["horizon"] = layer.horizon;
    item["consolidation_interval"] = layer.consolidationInterval;
    item["perspective_past"] = layer.perspectivePast;
    item["perspective_current"] = layer.perspectiveCurrent;
    item["perspective_future"] = layer.perspectiveFuture;
    return item;
}

Json::Value MemoryManager::SerializeObservation(const KernelConfig::MemoryObservation& observation) {
    Json::Value out(Json::objectValue);
    out["id"] = observation.id;
    out["text"] = observation.text;
    Json::Value tags(Json::arrayValue);
    for (const auto& tag : observation.tags) {
        tags.append(tag);
    }
    out["tags"] = tags;
    out["timestamp"] = static_cast<Json::UInt64>(observation.timestampUnixMs);
    out["timestamp_unix_ms"] = static_cast<Json::UInt64>(observation.timestampUnixMs);
    out["weight"] = observation.weight;
    out["decay_rate"] = observation.decayRate;
    out["layer"] = observation.layer;
    out["created_in_layer"] = observation.createdInLayer;
    if (observation.demotionReason.empty()) {
        out["demotion_reason"] = Json::nullValue;
    } else {
        out["demotion_reason"] = observation.demotionReason;
    }
    if (observation.demotionTimestampUnixMs == 0) {
        out["demotion_timestamp"] = Json::nullValue;
        out["demotion_timestamp_unix_ms"] = Json::nullValue;
    } else {
        out["demotion_timestamp"] = static_cast<Json::UInt64>(observation.demotionTimestampUnixMs);
        out["demotion_timestamp_unix_ms"] =
            static_cast<Json::UInt64>(observation.demotionTimestampUnixMs);
    }
    return out;
}

Json::Value MemoryManager::BuildObservationApiJson(
    const KernelConfig::MemoryObservation& observation) {
    Json::Value out = SerializeObservation(observation);
    out["content"] = observation.text;
    out["age_seconds"] = ObservationAgeSeconds(observation, NowUnixMs());
    return out;
}

MemoryManager::OperationResult MemoryManager::SqlCreateLayer(
    memory::MemoryStore* store,
    const Json::Value* body) const {
    OperationResult out;
    if (!store) {
        out.httpStatusCode = 503;
        out.body = Json::Value("memory store not available");
        return out;
    }
    if (!body) {
        out.httpStatusCode = 400;
        out.body = Json::Value("invalid JSON");
        return out;
    }

    memory::MemoryLayer layer;
    layer.agent_id = (*body).get("agent_id", "default").asString();
    layer.name = (*body)["name"].asString();
    layer.horizon = (*body).get("horizon", "").asString();
    layer.sort_order = (*body).get("sort_order", 0).asInt();
    layer.evaluation_interval_seconds = (*body).get("evaluation_interval_seconds", 86400).asInt64();
    layer.cron_expr = (*body).get("cron_expr", "0 * * * *").asString();
    if ((*body).isMember("consolidation_review_prompt")) {
        layer.consolidation_review_prompt = (*body).get("consolidation_review_prompt", "").asString();
    } else {
        layer.consolidation_review_prompt = (*body).get("consolidation_prompt", "").asString();
    }
    layer.consolidation_intake_prompt = (*body).get("consolidation_intake_prompt", "").asString();
    if ((*body).isMember("intake_interval")) {
        if (!(*body)["intake_interval"].isNull()) {
            const std::string intakeInterval = (*body)["intake_interval"].asString();
            if (!intakeInterval.empty()) {
                layer.intake_interval = intakeInterval;
            }
        }
    }
    layer.token_budget = (*body).get("token_budget", 4096).asInt64();
    layer.enabled = (*body).get("enabled", false).asBool();
    if (layer.name.empty()) {
        out.httpStatusCode = 400;
        out.body = Json::Value("name is required");
        return out;
    }

    const auto created = store->CreateLayer(layer);
    if (created.id == 0) {
        out.httpStatusCode = 500;
        out.body = Json::Value("failed to create layer");
        return out;
    }

    // Single-intake-layer invariant per agent: clear intake on other layers for same agent.
    if (created.intake_interval.has_value()) {
        auto layers = store->ListLayersForAgent(created.agent_id);
        for (auto& existing : layers) {
            if (existing.id == created.id) continue;
            if (!existing.intake_interval.has_value()) continue;
            existing.intake_interval = std::nullopt;
            existing.consolidation_intake_prompt.clear();
            store->UpdateLayer(existing);
        }
    }

    out.httpStatusCode = 201;
    out.body["id"] = static_cast<Json::UInt64>(created.id);
    out.body["agent_id"] = created.agent_id;
    out.body["name"] = created.name;
    out.body["horizon"] = created.horizon;
    out.body["sort_order"] = created.sort_order;
    out.body["evaluation_interval_seconds"] = static_cast<Json::UInt64>(created.evaluation_interval_seconds);
    out.body["cron_expr"] = created.cron_expr;
    out.body["consolidation_review_prompt"] = created.consolidation_review_prompt;
    out.body["consolidation_prompt"] = created.consolidation_review_prompt;
    out.body["consolidation_intake_prompt"] = created.consolidation_intake_prompt;
    if (created.intake_interval.has_value()) {
        out.body["intake_interval"] = *created.intake_interval;
    } else {
        out.body["intake_interval"] = Json::nullValue;
    }
    out.body["token_budget"] = static_cast<Json::UInt64>(created.token_budget);
    out.body["enabled"] = created.enabled;
    out.body["created_at_unix_ms"] = static_cast<Json::UInt64>(created.created_at_unix_ms);
    return out;
}

MemoryManager::OperationResult MemoryManager::SqlListLayers(memory::MemoryStore* store,
    const std::string& agentId) const {
    OperationResult out;
    if (!store) {
        out.httpStatusCode = 503;
        out.body = Json::Value("memory store not available");
        return out;
    }

    const auto layers = agentId.empty()
        ? store->ListLayers()
        : store->ListLayersForAgent(agentId);
    Json::Value response(Json::arrayValue);
    for (const auto& layer : layers) {
        Json::Value item(Json::objectValue);
        item["id"] = static_cast<Json::UInt64>(layer.id);
        item["agent_id"] = layer.agent_id;
        item["name"] = layer.name;
        item["horizon"] = layer.horizon;
        item["sort_order"] = layer.sort_order;
        item["evaluation_interval_seconds"] =
            static_cast<Json::UInt64>(layer.evaluation_interval_seconds);
        item["cron_expr"] = layer.cron_expr;
        item["consolidation_review_prompt"] = layer.consolidation_review_prompt;
        item["consolidation_prompt"] = layer.consolidation_review_prompt;
        item["consolidation_intake_prompt"] = layer.consolidation_intake_prompt;
        if (layer.intake_interval.has_value()) {
            item["intake_interval"] = *layer.intake_interval;
        } else {
            item["intake_interval"] = Json::nullValue;
        }
        item["token_budget"] = static_cast<Json::UInt64>(layer.token_budget);
        item["enabled"] = layer.enabled;
        item["created_at_unix_ms"] = static_cast<Json::UInt64>(layer.created_at_unix_ms);
        item["updated_at_unix_ms"] = static_cast<Json::UInt64>(layer.updated_at_unix_ms);
        item["observation_count"] = static_cast<Json::UInt64>(store->ListObservations(layer.id).size());
        item["has_perspective"] = static_cast<bool>(store->GetPerspective(layer.id).has_value());
        response.append(item);
    }

    out.httpStatusCode = 200;
    out.body = response;
    return out;
}

MemoryManager::OperationResult MemoryManager::SqlUpdateLayer(
    memory::MemoryStore* store,
    const std::string& idStr,
    const Json::Value* body) const {
    OperationResult out;
    if (!store) {
        out.httpStatusCode = 200;
        out.body = Json::Value("unavailable");
        return out;
    }
    auto idParsed = ParseInt64PathId(idStr);
    if (!idParsed) {
        out.httpStatusCode = 400;
        out.body = Json::Value("invalid id");
        return out;
    }
    auto existing = store->GetLayer(*idParsed);
    if (!existing) {
        out.httpStatusCode = 404;
        out.body = Json::Value("not found");
        return out;
    }

    if (body) {
        existing->name = (*body).get("name", existing->name).asString();
        existing->horizon = (*body).get("horizon", existing->horizon).asString();
        existing->sort_order = (*body).get("sort_order", existing->sort_order).asInt();
        existing->evaluation_interval_seconds = (*body).get(
            "evaluation_interval_seconds",
            existing->evaluation_interval_seconds).asInt64();
        existing->cron_expr = (*body).get("cron_expr", existing->cron_expr).asString();
        if ((*body).isMember("consolidation_review_prompt")) {
            existing->consolidation_review_prompt = (*body).get(
                "consolidation_review_prompt",
                existing->consolidation_review_prompt).asString();
        } else {
            existing->consolidation_review_prompt = (*body).get(
                "consolidation_prompt",
                existing->consolidation_review_prompt).asString();
        }
        existing->consolidation_intake_prompt = (*body).get(
            "consolidation_intake_prompt",
            existing->consolidation_intake_prompt).asString();
        if ((*body).isMember("intake_interval")) {
            if ((*body)["intake_interval"].isNull()) {
                existing->intake_interval = std::nullopt;
                existing->consolidation_intake_prompt.clear();
            } else {
                const std::string intakeInterval = (*body)["intake_interval"].asString();
                if (intakeInterval.empty()) {
                    existing->intake_interval = std::nullopt;
                    existing->consolidation_intake_prompt.clear();
                } else {
                    existing->intake_interval = intakeInterval;
                }
            }
        }
        existing->token_budget = (*body).get("token_budget", existing->token_budget).asInt64();
        existing->enabled = (*body).get("enabled", existing->enabled).asBool();
    }
    if (!store->UpdateLayer(*existing)) {
        out.httpStatusCode = 500;
        out.body = Json::Value("failed to update layer");
        return out;
    }

    // Single-intake-layer invariant: clear intake on all other layers.
    if (existing->intake_interval.has_value()) {
        auto layers = store->ListLayers();
        for (auto& layer : layers) {
            if (layer.id == existing->id) continue;
            if (!layer.intake_interval.has_value()) continue;
            layer.intake_interval = std::nullopt;
            layer.consolidation_intake_prompt.clear();
            store->UpdateLayer(layer);
        }
    }
    out.httpStatusCode = 200;
    out.body = Json::Value("ok");
    return out;
}

MemoryManager::OperationResult MemoryManager::SqlDeleteLayer(
    memory::MemoryStore* store,
    const std::string& idStr) const {
    OperationResult out;
    if (!store) {
        out.httpStatusCode = 200;
        out.body = Json::Value("unavailable");
        return out;
    }
    auto idParsed = ParseInt64PathId(idStr);
    if (!idParsed) {
        out.httpStatusCode = 400;
        out.body = Json::Value("invalid id");
        return out;
    }
    if (!store->DeleteLayer(*idParsed)) {
        out.httpStatusCode = 404;
        out.body = Json::Value("not found");
        return out;
    }
    out.httpStatusCode = 200;
    out.body = Json::Value("ok");
    return out;
}

MemoryManager::OperationResult MemoryManager::SqlListLayerObservations(
    memory::MemoryStore* store,
    const std::string& idStr,
    std::uint32_t page,
    std::uint32_t limit) const {
    OperationResult out;
    if (!store) {
        out.httpStatusCode = 503;
        out.body = Json::Value("memory store not available");
        return out;
    }
    auto idParsed = ParseInt64PathId(idStr);
    if (!idParsed) {
        out.httpStatusCode = 400;
        out.body = Json::Value("invalid id");
        return out;
    }
    auto layer = store->GetLayer(*idParsed);
    if (!layer) {
        out.httpStatusCode = 404;
        out.body = Json::Value("not found");
        return out;
    }

    if (page == 0) page = 1;
    if (limit == 0) limit = 1;
    if (limit > 200) limit = 200;

    const auto observations = store->ListObservations(*idParsed);
    const std::size_t total = observations.size();
    const std::size_t offset = static_cast<std::size_t>(page - 1) * limit;

    Json::Value items(Json::arrayValue);
    if (offset < total) {
        const std::size_t count = std::min<std::size_t>(limit, total - offset);
        for (std::size_t i = 0; i < count; ++i) {
            items.append(BuildSqlObservationJson(observations[offset + i]));
        }
    }

    out.httpStatusCode = 200;
    out.body["layer_id"] = static_cast<Json::UInt64>(*idParsed);
    out.body["layer_name"] = layer->name;
    out.body["page"] = page;
    out.body["limit"] = limit;
    out.body["total"] = static_cast<Json::UInt64>(total);
    out.body["observations"] = items;
    return out;
}

MemoryManager::OperationResult MemoryManager::SqlCreateLayerObservation(
    memory::MemoryStore* store,
    const std::string& idStr,
    const Json::Value* body) const {
    OperationResult out;
    if (!store) {
        out.httpStatusCode = 200;
        out.body = Json::Value("unavailable");
        return out;
    }
    auto idParsed = ParseInt64PathId(idStr);
    if (!idParsed) {
        out.httpStatusCode = 400;
        out.body = Json::Value("invalid id");
        return out;
    }
    if (!body) {
        out.httpStatusCode = 400;
        out.body = Json::Value("invalid JSON");
        return out;
    }
    memory::Observation obs;
    obs.layer_id = *idParsed;
    obs.text = (*body)["text"].asString();
    obs.weight = (*body).get("weight", 1.0).asDouble();
    obs.decay_rate = (*body).get("decay_rate", 0.95).asDouble();
    obs.tags_json = (*body).get("tags", "[]").asString();
    obs.source = (*body).get("source", "manual").asString();
    obs.memory_state = ParseMemoryState(*body, memory::MemoryState::New);
    const std::string agentId = (*body).get("agent_id", "default").asString();
    if (obs.text.empty()) {
        out.httpStatusCode = 400;
        out.body = Json::Value("text is required");
        return out;
    }

    const auto created = store->CreateObservationForAgent(agentId, obs);
    out.httpStatusCode = 201;
    out.body = BuildSqlObservationJson(created);
    return out;
}

MemoryManager::OperationResult MemoryManager::SqlGetObservation(
    memory::MemoryStore* store,
    const std::string& idStr) const {
    OperationResult out;
    if (!store) {
        out.httpStatusCode = 503;
        out.body = Json::Value("memory store not available");
        return out;
    }
    auto idParsed = ParseInt64PathId(idStr);
    if (!idParsed) {
        out.httpStatusCode = 400;
        out.body = Json::Value("invalid id");
        return out;
    }
    auto obs = store->GetObservation(*idParsed);
    if (!obs) {
        out.httpStatusCode = 404;
        out.body = Json::Value("not found");
        return out;
    }

    out.httpStatusCode = 200;
    out.body = BuildSqlObservationJson(*obs);
    return out;
}

MemoryManager::OperationResult MemoryManager::SqlDeleteObservation(
    memory::MemoryStore* store,
    const std::string& idStr) const {
    OperationResult out;
    if (!store) {
        out.httpStatusCode = 503;
        out.body = Json::Value("memory store not available");
        return out;
    }
    auto idParsed = ParseInt64PathId(idStr);
    if (!idParsed) {
        out.httpStatusCode = 400;
        out.body = Json::Value("invalid id");
        return out;
    }
    if (!store->DeleteObservation(*idParsed)) {
        out.httpStatusCode = 404;
        out.body = Json::Value("not found");
        return out;
    }
    out.httpStatusCode = 200;
    out.body = Json::Value("ok");
    return out;
}

MemoryManager::OperationResult MemoryManager::SqlGetLayerPerspective(
    memory::MemoryStore* store,
    const std::string& idStr) const {
    OperationResult out;
    if (!store) {
        out.httpStatusCode = 200;
        out.body = Json::Value("unavailable");
        return out;
    }
    auto idParsed = ParseInt64PathId(idStr);
    if (!idParsed) {
        out.httpStatusCode = 400;
        out.body = Json::Value("invalid id");
        return out;
    }
    auto persp = store->GetPerspective(*idParsed);
    if (!persp) {
        out.httpStatusCode = 404;
        out.body = Json::Value("not found");
        return out;
    }
    out.httpStatusCode = 200;
    out.body["id"] = static_cast<Json::UInt64>(persp->id);
    out.body["layer_id"] = static_cast<Json::UInt64>(persp->layer_id);
    out.body["retrospective"] = persp->retrospective;
    out.body["retrospective_valence"] = persp->retrospective_valence;
    out.body["current_perspective"] = persp->current_perspective;
    out.body["current_valence"] = persp->current_valence;
    out.body["future_perspective"] = persp->future_perspective;
    out.body["future_valence"] = persp->future_valence;
    out.body["updated_at_unix_ms"] = static_cast<Json::UInt64>(persp->updated_at_unix_ms);
    return out;
}

MemoryManager::OperationResult MemoryManager::SqlPutLayerPerspective(
    memory::MemoryStore* store,
    const std::string& idStr,
    const Json::Value* body) const {
    OperationResult out;
    if (!store) {
        out.httpStatusCode = 200;
        out.body = Json::Value("unavailable");
        return out;
    }
    auto idParsed = ParseInt64PathId(idStr);
    if (!idParsed) {
        out.httpStatusCode = 400;
        out.body = Json::Value("invalid id");
        return out;
    }
    if (!body) {
        out.httpStatusCode = 400;
        out.body = Json::Value("invalid JSON");
        return out;
    }

    memory::LayerPerspective p;
    p.layer_id = *idParsed;
    p.retrospective = (*body).get("retrospective", "").asString();
    p.retrospective_valence = (*body).get("retrospective_valence", "").asString();
    p.current_perspective = (*body).get("current_perspective", "").asString();
    p.current_valence = (*body).get("current_valence", "").asString();
    p.future_perspective = (*body).get("future_perspective", "").asString();
    p.future_valence = (*body).get("future_valence", "").asString();
    store->SetPerspective(p);
    out.httpStatusCode = 200;
    out.body = Json::Value("ok");
    return out;
}

bool MemoryManager::ParseObservationPatch(
    const Json::Value& patch,
    KernelConfig::MemoryObservation* observation,
    std::string* error) {
    if (!patch.isObject()) {
        if (error) {
            *error = "request body must be a JSON object";
        }
        return false;
    }
    if (!observation) {
        if (error) {
            *error = "observation target is null";
        }
        return false;
    }

    if (patch.isMember("tags")) {
        const Json::Value& tags = patch["tags"];
        if (!tags.isArray()) {
            if (error) {
                *error = "tags must be an array of strings";
            }
            return false;
        }
        std::vector<std::string> parsedTags;
        for (Json::ArrayIndex i = 0; i < tags.size(); ++i) {
            if (!tags[i].isString()) {
                if (error) {
                    *error = "tags must be an array of strings";
                }
                return false;
            }
            parsedTags.push_back(tags[i].asString());
        }
        observation->tags = std::move(parsedTags);
    }

    if (patch.isMember("weight")) {
        const Json::Value& weight = patch["weight"];
        if (!(weight.isDouble() || weight.isInt() || weight.isUInt())) {
            if (error) {
                *error = "weight must be a number";
            }
            return false;
        }
        const double parsedWeight = weight.asDouble();
        if (parsedWeight <= 0.0 || parsedWeight > 1000.0) {
            if (error) {
                *error = "weight must be between 0 and 1000";
            }
            return false;
        }
        observation->weight = parsedWeight;
    }

    return true;
}

std::string MemoryManager::DefaultFirstLayerNameLocked() const {
    if (m_memoryLayers.empty()) {
        return "day";
    }
    return m_memoryLayers.front().name;
}

} // namespace animus::kernel

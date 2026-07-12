#include "animus_kernel/admin/ConstitutionManager.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include <json/json.h>

namespace animus::kernel {

void ConstitutionManager::Configure(const KernelConfig::ConstitutionConfigStorage& storage) {
    std::lock_guard<std::mutex> lock(m_constitutionMutex);
    m_constitutionStorage = storage;
}

bool ConstitutionManager::LoadFromDisk(std::string* error) {
    std::lock_guard<std::mutex> lock(m_constitutionMutex);
    InitializeDefaultsLocked();

    if (!m_constitutionStorage.persistToDisk) {
        return true;
    }
    if (m_constitutionStorage.filePath.empty()) {
        if (error) {
            *error = "constitution file path must not be empty when persistence is enabled";
        }
        return false;
    }

    const std::filesystem::path path(m_constitutionStorage.filePath);
    if (!std::filesystem::exists(path)) {
        return true;
    }

    std::ifstream in(path);
    if (!in.is_open()) {
        if (error) {
            *error = "failed to open constitution file for reading: " + path.string();
        }
        return false;
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string parseErrors;
    if (!Json::parseFromStream(builder, in, &root, &parseErrors)) {
        if (error) {
            *error = "failed to parse constitution file: " + parseErrors;
        }
        return false;
    }

    if (root.isMember("core") && root["core"].isObject()) {
        m_constitutionCore = root["core"];
    }
    if (root.isMember("contracts")
        && (root["contracts"].isArray() || root["contracts"].isObject())) {
        m_constitutionContracts = root["contracts"];
    }
    if (root.isMember("adaptation") && root["adaptation"].isObject()) {
        // Preserve default adaptation keys when persisted state is partial.
        MergeJsonObject(root["adaptation"], &m_constitutionAdaptation);
    }
    if (root.isMember("audit_log") && root["audit_log"].isArray()) {
        m_constitutionAuditLog = root["audit_log"];
    } else if (root.isMember("audit") && root["audit"].isArray()) {
        m_constitutionAuditLog = root["audit"];
    }
    return true;
}

bool ConstitutionManager::SaveToDisk(std::string* error) const {
    std::lock_guard<std::mutex> lock(m_constitutionMutex);
    return SaveToDiskLocked(error);
}

ConstitutionManager::Snapshot ConstitutionManager::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(m_constitutionMutex);
    Snapshot snapshot;
    snapshot.core = m_constitutionCore;
    snapshot.contracts = m_constitutionContracts;
    snapshot.adaptation = m_constitutionAdaptation;
    snapshot.auditLogSize = static_cast<std::uint64_t>(m_constitutionAuditLog.size());
    return snapshot;
}

Json::Value ConstitutionManager::GetCore() const {
    std::lock_guard<std::mutex> lock(m_constitutionMutex);
    return m_constitutionCore;
}

Json::Value ConstitutionManager::GetContracts() const {
    std::lock_guard<std::mutex> lock(m_constitutionMutex);
    return m_constitutionContracts;
}

Json::Value ConstitutionManager::GetAdaptation(std::uint64_t* auditLogSize) const {
    std::lock_guard<std::mutex> lock(m_constitutionMutex);
    if (auditLogSize) {
        *auditLogSize = static_cast<std::uint64_t>(m_constitutionAuditLog.size());
    }
    return m_constitutionAdaptation;
}

bool ConstitutionManager::PatchAdaptation(
    const Json::Value& patch,
    Json::Value* state,
    std::uint64_t* auditLogSize,
    std::string* error) {
    if (!patch.isObject()) {
        if (error) {
            *error = "request body must be a JSON object";
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(m_constitutionMutex);
    const Json::Value before = m_constitutionAdaptation;
    const Json::Value previousAuditLog = m_constitutionAuditLog;

    Json::Value after = before;
    MergeJsonObject(patch, &after);

    Json::Value auditEntry(Json::objectValue);
    auditEntry["action"] = "patch_adaptation";
    auditEntry["timestamp_unix_ms"] = static_cast<Json::UInt64>(NowUnixMs());
    auditEntry["source"] = "api";
    auditEntry["patch"] = patch;
    auditEntry["before"] = before;
    auditEntry["after"] = after;

    Json::Value nextAuditLog = previousAuditLog;
    nextAuditLog.append(auditEntry);
    m_constitutionAdaptation = after;
    m_constitutionAuditLog = nextAuditLog;

    std::string persistError;
    if (!SaveToDiskLocked(&persistError)) {
        m_constitutionAdaptation = before;
        m_constitutionAuditLog = previousAuditLog;
        if (error) {
            *error = persistError;
        }
        return false;
    }

    if (state) {
        *state = after;
    }
    if (auditLogSize) {
        *auditLogSize = static_cast<std::uint64_t>(nextAuditLog.size());
    }
    return true;
}

Json::Value ConstitutionManager::BuildConstitutionResponse() const {
    const auto snapshot = GetSnapshot();
    Json::Value out(Json::objectValue);
    out["core"] = snapshot.core;
    out["contracts"] = snapshot.contracts;
    out["adaptation"] = snapshot.adaptation;
    out["audit_log_size"] = static_cast<Json::UInt64>(snapshot.auditLogSize);
    Json::Value layers(Json::arrayValue);
    {
        Json::Value layer(Json::objectValue);
        layer["layer_type"] = "core";
        layer["mutability"] = "read_only";
        layer["state"] = snapshot.core;
        layers.append(layer);
    }
    {
        Json::Value layer(Json::objectValue);
        layer["layer_type"] = "contracts";
        layer["mutability"] = "read_only";
        layer["state"] = snapshot.contracts;
        layers.append(layer);
    }
    {
        Json::Value layer(Json::objectValue);
        layer["layer_type"] = "adaptation";
        layer["mutability"] = "mutable";
        layer["state"] = snapshot.adaptation;
        layers.append(layer);
    }
    out["layers"] = layers;
    return out;
}

Json::Value ConstitutionManager::BuildCoreLayerResponse() const {
    Json::Value out(Json::objectValue);
    out["layer_type"] = "core";
    out["mutability"] = "read_only";
    out["state"] = GetCore();
    return out;
}

Json::Value ConstitutionManager::BuildContractsLayerResponse() const {
    Json::Value out(Json::objectValue);
    out["layer_type"] = "contracts";
    out["mutability"] = "read_only";
    out["state"] = GetContracts();
    return out;
}

Json::Value ConstitutionManager::BuildAdaptationLayerResponse() const {
    Json::Value out(Json::objectValue);
    std::uint64_t auditLogSize = 0;
    out["layer_type"] = "adaptation";
    out["mutability"] = "mutable";
    out["state"] = GetAdaptation(&auditLogSize);
    out["audit_log_size"] = static_cast<Json::UInt64>(auditLogSize);
    return out;
}

ConstitutionManager::OperationResult ConstitutionManager::PatchAdaptationFromBody(
    const Json::Value& body) {
    OperationResult out;
    if (!body.isObject()) {
        out.httpStatusCode = 400;
        out.body["error"] = "request body must be a JSON object";
        return out;
    }

    Json::Value after(Json::objectValue);
    std::uint64_t auditLogSize = 0;
    std::string patchError;
    if (!PatchAdaptation(body, &after, &auditLogSize, &patchError)) {
        out.httpStatusCode =
            patchError == "request body must be a JSON object" ? 400 : 500;
        out.body["error"] = patchError;
        return out;
    }

    out.httpStatusCode = 200;
    out.body["layer_type"] = "adaptation";
    out.body["mutability"] = "mutable";
    out.body["state"] = after;
    out.body["audit_log_size"] = static_cast<Json::UInt64>(auditLogSize);
    return out;
}

std::uint64_t ConstitutionManager::NowUnixMs() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch());
    return static_cast<std::uint64_t>(ms.count());
}

void ConstitutionManager::MergeJsonObject(const Json::Value& patch, Json::Value* target) {
    if (!target) {
        return;
    }
    if (!patch.isObject() || !target->isObject()) {
        *target = patch;
        return;
    }

    const std::vector<std::string> fields = patch.getMemberNames();
    for (const auto& key : fields) {
        const Json::Value& next = patch[key];
        if ((*target).isMember(key) && (*target)[key].isObject() && next.isObject()) {
            MergeJsonObject(next, &(*target)[key]);
            continue;
        }
        (*target)[key] = next;
    }
}

void ConstitutionManager::InitializeDefaultsLocked() {
    m_constitutionCore = Json::Value(Json::objectValue);
    m_constitutionCore["identity"] = "animus";
    m_constitutionCore["invariants"] = Json::Value(Json::arrayValue);
    m_constitutionCore["invariants"].append("protect_core_identity");
    m_constitutionCore["invariants"].append("respect_governance_boundaries");
    m_constitutionCore["governance_required"] = true;

    m_constitutionContracts = Json::Value(Json::arrayValue);
    {
        Json::Value contract(Json::objectValue);
        contract["id"] = "contract.memory_write_thresholds";
        contract["description"] = "High-stability writes require governance review.";
        contract["approval_requirement"] = "governance_approved";
        m_constitutionContracts.append(contract);
    }
    {
        Json::Value contract(Json::objectValue);
        contract["id"] = "contract.interface_safety";
        contract["description"] = "External integrations remain scoped by explicit permissions.";
        contract["approval_requirement"] = "instance_approved";
        m_constitutionContracts.append(contract);
    }

    m_constitutionAdaptation = Json::Value(Json::objectValue);
    m_constitutionAdaptation["interaction_style"]["tone"] = "balanced";
    m_constitutionAdaptation["interaction_style"]["verbosity"] = "medium";
    m_constitutionAdaptation["retrieval_preference"]["weight_recency"] = 0.6;
    m_constitutionAdaptation["retrieval_preference"]["weight_relevance"] = 0.4;

    m_constitutionAuditLog = Json::Value(Json::arrayValue);
}

bool ConstitutionManager::SaveToDiskLocked(std::string* error) const {
    if (!m_constitutionStorage.persistToDisk) {
        return true;
    }
    if (m_constitutionStorage.filePath.empty()) {
        if (error) {
            *error = "constitution file path must not be empty when persistence is enabled";
        }
        return false;
    }

    const std::filesystem::path path(m_constitutionStorage.filePath);
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            if (error) {
                *error = "failed to create constitution directory: " + parent.string();
            }
            return false;
        }
    }

    Json::Value root(Json::objectValue);
    root["core"] = m_constitutionCore;
    root["contracts"] = m_constitutionContracts;
    root["adaptation"] = m_constitutionAdaptation;
    root["audit_log"] = m_constitutionAuditLog;
    root["updated_at_unix_ms"] = static_cast<Json::UInt64>(NowUnixMs());

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "  ";

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        if (error) {
            *error = "failed to open constitution file for writing: " + path.string();
        }
        return false;
    }

    std::unique_ptr<Json::StreamWriter> writer(writerBuilder.newStreamWriter());
    writer->write(root, &out);
    out << "\n";
    if (!out.good()) {
        if (error) {
            *error = "failed to write constitution file: " + path.string();
        }
        return false;
    }
    return true;
}

} // namespace animus::kernel

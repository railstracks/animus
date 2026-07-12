#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include <json/value.h>

#include "animus_kernel/KernelConfig.h"

namespace animus::kernel {

class ConstitutionManager {
public:
    struct OperationResult {
        int httpStatusCode{200};
        Json::Value body{Json::objectValue};
    };

    struct Snapshot {
        Json::Value core{Json::objectValue};
        Json::Value contracts{Json::arrayValue};
        Json::Value adaptation{Json::objectValue};
        std::uint64_t auditLogSize{0};
    };

    void Configure(const KernelConfig::ConstitutionConfigStorage& storage);

    bool LoadFromDisk(std::string* error);
    bool SaveToDisk(std::string* error) const;

    Snapshot GetSnapshot() const;
    Json::Value GetCore() const;
    Json::Value GetContracts() const;
    Json::Value GetAdaptation(std::uint64_t* auditLogSize) const;

    bool PatchAdaptation(
        const Json::Value& patch,
        Json::Value* state,
        std::uint64_t* auditLogSize,
        std::string* error);

    Json::Value BuildConstitutionResponse() const;
    Json::Value BuildCoreLayerResponse() const;
    Json::Value BuildContractsLayerResponse() const;
    Json::Value BuildAdaptationLayerResponse() const;
    OperationResult PatchAdaptationFromBody(const Json::Value& body);

private:
    static std::uint64_t NowUnixMs();
    static void MergeJsonObject(const Json::Value& patch, Json::Value* target);
    void InitializeDefaultsLocked();
    bool SaveToDiskLocked(std::string* error) const;

    KernelConfig::ConstitutionConfigStorage m_constitutionStorage{};
    Json::Value m_constitutionCore{Json::objectValue};
    Json::Value m_constitutionContracts{Json::arrayValue};
    Json::Value m_constitutionAdaptation{Json::objectValue};
    Json::Value m_constitutionAuditLog{Json::arrayValue};
    mutable std::mutex m_constitutionMutex;
};

} // namespace animus::kernel

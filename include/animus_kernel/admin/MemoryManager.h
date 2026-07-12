#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <json/value.h>

#include "animus_kernel/KernelConfig.h"

namespace animus::kernel {
namespace memory {
class MemoryStore;
}

struct ConsolidationSummary {
    std::uint64_t promoted{0};
    std::uint64_t demoted{0};
    std::uint64_t archived{0};
};

struct ConsolidationState {
    bool running{false};
    std::uint64_t activeJobId{0};
    std::uint64_t lastRunUnixMs{0};
    std::uint64_t lastJobId{0};
    std::string lastError{};
    ConsolidationSummary lastSummary{};
};

class MemoryManager {
public:
    struct OperationResult {
        int httpStatusCode{200};
        Json::Value body{Json::objectValue};
    };

    struct LayerWithCount {
        KernelConfig::MemoryLayerConfig layer;
        std::uint64_t observationCount{0};
    };

    struct LayerObservationQueryResult {
        bool layerExists{false};
        std::string sort{"weight"};
        bool ascending{false};
        std::uint64_t total{0};
        std::vector<KernelConfig::MemoryObservation> items;
    };

    void Configure(
        const KernelConfig::MemoryConfig& memoryConfig,
        std::atomic<std::uint64_t>* nextObservationId);

    bool LoadFromDisk(std::string* error);
    bool SaveToDisk(std::string* error) const;

    KernelConfig::MemoryObservation CaptureObservation(
        const std::string& text,
        const std::vector<std::string>& tags,
        const std::string& idPrefix = "obs_capture_");

    std::vector<std::string> ListLayerNames() const;
    std::vector<LayerWithCount> ListLayersWithCounts() const;
    Json::Value BuildLayersResponse() const;
    std::vector<KernelConfig::MemoryLayerConfig> ListLayers() const;
    std::vector<KernelConfig::MemoryObservation> ListAllObservations() const;
    std::vector<KernelConfig::MemoryObservation> ListLayerObservations(
        const std::string& layerName,
        bool* layerExists) const;
    LayerObservationQueryResult QueryLayerObservations(
        const std::string& layerName,
        const std::string& sort,
        bool ascending,
        std::uint32_t page,
        std::uint32_t limit) const;
    OperationResult BuildLayerObservationsResponse(
        const std::string& layerName,
        const std::string& sort,
        bool ascending,
        std::uint32_t page,
        std::uint32_t limit) const;

    std::optional<KernelConfig::MemoryObservation> GetObservation(
        const std::string& observationId) const;
    OperationResult BuildObservationResponse(const std::string& observationId) const;
    bool UpdateObservation(
        const std::string& observationId,
        const Json::Value& patch,
        KernelConfig::MemoryObservation* updated,
        std::string* error);
    OperationResult PatchObservationFromBody(
        const std::string& observationId,
        const Json::Value& body);
    bool ArchiveObservation(
        const std::string& observationId,
        KernelConfig::MemoryObservation* archived,
        std::string* error);
    OperationResult ArchiveObservationById(const std::string& observationId);

    bool BeginConsolidation(
        std::uint64_t* jobId,
        std::uint64_t* activeJobId,
        std::string* error);
    OperationResult BeginConsolidationRequest();
    void RunConsolidationJob(std::uint64_t jobId);
    ConsolidationState GetConsolidationState() const;
    Json::Value BuildConsolidationStatusResponse() const;

    OperationResult SqlCreateLayer(
        memory::MemoryStore* store,
        const Json::Value* body) const;
    OperationResult SqlListLayers(memory::MemoryStore* store,
                            const std::string& agentId = "") const;
    OperationResult SqlUpdateLayer(
        memory::MemoryStore* store,
        const std::string& idStr,
        const Json::Value* body) const;
    OperationResult SqlDeleteLayer(
        memory::MemoryStore* store,
        const std::string& idStr) const;
    OperationResult SqlListLayerObservations(
        memory::MemoryStore* store,
        const std::string& idStr,
        std::uint32_t page,
        std::uint32_t limit) const;
    OperationResult SqlCreateLayerObservation(
        memory::MemoryStore* store,
        const std::string& idStr,
        const Json::Value* body) const;
    OperationResult SqlGetObservation(
        memory::MemoryStore* store,
        const std::string& idStr) const;
    OperationResult SqlDeleteObservation(
        memory::MemoryStore* store,
        const std::string& idStr) const;
    OperationResult SqlGetLayerPerspective(
        memory::MemoryStore* store,
        const std::string& idStr) const;
    OperationResult SqlPutLayerPerspective(
        memory::MemoryStore* store,
        const std::string& idStr,
        const Json::Value* body) const;

private:
    static std::uint64_t NowUnixMs();
    static double ObservationAgeSeconds(
        const KernelConfig::MemoryObservation& observation,
        std::uint64_t nowUnixMs);
    static bool ParsePrefixedUInt64(
        const std::string& value,
        const std::string& prefix,
        std::uint64_t* out);
    static Json::Value SerializeLayer(const KernelConfig::MemoryLayerConfig& layer);
    static Json::Value SerializeObservation(const KernelConfig::MemoryObservation& observation);
    static Json::Value BuildObservationApiJson(
        const KernelConfig::MemoryObservation& observation);
    static bool ParseObservationPatch(
        const Json::Value& patch,
        KernelConfig::MemoryObservation* observation,
        std::string* error);
    std::string DefaultFirstLayerNameLocked() const;

    KernelConfig::MemoryConfig m_memoryConfig{};
    std::vector<KernelConfig::MemoryLayerConfig> m_memoryLayers;
    std::unordered_map<std::string, KernelConfig::MemoryObservation> m_observationsById;
    ConsolidationState m_consolidationState;
    std::atomic<std::uint64_t> m_nextMemoryConsolidationJobId{1};
    std::atomic<std::uint64_t>* m_nextObservationId{nullptr};
    mutable std::mutex m_memoryMutex;
};

} // namespace animus::kernel

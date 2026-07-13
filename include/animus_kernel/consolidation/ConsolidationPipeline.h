#pragma once

#include "animus_kernel/consolidation/ConsolidationStore.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/OntologyStore.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/admin/DiaryManager.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

#include <json/value.h>

namespace animus::kernel {

// Forward declarations
class IDataStore;
class Scheduler;
class SessionManager;
namespace memory { class MemoryFileStore; }
class AgentStore;

// ============================================================================
// LLM callback — how the pipeline calls the LLM for evaluation
// ============================================================================

// The pipeline doesn't depend on ChainRunner directly. Instead, the kernel
// provides a callback that takes a prompt and returns the LLM response.
using ConsolidationLLMCallback = std::function<std::string(
    const std::string& agentId,
    const std::string& systemPrompt,
    const std::string& userPrompt)>;

// Context window resolver — returns the effective context window (in tokens)
// for an agent, accounting for provider defaults, model overrides, and agent-level caps.
using ContextWindowResolver = std::function<std::size_t(const std::string& agentId)>;

// ============================================================================
// ConsolidationPipeline — orchestrates intake, layer consolidation,
//                          and perspective revision
// ============================================================================

class ConsolidationPipeline {
public:
    struct Config {
        // Intake
        bool intake_enabled{true};
        int intake_batch_size{200};
        double intake_min_weight{0.1};
        std::string intake_cron_expr{"*/5 * * * *"};  // cron for intake poll (default every 5 min)

        // Layer consolidation
        bool consolidation_enabled{true};

        // Perspective revision
        bool perspectives_enabled{true};
        bool perspectives_update_after_consolidation{true};

        // LLM prompts (defaults, can be overridden per-layer)
        std::string intake_prompt{
            "Review the following agent activity and extract lasting observations. "
            "For each observation worth keeping, provide a JSON array where each element has: "
            "\"text\" (the observation), \"tags\" (array of category strings), "
            "\"weight\" (0.0-1.0 importance score). "
            "Skip trivial, redundant, or already-known information. "
            "If nothing is worth keeping, return an empty array []."};
        std::string ontology_prompt{
            "You are reconciling freshly extracted observations into ontology updates. "
            "Return JSON as {\"upserts\":[...]} where each upsert includes root_category, path, "
            "and properties map keyed by property name. Each property object includes value, "
            "value_type (text|number|date|reference|url), linked_observation_id, and optional motivation. "
            "Only emit confident, non-duplicative updates."};
        std::string consolidation_prompt{
            "Review these observations from a memory layer. For each observation, decide: "
            "\"promote\" (move to higher layer — lasting significance), "
            "\"retain\" (keep in current layer — still relevant), "
            "\"demote\" (move to lower layer — fading relevance). "
            "Respond as a JSON array where each element has: "
            "\"id\" (observation id), \"action\" (promote/retain/demote), "
            "\"reason\" (brief justification)."};
        std::string perspective_prompt{
            "Based on the observations in this memory layer, generate three perspectives:\n"
            "1. retrospective: \"Looking back: what patterns, lessons, and through-lines emerged?\"\n"
            "2. current: \"Right now: what's active, what's blocked, what's the operational state?\"\n"
            "3. future: \"Looking ahead: what's expected, what risks, what opportunities?\"\n"
            "Respond as JSON with keys: retrospective, current, future. Each value is a string."};
    };

    struct PipelineStats {
        int64_t observations_created{0};
        int64_t observations_promoted{0};
        int64_t observations_demoted{0};
        int64_t observations_archived{0};
        int64_t perspectives_updated{0};
        int64_t runs_completed{0};
        int64_t runs_failed{0};
    };

    ConsolidationPipeline(
        IDataStore* dataStore,
        memory::MemoryStore* memoryStore,
        ontology::OntologyStore* ontologyStore,
        DiaryStore* diaryStore,
        SessionManager* sessionManager,
        AgentStore* agentStore,
        ConsolidationLLMCallback llmCallback);
    ~ConsolidationPipeline();

    ConsolidationPipeline(const ConsolidationPipeline&) = delete;
    ConsolidationPipeline& operator=(const ConsolidationPipeline&) = delete;

    // Configuration
    void Configure(const Config& config);
    Config GetConfig() const;

    // Dependency injection (optional, set after construction)
    void SetMemoryFileStore(memory::MemoryFileStore* store) { m_memoryFileStore = store; }
    void SetContextWindowResolver(ContextWindowResolver resolver) { m_contextWindowResolver = std::move(resolver); }

    // Lifecycle — Start registers cron jobs with the scheduler, Stop cancels them
    bool Start(Scheduler* scheduler, std::string* error);
    void Stop();
    bool IsRunning() const;

    // Manual trigger (for admin API or testing)
    bool RunIntake(
        const std::string& agentId,
        std::optional<int64_t> targetLayerId,
        std::string* error);
    bool RunLayerConsolidation(const std::string& agentId,
                                const std::string& layerName,
                                std::string* error);
    bool RunPerspectiveRevision(const std::string& agentId,
                                 const std::string& layerName,
                                 std::string* error);
    bool RunFullPipeline(const std::string& agentId, std::string* error);

    // Query state
    PipelineStats GetStats() const;
    Json::Value GetStatusJson() const;
    ConsolidationStore& Store() { return m_store; }

    // List agents known to have data (for per-agent processing)
    std::vector<std::string> GetKnownAgents() const;

    // Check if there is pending intake data (unprocessed diary entries or session turns)
    bool HasPendingIntakeData(const std::string& agentId);
    // Check if any known agent has pending intake data
    bool HasAnyPendingIntakeData();

private:
    memory::MemoryFileStore* m_memoryFileStore{nullptr};

    // Register cron schedules for enabled layers + intake
    void RegisterSchedules(Scheduler* scheduler);

    // Intake internals
    int64_t IntakeFromDiary(
        const std::string& agentId,
        int64_t targetLayerId,
        std::vector<memory::Observation>* createdObservations,
        std::string* error);
    int64_t IntakeFromSessions(
        const std::string& agentId,
        int64_t targetLayerId,
        std::vector<memory::Observation>* createdObservations,
        std::string* error);
    int64_t ReconcileOntologyFromObservations(
        const std::string& agentId,
        int64_t targetLayerId,
        const std::vector<memory::Observation>& observations,
        std::string* error);

    // Layer consolidation internals
    int PromoteObservation(int64_t obsId, int64_t fromLayerId,
                           const std::string& reason);
    int DemoteObservation(int64_t obsId, int64_t fromLayerId,
                          const std::string& reason);
    int ArchiveObservation(int64_t obsId, int64_t fromLayerId,
                           const std::string& reason);

    // LLM parsing helpers
    std::vector<Json::Value> ParseLLMJsonArray(
        const std::string& llmResponse, std::string* error);

    ConsolidationStore m_store;
    memory::MemoryStore* m_memoryStore;
    ontology::OntologyStore* m_ontologyStore;
    DiaryStore* m_diaryStore;
    SessionManager* m_sessionManager;
    AgentStore* m_agentStore;
    ConsolidationLLMCallback m_llmCallback;
    ContextWindowResolver m_contextWindowResolver;
    Config m_config;
    Scheduler* m_scheduler{nullptr};
    std::vector<std::string> m_registeredScheduleIds;

    std::atomic<bool> m_running{false};
    mutable std::mutex m_statsMutex;
    PipelineStats m_stats;
};

} // namespace animus::kernel


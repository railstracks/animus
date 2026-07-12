#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace animus::kernel {

class IDataStore;

namespace memory {

// ============================================================================
// Data types
// ============================================================================

enum class MemoryState : int32_t {
    New = 0,
    Current = 1,
    Deprecated = 2
};

struct MemoryLayer {
    int64_t id{0};
    std::string agent_id{"default"};                   // owning agent — layers are per-agent
    std::string name;
    std::string horizon;                              // TEXT — semantic hint for AI ("1 month"), NOT parsed
    int32_t sort_order{0};                            // explicit ordering
    int64_t evaluation_interval_seconds{86400};        // min age before observation enters eval batch (1 day)
    std::string cron_expr{"0 * * * *"};               // cron schedule for consolidation fire (hourly)
    std::string consolidation_review_prompt;
    std::string consolidation_intake_prompt;
    std::optional<std::string> intake_interval;       // cron string, null = intake disabled
    int64_t token_budget{4096};
    bool enabled{false};
    int64_t created_at_unix_ms{0};
    int64_t updated_at_unix_ms{0};
};

struct Observation {
    int64_t id{0};
    int64_t layer_id{0};
    std::string agent_id{"default"};  // First-class agent scope — survives demotion
    std::string text;
    double weight{1.0};
    double decay_rate{0.95};
    std::string tags_json;   // JSON array string
    std::string source;
    int64_t created_at_unix_ms{0};
    int64_t updated_at_unix_ms{0};
    int64_t last_evaluated_at_ms{0};  // last LLM evaluation (0 = never)
    int64_t next_review_at_ms{0};     // explicit due timestamp for next review
    MemoryState memory_state{MemoryState::New};
    int64_t superseded_by{0};          // ID of the observation that replaced this one (0 = current)
};

struct LayerPerspective {
    int64_t id{0};
    int64_t layer_id{0};
    std::string retrospective;
    std::string retrospective_valence;
    std::string current_perspective;
    std::string current_valence;
    std::string future_perspective;
    std::string future_valence;
    int64_t updated_at_unix_ms{0};
};

struct MemoryMutation {
    int64_t id{0};
    std::string mutation_type;
    std::string target_type;
    int64_t target_id{0};
    std::optional<int64_t> from_layer_id;
    std::optional<int64_t> to_layer_id;
    std::string previous_state;
    std::string motivation;
    int64_t unix_ms{0};
};

// ============================================================================
// MemoryStore — SQLite-backed memory layer storage
// ============================================================================

class MemoryStore {
public:
    // Takes a shared IDataStore (does not assume ownership).
    // Creates memory schema tables if they don't exist.
    explicit MemoryStore(IDataStore* dataStore);
    ~MemoryStore();

    // No copy
    MemoryStore(const MemoryStore&) = delete;
    MemoryStore& operator=(const MemoryStore&) = delete;

    // ---- Layers ----
    std::vector<MemoryLayer> ListLayers();                              // all layers, sorted by sort_order ASC
    std::vector<MemoryLayer> ListLayersForAgent(const std::string& agent_id); // layers for a specific agent
    std::optional<MemoryLayer> GetLayer(int64_t id);
    MemoryLayer CreateLayer(const MemoryLayer& layer);
    bool UpdateLayer(const MemoryLayer& layer);
    bool DeleteLayer(int64_t id);

    // Copy all layers from one agent to another (for agent creation).
    // Returns the number of layers copied.
    int CopyLayersForAgent(const std::string& source_agent_id, const std::string& dest_agent_id);

    // Convenience: find the lowest-sort-order layer (for intake), scoped by agent
    std::optional<MemoryLayer> GetLowestLayer(const std::string& agent_id);
    // Convenience: find the first configured intake layer (sort_order ASC), scoped by agent.
    std::optional<MemoryLayer> GetIntakeLayer(const std::string& agent_id);

    // ---- Observations (legacy, unscoped) ----
    std::vector<Observation> ListObservations(int64_t layer_id);
    std::optional<Observation> GetObservation(int64_t id);
    Observation CreateObservation(const Observation& obs);
    bool UpdateObservation(const Observation& obs);
    bool SetObservationMemoryState(
        int64_t obs_id, MemoryState memory_state, const std::string& motivation);
    bool DeleteObservation(int64_t id);
    bool MoveObservation(int64_t obs_id, int64_t to_layer_id,
                         const std::string& motivation);

    // ---- Observations (agent-scoped) ----
    // These filter by agent_id. Use these when operating in multi-tenant mode.
    std::vector<Observation> ListObservationsForAgent(const std::string& agent_id, int64_t layer_id);

    // List observations by layer only — use when layer ownership is already verified.
    // The layer is the access boundary; the observation's agent_id is a stamp, not access control.
    std::vector<Observation> ListObservationsForLayer(int64_t layer_id);

    // List only observations due for review:
    //   next_review_at_ms <= now_ms
    std::vector<Observation> ListObservationsDueForReview(
        const std::string& agent_id, int64_t layer_id, int64_t now_ms);

    Observation CreateObservationForAgent(const std::string& agent_id, const Observation& obs);
    std::vector<Observation> SearchObservationsForAgent(const std::string& agent_id, int64_t since_unix_ms = 0, int64_t limit = 100);

    // Update last_evaluated_at_ms to now and compute next_review_at_ms from layer policy.
    bool TouchEvaluation(int64_t obs_id);

    // ---- Perspectives ----
    std::optional<LayerPerspective> GetPerspective(int64_t layer_id);
    LayerPerspective SetPerspective(const LayerPerspective& p);

    // ---- Mutations (append-only) ----
    void LogMutation(const MemoryMutation& m);
    std::vector<MemoryMutation> QueryMutations(int64_t since_unix_ms = 0,
                                                int64_t limit = 100);

    // ---- Observations (versioning) ----
    // Copy-on-write revision: creates a new version of the observation,
    // preserving the old one as a historical snapshot.
    // Returns the new observation (with the new ID).
    Observation ReviseObservation(int64_t obs_id, const std::string& new_text,
                                  double new_weight, const std::string& new_tags_json);
    // List the version chain for an observation (oldest first).
    // Returns all versions: the current one and all superseded predecessors.
    std::vector<Observation> GetObservationHistory(int64_t obs_id);

    // Expose shared datastore for cross-domain search/index maintenance.
    IDataStore* DataStore() const { return m_store; }

    // Seed the 7-layer default memory hierarchy if no layers exist yet.
    // Returns true if layers were created, false if layers already existed.
    bool CreateDefaultLayersForAgent(const std::string& agent_id);

private:
    void EnsureSchema();

    IDataStore* m_store;
};

} // namespace animus::kernel::memory
} // namespace animus::kernel

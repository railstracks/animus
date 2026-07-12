#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/IDataStore.h"
#include "animus_kernel/SchemaHelpers.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <utility>

namespace animus::kernel::memory {

// ============================================================================
// Helpers
// ============================================================================

static int64_t NowUnixMs() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

int64_t MemoryStateToInt(MemoryState state) {
    return static_cast<int64_t>(state);
}

MemoryState IntToMemoryState(int64_t value) {
    switch (value) {
        case 1: return MemoryState::Current;
        case 2: return MemoryState::Deprecated;
        case 0:
        default:
            return MemoryState::New;
    }
}

// ============================================================================
// Construction + schema
// ============================================================================

MemoryStore::MemoryStore(IDataStore* dataStore) : m_store(dataStore) {
    EnsureSchema();
}

MemoryStore::~MemoryStore() = default;

namespace {
bool DidWriteRows(IDataStore* store) {
    if (!store) return false;
    return store->Changes() > 0;
}
} // namespace

void MemoryStore::EnsureSchema() {
    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS memory_layers (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            agent_id TEXT NOT NULL DEFAULT 'default',
            name TEXT NOT NULL,
            horizon TEXT NOT NULL DEFAULT '',
            sort_order INTEGER NOT NULL DEFAULT 0,
            evaluation_interval_seconds INTEGER NOT NULL DEFAULT 86400,
            cron_expr TEXT NOT NULL DEFAULT '0 * * * *',
            consolidation_prompt TEXT NOT NULL DEFAULT '',
            consolidation_intake_prompt TEXT NOT NULL DEFAULT '',
            intake_interval TEXT DEFAULT NULL,
            token_budget INTEGER NOT NULL DEFAULT 4096,
            enabled INTEGER NOT NULL DEFAULT 0,
            created_at_unix_ms INTEGER NOT NULL,
            updated_at_unix_ms INTEGER NOT NULL,
            UNIQUE(agent_id, name)
        );
    )");

    // Migration: add agent_id column if missing (existing rows become 'default')
    // and rebuild unique constraint to be (agent_id, name) instead of (name)
    // Only needed for SQLite — PostgreSQL tables are created fresh with correct schema
    if (m_store->Dialect() == DataStoreDialect::SQLite) {
        if (!schema::ColumnExists(m_store, "memory_layers", "agent_id")) {
            std::cerr << "[memory] migrating memory_layers: adding agent_id column..." << std::endl;
            // Disable foreign keys for the table swap
            schema::Pragma(m_store, "PRAGMA foreign_keys=OFF");
            m_store->Exec("CREATE TABLE IF NOT EXISTS memory_layers_v2 ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "agent_id TEXT NOT NULL DEFAULT 'default',"
                "name TEXT NOT NULL,"
                "horizon TEXT NOT NULL DEFAULT '',"
                "sort_order INTEGER NOT NULL DEFAULT 0,"
                "evaluation_interval_seconds INTEGER NOT NULL DEFAULT 86400,"
                "cron_expr TEXT NOT NULL DEFAULT '0 * * * *',"
                "consolidation_prompt TEXT NOT NULL DEFAULT '',"
                "consolidation_intake_prompt TEXT NOT NULL DEFAULT '',"
                "intake_interval TEXT DEFAULT NULL,"
                "token_budget INTEGER NOT NULL DEFAULT 4096,"
                "enabled INTEGER NOT NULL DEFAULT 0,"
                "created_at_unix_ms INTEGER NOT NULL,"
                "updated_at_unix_ms INTEGER NOT NULL,"
                "UNIQUE(agent_id, name))");
            m_store->Exec("INSERT INTO memory_layers_v2 "
                "(id, agent_id, name, horizon, sort_order, evaluation_interval_seconds, "
                "cron_expr, consolidation_prompt, consolidation_intake_prompt, intake_interval, "
                "token_budget, enabled, created_at_unix_ms, updated_at_unix_ms) "
                "SELECT id, 'default', name, horizon, sort_order, evaluation_interval_seconds, "
                "cron_expr, consolidation_prompt, consolidation_intake_prompt, intake_interval, "
                "token_budget, enabled, created_at_unix_ms, updated_at_unix_ms "
                "FROM memory_layers");
            m_store->Exec("DROP TABLE memory_layers");
            m_store->Exec("ALTER TABLE memory_layers_v2 RENAME TO memory_layers");
            schema::Pragma(m_store, "PRAGMA foreign_keys=ON");
            std::cerr << "[memory] migrated memory_layers: added agent_id, unique(agent_id, name)" << std::endl;
        }
    }

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS observations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            layer_id INTEGER NOT NULL REFERENCES memory_layers(id) ON DELETE CASCADE,
            agent_id TEXT NOT NULL DEFAULT 'default',
            text TEXT NOT NULL,
            weight REAL NOT NULL DEFAULT 1.0,
            decay_rate REAL NOT NULL DEFAULT 0.95,
            tags TEXT NOT NULL DEFAULT '[]',
            source TEXT NOT NULL DEFAULT '',
            created_at_unix_ms INTEGER NOT NULL,
            updated_at_unix_ms INTEGER NOT NULL,
            last_evaluated_at_ms INTEGER NOT NULL DEFAULT 0,
            next_review_at_ms INTEGER NOT NULL DEFAULT 0,
            memory_state INTEGER NOT NULL DEFAULT 0,
            superseded_by INTEGER NOT NULL DEFAULT 0
        );
    )");

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS layer_perspectives (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            layer_id INTEGER NOT NULL UNIQUE REFERENCES memory_layers(id) ON DELETE CASCADE,
            retrospective TEXT NOT NULL DEFAULT '',
            retrospective_valence TEXT NOT NULL DEFAULT '',
            current_perspective TEXT NOT NULL DEFAULT '',
            current_valence TEXT NOT NULL DEFAULT '',
            future_perspective TEXT NOT NULL DEFAULT '',
            future_valence TEXT NOT NULL DEFAULT '',
            updated_at_unix_ms INTEGER NOT NULL
        );
    )");

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS memory_mutations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            mutation_type TEXT NOT NULL,
            target_type TEXT NOT NULL,
            target_id INTEGER NOT NULL,
            from_layer_id INTEGER,
            to_layer_id INTEGER,
            previous_state TEXT NOT NULL DEFAULT '',
            motivation TEXT NOT NULL DEFAULT '',
            unix_ms INTEGER NOT NULL
        );
    )");

    // ── Migrations ──────────────────────────────────────────────────

    // Migration: rename horizon_seconds → horizon (text), add sort_order
    // Only applies to SQLite — PostgreSQL tables start with correct schema
    if (m_store->Dialect() == DataStoreDialect::SQLite) {
        if (schema::ColumnExists(m_store, "memory_layers", "horizon_seconds") &&
            !schema::ColumnExists(m_store, "memory_layers", "horizon")) {
            m_store->Exec("ALTER TABLE memory_layers ADD COLUMN horizon TEXT NOT NULL DEFAULT ''");
            m_store->Exec("ALTER TABLE memory_layers ADD COLUMN sort_order INTEGER NOT NULL DEFAULT 0");
            m_store->Exec("ALTER TABLE memory_layers ADD COLUMN evaluation_interval_seconds INTEGER NOT NULL DEFAULT 86400");
            m_store->Exec("ALTER TABLE memory_layers ADD COLUMN cron_expr TEXT NOT NULL DEFAULT '0 * * * *'");
            m_store->Exec(R"(
                UPDATE memory_layers SET
                    horizon = CASE
                        WHEN horizon_seconds <= 3600 THEN '1 hour'
                        WHEN horizon_seconds <= 86400 THEN '1 day'
                        WHEN horizon_seconds <= 604800 THEN '1 week'
                        WHEN horizon_seconds <= 2592000 THEN '1 month'
                        WHEN horizon_seconds <= 31536000 THEN '1 year'
                        WHEN horizon_seconds <= 315360000 THEN '1 decade'
                        WHEN horizon_seconds <= 3153600000 THEN '1 century'
                        ELSE '1 millennium'
                    END,
                    sort_order = CASE
                        WHEN name = 'day' THEN 0
                        WHEN name = 'week' THEN 1
                        WHEN name = 'month' THEN 2
                        WHEN name = 'year' THEN 3
                        WHEN name = 'decade' THEN 4
                        WHEN name = 'century' THEN 5
                        WHEN name = 'millennium' THEN 6
                        WHEN name = 'immediate' THEN 0
                        WHEN name = 'permanent' THEN 6
                        ELSE horizon_seconds
                    END,
                    evaluation_interval_seconds = consolidation_interval_seconds,
                    cron_expr = '0 * * * *'
            )");
        }
        if (!schema::ColumnExists(m_store, "memory_layers", "enabled")) {
            m_store->Exec("ALTER TABLE memory_layers ADD COLUMN enabled INTEGER NOT NULL DEFAULT 0");
        }
        if (!schema::ColumnExists(m_store, "memory_layers", "sort_order")) {
            m_store->Exec("ALTER TABLE memory_layers ADD COLUMN sort_order INTEGER NOT NULL DEFAULT 0");
        }
        if (!schema::ColumnExists(m_store, "memory_layers", "consolidation_intake_prompt")) {
            m_store->Exec(
                "ALTER TABLE memory_layers "
                "ADD COLUMN consolidation_intake_prompt TEXT NOT NULL DEFAULT ''");
        }
        if (!schema::ColumnExists(m_store, "memory_layers", "intake_interval")) {
            m_store->Exec(
                "ALTER TABLE memory_layers "
                "ADD COLUMN intake_interval TEXT DEFAULT NULL");
        }
    }

    // Migration: add last_evaluated_at_ms and next_review_at_ms to observations
    if (m_store->Dialect() == DataStoreDialect::SQLite) {
        if (!schema::ColumnExists(m_store, "observations", "last_evaluated_at_ms")) {
            m_store->Exec("ALTER TABLE observations ADD COLUMN last_evaluated_at_ms INTEGER NOT NULL DEFAULT 0");
        }
        if (!schema::ColumnExists(m_store, "observations", "next_review_at_ms")) {
            m_store->Exec("ALTER TABLE observations ADD COLUMN next_review_at_ms INTEGER NOT NULL DEFAULT 0");
        }
        if (!schema::ColumnExists(m_store, "observations", "memory_state")) {
            m_store->Exec("ALTER TABLE observations ADD COLUMN memory_state INTEGER NOT NULL DEFAULT 0");
        }
        if (!schema::ColumnExists(m_store, "observations", "superseded_by")) {
            m_store->Exec("ALTER TABLE observations ADD COLUMN superseded_by INTEGER NOT NULL DEFAULT 0");
        }

        // Backfill explicit review timestamps for existing rows.
        m_store->Exec(R"(
                UPDATE observations
                SET next_review_at_ms = CASE
                    WHEN last_evaluated_at_ms > 0 THEN
                        last_evaluated_at_ms + CAST(
                            COALESCE(
                                (SELECT evaluation_interval_seconds
                                 FROM memory_layers
                                 WHERE memory_layers.id = observations.layer_id),
                                0
                            ) * 1000 * decay_rate AS INTEGER
                        )
                    ELSE
                        created_at_unix_ms + CAST(
                            COALESCE(
                                (SELECT evaluation_interval_seconds
                                 FROM memory_layers
                                 WHERE memory_layers.id = observations.layer_id),
                                0
                            ) * 1000 * decay_rate AS INTEGER
                        )
                END
                WHERE next_review_at_ms = 0
            )");
    }

    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_observations_review_due "
        "ON observations(agent_id, layer_id, next_review_at_ms)");
}

// ============================================================================
// Layers
// ============================================================================

static MemoryLayer RowToLayer(IStatement* stmt) {
    MemoryLayer l;
    l.id = stmt->ColumnInt64(0);
    l.agent_id = stmt->ColumnText(1);
    l.name = stmt->ColumnText(2);
    l.horizon = stmt->ColumnText(3);
    l.sort_order = static_cast<int>(stmt->ColumnInt64(4));
    l.evaluation_interval_seconds = stmt->ColumnInt64(5);
    l.cron_expr = stmt->ColumnText(6);
    l.consolidation_review_prompt = stmt->ColumnText(7);
    l.consolidation_intake_prompt = stmt->ColumnText(8);
    if (!stmt->IsColumnNull(9)) {
        l.intake_interval = stmt->ColumnText(9);
    }
    l.token_budget = stmt->ColumnInt64(10);
    l.enabled = stmt->ColumnInt64(11) != 0;
    l.created_at_unix_ms = stmt->ColumnInt64(12);
    l.updated_at_unix_ms = stmt->ColumnInt64(13);
    return l;
}

static const char* kLayerSelectCols =
    "id, agent_id, name, horizon, sort_order, evaluation_interval_seconds, cron_expr, "
    "consolidation_prompt, consolidation_intake_prompt, intake_interval, "
    "token_budget, enabled, created_at_unix_ms, updated_at_unix_ms";

std::vector<MemoryLayer> MemoryStore::ListLayers() {
    std::vector<MemoryLayer> result;
    auto stmt = m_store->Prepare(
        std::string("SELECT ") + kLayerSelectCols +
        " FROM memory_layers ORDER BY agent_id ASC, sort_order ASC");
    if (!stmt) return result;

    while (stmt->Step()) {
        result.push_back(RowToLayer(stmt.get()));
    }
    return result;
}

std::vector<MemoryLayer> MemoryStore::ListLayersForAgent(const std::string& agent_id) {
    std::vector<MemoryLayer> result;
    auto stmt = m_store->Prepare(
        std::string("SELECT ") + kLayerSelectCols +
        " FROM memory_layers WHERE agent_id=? ORDER BY sort_order ASC");
    if (!stmt) return result;
    stmt->BindText(1, agent_id);

    while (stmt->Step()) {
        result.push_back(RowToLayer(stmt.get()));
    }
    return result;
}

std::optional<MemoryLayer> MemoryStore::GetLayer(int64_t id) {
    auto stmt = m_store->Prepare(
        std::string("SELECT ") + kLayerSelectCols +
        " FROM memory_layers WHERE id=?");
    if (!stmt) return std::nullopt;
    stmt->BindInt64(1, id);

    std::optional<MemoryLayer> result;
    if (stmt->Step()) {
        result = RowToLayer(stmt.get());
    }
    return result;
}

std::optional<MemoryLayer> MemoryStore::GetLowestLayer(const std::string& agent_id) {
    auto layers = ListLayersForAgent(agent_id);
    if (layers.empty()) return std::nullopt;
    return layers.front();
}

std::optional<MemoryLayer> MemoryStore::GetIntakeLayer(const std::string& agent_id) {
    auto stmt = m_store->Prepare(
        std::string("SELECT ") + kLayerSelectCols +
        " FROM memory_layers WHERE agent_id=? "
        "AND intake_interval IS NOT NULL "
        "AND TRIM(intake_interval) <> '' ORDER BY sort_order ASC LIMIT 1");
    if (!stmt) return std::nullopt;
    stmt->BindText(1, agent_id);
    if (!stmt->Step()) return std::nullopt;
    return RowToLayer(stmt.get());
}

MemoryLayer MemoryStore::CreateLayer(const MemoryLayer& layer) {
    auto now = NowUnixMs();
    std::string agentId = layer.agent_id;
    auto stmt = m_store->Prepare(
        "INSERT INTO memory_layers (agent_id, name, horizon, sort_order, evaluation_interval_seconds, "
        "cron_expr, consolidation_prompt, consolidation_intake_prompt, intake_interval, "
        "token_budget, enabled, created_at_unix_ms, updated_at_unix_ms) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)");
    if (!stmt) return {};

    stmt->BindText(1, agentId);
    stmt->BindText(2, layer.name);
    stmt->BindText(3, layer.horizon);
    stmt->BindInt(4, layer.sort_order);
    stmt->BindInt64(5, layer.evaluation_interval_seconds);
    stmt->BindText(6, layer.cron_expr);
    stmt->BindText(7, layer.consolidation_review_prompt);
    stmt->BindText(8, layer.consolidation_intake_prompt);
    if (layer.intake_interval.has_value() && !layer.intake_interval->empty()) {
        stmt->BindText(9, *layer.intake_interval);
    } else {
        stmt->BindNull(9);
    }
    stmt->BindInt64(10, layer.token_budget);
    stmt->BindInt(11, layer.enabled ? 1 : 0);
    stmt->BindInt64(12, now);
    stmt->BindInt64(13, now);

    stmt->Step();
    if (!DidWriteRows(m_store)) {
        std::cerr << "[memory] insert layer failed: " << m_store->ErrMsg() << std::endl;
        return {};
    }

    auto result = GetLayer(m_store->LastInsertRowId());

    // Auto-create perspective row
    if (result) {
        std::string sql = schema::InsertIgnoreSql(m_store,
            "layer_perspectives", "layer_id, updated_at_unix_ms", "layer_id");
        auto ps = m_store->Prepare(sql);
        if (ps) {
            ps->BindInt64(1, result->id);
            ps->BindInt64(2, now);
            ps->Step();
        }
    }

    return result.value_or(MemoryLayer{});
}

bool MemoryStore::CreateDefaultLayersForAgent(const std::string& agent_id) {
    // Check if layers already exist for this agent
    auto existing = ListLayersForAgent(agent_id);
    if (!existing.empty()) return false;

    // 7-layer memory hierarchy: day → week → month → year → decade → century → millennium
    // Each layer has progressively longer evaluation intervals and consolidation windows.
    // Intake flows into the day layer only. Promote/merge moves observations up.
    struct LayerDef {
        const char* name;
        const char* horizon;
        int sort_order;
        int64_t eval_interval_seconds;  // min age before entering eval batch
        const char* cron_expr;           // consolidation review schedule
        const char* intake_cron;         // intake schedule (null = no intake)
        int64_t token_budget;
        const char* review_prompt;
        const char* intake_prompt;
    };

    LayerDef defaults[] = {
        {
            "day", "1 day", 0,
            3600,                    // 1 hour minimum age
            "0 */2 * * *",          // review every 2 hours
            "0 * * * *",             // intake every hour
            4096,
            "Review your day-layer observations. Promote significant ones to the week layer, "
            "merge related ones, and retire noise or duplicates. Generate or update your "
            "retrospective, current, and future perspectives for this layer.",
            "Extract lasting observations from recent session and diary activity. "
            "Focus on facts, decisions, and patterns worth remembering."
        },
        {
            "week", "1 week", 1,
            86400,                   // 1 day minimum age
            "0 3 * * *",             // review daily at 3am
            nullptr,                  // no direct intake
            4096,
            "Review your week-layer observations. Promote significant ones to the month layer, "
            "merge related observations, retire duplicates or noise. Update perspectives.",
            ""
        },
        {
            "month", "1 month", 2,
            604800,                  // 1 week minimum age
            "0 4 * * 1",             // review weekly on Mondays at 4am
            nullptr,
            4096,
            "Review your month-layer observations. Promote significant ones to the year layer, "
            "merge related ones, retire noise. Update perspectives.",
            ""
        },
        {
            "year", "1 year", 3,
            2592000,                 // 30 days minimum age
            "0 5 1 * *",             // review monthly on the 1st at 5am
            nullptr,
            8192,
            "Review your year-layer observations. Promote significant ones to the decade layer, "
            "merge related ones, retire noise. Update perspectives with a long-term view.",
            ""
        },
        {
            "decade", "1 decade", 4,
            7776000,                 // 90 days minimum age
            "0 6 1 1 *",             // review yearly on Jan 1 at 6am
            nullptr,
            8192,
            "Review your decade-layer observations. Promote truly significant ones to the century "
            "layer, merge related ones, retire noise. Focus on large-scale patterns and shifts.",
            ""
        },
        {
            "century", "1 century", 5,
            31536000,                // 1 year minimum age
            "0 7 1 1 *",             // review yearly on Jan 1 at 7am
            nullptr,
            16384,
            "Review your century-layer observations. These are your most durable memories. "
            "Promote to millennium only if truly epochal. Merge where appropriate. "
            "Update perspectives with a civilizational-scale view.",
            ""
        },
        {
            "millennium", "1 millennium", 6,
            315360000,               // 10 years minimum age
            "0 8 1 1 *",             // review yearly on Jan 1 at 8am
            nullptr,
            16384,
            "Review your millennium-layer observations. These define your deepest identity. "
            "Merge rarely, retire never. Update perspectives with the longest possible view.",
            ""
        },
    };

    for (const auto& def : defaults) {
        MemoryLayer layer;
        layer.agent_id = agent_id;
        layer.name = def.name;
        layer.horizon = def.horizon;
        layer.sort_order = def.sort_order;
        layer.evaluation_interval_seconds = def.eval_interval_seconds;
        layer.cron_expr = def.cron_expr;
        layer.consolidation_review_prompt = def.review_prompt;
        layer.consolidation_intake_prompt = def.intake_prompt;
        if (def.intake_cron) {
            layer.intake_interval = std::string(def.intake_cron);
        }
        layer.token_budget = def.token_budget;
        layer.enabled = true;

        auto created = CreateLayer(layer);
        if (created.id == 0) {
            std::cerr << "[memory] failed to seed layer: " << def.name << std::endl;
        }
    }

    std::cout << "[memory] seeded 7 default memory layers for agent " << agent_id << std::endl;
    return true;
}

bool MemoryStore::UpdateLayer(const MemoryLayer& layer) {
    auto now = NowUnixMs();
    auto stmt = m_store->Prepare(
        "UPDATE memory_layers SET name=?, horizon=?, sort_order=?, "
        "evaluation_interval_seconds=?, cron_expr=?, consolidation_prompt=?, "
        "consolidation_intake_prompt=?, intake_interval=?, "
        "token_budget=?, enabled=?, updated_at_unix_ms=? WHERE id=?");
    if (!stmt) return false;

    stmt->BindText(1, layer.name);
    stmt->BindText(2, layer.horizon);
    stmt->BindInt(3, layer.sort_order);
    stmt->BindInt64(4, layer.evaluation_interval_seconds);
    stmt->BindText(5, layer.cron_expr);
    stmt->BindText(6, layer.consolidation_review_prompt);
    stmt->BindText(7, layer.consolidation_intake_prompt);
    if (layer.intake_interval.has_value() && !layer.intake_interval->empty()) {
        stmt->BindText(8, *layer.intake_interval);
    } else {
        stmt->BindNull(8);
    }
    stmt->BindInt64(9, layer.token_budget);
    stmt->BindInt(10, layer.enabled ? 1 : 0);
    stmt->BindInt64(11, now);
    stmt->BindInt64(12, layer.id);

    stmt->Step();
    return DidWriteRows(m_store);
}

bool MemoryStore::DeleteLayer(int64_t id) {
    auto stmt = m_store->Prepare("DELETE FROM memory_layers WHERE id=?");
    if (!stmt) return false;
    stmt->BindInt64(1, id);
    stmt->Step();
    return DidWriteRows(m_store);
}

int MemoryStore::CopyLayersForAgent(
        const std::string& source_agent_id,
        const std::string& dest_agent_id) {
    auto sourceLayers = ListLayersForAgent(source_agent_id);
    int copied = 0;
    for (const auto& layer : sourceLayers) {
        MemoryLayer copy = layer;
        copy.id = 0;  // let auto-increment assign new id
        copy.agent_id = dest_agent_id;
        copy.created_at_unix_ms = 0;
        copy.updated_at_unix_ms = 0;
        auto created = CreateLayer(copy);
        if (created.id > 0) {
            copied++;
        }
    }
    return copied;
}

// ============================================================================
// Observations
// ============================================================================

static Observation RowToObservation(IStatement* stmt) {
    Observation o;
    o.id = stmt->ColumnInt64(0);
    o.layer_id = stmt->ColumnInt64(1);
    o.agent_id = stmt->ColumnText(2);
    o.text = stmt->ColumnText(3);
    o.weight = stmt->ColumnDouble(4);
    o.decay_rate = stmt->ColumnDouble(5);
    o.tags_json = stmt->ColumnText(6);
    o.source = stmt->ColumnText(7);
    o.created_at_unix_ms = stmt->ColumnInt64(8);
    o.updated_at_unix_ms = stmt->ColumnInt64(9);
    o.last_evaluated_at_ms = stmt->ColumnInt64(10);
    o.next_review_at_ms = stmt->ColumnInt64(11);
    o.memory_state = IntToMemoryState(stmt->ColumnInt64(12));
    o.superseded_by = stmt->ColumnInt64(13);
    return o;
}

static const char* kObsSelectCols =
    "id, layer_id, agent_id, text, weight, decay_rate, tags, source, "
    "created_at_unix_ms, updated_at_unix_ms, last_evaluated_at_ms, next_review_at_ms, "
    "memory_state, superseded_by";

std::vector<Observation> MemoryStore::ListObservations(int64_t layer_id) {
    std::vector<Observation> result;
    auto stmt = m_store->Prepare(
        std::string("SELECT ") + kObsSelectCols +
        " FROM observations WHERE layer_id=? "
        "ORDER BY CASE memory_state WHEN 1 THEN 0 WHEN 0 THEN 1 WHEN 2 THEN 2 ELSE 3 END ASC, "
        "weight DESC, created_at_unix_ms DESC");
    if (!stmt) return result;
    stmt->BindInt64(1, layer_id);

    while (stmt->Step()) {
        result.push_back(RowToObservation(stmt.get()));
    }
    return result;
}

std::optional<Observation> MemoryStore::GetObservation(int64_t id) {
    auto stmt = m_store->Prepare(
        std::string("SELECT ") + kObsSelectCols + " FROM observations WHERE id=?");
    if (!stmt) return std::nullopt;
    stmt->BindInt64(1, id);

    std::optional<Observation> result;
    if (stmt->Step()) {
        result = RowToObservation(stmt.get());
    }
    return result;
}

Observation MemoryStore::CreateObservation(const Observation& obs) {
    auto now = NowUnixMs();
    int64_t nextReviewAt = obs.next_review_at_ms;
    if (nextReviewAt <= 0) {
        int64_t evalIntervalSeconds = 0;
        if (auto layer = GetLayer(obs.layer_id); layer) {
            evalIntervalSeconds = layer->evaluation_interval_seconds;
        }
        const auto effectiveDelayMs = static_cast<int64_t>(
            static_cast<double>(evalIntervalSeconds) * 1000.0 * obs.decay_rate);
        nextReviewAt = now + std::max<int64_t>(0, effectiveDelayMs);
    }
    auto stmt = m_store->Prepare(
        "INSERT INTO observations (layer_id, agent_id, text, weight, decay_rate, tags, source, "
        "created_at_unix_ms, updated_at_unix_ms, last_evaluated_at_ms, next_review_at_ms, "
        "memory_state, superseded_by) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)");
    if (!stmt) return {};

    stmt->BindInt64(1, obs.layer_id);
    stmt->BindText(2, obs.agent_id);
    stmt->BindText(3, obs.text);
    stmt->BindDouble(4, obs.weight);
    stmt->BindDouble(5, obs.decay_rate);
    stmt->BindText(6, obs.tags_json);
    stmt->BindText(7, obs.source);
    stmt->BindInt64(8, now);
    stmt->BindInt64(9, now);
    stmt->BindInt64(10, obs.last_evaluated_at_ms);
    stmt->BindInt64(11, nextReviewAt);
    stmt->BindInt64(12, MemoryStateToInt(obs.memory_state));
    stmt->BindInt64(13, obs.superseded_by);

    stmt->Step();
    if (!DidWriteRows(m_store)) {
        std::cerr << "[memory] insert observation failed: " << m_store->ErrMsg() << std::endl;
        return {};
    }

    int64_t newId = m_store->LastInsertRowId();

    // Log mutation
    MemoryMutation m;
    m.mutation_type = "observation_created";
    m.target_type = "observation";
    m.target_id = newId;
    m.to_layer_id = obs.layer_id;
    m.motivation = "new observation from " + obs.source;
    m.unix_ms = now;
    LogMutation(m);

    return GetObservation(newId).value_or(Observation{});
}

bool MemoryStore::UpdateObservation(const Observation& obs) {
    auto now = NowUnixMs();
    auto stmt = m_store->Prepare(
        "UPDATE observations SET layer_id=?, agent_id=?, text=?, weight=?, decay_rate=?, tags=?, "
        "source=?, updated_at_unix_ms=?, last_evaluated_at_ms=?, next_review_at_ms=?, "
        "memory_state=? WHERE id=?");
    if (!stmt) return false;

    stmt->BindInt64(1, obs.layer_id);
    stmt->BindText(2, obs.agent_id);
    stmt->BindText(3, obs.text);
    stmt->BindDouble(4, obs.weight);
    stmt->BindDouble(5, obs.decay_rate);
    stmt->BindText(6, obs.tags_json);
    stmt->BindText(7, obs.source);
    stmt->BindInt64(8, now);
    stmt->BindInt64(9, obs.last_evaluated_at_ms);
    stmt->BindInt64(10, obs.next_review_at_ms);
    stmt->BindInt64(11, MemoryStateToInt(obs.memory_state));
    stmt->BindInt64(12, obs.id);

    stmt->Step();
    return DidWriteRows(m_store);
}

bool MemoryStore::SetObservationMemoryState(
        int64_t obs_id, MemoryState memory_state, const std::string& motivation) {
    auto existing = GetObservation(obs_id);
    if (!existing) return false;

    if (existing->memory_state == memory_state) {
        return true;
    }

    auto now = NowUnixMs();
    auto stmt = m_store->Prepare(
        "UPDATE observations SET memory_state=?, updated_at_unix_ms=? WHERE id=?");
    if (!stmt) return false;
    stmt->BindInt64(1, MemoryStateToInt(memory_state));
    stmt->BindInt64(2, now);
    stmt->BindInt64(3, obs_id);
    stmt->Step();
    if (!DidWriteRows(m_store)) return false;

    MemoryMutation m;
    m.mutation_type = "observation_state_changed";
    m.target_type = "observation";
    m.target_id = obs_id;
    m.previous_state = std::to_string(static_cast<int>(existing->memory_state));
    m.motivation = motivation;
    m.unix_ms = now;
    LogMutation(m);
    return true;
}

bool MemoryStore::DeleteObservation(int64_t id) {
    auto stmt = m_store->Prepare("DELETE FROM observations WHERE id=?");
    if (!stmt) return false;
    stmt->BindInt64(1, id);
    stmt->Step();
    return DidWriteRows(m_store);
}

bool MemoryStore::MoveObservation(int64_t obs_id, int64_t to_layer_id,
                                   const std::string& motivation) {
    auto obs = GetObservation(obs_id);
    if (!obs) return false;

    int64_t from_layer = obs->layer_id;
    auto now = NowUnixMs();
    int64_t evalIntervalSeconds = 0;
    if (auto toLayer = GetLayer(to_layer_id); toLayer) {
        evalIntervalSeconds = toLayer->evaluation_interval_seconds;
    }
    const auto effectiveDelayMs = static_cast<int64_t>(
        static_cast<double>(evalIntervalSeconds) * 1000.0 * obs->decay_rate);
    const int64_t nextReviewAt = now + std::max<int64_t>(0, effectiveDelayMs);

    // Move observation and give it a fresh review window in the destination layer.
    auto stmt = m_store->Prepare(
        "UPDATE observations SET layer_id=?, updated_at_unix_ms=?, last_evaluated_at_ms=?, "
        "next_review_at_ms=? "
        "WHERE id=?");
    if (!stmt) return false;
    stmt->BindInt64(1, to_layer_id);
    stmt->BindInt64(2, now);
    stmt->BindInt64(3, now);
    stmt->BindInt64(4, nextReviewAt);
    stmt->BindInt64(5, obs_id);

    stmt->Step();
    if (!DidWriteRows(m_store)) return false;

    MemoryMutation m;
    m.mutation_type = to_layer_id > from_layer ? "observation_promoted" : "observation_demoted";
    m.target_type = "observation";
    m.target_id = obs_id;
    m.from_layer_id = from_layer;
    m.to_layer_id = to_layer_id;
    m.previous_state = obs->text.substr(0, 200);
    m.motivation = motivation;
    m.unix_ms = now;
    LogMutation(m);

    return true;
}

bool MemoryStore::TouchEvaluation(int64_t obs_id) {
    auto obs = GetObservation(obs_id);
    if (!obs) return false;

    auto now = NowUnixMs();
    int64_t evalIntervalSeconds = 0;
    if (auto layer = GetLayer(obs->layer_id); layer) {
        evalIntervalSeconds = layer->evaluation_interval_seconds;
    }
    const auto effectiveDelayMs = static_cast<int64_t>(
        static_cast<double>(evalIntervalSeconds) * 1000.0 * obs->decay_rate);
    const int64_t nextReviewAt = now + std::max<int64_t>(0, effectiveDelayMs);

    auto stmt = m_store->Prepare(
        "UPDATE observations SET last_evaluated_at_ms=?, next_review_at_ms=? WHERE id=?");
    if (!stmt) return false;
    stmt->BindInt64(1, now);
    stmt->BindInt64(2, nextReviewAt);
    stmt->BindInt64(3, obs_id);
    stmt->Step();
    return DidWriteRows(m_store);
}

Observation MemoryStore::ReviseObservation(int64_t obs_id, const std::string& new_text,
                                           double new_weight, const std::string& new_tags_json) {
    auto original = GetObservation(obs_id);
    if (!original) {
        std::cerr << "[memory] ReviseObservation: observation " << obs_id << " not found" << std::endl;
        return {};
    }

    auto now = NowUnixMs();

    // Create the new version as a current observation
    Observation newVersion;
    newVersion.layer_id = original->layer_id;
    newVersion.agent_id = original->agent_id;
    newVersion.text = new_text;
    newVersion.weight = new_weight;
    newVersion.decay_rate = original->decay_rate;
    newVersion.tags_json = new_tags_json.empty() ? original->tags_json : new_tags_json;
    newVersion.source = original->source;
    newVersion.memory_state = MemoryState::New;
    newVersion.superseded_by = 0;  // current version

    auto stmt = m_store->Prepare(
        "INSERT INTO observations (layer_id, agent_id, text, weight, decay_rate, tags, source, "
        "created_at_unix_ms, updated_at_unix_ms, last_evaluated_at_ms, next_review_at_ms, "
        "memory_state, superseded_by) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)");
    if (!stmt) return {};

    stmt->BindInt64(1, newVersion.layer_id);
    stmt->BindText(2, newVersion.agent_id);
    stmt->BindText(3, newVersion.text);
    stmt->BindDouble(4, newVersion.weight);
    stmt->BindDouble(5, newVersion.decay_rate);
    stmt->BindText(6, newVersion.tags_json);
    stmt->BindText(7, newVersion.source);
    stmt->BindInt64(8, original->created_at_unix_ms);  // preserve original creation time
    stmt->BindInt64(9, now);                           // updated now
    stmt->BindInt64(10, original->last_evaluated_at_ms);
    stmt->BindInt64(11, original->next_review_at_ms);
    stmt->BindInt64(12, MemoryStateToInt(newVersion.memory_state));
    stmt->BindInt64(13, 0);  // current version

    stmt->Step();
    if (!DidWriteRows(m_store)) {
        std::cerr << "[memory] ReviseObservation: insert new version failed: " << m_store->ErrMsg() << std::endl;
        return {};
    }

    int64_t newId = m_store->LastInsertRowId();

    // Mark the original as superseded
    auto supersede = m_store->Prepare(
        "UPDATE observations SET superseded_by=?, updated_at_unix_ms=? WHERE id=?");
    if (!supersede) return {};
    supersede->BindInt64(1, newId);
    supersede->BindInt64(2, now);
    supersede->BindInt64(3, obs_id);
    supersede->Step();

    // Log the mutation
    MemoryMutation m;
    m.mutation_type = "revise";
    m.target_type = "observation";
    m.target_id = newId;
    m.from_layer_id = original->layer_id;
    m.to_layer_id = original->layer_id;
    m.motivation = "Copy-on-write revision: old version " + std::to_string(obs_id) + " preserved as snapshot";
    LogMutation(m);

    return GetObservation(newId).value_or(Observation{});
}

std::vector<Observation> MemoryStore::GetObservationHistory(int64_t obs_id) {
    std::vector<Observation> result;

    // Start from the current version (or the given obs if it IS current)
    auto current = GetObservation(obs_id);
    if (!current) return result;

    // If this observation was superseded, follow the chain to the current version
    int64_t currentId = obs_id;
    while (current->superseded_by != 0) {
        currentId = current->superseded_by;
        current = GetObservation(currentId);
        if (!current) break;
    }

    if (!current) return result;

    // Walk backwards: find all observations where superseded_by == currentId's chain
    // The current version goes first (most recent), then walk back through predecessors
    result.push_back(*current);

    int64_t searchId = currentId;
    while (true) {
        auto stmt = m_store->Prepare(
            std::string("SELECT ") + kObsSelectCols +
            " FROM observations WHERE superseded_by=? ORDER BY id DESC LIMIT 1");
        if (!stmt) break;
        stmt->BindInt64(1, searchId);
        if (stmt->Step()) {
            result.push_back(RowToObservation(stmt.get()));
            searchId = result.back().id;
            stmt->Finalize();
        } else {
            stmt->Finalize();
            break;
        }
    }

    // Reverse to get oldest-first ordering
    std::reverse(result.begin(), result.end());
    return result;
}

std::vector<Observation> MemoryStore::ListObservationsDueForReview(
        const std::string& agent_id, int64_t layer_id, int64_t now_ms) {
    std::vector<Observation> result;
    auto stmt = m_store->Prepare(
        "SELECT " + std::string(kObsSelectCols) +
        " FROM observations WHERE layer_id=? "
        "AND memory_state IN (0, 1) "
        "AND next_review_at_ms <= ? "
        "ORDER BY CASE memory_state WHEN 1 THEN 0 WHEN 0 THEN 1 WHEN 2 THEN 2 ELSE 3 END ASC, "
        "next_review_at_ms ASC, created_at_unix_ms DESC");
    if (!stmt) return result;
    stmt->BindInt64(1, layer_id);
    stmt->BindInt64(2, now_ms);

    while (stmt->Step()) {
        result.push_back(RowToObservation(stmt.get()));
    }
    return result;
}

// ============================================================================
// Perspectives
// ============================================================================

std::optional<LayerPerspective> MemoryStore::GetPerspective(int64_t layer_id) {
    auto stmt = m_store->Prepare(
        "SELECT id, layer_id, retrospective, retrospective_valence, current_perspective, "
        "current_valence, future_perspective, future_valence, updated_at_unix_ms "
        "FROM layer_perspectives WHERE layer_id=?");
    if (!stmt) return std::nullopt;
    stmt->BindInt64(1, layer_id);

    std::optional<LayerPerspective> result;
    if (stmt->Step()) {
        LayerPerspective p;
        p.id = stmt->ColumnInt64(0);
        p.layer_id = stmt->ColumnInt64(1);
        p.retrospective = stmt->ColumnText(2);
        p.retrospective_valence = stmt->ColumnText(3);
        p.current_perspective = stmt->ColumnText(4);
        p.current_valence = stmt->ColumnText(5);
        p.future_perspective = stmt->ColumnText(6);
        p.future_valence = stmt->ColumnText(7);
        p.updated_at_unix_ms = stmt->ColumnInt64(8);
        result = std::move(p);
    }
    return result;
}

LayerPerspective MemoryStore::SetPerspective(const LayerPerspective& p) {
    auto now = NowUnixMs();

    auto stmt = m_store->Prepare(
        "INSERT INTO layer_perspectives "
        "(layer_id, retrospective, retrospective_valence, current_perspective, current_valence, "
        "future_perspective, future_valence, updated_at_unix_ms) "
        "VALUES (?,?,?,?,?,?,?,?) "
        "ON CONFLICT(layer_id) DO UPDATE SET retrospective=?, retrospective_valence=?, "
        "current_perspective=?, current_valence=?, future_perspective=?, future_valence=?, "
        "updated_at_unix_ms=?");
    if (!stmt) return {};

    stmt->BindInt64(1, p.layer_id);
    stmt->BindText(2, p.retrospective);
    stmt->BindText(3, p.retrospective_valence);
    stmt->BindText(4, p.current_perspective);
    stmt->BindText(5, p.current_valence);
    stmt->BindText(6, p.future_perspective);
    stmt->BindText(7, p.future_valence);
    stmt->BindInt64(8, now);
    // UPDATE args (9-15)
    stmt->BindText(9, p.retrospective);
    stmt->BindText(10, p.retrospective_valence);
    stmt->BindText(11, p.current_perspective);
    stmt->BindText(12, p.current_valence);
    stmt->BindText(13, p.future_perspective);
    stmt->BindText(14, p.future_valence);
    stmt->BindInt64(15, now);

    stmt->Step();
    if (!DidWriteRows(m_store)) {
        std::cerr << "[memory] upsert perspective failed: " << m_store->ErrMsg() << std::endl;
        return {};
    }

    // Log mutation
    MemoryMutation m;
    m.mutation_type = "perspective_revised";
    m.target_type = "perspective";
    m.target_id = p.layer_id;
    m.motivation = "perspective update";
    m.unix_ms = now;
    LogMutation(m);

    return GetPerspective(p.layer_id).value_or(LayerPerspective{});
}

// ============================================================================
// Mutations
// ============================================================================

void MemoryStore::LogMutation(const MemoryMutation& m) {
    auto stmt = m_store->Prepare(
        "INSERT INTO memory_mutations (mutation_type, target_type, target_id, "
        "from_layer_id, to_layer_id, previous_state, motivation, unix_ms) "
        "VALUES (?,?,?,?,?,?,?,?)");
    if (!stmt) return;

    stmt->BindText(1, m.mutation_type);
    stmt->BindText(2, m.target_type);
    stmt->BindInt64(3, m.target_id);
    if (m.from_layer_id.has_value()) stmt->BindInt64(4, *m.from_layer_id);
    else stmt->BindNull(4);
    if (m.to_layer_id.has_value()) stmt->BindInt64(5, *m.to_layer_id);
    else stmt->BindNull(5);
    stmt->BindText(6, m.previous_state);
    stmt->BindText(7, m.motivation);
    stmt->BindInt64(8, m.unix_ms);

    stmt->Step();
    if (!DidWriteRows(m_store)) {
        std::cerr << "[memory] insert mutation failed: " << m_store->ErrMsg() << std::endl;
    }
}

std::vector<MemoryMutation> MemoryStore::QueryMutations(int64_t since_unix_ms, int64_t limit) {
    std::vector<MemoryMutation> result;
    auto stmt = m_store->Prepare(
        "SELECT id, mutation_type, target_type, target_id, from_layer_id, to_layer_id, "
        "previous_state, motivation, unix_ms "
        "FROM memory_mutations WHERE unix_ms > ? ORDER BY unix_ms DESC LIMIT ?");
    if (!stmt) return result;

    stmt->BindInt64(1, since_unix_ms);
    stmt->BindInt64(2, limit);

    while (stmt->Step()) {
        MemoryMutation m;
        m.id = stmt->ColumnInt64(0);
        m.mutation_type = stmt->ColumnText(1);
        m.target_type = stmt->ColumnText(2);
        m.target_id = stmt->ColumnInt64(3);
        if (!stmt->IsColumnNull(4)) m.from_layer_id = stmt->ColumnInt64(4);
        if (!stmt->IsColumnNull(5)) m.to_layer_id = stmt->ColumnInt64(5);
        m.previous_state = stmt->ColumnText(6);
        m.motivation = stmt->ColumnText(7);
        m.unix_ms = stmt->ColumnInt64(8);
        result.push_back(std::move(m));
    }
    return result;
}

// ============================================================================
// Agent-scoped observation methods
// ============================================================================

std::vector<Observation> MemoryStore::ListObservationsForAgent(
        const std::string& agent_id, int64_t layer_id) {
    std::vector<Observation> result;
    auto stmt = m_store->Prepare(
        std::string("SELECT ") + kObsSelectCols +
        " FROM observations WHERE agent_id=? AND layer_id=? "
        "ORDER BY CASE memory_state WHEN 1 THEN 0 WHEN 0 THEN 1 WHEN 2 THEN 2 ELSE 3 END ASC, "
        "weight DESC, created_at_unix_ms DESC");
    if (!stmt) return result;
    stmt->BindText(1, agent_id);
    stmt->BindInt64(2, layer_id);

    while (stmt->Step()) {
        result.push_back(RowToObservation(stmt.get()));
    }
    return result;
}

std::vector<Observation> MemoryStore::ListObservationsForLayer(int64_t layer_id) {
    std::vector<Observation> result;
    auto stmt = m_store->Prepare(
        std::string("SELECT ") + kObsSelectCols +
        " FROM observations WHERE layer_id=? "
        "ORDER BY CASE memory_state WHEN 1 THEN 0 WHEN 0 THEN 1 WHEN 2 THEN 2 ELSE 3 END ASC, "
        "weight DESC, created_at_unix_ms DESC");
    if (!stmt) return result;
    stmt->BindInt64(1, layer_id);

    while (stmt->Step()) {
        result.push_back(RowToObservation(stmt.get()));
    }
    return result;
}

Observation MemoryStore::CreateObservationForAgent(
        const std::string& agent_id, const Observation& obs) {
    Observation agentObs = obs;
    agentObs.agent_id = agent_id;
    return CreateObservation(agentObs);
}

std::vector<Observation> MemoryStore::SearchObservationsForAgent(
        const std::string& agent_id, int64_t since_unix_ms, int64_t limit) {
    std::vector<Observation> result;
    auto stmt = m_store->Prepare(
        std::string("SELECT ") + kObsSelectCols +
        " FROM observations WHERE agent_id=? AND created_at_unix_ms > ? "
        "ORDER BY CASE memory_state WHEN 1 THEN 0 WHEN 0 THEN 1 WHEN 2 THEN 2 ELSE 3 END ASC, "
        "created_at_unix_ms DESC LIMIT ?");
    if (!stmt) return result;
    stmt->BindText(1, agent_id);
    stmt->BindInt64(2, since_unix_ms);
    stmt->BindInt64(3, limit);

    while (stmt->Step()) {
        result.push_back(RowToObservation(stmt.get()));
    }
    return result;
}

} // namespace animus::kernel::memory

#include "animus_kernel/IdRanges.h"

#include <json/json.h>
#include <sstream>
#include "animus_kernel/Log.h"

#include <algorithm>

namespace animus::kernel {

const std::vector<std::string>& AgentGlobalTables() {
    static const std::vector<std::string> tables = {
        "memory_layers",
        "observations",
        "layer_perspectives",
        "memory_mutations",
        "memory_files",
        "memory_file_chunks",
        "ontology_entities",
        "ontology_properties",
        "ontology_mutations",
        "diary_entries",
        // #78 P2a: scheduler tables join the replicated set. schedules.id
        // is TEXT (random hex — globally unique by construction, no id-range
        // needed); task_runs.id is INTEGER (node-range seeded alongside the
        // agent-global set in SeedAgentGlobalIdRanges). The P2a sync-layer
        // migration widens row-id columns to TEXT to carry both key styles.
        "schedules",
        "task_runs",
        // #78 P2b: schedule leases replicate too (epoch-fenced failover —
        // the merged effective lease is derived at read time: max epoch,
        // then max expires_ms, then max id). Integer ids, node-range seeded.
        "schedule_leases",
        // #93 P3: config replication. agent_config (channel + agent config
        // kv — COMPOSITE key (agent_id, key), the only non-single-row-id
        // member; sync row identity is the surrogate pair string
        // "agent_idkey") and the #23 vault (api_package_secrets —
        // ciphertext replicates; the master key does NOT — verified-node
        // key distribution is manual for now, decided on #93).
        "agent_config",
        "api_package_secrets",
        // #274: federation epoch DEK envelopes — sync as data (a node can
        // only unwrap the row addressed to its own key). TEXT PK
        // "<epoch>:<target_node>" rides the plain NEW.id trigger path.
        "federation_key_envelopes",
    };
    return tables;
}

// ── #93 P3: per-table row identity ─────────────────────────────────────
// Single-row-id tables use "id" (P1/P2 behavior, unchanged). agent_config
// is the composite exception: row identity is the escaped pair string
// agent_id  key (unit separator can't appear in either half).
const std::vector<std::string>& CompositeKeyTables() {
    static const std::vector<std::string> tables = {"agent_config"};
    return tables;
}

bool IsCompositeKeyTable(const std::string& t) {
    for (const auto& x : CompositeKeyTables()) if (x == t) return true;
    return false;
}

std::string CompositeKeyToJson(const std::string& compositeKey) {
    const auto pos = compositeKey.find('\x1F');
    if (pos == std::string::npos) return compositeKey;
    Json::Value j(Json::objectValue);
    j["agent_id"] = compositeKey.substr(0, pos);
    j["key"] = compositeKey.substr(pos + 1);
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, j);
}

std::string JsonToCompositeKey(const std::string& payloadKeyJson) {
    if (payloadKeyJson.empty() || payloadKeyJson.front() != '{') return payloadKeyJson;
    Json::CharReaderBuilder rb;
    std::string errs;
    Json::Value v;
    std::istringstream ss(payloadKeyJson);
    if (!Json::parseFromStream(rb, ss, &v, &errs)) return payloadKeyJson;
    if (!v.isObject() || !v.isMember("agent_id") || !v.isMember("key"))
        return payloadKeyJson;
    return v["agent_id"].asString() + std::string(1, '\x1F') + v["key"].asString();
}

namespace {

constexpr uint64_t kMaxNodeId = (uint64_t{1} << 20) - 1;   // 1,048,575 nodes
constexpr int kShift = 40;                                  // 2^40 ids per node

// SQLite: sqlite_sequence rows key the table name. The table itself is
// created lazily by SQLite after the first AUTOINCREMENT insert — the name
// is reserved and cannot be created manually ("object name reserved for
// internal use"). On a fresh database we trigger its creation with a
// throwaway insert into memory_layers (always present: MemoryStore is the
// first agent-global store constructed) and delete the row immediately;
// the sentinel's small id is immediately out-ranked by the seed.
bool EnsureSqliteSequenceTable(IDataStore* store) {
    auto q = store->Prepare(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='sqlite_sequence'");
    if (q && q->Step()) return true;   // already exists

    auto ins = store->Prepare(
        "INSERT INTO memory_layers (name, created_at_unix_ms, updated_at_unix_ms) "
        "VALUES ('__idrange_seed__', 0, 0)");
    if (!ins) return false;
    ins->ExecDML();
    auto del = store->Prepare("DELETE FROM memory_layers WHERE name='__idrange_seed__'");
    if (del) del->ExecDML();

    q = store->Prepare(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='sqlite_sequence'");
    return q && q->Step();
}

bool SeedSqlite(IDataStore* store, const std::string& table, uint64_t seed) {
    if (!EnsureSqliteSequenceTable(store)) return false;
    auto q = store->Prepare("SELECT seq FROM sqlite_sequence WHERE name=?");
    if (!q) return false;
    q->BindText(1, table);
    const bool hasRow = q->Step();
    const int64_t current = hasRow ? q->ColumnInt64(0) : 0;

    if (hasRow && current >= static_cast<int64_t>(seed)) return true;  // raise-only

    if (hasRow) {
        auto u = store->Prepare(
            "UPDATE sqlite_sequence SET seq=? WHERE name=?");
        if (!u) return false;
        u->BindInt64(1, static_cast<int64_t>(seed));
        u->BindText(2, table);
        u->ExecDML();
    } else {
        auto i = store->Prepare(
            "INSERT INTO sqlite_sequence (name, seq) VALUES (?,?)");
        if (!i) return false;
        i->BindText(1, table);
        i->BindInt64(2, static_cast<int64_t>(seed));
        i->ExecDML();
    }
    return true;
}

// PostgreSQL: SERIAL columns own a per-table sequence; resolve it canonically
// (handles schema qualification and renamed tables), compare the NEXT value
// it would hand out, and only ever raise it.
//
// Legacy int4 SERIAL id columns (schema pre-BIGSERIAL) are widened to BIGINT
// first — a node_id<<40 seed cannot even be expressed in an int4 sequence,
// and federated ids would overflow the column. Widening only runs when node
// federation is active; single-node deployments keep their schema untouched.
bool WidenIdColumnIfInt4(IDataStore* store, const std::string& table) {
    auto q = store->Prepare(
        "SELECT data_type FROM information_schema.columns "
        "WHERE table_name = ? AND column_name = 'id'");
    if (!q) return false;
    q->BindText(1, table);
    if (!q->Step()) return true;   // table absent — skip quietly
    const std::string type = q->ColumnText(0);
    if (type != "integer") return true;   // bigint already
    if (!store->Exec("ALTER TABLE " + table + " ALTER COLUMN id TYPE BIGINT")) {
        ALOG_WARNING("id-ranges", "failed to widen " << table
                     << ".id to BIGINT: " << store->ErrMsg());
        return false;
    }
    ALOG_INFO("id-ranges", "widened " << table << ".id int4 -> BIGINT (federation range)");
    return true;
}

bool SeedPostgres(IDataStore* store, const std::string& table, uint64_t seed) {
    if (!WidenIdColumnIfInt4(store, table)) return false;
    auto q = store->Prepare("SELECT pg_get_serial_sequence(?, 'id')");
    if (!q) return false;
    q->BindText(1, table);
    std::string seq;
    if (q->Step()) seq = q->ColumnText(0);
    if (seq.empty()) {
        ALOG_DEBUG("id-ranges", "no id sequence on " << table << " — skipped");
        return true;
    }
    // Owned sequences are converted to bigint by ALTER COLUMN TYPE; for
    // any sequence that was not (detached, renamed), make it explicit.
    store->Exec("ALTER SEQUENCE " + seq + " AS BIGINT");

    auto v = store->Prepare(
        "SELECT last_value, is_called FROM " + seq);
    if (!v) return false;
    if (!v->Step()) return false;
    const int64_t last = v->ColumnInt64(0);
    const bool isCalled = v->ColumnInt64(1) != 0;
    // is_called=false: last_value hasn't been handed out yet; it IS the next.
    const int64_t next = isCalled ? last + 1 : last;
    if (next >= static_cast<int64_t>(seed)) return true;  // raise-only

    // RESTART WITH n makes nextval() return n; SQLite's seq=n yields n+1.
    // Seed+1 keeps both dialects allocating identically (first id = seed+1).
    return store->Exec("ALTER SEQUENCE " + seq + " RESTART WITH " +
                       std::to_string(seed + 1));
}

} // namespace

int SeedAgentGlobalIdRanges(IDataStore* store, uint64_t nodeId, std::string* error) {
    if (!store) {
        if (error) *error = "null data store";
        return -1;
    }
    if (nodeId == 0) return 0;                     // single-node default: no-op
    if (nodeId > kMaxNodeId) {
        if (error) *error = "node id out of range [1, " +
                            std::to_string(kMaxNodeId) + "]";
        return -1;
    }

    const uint64_t seed = nodeId << kShift;
    int raised = 0;
    for (const auto& table : AgentGlobalTables()) {
        const bool ok = (store->Dialect() == DataStoreDialect::PostgreSQL)
            ? SeedPostgres(store, table, seed)
            : SeedSqlite(store, table, seed);
        if (!ok) {
            ALOG_WARNING("id-ranges", "failed to seed " << table
                        << " (store error: " << store->ErrMsg() << ")");
            continue;
        }
        raised++;
    }
    // task_runs (#78 P2a): replicated table with integer ids — same
    // node-scoped range discipline (a peer's task_runs claim rows must be
    // distinguishable by id alone). Seeded alongside the agent-global set;
    // a failure here is non-fatal for the same reason.
    {
        const bool ok = (store->Dialect() == DataStoreDialect::PostgreSQL)
            ? SeedPostgres(store, "task_runs", seed)
            : SeedSqlite(store, "task_runs", seed);
        if (ok) raised++;
        else ALOG_WARNING("id-ranges", "failed to seed task_runs "
                          "(store error: " << store->ErrMsg() << ")");
    }
    ALOG_INFO("id-ranges", "node " << nodeId << " id ranges seeded at "
              << seed << " (" << raised << " tables)");
    return raised;
}

} // namespace animus::kernel

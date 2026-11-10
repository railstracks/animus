#include "animus_kernel/SyncStore.h"
#include "animus_kernel/IdRanges.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/ApiPackageStore.h"
#include "animus_kernel/api/SecretsVault.h"
#include "animus_kernel/FederationKeys.h"
#include "animus_kernel/CryptoUtils.h"
#include <array>
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/MemoryFileStore.h"
#include "animus_kernel/OntologyStore.h"
#include "animus_kernel/admin/DiaryManager.h"
#include "animus_kernel/scheduler/ScheduleStore.h"
#include "animus_kernel/scheduler/TaskRunStore.h"
#include "animus_kernel/scheduler/ScheduleLeaseStore.h"
#include "animus_kernel/scheduler/Scheduler.h"
#include "animus_kernel/IDataStore.h"

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

using namespace animus::kernel;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_sync_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

// A full agent-global store set + sync store = one "node".
struct Node {
    std::string dbPath;
    SqliteDataStore dataStore;
    memory::MemoryStore memory;
    memory::MemoryFileStore files;
    ontology::OntologyStore ontology;
    DiaryStore diary;
    ScheduleStore scheduleStore;
    TaskRunStore taskRunStore;
    ScheduleLeaseStore leaseStore;
    AgentConfigStore configStore;         // #93 P3: agent_config syncs
    ApiPackageStore pkgStore;             // #93 P3: vault rows sync
    SecretsVault vault;                   // empty key path = disabled (schema only)
    FederationKeys fedKeys;              // #274: envelope table joins the synced set
    SyncStore sync;

    Node(uint64_t nodeId)
        : dbPath(MakeTempDbPath()), dataStore(dbPath), memory(&dataStore),
          files(&dataStore), ontology(&dataStore), diary(&dataStore),
          scheduleStore(&dataStore), taskRunStore(&dataStore),
          leaseStore(&dataStore), configStore(&dataStore), pkgStore(&dataStore),
          vault(&dataStore, ""), fedKeys(&dataStore), sync(&dataStore, nodeId) {
        taskRunStore.EnsureSchema();   // ctor doesn't ensure; schedules does
        leaseStore.EnsureSchema();     // must pre-exist trigger install
        pkgStore.EnsureSchema();       // vault FK'd tables pre-exist sync too
        vault.EnsureSchema();         // api_package_secrets table
        fedKeys.EnsureSchema();     // #274: federation_key_envelopes table
        // Kernel parity: remote agent_config applies invalidate the cache.
        sync.SetApplyNotifier(
            [this](const std::string& t, const std::string& k) {
                configStore.OnSyncApplied(t, k);
            });
        // Kernel parity: node-scoped id ranges so cross-node rows never
        // collide (P2b double-claim test writes on BOTH nodes before pull).
        std::string rangeErr;
        SeedAgentGlobalIdRanges(&dataStore, nodeId, &rangeErr);
        std::string err;
        if (!sync.EnsureSchema(&err)) {
            std::cerr << "  FATAL: node " << nodeId << " sync init: " << err << "\n";
        }
    }
    ~Node() { std::filesystem::remove(dbPath); }

    // Pull everything new from `from` and apply it here. Returns applied count.
    int PullFrom(Node& from, const char* tag = nullptr) {
        const int64_t cursor = sync.GetPeerCursor(from.sync.LocalNodeId());
        auto records = from.sync.FetchOutboxSince(cursor, 1000);
        int applied = 0;
        for (const auto& r : records) {
            const bool ok = sync.ApplyRemoteChange(r);
            if (tag)
                std::cerr << "    [" << tag << "] " << r.table_name
                          << "/" << r.row_key << " op=" << r.op
                          << " in=" << r.unix_ms << "/" << r.origin_node
                          << (ok ? " APPLIED" : " SKIPPED") << "\n";
            if (ok) applied++;
        }
        sync.SetPeerCursor(from.sync.LocalNodeId(), from.sync.MaxOutboxId());
        return applied;
    }

    int64_t CountRows(const std::string& table) {
        auto q = dataStore.Prepare("SELECT COUNT(*) FROM " + table);
        if (!q || !q->Step()) return -1;
        return q->ColumnInt64(0);
    }

    bool HasRow(const std::string& table, int64_t id) {
        auto q = dataStore.Prepare(
            "SELECT 1 FROM " + table + " WHERE id = ?");
        if (!q) return false;
        q->BindInt64(1, id);
        return q->Step();
    }

    std::string TextOf(const std::string& table, int64_t id, const std::string& col) {
        auto q = dataStore.Prepare(
            "SELECT " + col + " FROM " + table + " WHERE id = ?");
        if (!q) return "<no-prepare>";
        q->BindInt64(1, id);
        if (!q->Step()) return "<no-row>";
        return q->ColumnText(0);
    }
};

} // namespace

// Writes land in the outbox via triggers — every agent-global table, both ops.
int TestTriggerCoverage() {
    std::cerr << "  [P1b] trigger coverage (all 10 tables + delete)...\n";
    Node a(1);

    memory::Observation o;
    a.memory.CreateDefaultLayersForAgent("ag");
    auto layers = a.memory.ListLayersForAgent("ag");
    Assert(!layers.empty(), "layers created");
    o.layer_id = layers[0].id;
    o.agent_id = "ag";
    o.text = "coverage probe";
    auto obs = a.memory.CreateObservationForAgent("ag", o);
    Assert(obs.id > 0, "observation created");

    ontology::OntologyEntity e;
    e.name = "Cov";
    e.full_path = "projects/Cov";
    e.root_category = ontology::RootCategory::Projects;
    e.agent_id = "ag";
    a.ontology.CreateEntity(e, "coverage");

    DiaryEntry de;
    de.id = "cov-diary";
    de.agent_id = "ag";
    de.content = "coverage";
    de.timestamp_unix_ms = 42;
    a.diary.Create(de);

    // Direct minimal rows for the remaining tables (payload-heavy stores).
    // (layer_perspectives rows come from CreateDefaultLayersForAgent itself)
    a.dataStore.Exec("INSERT INTO memory_mutations (mutation_type, target_type, target_id, unix_ms) "
                     "VALUES ('m', 't', 1, 1)");
    a.dataStore.Exec("INSERT INTO memory_files (source_path, file_type, created_at_unix_ms, imported_at_unix_ms) "
                     "VALUES ('/x', 1, 1, 1)");
    const int64_t fileId = [&]{
        auto q = a.dataStore.Prepare(
            "SELECT id FROM memory_files ORDER BY id DESC LIMIT 1");
        return (q && q->Step()) ? q->ColumnInt64(0) : 1;
    }();
    a.dataStore.Exec("INSERT INTO memory_file_chunks (file_id, source_path, content, created_at_unix_ms) "
                     "VALUES (" + std::to_string(fileId) + ", '/x', 'c', 1)");
    const int64_t entityId = [&]{
        auto q = a.dataStore.Prepare(
            "SELECT id FROM ontology_entities ORDER BY id DESC LIMIT 1");
        return (q && q->Step()) ? q->ColumnInt64(0) : 1;
    }();
    a.dataStore.Exec("INSERT INTO ontology_properties (entity_id, key, created_at_unix_ms, updated_at_unix_ms) "
                     "VALUES (" + std::to_string(entityId) + ", 'k', 1, 1)");
    a.dataStore.Exec("INSERT INTO ontology_mutations (mutation_type, target_type, target_id, unix_ms) "
                     "VALUES ('m', 't', 1, 1)");

    // P2a: scheduler tables are agent-global too — write one of each so
    // coverage exercises the TEXT-key (schedules) + seeded-int-key
    // (task_runs) trigger paths.
    a.dataStore.Exec("INSERT INTO schedules (id, agent_id, type, next_fire, message, enabled, created_at) "
                     "VALUES ('cov-sched', 'ag', 'one_shot', '2026-09-14T00:00:00Z', 'x', 1, '2026-09-13T00:00:00Z')");
    a.dataStore.Exec("INSERT INTO task_runs (run_uuid, schedule_id, agent_id, scheduled_for, started_at_unix_ms) "
                     "VALUES ('cov-run', 'cov-sched', 'ag', 'w', 1)");
    a.leaseStore.Acquire("cov-sched", "1", 1, 1, 60000);

    // #93 P3: config + vault tables join the covered set.
    a.configStore.Set("ag", "coverage.key", "v");
    {
        SecretsVault covVault(&a.dataStore, a.dbPath + ".cov.key");
        covVault.EnsureSchema();
        std::string cerr_;
        covVault.Set("cov-pkg", "tok", "secret-value", cerr_);
    }

    // #274: federation envelope table joins the covered set.
    {
        crypto::X25519KeyPair kp;
        std::string kerr;
        crypto::GenerateX25519KeyPair(kp, kerr);
        std::array<unsigned char, 32> covDek{};
        std::string ferr;
        a.fedKeys.SealEpochDek(1, "cov-node", "cov-node", kp.public_key, covDek, ferr);
    }

    auto records = a.sync.FetchOutboxSince(0, 1000);
    Assert(records.size() >= 13, "13+ outbox records, got " +
           std::to_string(records.size()));
    // distinct agent-global tables captured
    const auto tables = AgentGlobalTables();
    std::vector<bool> seen(tables.size(), false);
    for (const auto& r : records) {
        for (size_t i = 0; i < tables.size(); ++i)
            if (r.table_name == tables[i]) seen[i] = true;
    }
    for (size_t i = 0; i < tables.size(); ++i)
        Assert(seen[i], "outbox captured " + tables[i]);
    Assert(records[0].origin_node == 1, "origin = local node");
    for (const auto& r : records)
        Assert(r.unix_ms > 0 && r.payload.size() > 2,
               "record has ms + payload: " + r.table_name);

    // UPDATE + DELETE fire too (row created above — find its real id)
    const int64_t mutId = [&]{
        auto q = a.dataStore.Prepare(
            "SELECT id FROM memory_mutations ORDER BY id DESC LIMIT 1");
        return (q && q->Step()) ? q->ColumnInt64(0) : 1;
    }();
    a.dataStore.Exec("UPDATE memory_mutations SET motivation = 'x' WHERE id = "
                     + std::to_string(mutId));
    a.dataStore.Exec("DELETE FROM memory_mutations WHERE id = "
                     + std::to_string(mutId));
    auto after = a.sync.FetchOutboxSince(records.back().outbox_id, 100);
    Assert(after.size() == 2, "update+delete recorded, got " +
           std::to_string(after.size()));
    Assert(after.size() == 2 && after[1].op == "delete", "delete op recorded");
    return 0;
}

// Full A→B replication, echo death at A, then convergence on update + delete.
int TestTwoNodeReplication() {
    std::cerr << "  [P1b] two-node replication + echo death...\n";
    Node a(1), b(2);

    // A writes a layer + observation
    a.memory.CreateDefaultLayersForAgent("ag");
    auto layers = a.memory.ListLayersForAgent("ag");
    memory::Observation o;
    o.layer_id = layers[0].id;
    o.agent_id = "ag";
    o.text = "v1";
    auto obs = a.memory.CreateObservationForAgent("ag", o);

    // B pulls A
    int applied = b.PullFrom(a);
    Assert(applied >= 2, "B applied layer+observation, got " +
           std::to_string(applied));
    Assert(b.HasRow("memory_layers", layers[0].id), "layer replicated");
    Assert(b.HasRow("observations", obs.id), "observation replicated");
    Assert(b.TextOf("observations", obs.id, "text") == "v1", "text intact");

    // B's outbox now holds ECHOES stamped with A's origin (not B's).
    auto bEchoes = b.sync.FetchOutboxSince(0, 1000);
    Assert(!bEchoes.empty(), "B produced echo records");
    Assert(bEchoes[0].origin_node == 1,
           "echo stamped with ORIGIN node 1, got " +
           std::to_string(bEchoes[0].origin_node));

    // ECHO DEATH: A pulls B's echoes back — all skipped, A's outbox unchanged.
    const int64_t aBefore = a.sync.MaxOutboxId();
    const int aApplied = a.PullFrom(b);
    Assert(aApplied == 0, "echoes all skipped at origin, applied " +
           std::to_string(aApplied));
    Assert(a.sync.MaxOutboxId() == aBefore,
           "A's outbox unchanged after echo round-trip");

    // UPDATE on A → B applies the newer version
    a.dataStore.Exec("UPDATE observations SET text = 'v2' WHERE id = " +
                     std::to_string(obs.id));
    applied = b.PullFrom(a);
    Assert(applied == 1, "B applied the update, got " + std::to_string(applied));
    Assert(b.TextOf("observations", obs.id, "text") == "v2", "v2 replicated");

    // Stale record rejected: pair-lesser (older ms, same origin)
    OutboxRecord stale;
    stale.table_name = "observations";
    stale.row_key = std::to_string(obs.id);
    stale.op = "upsert";
    stale.origin_node = 1;
    stale.unix_ms = 1;   // ancient
    stale.payload = R"({"id":)" + std::to_string(obs.id) + R"(,"text":"ancient","layer_id":)" +
                    std::to_string(layers[0].id) + R"(,"agent_id":"ag"})";
    Assert(!b.sync.ApplyRemoteChange(stale), "stale rejected");
    Assert(b.TextOf("observations", obs.id, "text") == "v2", "v2 survived stale");

    // DELETE on A → B removes the row
    a.dataStore.Exec("DELETE FROM observations WHERE id = " + std::to_string(obs.id));
    applied = b.PullFrom(a);
    Assert(applied == 1, "B applied the delete, got " + std::to_string(applied));
    Assert(!b.HasRow("observations", obs.id), "delete replicated");

    // B's delete-echo dies at A too
    Assert(a.PullFrom(b) == 0, "delete echo skipped at origin");
    return 0;
}

// LWW pair semantics: (ms, node) tie-break, strictly-greater-only.
int TestLwwPairSemantics() {
    std::cerr << "  [P1b] LWW (ms, node) pair semantics...\n";
    Node a(1), b(2);

    a.memory.CreateDefaultLayersForAgent("ag");
    auto layers = a.memory.ListLayersForAgent("ag");
    memory::Observation o;
    o.layer_id = layers[0].id;
    o.agent_id = "ag";
    o.text = "node1-v1";
    auto obs = a.memory.CreateObservationForAgent("ag", o);
    b.PullFrom(a);
    Assert(b.HasRow("observations", obs.id), "seeded");

    // Craft from the REAL payload (full row — NOT NULL columns intact).
    auto current = a.sync.FetchOutboxSince(0, 1000);
    int64_t writeMs = 0; std::string realPayload;
    for (const auto& r : current)
        if (r.table_name == "observations" && r.row_key == std::to_string(obs.id)) {
            writeMs = r.unix_ms; realPayload = r.payload;
        }
    Assert(writeMs > 0, "found write ms");
    Assert(!realPayload.empty(), "captured real payload");
    // tweak only the text value inside the JSON
    const std::string marker = "\"text\":\"node1-v1\"";
    const size_t mpos = realPayload.find(marker);
    Assert(mpos != std::string::npos, "payload carries text field");
    const std::string pay = realPayload.substr(0, mpos) +
                            "\"text\":\"X\"" +
                            realPayload.substr(mpos + marker.size());

    // Equal ms, HIGHER node: version at b is (ms_of_write, 1);
    // send (same ms, node 2) → pair-greater → wins.
    OutboxRecord tie;
    tie.table_name = "observations"; tie.row_key = std::to_string(obs.id); tie.op = "upsert";
    tie.origin_node = 2; tie.unix_ms = writeMs; tie.payload = pay;
    Assert(b.sync.ApplyRemoteChange(tie), "tie broken by higher node id");
    Assert(b.TextOf("observations", obs.id, "text") == "X", "tie-winner applied");

    // Exact equal pair (same ms, same node) → skipped
    OutboxRecord echo = tie;   // exact same pair + payload value
    Assert(!b.sync.ApplyRemoteChange(echo), "exact equal pair skipped");
    Assert(b.TextOf("observations", obs.id, "text") == "X", "echo did not apply");

    // Unknown table / op rejected (identifier whitelist)
    OutboxRecord evil;
    evil.table_name = "sync_outbox; DROP TABLE observations;--";
    evil.row_key = "1"; evil.op = "upsert"; evil.origin_node = 9;
    evil.unix_ms = 99999999999999; evil.payload = "{}";
    Assert(!b.sync.ApplyRemoteChange(evil), "unknown table rejected");
    OutboxRecord badOp = tie;
    badOp.op = "truncate";
    Assert(!b.sync.ApplyRemoteChange(badOp), "unknown op rejected");
    return 0;
}

// Concurrency safety: local writes during a "sync batch" still hit the outbox.
int TestLocalWritesDuringApply() {
    std::cerr << "  [P1b] local writes during apply stay captured...\n";
    Node a(1), b(2);

    b.memory.CreateDefaultLayersForAgent("ag");
    auto layers = b.memory.ListLayersForAgent("ag");
    memory::Observation o;
    o.layer_id = layers[0].id;
    o.agent_id = "ag";
    o.text = "b-local";
    auto obsB = b.memory.CreateObservationForAgent("ag", o);

    // simulate mid-batch: apply context set, then a local write happens
    OutboxRecord from;
    from.table_name = "diary_entries"; from.row_key = "777"; from.op = "delete";
    from.origin_node = 1; from.unix_ms = 1; from.payload = R"({"id":777})";
    // (set apply context the same way ApplyRemoteChange does)
    auto q = b.dataStore.Prepare(
        "UPDATE sync_control SET apply_table = 'diary_entries', apply_row_id = '777', "
        "apply_origin = 1, apply_ms = 2");
    q->ExecDML();

    memory::Observation mid;
    mid.layer_id = layers[0].id;
    mid.agent_id = "ag";
    mid.text = "mid-batch";
    auto obsMid = b.memory.CreateObservationForAgent("ag", mid);
    auto midRec = b.sync.FetchOutboxSince(0, 1000);
    bool midCaptured = false; int64_t midMs = 0;
    for (const auto& r : midRec)
        if (r.table_name == "observations" && r.row_key == std::to_string(obsMid.id)) {
            midCaptured = true; midMs = r.unix_ms;
        }
    Assert(midCaptured, "mid-batch local write captured in outbox");
    // mid-batch write is a DIFFERENT row than the applying one -> local stamp
    Assert(midMs >= 2, "mid-batch stamp is real local time");

    // clear apply context (ApplyRemoteChange's finally-path)
    auto q2 = b.dataStore.Prepare(
        "UPDATE sync_control SET apply_table = NULL, apply_row_id = NULL, "
        "apply_origin = NULL, apply_ms = NULL");
    q2->ExecDML();

    // A can pull B's local writes
    Assert(a.HasRow("memory_layers", layers[0].id) == false, "not yet");
    const int applied = a.PullFrom(b);
    Assert(a.HasRow("observations", obsMid.id), "mid-batch write replicated to A");
    (void)obsB; (void)applied;
    return 0;
}

// Cursors: incremental pull returns only new records.
int TestPeerCursors() {
    std::cerr << "  [P1b] peer cursors...\n";
    Node a(1), b(2);
    a.memory.CreateDefaultLayersForAgent("ag");
    const int64_t c0 = b.sync.GetPeerCursor(1);
    Assert(c0 == 0, "cursor starts at 0");

    b.PullFrom(a);   // consumes everything
    const int64_t c1 = b.sync.GetPeerCursor(1);
    Assert(c1 == a.sync.MaxOutboxId(), "cursor = max outbox id");

    memory::Observation o;
    auto layers = a.memory.ListLayersForAgent("ag");
    o.layer_id = layers[0].id; o.agent_id = "ag"; o.text = "second";
    a.memory.CreateObservationForAgent("ag", o);

    // Incremental fetch reads A's outbox (b's cursor into it). The second
    // observation write lands as TWO records — the observation itself plus
    // its memory_mutations audit row (agent-global, captured by design):
    auto fresh = a.sync.FetchOutboxSince(b.sync.GetPeerCursor(1), 1000);
    Assert(fresh.size() == 2, "incremental fetch = 2 new records, got " +
           std::to_string(fresh.size()));
    bool sawObs = false, sawMutation = false;
    for (const auto& r : fresh) {
        if (r.table_name == "observations") sawObs = true;
        if (r.table_name == "memory_mutations") sawMutation = true;
    }
    Assert(sawObs && sawMutation, "observation + its audit row captured");
    return 0;
}

// Idempotent re-boot: EnsureSchema twice, triggers still single-fire.
int TestIdempotentBoot() {
    std::cerr << "  [P1b] idempotent double boot...\n";
    Node a(1);
    std::string err;
    Assert(a.sync.EnsureSchema(&err), "second EnsureSchema ok: " + err);
    a.memory.CreateDefaultLayersForAgent("ag");
    auto recs = a.sync.FetchOutboxSince(0, 1000);
    // default layers = multiple layer rows; each must appear exactly once
    int layerRecords = 0;
    for (const auto& r : recs)
        if (r.table_name == "memory_layers") layerRecords++;
    Assert(layerRecords == (int)a.memory.ListLayersForAgent("ag").size(),
           "no double-triggering after re-boot");
    return 0;
}

// P2a: TEXT-keyed replication — schedules (TEXT pk) + task_runs (node-seeded
// int ids) replicate through the same outbox/LWW machinery.
int TestSchedulerTableReplication() {
    std::cerr << "  [P2a] scheduler tables replicate (TEXT + int keys)...\n";
    Node a(1);
    Node b(2);

    // A creates a schedule through the real store API (random hex TEXT id).
    ScheduleDescriptor sd;
    sd.agent_id = "ag";
    sd.type = ScheduleType::OneShot;
    sd.next_fire = "2026-09-14T00:00:00Z";
    sd.message = "p2a replication probe";
    std::string err;
    std::string schedId = a.scheduleStore.CreateAndReturnId(sd, &err);
    Assert(!schedId.empty(), "schedule created: " + err);

    // A claims a task_run (node 1 identity) via the real claim path.
    const bool claimOk = a.taskRunStore.TryClaim(schedId + "@2026-09-14T00:00:00Z",
                                  schedId, "ag", "2026-09-14T00:00:00Z",
                                  12345, "1");
    if (!claimOk) {
        // Diagnose: does the table exist? does the insert work at all?
        auto q = a.dataStore.Prepare("SELECT count(*) FROM task_runs");
        std::string n = (q && q->Step()) ? std::to_string(q->ColumnInt64(0)) : "?";
        std::cerr << "    [diag] task_runs rows: " << n << "\n";
        auto raw = a.dataStore.Prepare(
            "INSERT INTO task_runs (run_uuid, schedule_id, agent_id, scheduled_for, "
            "node_id, started_at_unix_ms) VALUES ('diag','" + schedId + "','ag','w','1',1) "
            "ON CONFLICT(run_uuid) DO NOTHING");
        std::cerr << "    [diag] raw insert: " << (raw && raw->ExecDML()) << " err="
                  << a.dataStore.ErrMsg() << "\n";
        auto t = a.dataStore.Prepare(
            "SELECT name FROM sqlite_master WHERE type='trigger' AND tbl_name='task_runs'");
        while (t && t->Step()) std::cerr << "    [diag] trigger: " << t->ColumnText(0) << "\n";
    }
    Assert(claimOk, "claim inserted");

    // Both writes must be in A's outbox.
    auto recs = a.sync.FetchOutboxSince(0, 1000);
    bool sawSched = false, sawRun = false;
    std::string schedKey;
    for (const auto& r : recs) {
        if (r.table_name == "schedules" && r.row_key == schedId) sawSched = true;
        if (r.table_name == "task_runs") sawRun = true;
    }
    Assert(sawSched, "outbox captured schedule row (TEXT key)");
    Assert(sawRun, "outbox captured task_run claim");

    // B pulls: both tables converge.
    const int applied = b.PullFrom(a, "P2a-pull1");
    Assert(applied >= 2, "B applied both rows, got " + std::to_string(applied));
    Assert(b.scheduleStore.Get(schedId).has_value(),
           "B sees the schedule after pull");
    Assert(b.taskRunStore.GetByUuid(schedId + "@2026-09-14T00:00:00Z").has_value(),
           "B sees the task_run claim after pull");
    Assert(b.taskRunStore.GetByUuid(schedId + "@2026-09-14T00:00:00Z")->node_id == "1",
           "claim carries node identity");

    // Cross-node idempotency: B's claim of the SAME uuid fails (A owns it),
    // and replicating that conflict back to A changes nothing.
    Assert(!b.taskRunStore.TryClaim(schedId + "@2026-09-14T00:00:00Z",
                                    schedId, "ag", "2026-09-14T00:00:00Z",
                                    99999, "2"),
           "duplicate claim rejected on B (replicated claim fences)");

    // Update + delete of a schedule replicate too.
    auto got = a.scheduleStore.Get(schedId);
    Assert(got.has_value(), "refetch");
    got->message = "updated message";
    Assert(a.scheduleStore.Update(*got, &err), "update: " + err);
    std::string delErr;
    Assert(a.scheduleStore.Delete(schedId, &delErr), "delete: " + delErr);
    const int applied2 = b.PullFrom(a, "P2a-pull2");
    Assert(applied2 >= 2, "update+delete replicated, got " + std::to_string(applied2));
    Assert(!b.scheduleStore.Get(schedId).has_value(),
           "schedule delete converged on B");
    return 0;
}

// ── #78 P2b: leases replicate, epochs fence, double-claims reconcile ──
int TestLeaseReplicationAndFencing() {
    std::cerr << "  [P2c] default layer seeding converges cross-node...\n";
    {
        Node a(1), b(2);
        Assert(a.memory.CreateDefaultLayersForAgent("ag"), "A seeds defaults");
        Assert(b.memory.CreateDefaultLayersForAgent("ag"), "B seeds defaults");
        a.PullFrom(b);
        b.PullFrom(a);
        Assert(a.CountRows("memory_layers") == 7, "A: exactly 7 layers");
        Assert(b.CountRows("memory_layers") == 7, "B: exactly 7 layers");
        Assert(a.CountRows("layer_perspectives") == 7, "A: exactly 7 perspectives");
        Assert(b.CountRows("layer_perspectives") == 7, "B: exactly 7 perspectives");
        for (int64_t id = 1; id <= 7; ++id) {
            Assert(a.HasRow("memory_layers", id), "A has fixed-id layer");
            Assert(b.HasRow("memory_layers", id), "B has fixed-id layer");
            Assert(a.HasRow("layer_perspectives", id), "A has fixed-id perspective");
            Assert(b.HasRow("layer_perspectives", id), "B has fixed-id perspective");
        }
        // Wiped node rebuild: reseed produces the SAME fixed ids and reconverges.
        a.dataStore.Exec("DELETE FROM memory_layers");
        Assert(a.CountRows("memory_layers") == 0, "A wiped");
        Assert(a.memory.CreateDefaultLayersForAgent("ag"), "A reseeds");
        a.PullFrom(b);
        Assert(a.CountRows("memory_layers") == 7, "A rebuilt to 7");
        for (int64_t id = 1; id <= 7; ++id)
            Assert(a.HasRow("memory_layers", id), "A rebuilt fixed-id layer");
    }

    std::cerr << "  [P2b] lease replication + epoch fencing + double-claim heal...\n";
    Node a(1);
    Node b(2);
    const int64_t now = 1760000000000LL;

    // A acquires epoch 1 and claims a run under it.
    auto la = a.leaseStore.Acquire("sched-lease1", "1", 1, now, 60000);
    Assert(la.epoch == 1, "A lease epoch 1");
    const std::string uuid = "sched-lease1@2026-09-14T00:00:00Z";
    Assert(a.taskRunStore.TryClaim(uuid, "sched-lease1", "ag",
                                   "2026-09-14T00:00:00Z", now, "1", 1),
           "A claims under epoch 1");

    // Partition scenario: B (unaware) takes over epoch 2 and claims the
    // SAME window under epoch 2. Both nodes now hold a claim row.
    auto lb = b.leaseStore.Acquire("sched-lease1", "2", 2, now + 1000, 60000);
    Assert(lb.epoch == 2, "B takeover epoch 2");
    Assert(b.taskRunStore.TryClaim(uuid, "sched-lease1", "ag",
                                   "2026-09-14T00:00:00Z", now + 1000, "2", 2),
           "B claims same window under epoch 2");

    // Heal: B pulls A. The epoch-1 claim row arrives — apply MUST SUCCEED
    // (the P2a UNIQUE-constraint latent bug would fail here) and the
    // epoch fence marks A's row the loser on B.
    {
        auto recsDiag = a.sync.FetchOutboxSince(0, 1000);
        std::cerr << "    [diag] A outbox records: " << recsDiag.size() << "\n";
        for (const auto& r : recsDiag) {
            std::cerr << "    [diag]   " << r.table_name << "/" << r.row_key
                      << " op=" << r.op << " ms=" << r.unix_ms << "\n";
        }
    }
    const int appliedB = b.PullFrom(a);
    Assert(appliedB >= 2, "B applied A's lease+claim (no apply failure), got "
           + std::to_string(appliedB));
    auto bEff = b.leaseStore.EffectiveLease("sched-lease1");
    Assert(bEff.has_value() && bEff->epoch == 2 && bEff->holder_node_id == "2",
           "B effective lease stays epoch 2 / holder 2 after pull");
    auto rowsB = b.leaseStore.ListRows("sched-lease1");
    Assert(rowsB.size() == 2, "both lease rows coexist on B");
    auto gotB = b.taskRunStore.GetByUuid(uuid);
    Assert(gotB.has_value() && gotB->epoch == 2 && !gotB->fenced,
           "B survivor claim = epoch 2, unfenced");

    // Reverse pull: A receives epoch-2 lease + claim; A's own claim fences.
    const int appliedA = a.PullFrom(b);
    Assert(appliedA >= 2, "A applied B's rows, got " + std::to_string(appliedA));
    auto aEff = a.leaseStore.EffectiveLease("sched-lease1");
    Assert(aEff.has_value() && aEff->epoch == 2 && aEff->holder_node_id == "2",
           "A converges to epoch 2 / holder 2 (loser defers)");
    auto gotA = a.taskRunStore.GetByUuid(uuid);
    Assert(gotA.has_value() && gotA->epoch == 2 && !gotA->fenced,
           "A survivor view = epoch 2");

    // The loser row is visibly fenced on both nodes (tripwire witness).
    int fencedOnB = 0, fencedOnA = 0;
    for (const auto& r : b.taskRunStore.ListForSchedule("sched-lease1", 10))
        if (r.fenced) fencedOnB++;
    for (const auto& r : a.taskRunStore.ListForSchedule("sched-lease1", 10))
        if (r.fenced) fencedOnA++;
    Assert(fencedOnB == 1, "B fences the epoch-1 duplicate (got "
           + std::to_string(fencedOnB) + ")");
    Assert(fencedOnA == 1, "A fences its own superseded claim (got "
           + std::to_string(fencedOnA) + ")");
    return 0;
}

// ── #78 P2b: the fire loop gates lease_required schedules ──
int TestLeaseGateFireLoop() {
    std::cerr << "  [P2b] lease gate: acquire/renew/pause/takeover/self-fence...\n";
    Node a(1);
    Scheduler sched(&a.dataStore);
    sched.SetNodeId("1");
    sched.SetLeaseTtlMs(2500);
    sched.SetLeaseGraceMs(150);
    sched.SetClaimVisibilityMs(0);   // tests dispatch on the next pass
    int dispatched = 0;
    sched.SetFireCallback([&](const IncomingEvent&) {
        dispatched++;
        return "dispatched";
    });

    // Stub ack layers: mutable peer views the tests manipulate per case.
    Scheduler::LeasePeerState ps;   // defaults: 0 peers configured
    sched.SetLeaseAckFn([&](const std::string&, int64_t) { return ps; });
    Scheduler::ClaimPeerState cs;   // defaults: 0 peers configured
    sched.SetClaimAckFn([&](const std::string&) { return cs; });

    // Two-phase: claim pass + (aged) dispatch pass.
    auto pass2 = [&]() {
        sched.ProcessDueSchedules();
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        sched.ProcessDueSchedules();
    };

    auto mkDue = [&](const std::string& semantics,
                     const std::string& pastIso) {
        ScheduleDescriptor sd;
        sd.agent_id = "ag";
        sd.type = ScheduleType::OneShot;
        sd.next_fire = pastIso;
        sd.message = "gate probe";
        sd.semantics = semantics;
        std::string err;
        auto id = sched.Create(sd, &err);
        Assert(!id.empty(), "create schedule: " + err);
        return id;
    };

    // (a) at_least_once: no lease needed, fires single-phase.
    mkDue("at_least_once", "2026-09-14T00:00:00Z");
    sched.ProcessDueSchedules();
    Assert(dispatched == 1, "at_least_once fires without lease");

    // (b) lease_required, no lease row, no peers: acquires epoch 1,
    // two-phase dispatch (claim pass, then aged dispatch pass).
    std::string idB = mkDue("lease_required", "2026-09-14T00:01:00Z");
    sched.ProcessDueSchedules();
    Assert(dispatched == 1, "phase 1: claim only, no dispatch yet");
    Assert(a.taskRunStore.GetByUuid(idB + "@2026-09-14T00:01:00Z")
               ->outcome == "running",
           "claim row pending (running)");
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    sched.ProcessDueSchedules();
    Assert(dispatched == 2, "phase 2: dispatch after visibility window");
    auto eff = sched.LeaseStore().EffectiveLease(idB);
    Assert(eff.has_value() && eff->epoch == 1 && eff->holder_node_id == "1",
           "lease row epoch 1 held by node 1");

    // (c) second window, peer present + confirming my claim: dispatch.
    ps = Scheduler::LeasePeerState{1, true, false, 0};
    cs = Scheduler::ClaimPeerState{1, true, false, false};
    ScheduleDescriptor sd2;
    sd2.agent_id = "ag"; sd2.type = ScheduleType::OneShot;
    sd2.next_fire = "2026-09-14T00:02:00Z"; sd2.semantics = "lease_required";
    std::string idC = sched.Create(sd2, nullptr);
    pass2();
    Assert(dispatched == 3, "lease_required fires when claim confirmed");

    // (h) foreign SURVIVOR claim exists: stand down, no dispatch, no claim.
    const std::string uuidH = "sched-h@2026-09-14T00:05:00Z";
    Assert(a.taskRunStore.TryClaim(uuidH, "sched-h", "ag",
                                   "2026-09-14T00:05:00Z",
                                   1760000000000LL, "2", 7),
           "foreign claim row inserted");
    ScheduleDescriptor sdH;
    sdH.agent_id = "ag"; sdH.type = ScheduleType::OneShot;
    sdH.next_fire = "2026-09-14T00:05:00Z"; sdH.semantics = "lease_required";
    sdH.id = "sched-h";
    Assert(!sched.Create(sdH, nullptr).empty(), "create foreign-claim probe");
    pass2();
    Assert(dispatched == 3, "foreign survivor claim = stand down");
    Assert(a.taskRunStore.ListForSchedule("sched-h", 5).size() == 1,
           "no new claim row for the foreign survivor window");

    // (d) another node holds a VALID lease: paused (no claim, no advance).
    ps = Scheduler::LeasePeerState{1, false, false, 0};   // peer up, not us
    const int64_t nowMs = 1760000000000LL;
    sched.LeaseStore().Acquire("sched-node2", "2", 5, nowMs, 60000);
    ScheduleDescriptor sdD;
    sdD.agent_id = "ag"; sdD.type = ScheduleType::OneShot;
    sdD.next_fire = "2026-09-14T00:03:00Z"; sdD.semantics = "lease_required";
    std::string idD = sched.Create(sdD, nullptr);
    // Foreign holder: point the schedule id at the node-2 lease.
    // (Acquire used a fixed id; create the schedule with the SAME id.)
    // Simplest: reuse the lease's schedule id as the schedule id.
    sched.Cancel(idD, nullptr);
    ScheduleDescriptor sdD2 = sdD;
    sdD2.id = "sched-node2";
    std::string errD;
    Assert(!sched.Create(sdD2, &errD).empty(), "create with matching id");
    sched.ProcessDueSchedules();
    Assert(dispatched == 3, "valid foreign lease = paused, no dispatch");
    Assert(sched.Store().Get("sched-node2")->next_fire == "2026-09-14T00:03:00Z",
           "paused schedule did not advance");

    // (e) foreign lease expired beyond grace, peer known-down: takeover.
    ps = Scheduler::LeasePeerState{1, false, true, 0};    // all peers down
    sched.LeaseStore().Expire("sched-node2");   // expires now
    std::this_thread::sleep_for(std::chrono::milliseconds(3200)); // retry + grace
    sched.ProcessDueSchedules();
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    sched.ProcessDueSchedules();
    Assert(dispatched == 4, "expired foreign lease + peer down = takeover fires");
    auto eff2 = sched.LeaseStore().EffectiveLease("sched-node2");
    Assert(eff2.has_value() && eff2->epoch == 6 && eff2->holder_node_id == "1",
           "takeover epoch 6 (5+1) held by node 1");

    // (f) MY lease expired, peer reachable but not acking: self-fenced.
    ps = Scheduler::LeasePeerState{1, false, false, 0};
    const int64_t past = 1760000000000LL - 100000;   // expired long ago
    sched.LeaseStore().Acquire("sched-mine", "1", 3, past, 30000);
    ScheduleDescriptor sdF;
    sdF.agent_id = "ag"; sdF.type = ScheduleType::OneShot;
    sdF.next_fire = "2026-09-14T00:04:00Z"; sdF.semantics = "lease_required";
    sdF.id = "sched-mine";
    Assert(!sched.Create(sdF, nullptr).empty(), "create self-fence probe");
    std::this_thread::sleep_for(std::chrono::milliseconds(3200)); // retry window
    pass2();
    Assert(dispatched == 4, "self-fenced (no ack proof) = paused");

    // (g) same, but peer acks our epoch (no takeover visible): re-acquire.
    ps = Scheduler::LeasePeerState{1, true, false, 0};
    std::this_thread::sleep_for(std::chrono::milliseconds(3200));
    pass2();
    Assert(dispatched == 5, "acked expired-own lease = re-acquire fires");
    auto eff3 = sched.LeaseStore().EffectiveLease("sched-mine");
    Assert(eff3.has_value() && eff3->epoch == 4 && eff3->holder_node_id == "1",
           "re-acquire epoch 4 (3+1)");
    return 0;
}

// ── #93 P3: config replication — agent_config + vault rows ─────────────

int TestConfigReplication() {
    std::cerr << "  [P3] agent_config replicates (pair row-key, both ops)\n";
    Node a(1), b(2);

    // Upsert via the store API — trigger must capture the composite row
    a.configStore.Set("default", "channel.telegram-main.type", "telegram");
    a.configStore.Set("default", "channel.telegram-main.config",
                      "{\"bot_token\":\"***\"}");
    // Distinct-ms guarantee: LWW ties are skipped BY DESIGN (the echo-death
    // property), and under CPU contention the second Set + this UPDATE can
    // land in the same millisecond — the update would then skip as a tie
    // and this test would count 2 records instead of 3. Space the writes.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // ...and via direct SQL (paths the store doesn't cover)
    auto upd = a.dataStore.Prepare(
        "UPDATE agent_config SET value = 'irc' WHERE agent_id = ? AND key = ?");
    Assert(upd != nullptr, "direct update prepare");
    upd->BindText(1, "default");
    upd->BindText(2, "channel.telegram-main.type");
    Assert(upd->ExecDML(), "direct update runs");

    const int applied = b.PullFrom(a);
    // 3 outbox records (2 inserts + 1 update), all apply; 2 rows exist.
    Assert(applied == 3, "3 config records applied to node b, got " +
                            std::to_string(applied));
    Assert(b.configStore.GetRaw("default", "channel.telegram-main.type") == "irc",
           "composite upsert applied (value updated, not duplicated)");
    Assert(b.configStore.GetRaw("default", "channel.telegram-main.config")
               .find("bot_token") != std::string::npos,
           "pair-sibling row applied");
    Assert(b.CountRows("agent_config") == 2,
           "exactly two rows (one per pair), no duplicates");

    // LWW: b writes a newer value; a pulls it back
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    b.configStore.Set("default", "channel.telegram-main.type", "irc-newer");
    const int back = a.PullFrom(b);
    Assert(back >= 1, "b->a sync applied");
    Assert(a.configStore.GetRaw("default", "channel.telegram-main.type") == "irc-newer",
           "LWW carried b's newer write to a");

    // Delete via prefix delete — every deleted row fences
    b.configStore.DeleteByPrefix("default", "channel.telegram-main.");
    a.PullFrom(b);
    Assert(a.configStore.GetRaw("default", "channel.telegram-main.type").empty(),
           "delete replicated to a");
    return 0;
}

int TestSecretsNeverInPayloads() {
    std::cerr << "  [P3] vault ciphertext replicates; opened secrets never do\n";
    Node a(1), b(2);

    // Enabled vault on node a (with key file)
    std::string keyPath = a.dbPath + ".vault.key";
    {
        SecretsVault realVault(&a.dataStore, keyPath);
        realVault.EnsureSchema();
        std::string err;
        Assert(realVault.Set("pkg-1", "bot_token", "SUPER-SECRET-TOKEN-XYZ", err),
               "vault set on a");
    }
    const int applied = b.PullFrom(a);
    Assert(applied >= 1, "vault row applied to b, got " + std::to_string(applied));

    // The ciphertext row arrived; plaintext never existed on the wire.
    std::string wire;
    {
        auto records = a.sync.FetchOutboxSince(0, 1000);
        for (const auto& r : records) {
            wire += r.payload;
        }
    }
    Assert(wire.find("SUPER-SECRET-TOKEN-XYZ") == std::string::npos,
           "plaintext secret NEVER in any outbox payload");
    auto has = b.dataStore.Prepare(
        "SELECT COUNT(*) FROM api_package_secrets WHERE package_id = 'pkg-1'");
    Assert(has && has->Step() && has->ColumnInt64(0) == 1,
           "ciphertext row replicated to b");

    // Ref objects (agent_config) carry the ref NAME, never the value
    std::string raw;
    {
        auto upd = a.dataStore.Prepare(
            "INSERT INTO agent_config (agent_id, key, value) VALUES ('default', "
            "'channels.t:main.bot_token', '{\"secret_ref\":\"telegram_bot_token\"}')");
        Assert(upd && upd->ExecDML(), "ref row inserted");
    }
    std::string wire2;
    b.PullFrom(a);
    {
        auto records = a.sync.FetchOutboxSince(0, 2000);
        for (const auto& r : records) wire2 += r.payload;
    }
    Assert(wire2.find("SUPER-SECRET-TOKEN-XYZ") == std::string::npos,
           "opened secret still absent after ref row sync");
    Assert(wire2.find("secret_ref") != std::string::npos,
           "ref object carried as ref");
    return 0;
}

int TestP3DigestAndHandshake() {
    std::cerr << "  [P3] digests cover agent_config + vault tables\n";
    Node a(1);
    a.configStore.Set("default", "k1", "v1");
    const auto digests = a.sync.TableDigests();
    bool sawConfig = false, sawVault = false;
    for (const auto& d : digests) {
        if (d.table == "agent_config") sawConfig = true;
        if (d.table == "api_package_secrets") sawVault = true;
    }
    Assert(sawConfig && sawVault, "digests include both P3 tables");
    const auto tables = a.sync.SyncedTables();
    bool inSynced = false;
    for (const auto& t : tables) if (t == "agent_config") inSynced = true;
    Assert(inSynced, "agent_config reports as synced");
    return 0;
}

// ── #109 audit F2: cache invalidation must drop the warmed flag ────────
// A warmed GetAll() caches the slice and sets m_cacheWarmed. A remote
// apply then invalidates the slice. The pre-fix code erased the slice but
// KEPT the warmed flag — GetAll took the warmed path and returned {} for
// the agent forever, hiding every replicated row.
int TestCacheInvalidationReloads() {
    std::cerr << "  [P3] remote apply invalidates warmed GetAll cache\n";
    Node a(1), b(2);

    // Warm b's cache for 'default' (empty at this point).
    const auto before = b.configStore.GetAll("default");
    Assert(before.empty(), "b cache warm on empty slice");

    // Replicate two rows from a.
    a.configStore.Set("default", "channel.t.type", "telegram");
    a.configStore.Set("default", "channel.t.config", "{\"n\":1}");
    const int applied = b.PullFrom(a);
    Assert(applied == 2, "2 config rows applied to b, got " +
                             std::to_string(applied));

    // The fix under test: warmed-but-invalidated slice must RELOAD, not
    // return the erased empty map.
    const auto after = b.configStore.GetAll("default");
    Assert(after.size() == 2, "GetAll after remote apply sees replicated rows"
                              " (got " + std::to_string(after.size()) + ")");
    Assert(after.count("channel.t.type") && after.at("channel.t.type") == "telegram",
           "reloaded slice carries the replicated value");

    // Single-key reads reload too (GetRaw via cache-absent path).
    Assert(b.configStore.GetRaw("default", "channel.t.type") == "telegram",
           "GetRaw sees replicated row after invalidation");
    return 0;
}

int main() {
    std::cerr << "\n=== Outbox Sync Tests (#78 P1b) ===\n\n";
    TestTriggerCoverage();
    TestTwoNodeReplication();
    TestLwwPairSemantics();
    TestLocalWritesDuringApply();
    TestPeerCursors();
    TestIdempotentBoot();
    TestSchedulerTableReplication();
    TestLeaseReplicationAndFencing();
    TestLeaseGateFireLoop();
    TestConfigReplication();
    TestSecretsNeverInPayloads();
    TestP3DigestAndHandshake();
    TestCacheInvalidationReloads();
    if (g_failures == 0) std::cerr << "\nAll outbox sync tests passed.\n";
    else std::cerr << "\n" << g_failures << " test assertion(s) FAILED.\n";
    return g_failures == 0 ? 0 : 1;
}

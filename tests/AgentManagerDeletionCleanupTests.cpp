// #214 — agent deletion cleanup binds the right identity per table.
//
// Bug (verified at dev 994dd4a): AgentManager::DeleteAgent's cleanup loop ran
// DELETE FROM <table> WHERE agent_id=? binding the TEXT agent id for every
// table. Two type errors:
//   * memory_files.agent_id is INTEGER (Agent::numeric_id) — the TEXT bind
//     matched nothing, so a deleted agent's files, chunks and embeddings
//     outlived the agent (the chunk cascade never fired because no file row
//     was ever matched).
//   * ontology_properties carried 'default' on every row (writer bug, since
//     landed by #215/eb3d147), so cleanup deleted zero rows — or EVERY agent's
//     properties if the deleted agent's id was literally "default".
//
// Decided semantics (this fix):
//   * memory_files keeps its numeric representation; cleanup binds
//     Agent::numeric_id at that table (never 0 — that is the shared/global
//     sentinel, #225/#232).
//   * The four 'default'-sentinel tables (ontology_properties,
//     ontology_entities, gallivanting_sessions, gallivanting_threads) are
//     skipped when the deleted agent's id is literally "default": those rows
//     are indistinguishable from shared rows and belong to no single agent.
//     (DeleteAgent has refused id=="default" with 403 since the initial
//     commit, so the loop-level guard is defense-in-depth, not the fix.)
//   * Per-table deleted-row counts are logged unconditionally (zeros
//     included); Prepare failures are logged loudly instead of swallowed.

#include "animus_kernel/AgentStore.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/MemoryFileStore.h"
#include "animus_kernel/OntologyStore.h"
#include "animus_kernel/admin/AgentManager.h"
#include "animus_kernel/admin/DiaryManager.h"
#include "animus_kernel/SqliteDataStore.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace animus::kernel;
using namespace animus::kernel::memory;
using animus::kernel::AgentManager;
using animus::kernel::DiaryStore;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_214_cleanup_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

int64_t CountRows(SqliteDataStore& db, const std::string& sql) {
    auto stmt = db.Prepare(sql);
    if (!stmt || !stmt->Step()) return -1;
    return stmt->ColumnInt64(0);
}

// Acceptance leg 1: create agent → write ontology property + memory file →
// delete agent → property, file, and chunks are all gone.
int TestAgentScopedRowsDieWithAgent() {
    std::cerr << "  [214] agent-scoped rows die with the agent...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);

    AgentStore agentStore(&dataStore);
    MemoryStore memoryStore(&dataStore);
    MemoryFileStore fileStore(&dataStore);
    ontology::OntologyStore ontologyStore(&dataStore);
    DiaryStore diaryStore(&dataStore);

    Agent agent;
    agent.name = "cleanup-victim";
    agent.identity = "test agent";
    auto created = agentStore.Create(agent);
    Assert(created.id.size() > 0, "agent created with id");
    Assert(created.numeric_id > 0, "agent numeric_id populated");

    // Ontology property scoped to this agent (writer side binds agent_id
    // since #215).
    ontology::OntologyEntity entity;
    entity.name = "Entity";
    entity.full_path = "/Concepts/Entity";
    entity.agent_id = created.id;
    auto ent = ontologyStore.CreateEntity(entity);
    Assert(ent.id > 0, "entity created");

    ontology::OntologyProperty prop;
    prop.entity_id = ent.id;
    prop.key = "k";
    prop.value = "v";
    prop.agent_id = created.id;
    auto written = ontologyStore.SetProperty(prop);
    Assert(written.id > 0, "property written");

    // Raw check: the property row actually carries the agent's TEXT id
    // (guards against a regression to the pre-#215 writer).
    {
        auto stmt = dataStore.Prepare(
            "SELECT COUNT(*) FROM ontology_properties WHERE agent_id=?");
        stmt->BindText(1, created.id);
        stmt->Step();
        Assert(stmt->ColumnInt64(0) == 1,
               "property row carries the agent's TEXT agent_id");
    }

    // Memory file + chunk scoped to this agent's NUMERIC id.
    MemoryFile file;
    file.source_path = "consolidation/test.md";
    file.file_type = MemoryFileType::Journal;
    file.content = "# Test\n\nhello";
    file.agent_id = created.numeric_id;
    auto saved = fileStore.CreateFile(file);
    Assert(saved.id > 0, "memory file created");

    MemoryFileChunk chunk;
    chunk.file_id = saved.id;
    chunk.source_path = file.source_path;
    chunk.content = "hello";
    chunk.content_hash = "h1";
    chunk.chunk_index = 0;
    fileStore.ReplaceChunks(saved.id, {chunk});
    Assert(CountRows(dataStore,
                     "SELECT COUNT(*) FROM memory_file_chunks WHERE file_id=" +
                         std::to_string(saved.id)) == 1,
           "chunk created");

    // Diary entry: TEXT-keyed, present so the cleanup loop demonstrably runs.
    DiaryEntry entry;
    entry.agent_id = created.id;
    entry.content = "note";
    entry.timestamp_unix_ms = 1;
    diaryStore.Create(entry);
    Assert(diaryStore.CountByAgent(created.id) == 1, "diary entry created");

    // Delete the agent through AgentManager (the code under test).
    AgentManager manager;
    manager.Configure(&agentStore, /*providerManager=*/nullptr, &memoryStore);
    auto result = manager.DeleteAgent(created.id);
    Assert(result.statusCode == 200,
           "delete succeeded (status " + std::to_string(result.statusCode) +
               " " + result.error + ")");

    // Property, file, chunks, diary all gone.
    Assert(CountRows(dataStore,
                     "SELECT COUNT(*) FROM ontology_properties WHERE agent_id='" +
                         created.id + "'") == 0,
           "ontology property deleted");
    Assert(CountRows(dataStore,
                     "SELECT COUNT(*) FROM memory_files WHERE agent_id=" +
                         std::to_string(created.numeric_id)) == 0,
           "memory file deleted (numeric bind)");
    Assert(CountRows(dataStore, "SELECT COUNT(*) FROM memory_file_chunks") == 0,
           "chunks cascaded away with the file");
    Assert(diaryStore.CountByAgent(created.id) == 0, "diary entries deleted");

    // The agent row itself is gone.
    Assert(!agentStore.GetById(created.id).has_value(), "agent row gone");

    std::cerr << "  [214] leg 1 done\n";
    return 0;
}

// Acceptance leg 2: an agent whose id is literally "default" cannot take
// other agents' properties with it.
//
// Layered answer: DeleteAgent has refused id=="default" with 403 since the
// initial commit (the only agent-deletion path), so the catastrophic delete
// is already impossible at the API layer. The cleanup loop's skip guard
// (AgentManager.cpp) is SQL-layer defense-in-depth for the day a second
// deletion path appears; it is not reachable through the public API while
// the 403 stands, so this leg asserts the 403 contract.
int TestDefaultAgentCannotTakeSharedRows() {
    std::cerr << "  [214] deleting agent \"default\" is refused, shared rows survive...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);

    AgentStore agentStore(&dataStore);
    MemoryStore memoryStore(&dataStore);
    ontology::OntologyStore ontologyStore(&dataStore);

    // A normal agent with scoped ontology data.
    Agent normal;
    normal.name = "normal-agent";
    auto normalAgent = agentStore.Create(normal);
    Assert(normalAgent.id.size() > 0 && normalAgent.numeric_id > 0,
           "normal agent created");

    ontology::OntologyEntity entity;
    entity.name = "Shared";
    entity.full_path = "/Concepts/Shared";
    entity.agent_id = "default";
    auto sharedEntity = ontologyStore.CreateEntity(entity);
    Assert(sharedEntity.id > 0, "shared entity created");

    ontology::OntologyProperty prop;
    prop.entity_id = sharedEntity.id;
    prop.key = "shared-key";
    prop.value = "shared-value";
    prop.agent_id = "default";
    auto sharedProp = ontologyStore.SetProperty(prop);
    Assert(sharedProp.id > 0, "shared property written");

    // The pathological agent: id literally "default".
    Agent pathological;
    pathological.id = "default";
    pathological.name = "pathological";
    auto defaultAgent = agentStore.Create(pathological);
    Assert(defaultAgent.id == "default", "agent with id 'default' created");

    // Deletion is refused (403) — the catastrophic leg is impossible.
    AgentManager manager;
    manager.Configure(&agentStore, /*providerManager=*/nullptr, &memoryStore);
    auto result = manager.DeleteAgent("default");
    Assert(result.statusCode == 403,
           "delete of agent 'default' refused with 403 (got " +
               std::to_string(result.statusCode) + " " + result.error + ")");

    // Nothing moved: shared rows and the agent row all survive.
    Assert(CountRows(dataStore,
                     "SELECT COUNT(*) FROM ontology_properties WHERE key='shared-key'") == 1,
           "shared property survives");
    Assert(CountRows(dataStore,
                     "SELECT COUNT(*) FROM ontology_entities WHERE id=" +
                         std::to_string(sharedEntity.id)) == 1,
           "shared entity survives");
    Assert(agentStore.GetById("default").has_value(),
           "agent 'default' still exists");

    std::cerr << "  [214] leg 2 done\n";
    return 0;
}

} // namespace

int main() {
    int r = 0;
    r |= TestAgentScopedRowsDieWithAgent();
    r |= TestDefaultAgentCannotTakeSharedRows();
    if (g_failures > 0) {
        std::cerr << "Animus214AgentDeletionCleanupTests: " << g_failures
                  << " failure(s)\n";
        return 1;
    }
    std::cerr << "Animus214AgentDeletionCleanupTests: all assertions passed\n";
    return 0;
}
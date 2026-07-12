#include "animus_kernel/admin/DiaryManager.h"
#include "animus_kernel/admin/AiDiaryFormat.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/tools/DiaryTool.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <json/json.h>
#include <json/reader.h>
#include <json/writer.h>

using namespace animus::kernel;

namespace {

int g_failures = 0;

int Fail(int code, const std::string& message) {
    std::cerr << "FAIL: " << message << " (code " << code << ")\n";
    g_failures++;
    return code;
}

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_diary_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

// ============================================================================
// DiaryStore tests
// ============================================================================

int TestCreateAndRetrieve() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);

    DiaryEntry entry;
    entry.id = "diary_test_001";
    entry.agent_id = "agent1";
    entry.timestamp_unix_ms = 1700000000000;
    entry.layer = "reflection";
    entry.content = "Test reflection content";
    entry.tags_json = "[\"test\"]";
    entry.session_id = "session1";

    auto created = diaryStore.Create(entry);
    if (created.id != "diary_test_001") return Fail(1, "Create returned wrong id");

    auto retrieved = diaryStore.GetById("diary_test_001");
    if (!retrieved.has_value()) return Fail(2, "GetById returned nullopt");
    if (retrieved->agent_id != "agent1") return Fail(3, "Wrong agent_id");
    if (retrieved->content != "Test reflection content") return Fail(4, "Wrong content");
    if (retrieved->layer != "reflection") return Fail(5, "Wrong layer");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestListByAgent() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);

    for (int i = 0; i < 5; ++i) {
        DiaryEntry entry;
        entry.id = "diary_list_" + std::to_string(i);
        entry.agent_id = "agent1";
        entry.timestamp_unix_ms = 1700000000000 + i * 1000;
        entry.layer = "reflection";
        entry.content = "Entry " + std::to_string(i);
        entry.tags_json = "[]";
        diaryStore.Create(entry);
    }

    auto entries = diaryStore.ListByAgent("agent1");
    if (entries.size() != 5) return Fail(1, "Expected 5 entries, got " + std::to_string(entries.size()));
    if (entries[0].content != "Entry 4") return Fail(2, "Expected reverse chronological order");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestListByAgentFiltersByLayer() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);

    DiaryEntry r1;
    r1.id = "diary_f1"; r1.agent_id = "a1"; r1.timestamp_unix_ms = 1000;
    r1.layer = "reflection"; r1.content = "reflection"; r1.tags_json = "[]";
    diaryStore.Create(r1);

    DiaryEntry o1;
    o1.id = "diary_f2"; o1.agent_id = "a1"; o1.timestamp_unix_ms = 2000;
    o1.layer = "observation"; o1.content = "observation"; o1.tags_json = "[]";
    diaryStore.Create(o1);

    auto reflections = diaryStore.ListByAgent("a1", 0, 0, "reflection");
    if (reflections.size() != 1) return Fail(1, "Expected 1 reflection");
    if (reflections[0].content != "reflection") return Fail(2, "Wrong reflection content");

    auto observations = diaryStore.ListByAgent("a1", 0, 0, "observation");
    if (observations.size() != 1) return Fail(3, "Expected 1 observation");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestSearchByAgent() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);

    DiaryEntry e1;
    e1.id = "diary_s1"; e1.agent_id = "a1"; e1.timestamp_unix_ms = 1000;
    e1.layer = "reflection"; e1.content = "I learned about neural networks today"; e1.tags_json = "[]";
    diaryStore.Create(e1);

    DiaryEntry e2;
    e2.id = "diary_s2"; e2.agent_id = "a1"; e2.timestamp_unix_ms = 2000;
    e2.layer = "observation"; e2.content = "The weather is cloudy"; e2.tags_json = "[]";
    diaryStore.Create(e2);

    auto results = diaryStore.SearchByAgent("a1", "neural");
    if (results.size() != 1) return Fail(1, "Expected 1 search result");
    if (results[0].content != "I learned about neural networks today") return Fail(2, "Wrong search result");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestDeleteEntry() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);

    DiaryEntry e;
    e.id = "diary_del1"; e.agent_id = "a1"; e.timestamp_unix_ms = 1000;
    e.layer = "reflection"; e.content = "to delete"; e.tags_json = "[]";
    diaryStore.Create(e);

    if (!diaryStore.Delete("diary_del1")) return Fail(1, "Delete returned false");
    if (diaryStore.GetById("diary_del1").has_value()) return Fail(2, "Entry still exists after delete");
    if (diaryStore.Delete("nonexistent")) return Fail(3, "Delete nonexistent should return false");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestCountAndLastTimestamp() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);

    if (diaryStore.CountByAgent("a1") != 0) return Fail(1, "Expected 0 count");
    if (diaryStore.LastEntryTimestampByAgent("a1").has_value()) return Fail(2, "Expected nullopt for empty");

    for (int i = 0; i < 3; ++i) {
        DiaryEntry e;
        e.id = "diary_cnt_" + std::to_string(i);
        e.agent_id = "a1"; e.timestamp_unix_ms = 1000 + i * 1000;
        e.layer = "reflection"; e.content = "entry"; e.tags_json = "[]";
        diaryStore.Create(e);
    }

    if (diaryStore.CountByAgent("a1") != 3) return Fail(3, "Expected 3 count");
    auto last = diaryStore.LastEntryTimestampByAgent("a1");
    if (!last.has_value() || *last != 3000) return Fail(4, "Expected last timestamp 3000");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestMultiTenantIsolation() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);

    DiaryEntry e1;
    e1.id = "diary_mt1"; e1.agent_id = "agentA"; e1.timestamp_unix_ms = 1000;
    e1.layer = "reflection"; e1.content = "Agent A private"; e1.tags_json = "[]";
    diaryStore.Create(e1);

    DiaryEntry e2;
    e2.id = "diary_mt2"; e2.agent_id = "agentB"; e2.timestamp_unix_ms = 2000;
    e2.layer = "reflection"; e2.content = "Agent B private"; e2.tags_json = "[]";
    diaryStore.Create(e2);

    auto aEntries = diaryStore.ListByAgent("agentA");
    if (aEntries.size() != 1 || aEntries[0].content != "Agent A private")
        return Fail(1, "Agent A isolation failed");

    auto bEntries = diaryStore.ListByAgent("agentB");
    if (bEntries.size() != 1 || bEntries[0].content != "Agent B private")
        return Fail(2, "Agent B isolation failed");

    std::filesystem::remove(dbPath);
    return 0;
}

// ============================================================================
// DiaryManager tests
// ============================================================================

int TestManagerWriteEntry() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);
    DiaryManager manager;
    manager.Configure(&diaryStore);

    Json::Value body;
    body["content"] = "First diary entry";
    body["layer"] = "reflection";

    auto result = manager.WriteEntry("agent1", body);
    if (result.httpStatusCode != 201) return Fail(1, "Expected 201, got " + std::to_string(result.httpStatusCode));
    if (!result.body.isMember("id")) return Fail(2, "Missing id in response");
    if (result.body["content"].asString() != "First diary entry") return Fail(3, "Wrong content");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestManagerWriteValidatesContent() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);
    DiaryManager manager;
    manager.Configure(&diaryStore);

    Json::Value body; // No content field
    auto result = manager.WriteEntry("agent1", body);
    if (result.httpStatusCode != 400) return Fail(1, "Expected 400 for missing content");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestManagerReadAndSearch() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);
    DiaryManager manager;
    manager.Configure(&diaryStore);

    for (int i = 0; i < 3; ++i) {
        Json::Value body;
        body["content"] = "Entry " + std::to_string(i);
        body["layer"] = "reflection";
        manager.WriteEntry("agent1", body);
    }

    auto readResult = manager.ReadEntries("agent1", "", 0, 0, 100);
    if (readResult.httpStatusCode != 200) return Fail(1, "Read failed");
    if (readResult.body["count"].asInt() != 3) return Fail(2, "Expected 3 entries");

    // Write one with searchable content
    Json::Value body;
    body["content"] = "I explored the Lenia simulation parameters";
    manager.WriteEntry("agent1", body);

    auto searchResult = manager.SearchEntries("agent1", "Lenia", 1, 50);
    if (searchResult.httpStatusCode != 200) return Fail(3, "Search failed");
    if (searchResult.body["count"].asInt() < 1) return Fail(4, "Expected at least 1 search result");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestManagerDeleteEntry() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);
    DiaryManager manager;
    manager.Configure(&diaryStore);

    Json::Value body;
    body["content"] = "To be deleted";
    auto writeResult = manager.WriteEntry("agent1", body);
    std::string entryId = writeResult.body["id"].asString();

    auto deleteResult = manager.DeleteEntry("agent1", entryId);
    if (deleteResult.httpStatusCode != 200) return Fail(1, "Delete failed");
    if (deleteResult.body["deleted"].asString() != entryId) return Fail(2, "Wrong deleted id");

    // Try deleting with wrong agent
    Json::Value body2;
    body2["content"] = "Agent 1 entry";
    auto writeResult2 = manager.WriteEntry("agent1", body2);
    std::string entryId2 = writeResult2.body["id"].asString();

    auto deleteResult2 = manager.DeleteEntry("agent2", entryId2);
    if (deleteResult2.httpStatusCode != 403) return Fail(3, "Expected 403 for wrong agent");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestManagerMetadata() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);
    DiaryManager manager;
    manager.Configure(&diaryStore);

    auto meta = manager.GetMetadata("agent1");
    if (meta.httpStatusCode != 200) return Fail(1, "Metadata failed");
    if (meta.body["exists"].asBool() != false) return Fail(2, "Expected exists=false for empty");
    if (meta.body["entry_count"].asInt() != 0) return Fail(3, "Expected count=0 for empty");

    Json::Value body;
    body["content"] = "First entry";
    manager.WriteEntry("agent1", body);

    meta = manager.GetMetadata("agent1");
    if (meta.body["exists"].asBool() != true) return Fail(4, "Expected exists=true");
    if (meta.body["entry_count"].asInt() != 1) return Fail(5, "Expected count=1");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestManagerExportImport() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);
    DiaryManager manager;
    manager.Configure(&diaryStore);

    // Write entries
    for (int i = 0; i < 3; ++i) {
        Json::Value body;
        body["content"] = "Export entry " + std::to_string(i);
        manager.WriteEntry("agent1", body);
    }

    // Export
    auto exported = manager.ExportEntriesJson("agent1");
    if (exported.size() != 3) return Fail(1, "Expected 3 exported entries");

    // Import with merge to different agent
    auto importResult = manager.ImportEntriesJson("agent2", exported, "merge");
    if (importResult.httpStatusCode != 200) return Fail(2, "Import failed");
    if (importResult.body["imported"].asInt() != 3) return Fail(3, "Expected 3 imported");

    auto agent2Meta = manager.GetMetadata("agent2");
    if (agent2Meta.body["entry_count"].asInt() != 3) return Fail(4, "Agent2 should have 3 entries");

    // Test replace strategy
    Json::Value importEntries(Json::arrayValue);
    Json::Value e1; e1["content"] = "Replacement 1"; importEntries.append(e1);
    Json::Value e2; e2["content"] = "Replacement 2"; importEntries.append(e2);

    auto replaceResult = manager.ImportEntriesJson("agent2", importEntries, "replace");
    if (replaceResult.body["imported"].asInt() != 2) return Fail(5, "Expected 2 replaced entries");

    auto metaAfter = manager.GetMetadata("agent2");
    if (metaAfter.body["entry_count"].asInt() != 2) return Fail(6, "Expected 2 entries after replace");

    std::filesystem::remove(dbPath);
    return 0;
}

// ============================================================================
// AiDiaryFormat tests
// ============================================================================

int TestAiDiaryEncryptDecrypt() {
    Json::Value payload(Json::objectValue);
    payload["version"] = 1;
    payload["agent_id"] = "test-agent";
    payload["entries"] = Json::Value(Json::arrayValue);

    Json::Value entry;
    entry["id"] = "diary_001";
    entry["timestamp"] = 1700000000000;
    entry["layer"] = "reflection";
    entry["content"] = "Test diary entry for encryption round-trip";
    entry["tags"] = Json::Value(Json::arrayValue);
    entry["session_id"] = "";
    payload["entries"].append(entry);

    std::string secret = "test-agent-secret-key";
    auto encrypted = AiDiaryFormat::Encrypt(payload, secret);
    if (encrypted.size() == 0) return Fail(1, "Encrypt returned empty");
    if (!AiDiaryFormat::IsAiDiaryContainer(encrypted)) return Fail(2, "Not recognized as AIDI container");

    auto decrypted = AiDiaryFormat::Decrypt(encrypted, secret);
    if (!decrypted.has_value()) return Fail(3, "Decrypt returned nullopt");
    if ((*decrypted)["entries"].size() != 1) return Fail(4, "Wrong entry count after decrypt");
    if ((*decrypted)["entries"][0]["content"].asString() != "Test diary entry for encryption round-trip")
        return Fail(5, "Content mismatch after decrypt");

    return 0;
}

int TestAiDiaryWrongKeyFails() {
    Json::Value payload(Json::objectValue);
    payload["version"] = 1;
    payload["entries"] = Json::Value(Json::arrayValue);

    auto encrypted = AiDiaryFormat::Encrypt(payload, "correct-secret");
    auto decrypted = AiDiaryFormat::Decrypt(encrypted, "wrong-secret");
    if (decrypted.has_value()) return Fail(1, "Decryption with wrong key should fail");

    return 0;
}

int TestAiDiaryCorruptedDataFails() {
    Json::Value payload(Json::objectValue);
    payload["version"] = 1;
    auto encrypted = AiDiaryFormat::Encrypt(payload, "secret");

    // Corrupt the ciphertext
    if (encrypted.size() > 70) {
        encrypted[70] ^= 0xFF;
    }

    auto decrypted = AiDiaryFormat::Decrypt(encrypted, "secret");
    if (decrypted.has_value()) return Fail(1, "Corrupted data should fail to decrypt");

    return 0;
}

// ============================================================================
// DiaryTool tests
// ============================================================================

int TestDiaryToolWriteAndRead() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);
    DiaryManager manager;
    manager.Configure(&diaryStore);
    DiaryTool tool(&manager);

    // Write
    ToolCall writeCall;
    writeCall.id = "call_1";
    writeCall.name = "diary";
    writeCall.arguments = R"({"action":"write","content":"Hello diary","layer":"reflection","__agent_id":"agent1"})";

    auto writeResult = tool.Execute(writeCall);
    if (!writeResult.success) return Fail(1, "Write failed: " + writeResult.error);

    // Read
    ToolCall readCall;
    readCall.id = "call_2";
    readCall.name = "diary";
    readCall.arguments = R"({"action":"read","__agent_id":"agent1"})";

    auto readResult = tool.Execute(readCall);
    if (!readResult.success) return Fail(2, "Read failed: " + readResult.error);

    // Search
    ToolCall searchCall;
    searchCall.id = "call_3";
    searchCall.name = "diary";
    searchCall.arguments = R"({"action":"search","query":"Hello","__agent_id":"agent1"})";

    auto searchResult = tool.Execute(searchCall);
    if (!searchResult.success) return Fail(3, "Search failed: " + searchResult.error);

    std::filesystem::remove(dbPath);
    return 0;
}

int TestDiaryToolMissingAction() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);
    DiaryManager manager;
    manager.Configure(&diaryStore);
    DiaryTool tool(&manager);

    ToolCall call;
    call.id = "call_1";
    call.name = "diary";
    call.arguments = R"({"content":"no action specified","__agent_id":"agent1"})";

    auto result = tool.Execute(call);
    if (result.success) return Fail(1, "Missing action should fail");

    std::filesystem::remove(dbPath);
    return 0;
}

// ============================================================================
// List + pagination tests
// ============================================================================

int TestManagerListEntries() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);
    DiaryManager manager;
    manager.Configure(&diaryStore);

    for (int i = 0; i < 5; ++i) {
        Json::Value body;
        body["content"] = "List entry " + std::to_string(i);
        body["layer"] = (i % 2 == 0) ? "reflection" : "observation";
        manager.WriteEntry("agent1", body);
    }

    // Page 1, limit 2
    auto result = manager.ListEntries("agent1", "", 1, 2);
    if (result.httpStatusCode != 200) return Fail(1, "List failed");
    if (result.body["count"].asInt() != 2) return Fail(2, "Expected 2 entries on page");
    if (result.body["total"].asInt() != 5) return Fail(3, "Expected total 5");
    if (result.body["page"].asInt() != 1) return Fail(4, "Expected page 1");
    if (result.body["total_pages"].asInt() != 3) return Fail(5, "Expected 3 total_pages");

    // Page 2
    auto result2 = manager.ListEntries("agent1", "", 2, 2);
    if (result2.body["count"].asInt() != 2) return Fail(6, "Expected 2 entries on page 2");

    // Page 3 (last page, 1 entry)
    auto result3 = manager.ListEntries("agent1", "", 3, 2);
    if (result3.body["count"].asInt() != 1) return Fail(7, "Expected 1 entry on page 3");

    // Beyond last page
    auto result4 = manager.ListEntries("agent1", "", 4, 2);
    if (result4.body["count"].asInt() != 0) return Fail(8, "Expected 0 entries beyond last page");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestManagerListWithLayerFilter() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);
    DiaryManager manager;
    manager.Configure(&diaryStore);

    for (int i = 0; i < 4; ++i) {
        Json::Value body;
        body["content"] = "Reflection " + std::to_string(i);
        body["layer"] = "reflection";
        manager.WriteEntry("agent1", body);
    }
    for (int i = 0; i < 2; ++i) {
        Json::Value body;
        body["content"] = "Observation " + std::to_string(i);
        body["layer"] = "observation";
        manager.WriteEntry("agent1", body);
    }

    auto reflections = manager.ListEntries("agent1", "reflection", 1, 10);
    if (reflections.body["count"].asInt() != 4) return Fail(1, "Expected 4 reflections");
    if (reflections.body["total"].asInt() != 4) return Fail(2, "Total should be 4 for reflections");

    auto observations = manager.ListEntries("agent1", "observation", 1, 10);
    if (observations.body["count"].asInt() != 2) return Fail(3, "Expected 2 observations");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestManagerSearchPagination() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);
    DiaryManager manager;
    manager.Configure(&diaryStore);

    for (int i = 0; i < 5; ++i) {
        Json::Value body;
        body["content"] = "Neural network experiment " + std::to_string(i);
        manager.WriteEntry("agent1", body);
    }

    // Page 1, limit 2
    auto result = manager.SearchEntries("agent1", "Neural", 1, 2);
    if (result.httpStatusCode != 200) return Fail(1, "Search failed");
    if (result.body["count"].asInt() != 2) return Fail(2, "Expected 2 results on page 1");
    if (result.body["page"].asInt() != 1) return Fail(3, "Expected page 1");

    // Page 2
    auto result2 = manager.SearchEntries("agent1", "Neural", 2, 2);
    if (result2.body["count"].asInt() != 2) return Fail(4, "Expected 2 results on page 2");

    // Page 3
    auto result3 = manager.SearchEntries("agent1", "Neural", 3, 2);
    if (result3.body["count"].asInt() != 1) return Fail(5, "Expected 1 result on page 3");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestDiaryToolListAction() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);
    DiaryManager manager;
    manager.Configure(&diaryStore);
    DiaryTool tool(&manager);

    for (int i = 0; i < 3; ++i) {
        ToolCall writeCall;
        writeCall.id = "call_w" + std::to_string(i);
        writeCall.name = "diary";
        writeCall.arguments = "{\"action\":\"write\",\"content\":\"Entry " + std::to_string(i) + "\",\"__agent_id\":\"agent1\"}";
        tool.Execute(writeCall);
    }

    // List
    ToolCall listCall;
    listCall.id = "call_l1";
    listCall.name = "diary";
    listCall.arguments = "{\"action\":\"list\",\"page\":1,\"limit\":2,\"__agent_id\":\"agent1\"}";

    auto listResult = tool.Execute(listCall);
    if (!listResult.success) return Fail(1, "List failed: " + listResult.error);

    // Verify the result JSON contains pagination fields
    Json::Value parsed;
    Json::CharReaderBuilder rb;
    std::istringstream stream(listResult.output);
    std::string parseErr;
    if (!Json::parseFromStream(rb, stream, &parsed, &parseErr)) return Fail(2, "Failed to parse list result");

    if (!parsed["data"].isMember("total")) return Fail(3, "Missing total field");
    if (!parsed["data"].isMember("page")) return Fail(4, "Missing page field");
    if (!parsed["data"].isMember("total_pages")) return Fail(5, "Missing total_pages field");
    if (parsed["data"]["total"].asInt() != 3) return Fail(6, "Expected total 3");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestStoreListByAgentPagination() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);

    for (int i = 0; i < 10; ++i) {
        DiaryEntry entry;
        entry.id = "diary_pag_" + std::to_string(i);
        entry.agent_id = "a1";
        entry.timestamp_unix_ms = 1000 + i * 100;
        entry.layer = "reflection";
        entry.content = "Page entry " + std::to_string(i);
        entry.tags_json = "[]";
        diaryStore.Create(entry);
    }

    // Page 1 (offset 0, limit 3)
    auto page1 = diaryStore.ListByAgent("a1", 0, 0, "", 3, 0);
    if (page1.size() != 3) return Fail(1, "Expected 3 entries on page 1");

    // Page 2 (offset 3, limit 3)
    auto page2 = diaryStore.ListByAgent("a1", 0, 0, "", 3, 3);
    if (page2.size() != 3) return Fail(2, "Expected 3 entries on page 2");

    // Last page (offset 9, limit 3) — only 1 entry left
    auto page4 = diaryStore.ListByAgent("a1", 0, 0, "", 3, 9);
    if (page4.size() != 1) return Fail(3, "Expected 1 entry on last page");

    // Verify ordering (reverse chronological)
    if (page1[0].content != "Page entry 9") return Fail(4, "First entry should be newest");
    if (page2[0].content != "Page entry 6") return Fail(5, "Page 2 first should be entry 6");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestStoreSearchByAgentPagination() {
    auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    DiaryStore diaryStore(&dataStore);

    for (int i = 0; i < 5; ++i) {
        DiaryEntry entry;
        entry.id = "diary_spag_" + std::to_string(i);
        entry.agent_id = "a1";
        entry.timestamp_unix_ms = 1000 + i * 100;
        entry.layer = "reflection";
        entry.content = "Searchable topic " + std::to_string(i);
        entry.tags_json = "[]";
        diaryStore.Create(entry);
    }

    auto page1 = diaryStore.SearchByAgent("a1", "topic", 2, 0);
    if (page1.size() != 2) return Fail(1, "Expected 2 search results on page 1");

    auto page2 = diaryStore.SearchByAgent("a1", "topic", 2, 2);
    if (page2.size() != 2) return Fail(2, "Expected 2 search results on page 2");

    auto page3 = diaryStore.SearchByAgent("a1", "topic", 2, 4);
    if (page3.size() != 1) return Fail(3, "Expected 1 search result on page 3");

    std::filesystem::remove(dbPath);
    return 0;
}

} // anonymous namespace

int main() {
    int total = 0;
    int failed = 0;

    #define RUN(test) do { \
        std::cerr << "Running " #test "..." << std::endl; \
        int rc = test(); \
        total++; \
        if (rc != 0) { failed++; std::cerr << "  FAILED (code " << rc << ")" << std::endl; } \
        else { std::cerr << "  OK" << std::endl; } \
    } while(0)

    RUN(TestCreateAndRetrieve);
    RUN(TestListByAgent);
    RUN(TestListByAgentFiltersByLayer);
    RUN(TestSearchByAgent);
    RUN(TestDeleteEntry);
    RUN(TestCountAndLastTimestamp);
    RUN(TestMultiTenantIsolation);
    RUN(TestManagerWriteEntry);
    RUN(TestManagerWriteValidatesContent);
    RUN(TestManagerReadAndSearch);
    RUN(TestManagerDeleteEntry);
    RUN(TestManagerMetadata);
    RUN(TestManagerExportImport);
    RUN(TestAiDiaryEncryptDecrypt);
    RUN(TestAiDiaryWrongKeyFails);
    RUN(TestAiDiaryCorruptedDataFails);
    RUN(TestDiaryToolWriteAndRead);
    RUN(TestDiaryToolMissingAction);
    RUN(TestManagerListEntries);
    RUN(TestManagerListWithLayerFilter);
    RUN(TestManagerSearchPagination);
    RUN(TestDiaryToolListAction);
    RUN(TestStoreListByAgentPagination);
    RUN(TestStoreSearchByAgentPagination);

    #undef RUN

    std::cerr << "\n" << total << " tests, " << failed << " failures" << std::endl;
    return failed;
}
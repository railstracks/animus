#include "animus_kernel/MemoryFileStore.h"
#include "animus_kernel/SqliteDataStore.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

using namespace animus::kernel;
using namespace animus::kernel::memory;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_memory_file_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

int TestMemoryFileCrudAndMutabilityRules() {
    std::cerr << "  [MemoryFileStore] CRUD + content mutability policy...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);
    MemoryFileStore store(&dataStore);

    MemoryFile file;
    file.source_path = "/imports/reference-a.txt";
    file.file_type = MemoryFileType::ExternalDoc;
    file.content = "alpha memory artifact";
    file.content_mutable = true;
    file.agent_id = 7;
    auto created = store.CreateFile(file);
    Assert(created.id > 0, "CreateFile should return persistent row");
    Assert(created.content == "alpha memory artifact", "created content mismatch");

    auto listAll = store.ListFiles();
    Assert(listAll.size() == 1, "ListFiles should return 1 row");

    auto listExternal = store.ListFiles(MemoryFileType::ExternalDoc);
    Assert(listExternal.size() == 1, "typed list should include created row");

    auto fetched = store.GetFile(created.id);
    Assert(fetched.has_value(), "GetFile should return created row");
    Assert(fetched->source_path == "/imports/reference-a.txt", "source_path mismatch");

    fetched->content = "updated mutable content";
    fetched->source_path = "/imports/reference-a-v2.txt";
    fetched->superseded = true;
    fetched->agent_id = 11;
    fetched->content_mutable = true;
    Assert(store.UpdateFile(*fetched), "metadata update should succeed");

    auto afterUpdate = store.GetFile(created.id);
    Assert(afterUpdate.has_value(), "GetFile after update should succeed");
    Assert(afterUpdate->source_path == "/imports/reference-a-v2.txt", "source_path should update");
    Assert(afterUpdate->superseded, "superseded flag should update");
    Assert(afterUpdate->agent_id == 11, "agent_id should update");
    Assert(afterUpdate->content == "updated mutable content", "content should update when mutable");
    Assert(afterUpdate->content_mutable, "content_mutable should remain true");

    afterUpdate->content_mutable = false;
    Assert(store.UpdateFile(*afterUpdate), "disabling content mutability should succeed");

    auto immutableNow = store.GetFile(created.id);
    Assert(immutableNow.has_value(), "file should still exist after disabling mutability");
    Assert(!immutableNow->content_mutable, "content_mutable should be false");

    immutableNow->content = "blocked mutation attempt";
    Assert(!store.UpdateFile(*immutableNow), "content update should fail when immutable");

    auto afterBlockedMutation = store.GetFile(created.id);
    Assert(afterBlockedMutation.has_value(), "file should still exist after blocked mutation");
    Assert(afterBlockedMutation->content == "updated mutable content",
           "content should remain unchanged after blocked mutation");

    Assert(store.CountByType(MemoryFileType::ExternalDoc) == 1, "count by type should match");

    Assert(store.DeleteFile(created.id), "DeleteFile should succeed");
    auto missing = store.GetFile(created.id);
    Assert(!missing.has_value(), "deleted row should not exist");

    std::filesystem::remove(dbPath);
    return 0;
}

} // namespace

int main() {
    std::cerr << "\n=== MemoryFile Store Tests ===\n\n";

    TestMemoryFileCrudAndMutabilityRules();

    std::cerr << "\n";
    if (g_failures == 0) {
        std::cerr << "All memory file store tests passed.\n";
    } else {
        std::cerr << g_failures << " test(s) FAILED.\n";
    }
    std::cerr << "\n";

    return g_failures;
}

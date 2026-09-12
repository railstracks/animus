#include "animus_kernel/MemoryFileStore.h"
#include "animus_kernel/MemorySearch.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/OntologyStore.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/admin/DiaryManager.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

using namespace animus::kernel;
using namespace animus::kernel::memory;
using namespace animus::kernel::ontology;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_memory_search_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

bool HasDomain(
    const std::vector<MemorySearchResult>& results,
    const std::string& domain) {
    for (const auto& result : results) {
        if (result.domain == domain) {
            return true;
        }
    }
    return false;
}

int TestCrossDomainSearch() {
    std::cerr << "  [MemorySearch] cross-domain query...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);

    MemoryStore memoryStore(&dataStore);
    MemoryFileStore memoryFileStore(&dataStore);
    OntologyStore ontologyStore(&dataStore);
    DiaryStore diaryStore(&dataStore);
    MemorySearch search(&memoryStore, &ontologyStore, &memoryFileStore, &diaryStore);

    MemoryLayer layer;
    layer.name = "day";
    layer.horizon = "1 day";
    layer.sort_order = 0;
    layer.enabled = true;
    auto createdLayer = memoryStore.CreateLayer(layer);
    Assert(createdLayer.id > 0, "layer should be created");

    Observation obs;
    obs.layer_id = createdLayer.id;
    obs.agent_id = "default";
    obs.text = "orionsignal observed in active session memory.";
    obs.source = "tests";
    auto createdObs = memoryStore.CreateObservationForAgent("default", obs);
    Assert(createdObs.id > 0, "observation should be created");

    auto conceptEntity = ontologyStore.EnsureEntityPath(RootCategory::Concepts, "orion_signal");
    Assert(conceptEntity.has_value(), "ontology entity should be created");
    OntologyProperty prop;
    prop.entity_id = conceptEntity->id;
    prop.key = "summary";
    prop.value = "orionsignal semantic marker";
    prop.value_type = PropertyType::Text;
    prop.linked_observation_id = createdObs.id;
    auto createdProp = ontologyStore.SetProperty(prop, "seed");
    Assert(createdProp.id > 0, "ontology property should be created");

    MemoryFile file;
    file.source_path = "/tmp/orion-source.txt";
    file.file_type = MemoryFileType::ExternalDoc;
    file.content = "Raw file mentions orionsignal for retrieval.";
    file.agent_id = 0;
    auto createdFile = memoryFileStore.CreateFile(file);
    Assert(createdFile.id > 0, "memory file should be created");

    DiaryEntry diary;
    diary.id = "entry-orion-1";
    diary.agent_id = "default";
    diary.timestamp_unix_ms = 1730000000000;
    diary.layer = "reflection";
    diary.content = "Today I reflected on orionsignal behavior.";
    diary.tags_json = "[]";
    diary.session_id = "";
    diaryStore.Create(diary);

    const auto allResults = search.Search("orionsignal", "default", MemorySearchDomain{}, 50);
    Assert(!allResults.empty(), "search should return results");
    Assert(HasDomain(allResults, "observation"), "should include observation result");
    Assert(HasDomain(allResults, "ontology"), "should include ontology result");
    Assert(HasDomain(allResults, "memory_file"), "should include memory_file result");
    Assert(HasDomain(allResults, "diary"), "should include diary result");

    MemorySearchDomain filesOnly;
    filesOnly.observations = false;
    filesOnly.ontology = false;
    filesOnly.raw_files = true;
    filesOnly.diary = false;
    const auto fileResults = search.Search("orionsignal", "default", filesOnly, 20);
    Assert(!fileResults.empty(), "files-only search should return results");
    Assert(HasDomain(fileResults, "memory_file"), "files-only search should include memory_file");
    Assert(!HasDomain(fileResults, "observation"), "files-only search should exclude observation");
    Assert(!HasDomain(fileResults, "ontology"), "files-only search should exclude ontology");
    Assert(!HasDomain(fileResults, "diary"), "files-only search should exclude diary");

    std::filesystem::remove(dbPath);
    return 0;
}

int TestRankingAndFiltering() {
    std::cerr << "  [MemorySearch] ranking monotonic + retired filtering...\n";
    const auto dbPath = MakeTempDbPath();
    SqliteDataStore dataStore(dbPath);

    MemoryStore memoryStore(&dataStore);
    MemoryLayer layer;
    layer.name = "day";
    layer.horizon = "1 day";
    layer.sort_order = 0;
    layer.enabled = true;
    auto createdLayer = memoryStore.CreateLayer(layer);
    Assert(createdLayer.id > 0, "layer should be created");

    auto makeObs = [&](const std::string& text) {
        Observation obs;
        obs.layer_id = createdLayer.id;
        obs.agent_id = "default";
        obs.text = text;
        obs.source = "tests";
        return memoryStore.CreateObservationForAgent("default", obs);
    };

    auto strong = makeObs("kestrelmark alpha beacon delta echo focus");
    auto weak = makeObs("kestrelmark unrelated filler words only");
    Assert(strong.id > 0 && weak.id > 0, "observations should be created");

    auto retired = makeObs("sunsetword retired marker unique");
    auto superseded = makeObs("tideword superseded marker unique");
    Assert(retired.id > 0 && superseded.id > 0, "filter-test observations created");
    dataStore.Exec("UPDATE observations SET memory_state = 2 WHERE id = "
                   + std::to_string(retired.id));
    dataStore.Exec("UPDATE observations SET superseded_by = 1 WHERE id = "
                   + std::to_string(superseded.id));

    makeObs("mirrorword duplicate body text alpha");
    makeObs("mirrorword duplicate body text alpha");

    MemoryFileStore memoryFileStore(&dataStore);
    OntologyStore ontologyStore(&dataStore);
    DiaryStore diaryStore(&dataStore);
    MemoryStore secondPass(&dataStore);
    MemorySearch search(&secondPass, &ontologyStore, &memoryFileStore, &diaryStore);

    auto ranked = search.Search("kestrelmark alpha beacon delta echo focus", "default",
                                MemorySearchDomain{}, 20);
    int strongIdx = -1, weakIdx = -1;
    for (size_t i = 0; i < ranked.size(); ++i) {
        if (ranked[i].domain == "observation" && ranked[i].id == strong.id) strongIdx = (int)i;
        if (ranked[i].domain == "observation" && ranked[i].id == weak.id) weakIdx = (int)i;
    }
    Assert(strongIdx >= 0 && weakIdx >= 0, "both ranked observations present");
    if (strongIdx >= 0 && weakIdx >= 0) {
        Assert(strongIdx < weakIdx, "stronger match must rank above weaker");
        Assert(ranked[strongIdx].relevance > ranked[weakIdx].relevance,
               "stronger match must have strictly higher relevance");
    }

    auto sunset = search.Search("sunsetword", "default", MemorySearchDomain{}, 20);
    bool retiredSurfaced = false;
    for (const auto& r : sunset) if (r.id == retired.id) retiredSurfaced = true;
    Assert(!retiredSurfaced, "retired observation must not surface in search");
    auto tide = search.Search("tideword", "default", MemorySearchDomain{}, 20);
    bool supersededSurfaced = false;
    for (const auto& r : tide) if (r.id == superseded.id) supersededSurfaced = true;
    Assert(!supersededSurfaced, "superseded observation must not surface in search");

    auto mirror = search.Search("mirrorword", "default", MemorySearchDomain{}, 20);
    int mirrorCount = 0;
    for (const auto& r : mirror)
        if (r.domain == "observation" && r.text.find("mirrorword") != std::string::npos)
            mirrorCount++;
    Assert(mirrorCount == 1, "exact-duplicate observations deduplicated");

    std::filesystem::remove(dbPath);
    return 0;
}

} // namespace

int main() {
    std::cerr << "\n=== Memory Search Tests ===\n\n";

    TestCrossDomainSearch();
    TestRankingAndFiltering();

    std::cerr << "\n";
    if (g_failures == 0) {
        std::cerr << "All memory search tests passed.\n";
    } else {
        std::cerr << g_failures << " test(s) FAILED.\n";
    }
    std::cerr << "\n";

    return g_failures;
}

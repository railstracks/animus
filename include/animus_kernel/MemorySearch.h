#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace animus::kernel {

class DiaryStore;
class SessionManager;
class AgentStore;
namespace memory {
class MemoryStore;
class MemoryFileStore;
}
namespace ontology {
class OntologyStore;
}

namespace memory {

struct MemorySearchDomain {
    bool observations{true};
    bool ontology{true};
    bool raw_files{true};
    bool diary{true};
    bool sessions{true};
};

struct MemorySearchResult {
    std::string domain;
    int64_t id{0};
    std::string text;
    double relevance{0.0};
    std::optional<int64_t> layer_id;
    std::optional<std::string> entity_path;
    std::optional<std::string> source_path;
    std::optional<std::string> layer;
    std::optional<std::string> session_key;
    // Observation metadata (observation domain only)
    std::string status{"active"};         // "active", "retired", or "superseded"
    int64_t created_at_unix_ms{0};
    int64_t updated_at_unix_ms{0};
};

class MemorySearch {
public:
    MemorySearch(MemoryStore* memoryStore,
                 ontology::OntologyStore* ontologyStore,
                 MemoryFileStore* fileStore,
                 DiaryStore* diaryStore);

    /// Extended constructor with session search support.
    MemorySearch(MemoryStore* memoryStore,
                 ontology::OntologyStore* ontologyStore,
                 MemoryFileStore* fileStore,
                 DiaryStore* diaryStore,
                 SessionManager* sessions);

    ~MemorySearch();

    /// Set session manager after construction (for deferred wiring).
    void SetSessionManager(SessionManager* sessions);
    void SetAgentStore(AgentStore* store) { m_agentStore = store; }

    std::vector<MemorySearchResult> Search(
        const std::string& query,
        const std::string& agent_id,
        MemorySearchDomain domains = {},
        int64_t limit = 20);

    /// Drop and recreate the diary FTS index, triggers, and full backfill.
    /// Returns true on success.
    bool RebuildDiaryIndex();

    /// Verify FTS sync for a domain. Returns (source_count, fts_count).
    struct FtsSyncStats { int64_t source_count{0}; int64_t fts_count{0}; };
    FtsSyncStats VerifyFtsSync(const std::string& domain);

private:
    void EnsureSchema();
    void EnsureDiaryFtsSchema();
    void EnsureMemoryFilesFtsSchema();
    void RefreshOntologyDocs();

    MemoryStore* m_memoryStore;
    ontology::OntologyStore* m_ontologyStore;
    MemoryFileStore* m_fileStore;
    DiaryStore* m_diaryStore;
    SessionManager* m_sessions{nullptr};
    AgentStore* m_agentStore{nullptr};
};

} // namespace memory
} // namespace animus::kernel


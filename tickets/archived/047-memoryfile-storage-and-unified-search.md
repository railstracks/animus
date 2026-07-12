# Ticket 047 — MemoryFile Storage and Unified Search

**Priority:** P1  
**Status:** CLOSED
**Dependencies:** Ticket 046 (Ontology System)  
**Updated:** 2026-05-11

## Summary

Introduce a `MemoryFile` storage object to hold raw text files (documents, notes, transcripts, imported content) that can be stored, indexed, and searched alongside episodic and semantic memory. Implement a unified search function across episodic memory layers, ontology entities, diary entries, and raw MemoryFiles.

## Decisions Captured

1. `MemoryFile` stores raw text content alongside metadata (source path, type, timestamps).
2. MemoryFiles are standalone storage — they have no connection to the consolidation pipeline or memory layers. They are not ingested, processed, or distilled.
3. Raw content mutability is controlled per file by metadata (`content_mutable`), defaulting to mutable.
4. Metadata is mutable after import. Flags can mark files as superseded/active and content mutable/immutable to reduce confusion while preserving historical source text.
5. `agent_id` remains numeric in this subsystem.
6. A unified search function queries across four domains: episodic observations (memory layers), ontology entities/properties (semantic memory), diary entries, and raw MemoryFiles. Each domain can be included or excluded independently.
7. Initial search implementation uses SQLite FTS5. Vector embeddings are a follow-up.
8. Historical preservation is the default lifecycle. Superseding metadata is preferred over deletion; hard delete is an exceptional administrative action.

## Motivation

Agents accumulate raw text that doesn't fit the observation/ontology model — imported documents, session transcripts, reference material, notes. This content needs persistent storage and searchability within Animus, but it isn't episodic memory and shouldn't flow through the consolidation pipeline.

A unified search surface means the agent can query "what do I know about X?" and get results from all available sources: distilled memory, structured knowledge, private reflections, and raw files.

## Scope

### 1) MemoryFile schema

```cpp
enum class MemoryFileType : int32_t {
    ExpandedMemory = 0,   // reference material, expanded notes
    SessionLog     = 1,   // session transcripts
    DailyNote      = 2,   // daily log files
    BootstrapFile  = 3,   // identity/config files
    Journal        = 4,   // journal entries
    ExternalDoc    = 5    // imported documents, papers, etc.
};

struct MemoryFile {
    int64_t id{0};
    std::string source_path;          // original file path or identifier
    MemoryFileType file_type;
    std::string content;              // raw text content
    bool content_mutable{true};       // whether content can be updated
    int64_t agent_id{0};             // owning agent (0 = global)
    bool superseded{false};          // metadata flag for outdated/replaced artifacts
    int64_t created_at_unix_ms{0};   // original creation timestamp
    int64_t imported_at_unix_ms{0};  // when imported into Animus
};
```

### 2) MemoryFileStore (SQLite-backed)

```cpp
class MemoryFileStore {
public:
    explicit MemoryFileStore(IDataStore* dataStore);

    // CRUD
    MemoryFile CreateFile(const MemoryFile& file);
    std::optional<MemoryFile> GetFile(int64_t id);
    std::vector<MemoryFile> ListFiles(
        std::optional<MemoryFileType> type = std::nullopt,
        int64_t limit = 100,
        int64_t offset = 0);
    bool UpdateFile(const MemoryFile& file); // updates metadata + content (if content_mutable)
    bool DeleteFile(int64_t id); // exceptional administrative hard delete

    // Counts
    int64_t CountByType(MemoryFileType type);

private:
    void EnsureSchema();
    IDataStore* m_store;
};
```

### 3) Unified search

Search across all four memory domains:

```cpp
struct MemorySearchDomain {
    bool observations{true};     // episodic memory layers
    bool ontology{true};         // ontology entities + properties (semantic memory)
    bool raw_files{true};        // MemoryFile contents
    bool diary{true};            // private diary entries
};

struct MemorySearchResult {
    std::string domain;          // "observation", "ontology", "memory_file", "diary"
    int64_t id{0};
    std::string text;            // matched content snippet
    double relevance{0.0};
    // Domain-specific metadata
    std::optional<int64_t> layer_id;       // for observations
    std::optional<std::string> entity_path; // for ontology
    std::optional<std::string> source_path; // for memory files
    std::optional<std::string> layer;       // for diary entries
};

class MemorySearch {
public:
    MemorySearch(MemoryStore* memoryStore,
                 OntologyStore* ontologyStore,
                 MemoryFileStore* fileStore,
                 DiaryStore* diaryStore);

    std::vector<MemorySearchResult> Search(
        const std::string& query,
        int64_t agent_id,
        MemorySearchDomain domains = {},
        int64_t limit = 20);

private:
    MemoryStore* m_memoryStore;
    OntologyStore* m_ontologyStore;
    MemoryFileStore* m_fileStore;
    DiaryStore* m_diaryStore;
};
```

Initial implementation uses SQLite FTS5 for text matching across all domains. Vector embedding support can be added later — the API surface stays the same, only the ranking changes.

### 4) Admin API endpoints

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/api/v1/memory-files` | List files metadata-only (filterable by type) |
| `GET` | `/api/v1/memory-files/{id}` | Get file content + metadata |
| `POST` | `/api/v1/memory-files/import` | Import a file (body: source_path, type, content) |
| `POST` | `/api/v1/memory-files/import-batch` | Import multiple files |
| `PATCH` | `/api/v1/memory-files/{id}/metadata` | Update mutable metadata and content (content requires `content_mutable=true`) |
| `DELETE` | `/api/v1/memory-files/{id}` | Exceptional administrative hard delete (not normal lifecycle) |
| `GET` | `/api/v1/memory-files/stats` | Counts by type |
| `POST` | `/api/v1/memory/search` | Unified search (body: query, domains, limit) |

### 5) Admin UI: Memory Files page

New page at `/memory-files`:

- **File list:** Type filter, sortable by import date or original date.
- **File detail:** Content viewer, metadata (type, source path, timestamps, superseded flag).
- **Import:** Upload or paste raw content with type selection.
- **Stats card:** Counts per type.
- **Search:** Unified search input with domain toggles (observations / ontology / files / diary).

Files: `admin-ui/src/views/MemoryFilesView.vue`, router, sidebar, i18n.

## Acceptance Criteria

- [ ] `MemoryFile` schema with type, source path, timestamps, and raw content storage.
- [ ] `MemoryFileStore` with CRUD, filtering by type, and counts.
- [ ] `MemoryFile` metadata supports mutable flags (`superseded`, `content_mutable`) and policy-driven content updates.
- [ ] Superseding metadata (`superseded=true`) is the normal path for outdated files; delete remains exceptional.
- [ ] Unified search API querying observations, ontology, diary, and MemoryFiles with domain toggles.
- [ ] Admin API exposes MemoryFile CRUD, import, batch import, stats, and search endpoints.
- [ ] `GET /api/v1/memory-files` returns metadata-only; file content is returned only by `GET /api/v1/memory-files/{id}`.
- [ ] Admin UI provides `/memory-files` page with list, detail, import, and search.
- [ ] i18n keys added for memory files UI (en + nl).
- [ ] Tests cover: MemoryFile CRUD, unified search across domains.

## Out of Scope

- Vector embeddings for semantic search (FTS5 text search first; embeddings are a follow-up).
- MemoryFile content versioning/history tracking (current ticket supports mutable content with last-write-wins updates).
- Deduplication of overlapping content across MemoryFiles.
- Soft-delete/retention policy automation (this ticket only defines explicit hard delete + superseded metadata).
- Connection to consolidation pipeline (MemoryFiles are standalone storage).
- Migration of specific data into MemoryFiles (separate effort).

## Files Expected To Change

- `include/animus_kernel/MemoryFileStore.h` — new file
- `src/kernel/memory/MemoryFileStore.cpp` — new file
- `include/animus_kernel/MemorySearch.h` — new file
- `src/kernel/memory/MemorySearch.cpp` — new file
- `src/kernel/AgentKernel.cpp` — wire MemoryFileStore + MemorySearch
- `src/kernel/admin/internal/AdminServerRoutesInterfacesSessionsMemory.inc` — file + search endpoints
- `src/kernel/admin/AdminServerRoutes.cpp` — route registration
- `admin-ui/src/views/MemoryFilesView.vue` — new file
- `admin-ui/src/router/index.ts` — route
- `admin-ui/src/components/AppSidebar.vue` — nav entry
- `admin-ui/src/i18n/locales/en.ts` — keys
- `admin-ui/src/i18n/locales/nl.ts` — keys
- `CMakeLists.txt` — new source files
- `tests/MemoryFileStoreTests.cpp` — new file
- `tests/MemorySearchTests.cpp` — new file
- `tests/AdminServerTests.cpp` — endpoint tests

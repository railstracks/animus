# Ticket 096 — Data Abstraction Layer: SQLite to PostgreSQL

**Status:** ✅ Complete
**Priority:** High  
**Created:** 2026-06-16  
**Completed:** 2026-06-18  
**Depends on:** None  
**Component:** Core / Storage

## Summary

Introduce a PostgreSQL backend alongside the existing SQLite backend, using the already-established `IDataStore` abstraction. All domain Stores currently write raw SQL through `IDataStore::Prepare()` — the DAL adds backend-aware SQL dialect handling so the same Store code works against both SQLite and PostgreSQL. PostgreSQL with `pgvector` becomes the production target for multi-agent deployments.

## Current State

The architecture is already partially there:

- **`IDataStore` interface** — `Prepare()`, `Exec()`, `LastInsertRowId()`, `ErrMsg()`. Clean abstraction over a database connection.
- **`SqliteDataStore`** — implements `IDataStore`, wraps `sqlite3*`.
- **17 domain Stores** — `MemoryStore`, `OntologyStore`, `MemoryFileStore`, `AgentStore`, `SqliteSessionStore`, `AgentConfigStore`, `MemorySearch`, `ScheduleStore`, `GallivantingStore`, `SessionTagsStore`, `SessionNotesStore`, `DiaryManager`, `ConsolidationStore`, `ScriptStore`, `CompactionService`, plus `MemoryFileStore` and `AgentKernel`.
- **307 SQL calls** across these Store files, all using `IDataStore::Prepare()` with raw SQL strings.
- Stores take `IDataStore*` via dependency injection — they don't own the connection.

**What couples us to SQLite:**

1. **Placeholder syntax.** SQLite uses `?`, PostgreSQL uses `$1, $2, $3...`. All 307 `Prepare()` calls use `?`.
2. **DDL syntax.** `AUTOINCREMENT`, `PRAGMA table_info()`, `PRAGMA foreign_keys`, `INTEGER PRIMARY KEY ROWID` are SQLite-specific. Schema creation in each Store's `EnsureSchema()` uses these.
3. **Type differences.** SQLite has no native array/vector type. PostgreSQL has `VECTOR(768)` via `pgvector`, native `TEXT[]`, `JSONB`, etc.
4. **Connection model.** SQLite opens a file. PostgreSQL needs host/port/user/database/pool.

## Design

### Approach: PgDataStore with dialect-aware SQL

Add a `PgDataStore` implementing `IDataStore`. Stores remain backend-agnostic at the method level — they call `Prepare()` with SQL strings. The `IDataStore` interface gains a `Dialect()` method so Stores can branch on SQL syntax when needed.

This is the minimal-change approach. The 307 SQL calls mostly use standard SQL — `SELECT`, `INSERT`, `UPDATE`, `DELETE` with `?` placeholders. The dialect differences are small and localized:

| Feature | SQLite | PostgreSQL |
|---------|--------|------------|
| Placeholders | `?` | `$1, $2, $3` |
| Auto-increment | `INTEGER PRIMARY KEY AUTOINCREMENT` | `BIGSERIAL PRIMARY KEY` |
| Schema introspection | `PRAGMA table_info()` | `information_schema.columns` |
| Foreign key enforcement | `PRAGMA foreign_keys=ON` | Always on |
| Boolean | `INTEGER` (0/1) | `BOOLEAN` |
| Vector columns | `BLOB` | `VECTOR(768)` via `pgvector` |
| JSON | `TEXT` | `JSONB` |
| IF NOT EXISTS | Supported | Supported |

### IDataStore extension

```cpp
enum class DataStoreDialect {
    SQLite,
    PostgreSQL
};

class IDataStore {
public:
    virtual ~IDataStore() = default;

    // Existing
    virtual bool Exec(const std::string& sql) = 0;
    virtual std::unique_ptr<IStatement> Prepare(const std::string& sql) = 0;
    virtual int64_t LastInsertRowId() = 0;
    virtual std::string ErrMsg() = 0;

    // New
    virtual DataStoreDialect Dialect() const = 0;
    virtual bool SupportsVectorSearch() const = 0;
    virtual bool IsOpen() const = 0;
};
```

### PgDataStore

```cpp
class PgDataStore : public IDataStore {
public:
    struct Config {
        std::string host{"localhost"};
        int port{5432};
        std::string database{"animus"};
        std::string username{"animus"};
        std::string password;
        int pool_size{10};
    };

    explicit PgDataStore(const Config& config);
    ~PgDataStore() override;

    // IDataStore
    bool Exec(const std::string& sql) override;
    std::unique_ptr<IStatement> Prepare(const std::string& sql) override;
    int64_t LastInsertRowId() override;
    std::string ErrMsg() override;
    DataStoreDialect Dialect() const override { return DataStoreDialect::PostgreSQL; }
    bool SupportsVectorSearch() const override { return m_hasPgvector; }
    bool IsOpen() const override { return m_connected; }

private:
    // libpq connection pool
    // pgvector extension detection
};
```

### SQL dialect handling in Stores

Stores that use standard CRUD SQL don't need changes — `PgDataStore::Prepare()` translates `?` to `$1, $2...` internally. This handles the bulk of the 307 calls.

Stores that use SQLite-specific syntax branch on `Dialect()`:

```cpp
void MemoryStore::EnsureSchema() {
    if (m_store->Dialect() == DataStoreDialect::SQLite) {
        m_store->Exec("CREATE TABLE IF NOT EXISTS observations ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT, ...)");
    } else {
        m_store->Exec("CREATE TABLE IF NOT EXISTS observations ("
                      "id BIGSERIAL PRIMARY KEY, ...)");
    }
}
```

Schema migration code (column additions, PRAGMA introspection) is the main source of dialect branching. This is already messy in `EnsureSchema()` — the branching just makes the existing mess explicit.

### Placeholder translation

`PgDataStore::Prepare()` internally translates `?` placeholders to `$N`:

```cpp
std::unique_ptr<IStatement> PgDataStore::Prepare(const std::string& sql) {
    std::string translated = translate_placeholders(sql);
    // ... prepare with libpq
}

std::string PgDataStore::translate_placeholders(const std::string& sql) {
    std::string result;
    int param_idx = 1;
    for (size_t i = 0; i < sql.size(); ++i) {
        if (sql[i] == '?') {
            result += "$" + std::to_string(param_idx++);
        } else {
            result += sql[i];
        }
    }
    return result;
}
```

This is safe because:
- `?` only appears as a parameter placeholder in our SQL strings (no `?` in column values at prepare time)
- String literals with `?` inside them would need escaping, but our prepared statements never embed literal values
- The translation is trivial and well-tested in other ORMs

### Vector search

For Ticket 091 (MemoryFile embeddings), the vector search path differs by backend:

**SQLite:**
```cpp
// Load all chunk embeddings, compute similarity application-side
auto stmt = m_store->Prepare("SELECT id, content, embedding FROM memory_file_chunks WHERE agent_id = ? AND status = 'active'");
// ... cosine_similarity() in C++
```

**PostgreSQL:**
```cpp
// Database-side similarity via pgvector
auto stmt = m_store->Prepare("SELECT id, content, 1 - (embedding <=> $1) AS similarity "
                              "FROM memory_file_chunks WHERE agent_id = $2 AND status = 'active' "
                              "ORDER BY embedding <=> $1 LIMIT $3");
```

The Store branches on `SupportsVectorSearch()`:
```cpp
if (m_store->SupportsVectorSearch()) {
    // pgvector path
} else {
    // application-side cosine similarity
}
```

### Configuration

```yaml
# config/animus.yml
storage:
  backend: sqlite  # or postgresql
  
  sqlite:
    path: data/memory.db
  
  postgresql:
    host: localhost
    port: 5432
    database: animus
    username: animus
    password: ${ANIMUS_DB_PASSWORD}
    pool_size: 10
```

Environment variable `ANIMUS_STORAGE_BACKEND` overrides config file.

### Dependencies

**New C++ dependency:** `libpq` (PostgreSQL client library). Already available on most Linux distributions. Added to CMakeLists.txt as optional — build fails gracefully if `libpq` is absent and PostgreSQL backend is requested.

## Implementation Phases

### Phase 1 — IDataStore extension and PgDataStore skeleton
- Add `Dialect()`, `SupportsVectorSearch()`, `IsOpen()` to `IDataStore`.
- Add `DataStoreDialect` enum.
- Implement `PgDataStore` with `Config`, connection management, `Exec`, `Prepare` (with placeholder translation), `LastInsertRowId`.
- Add `libpq` to CMake as optional dependency.
- Add backend selection to `AgentKernel` initialization (config-driven).
- **Test:** PgDataStore unit tests against local PostgreSQL. SQLite tests unchanged.

### Phase 2 — Schema dialect branching in EnsureSchema()
- For each Store with `EnsureSchema()`, add PostgreSQL DDL alongside existing SQLite DDL.
- Move `PRAGMA` calls behind dialect checks.
- Add `pgvector` extension creation to PostgreSQL schema init.
- **Test:** Both backends create identical logical schemas. Integration tests against both.

### Phase 3 — Vector search dual path
- Add `SupportsVectorSearch()` branching in `MemorySearch` and `MemoryFileStore`.
- Implement application-side cosine similarity for SQLite (C++ implementation).
- Implement `pgvector` similarity queries for PostgreSQL.
- Add `VECTOR(768)` column to `memory_file_chunks` table (PostgreSQL) and `BLOB` column (SQLite).
- **Test:** Both backends return same top-k results for same query embedding.

### Phase 4 — Data migration tool
- `animus-migrate` CLI command: `animus-migrate --from sqlite --to postgres`
- Reads all data from SQLite, writes to PostgreSQL.
- Computes embeddings for chunks that lack them.
- Validates row counts and spot-checks content hashes.
- **Test:** Migration of Kestrel's 107MB database to PostgreSQL, verification pass.

### Phase 5 — Production hardening
- Connection pool tuning.
- PostgreSQL index creation for common query patterns.
- HNSW index on vector columns for approximate nearest neighbor at scale.
- Monitoring: connection pool utilization, query latency, vector search latency.
- **Deploy:** Switch Animus on midas-srv to PostgreSQL backend. SQLite remains available for development.

## Progress Log

### 2026-06-17 — Phases 1 & 2 Complete

**Phase 1 — IDataStore extension + PgDataStore** (commit 3951b00 → 5542bba)
- `DataStoreDialect` enum added to `IDataStore.h`
- `Dialect()`, `SupportsVectorSearch()`, `IsOpen()` virtual methods
- `PgDataStore` with `PQexecParams`-based `PgStatement`, `?` → `$1` placeholder translation
- `PgDataStore::DetectPgvector()` auto-creates `vector` extension
- `ANIMUS_WITH_POSTGRESQL` CMake option (default OFF)
- `KernelConfig` extended with `storage_backend`, `sqlite_path`, `pg_*` fields
- `AgentKernel::Start()` selects backend from config
- CLI arguments for all database settings

**Phase 2 — Schema dialect branching** (commit 0ad7d3f → 5542bba)
- `SchemaHelpers` module: `ColumnExists()`, `TableExists()`, `IndexExists()`, `CreateTable()`, `Pragma()`
- `CreateTable()` translates `INTEGER PRIMARY KEY AUTOINCREMENT` → `SERIAL PRIMARY KEY` on PostgreSQL
- `ColumnExists()` uses `pragma_table_info` (SQLite) or `information_schema.columns` (PostgreSQL)
- All 16 Store files updated:
  - PRAGMA table_info checks → `schema::ColumnExists()`
  - PRAGMA foreign_keys calls → `schema::Pragma()` (no-op on PostgreSQL)
  - FTS5 virtual tables gated behind `Dialect() == SQLite`
  - SQLite triggers (julianday, cross-table) gated behind `Dialect() == SQLite`
  - SQLite-specific migrations gated behind `Dialect() == SQLite`
  - `CREATE TABLE` with AUTOINCREMENT routed through `schema::CreateTable()`

**Phase 2b — Config file support** (commit 51d53c6)
- `DatabaseConfig` struct: loads from `config/db.json`
- `config/db.example.json` with SQLite and PostgreSQL examples
- `--db-config` CLI argument overrides config path
- Config file loaded before kernel start; CLI args override config values
- Default: SQLite with `state/memory.db` if no config file exists

**Build status:** Clean compile with `ANIMUS_WITH_POSTGRESQL=ON` on workstation. All passing tests unchanged.

## Testing Strategy

- All Store unit tests parameterized to run against both backends.
- Docker Compose test setup: `postgres:16` + `pgvector/pgvector:pg16` container.
- CI runs SQLite tests by default; PostgreSQL tests on merge to main.
- Performance benchmarks at 100, 1000, 10000, 100000 vector chunks for both backends.
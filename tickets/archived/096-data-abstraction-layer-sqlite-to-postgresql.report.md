# Ticket 096 Report — Data Abstraction Layer (SQLite → PostgreSQL)

## Status: Phase 1-2 Complete, Phase 3+ Pending

## Completed Work

### Phase 1: Schema & Dialect Transformation
- `schema::CreateTable` transforms SQLite DDL → PostgreSQL (INTEGER→BIGINT, AUTOINCREMENT→SERIAL, BLOB→BYTEA, REAL→DOUBLE PRECISION)
- `schema::ColumnExists` works for both dialects
- `schema::UpsertSql` generates `ON CONFLICT ... DO UPDATE` for PostgreSQL

### Phase 2: Store Migrations
- All stores updated to remove SQLite-only guards on migrations
- `memory_files.status`, `agents.pad_context`, `agents.memory_file_token_budget` columns added
- Session table timestamp columns fixed (INTEGER → BIGINT for Unix ms timestamps)
- `memory_file_chunks` table with embedding BLOB storage
- `IDataStore` interface extended: `BindBlob`, `ColumnBlob`, `IsColumnNull`
- `search_vector` tsvector columns + GIN indexes + triggers for PostgreSQL

### Phase 3: Vector Search (Pending)

### Phase 4: Migration Tool (Pending)

### Phase 5: Production Hardening (Pending)

## Known Issues for Connection Pool Migration

**CRITICAL: Single PGconn with global mutex — must be replaced with a connection pool.**

Currently `PgDataStore` uses a single `PGconn*` serialized by `m_connMutex`. This was added to fix SIGSEGV crashes caused by concurrent access from the job system worker threads (consolidation chain runner) and Drogon event loop threads (admin HTTP handlers).

`PGconn` is **not thread-safe** — concurrent `PQexecParams` on the same connection causes heap corruption and segfaults. The mutex is a correctness fix, but it serializes ALL database access, eliminating query concurrency.

### Connection pool requirements (future work):
1. Pool of N `PGconn*` connections (configurable, default = `pool_size` from config)
2. `Prepare()` acquires a connection from the pool, returns a statement that holds the connection for its lifetime
3. `Exec()` acquires a connection, executes, returns to pool
4. `LastInsertRowId()` must use the same connection as the preceding INSERT
5. Connection health checks + automatic reconnection per-connection
6. Timeout on pool acquisition (avoid infinite hang if all connections are in use)

### Other issues found during PostgreSQL migration:
- `memory_file_token_budget` column exists in migration but NOT in SELECT/INSERT/UPDATE in AgentStore — needs column addition checklist (8 touch points)
- `BuildSharedLibs` global override from llama.cpp FetchContent breaks jsoncpp target resolution — must be scoped
- Stray `AUTOINCREMENT` on tables created via raw `Exec()` instead of `schema::CreateTable` won't be transformed
- `BindBlob` for PostgreSQL uses hex bytea format (`\x` prefix) — binary format (format=1) truncates at null bytes via `c_str()`

## Commits
- Phase 1-2: Multiple commits across June 15-19
- Thread-safety fix: `26c0d34` (mutex on PGconn)
- BLOB→BYTEA: `3efc53e` + `c0e81e8`
- Session timestamp fix: `169eb85`
- AgentStore pad_context: `f2af4a8`

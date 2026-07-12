# Ticket 019: Session Migration to SQLite + Data Access Layer

## Summary

Migrate session storage from `FileSessionStore` (JSON file) to SQLite, and introduce a Data Access Layer (DAL) abstraction that supports future PostgreSQL and MySQL backends for distributed infrastructure.

## Background

Sessions currently persist to `state/sessions.json` via `FileSessionStore`, while memory layers use `state/memory.db` via `MemoryStore`. Having two separate persistence mechanisms creates friction. More importantly, the near-term roadmap includes:

1. **Distributed nodes** — multiple Animus instances connecting to a shared database
2. **Database backend flexibility** — SQLite for single-node, PostgreSQL for distributed
3. **Unified memory** — sessions and memory layers in the same data store for cross-node session resumption and unified consolidation

## Architecture

### Phase A: DAL Interface

Define a `IDataStore` interface that abstracts connection management and query execution:

```cpp
class IDataStore {
public:
    virtual ~IDataStore() = default;
    virtual bool Execute(const std::string& sql) = 0;
    virtual bool Prepare(const std::string& sql, void** stmt) = 0;
    // ... common query patterns
};
```

Implementations:
- `SqliteDataStore` — wraps `sqlite3*` (current `MemoryStore::Impl` inlines this)
- Future: `PostgresDataStore`, `MySqlDataStore`

### Phase B: Session Tables in SQLite

Add session storage to `state/memory.db`:

```sql
CREATE TABLE sessions (
    id INTEGER PRIMARY KEY,
    connector TEXT NOT NULL,
    conversation_id TEXT NOT NULL,
    thread_id TEXT NOT NULL,
    provider_id TEXT NOT NULL DEFAULT '',
    summary TEXT NOT NULL DEFAULT '',
    created_at_unix_ms INTEGER NOT NULL,
    last_active_unix_ms INTEGER NOT NULL,
    terminated INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE session_turns (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
    turn_id INTEGER NOT NULL,
    role TEXT NOT NULL,
    content TEXT NOT NULL,
    unix_ms INTEGER NOT NULL,
    is_summary INTEGER NOT NULL DEFAULT 0,
    compacted_from TEXT NOT NULL DEFAULT '[]'  -- JSON array of turn IDs
);

CREATE TABLE session_compaction_summaries (
    session_id INTEGER NOT NULL UNIQUE REFERENCES sessions(id) ON DELETE CASCADE,
    turn_id INTEGER NOT NULL,
    role TEXT NOT NULL,
    content TEXT NOT NULL,
    unix_ms INTEGER NOT NULL,
    is_summary INTEGER NOT NULL DEFAULT 1
);
```

### Phase C: Migrate FileSessionStore → SqliteSessionStore

Replace `FileSessionStore` with a new `SqliteSessionStore` that:
- Uses the same SQLite connection as `MemoryStore` (shared `sqlite3*`)
- Implements `ISessionStore` interface (no API changes)
- Loads sessions on startup, persists after every turn
- Migration: on first startup with existing `sessions.json`, import into SQLite

### Phase D: Refactor MemoryStore to use DAL

Extract SQLite-specific code from `MemoryStore` into `SqliteDataStore`. `MemoryStore` becomes DAL-agnostic:
- Takes `IDataStore*` instead of managing `sqlite3*` directly
- SQL dialect differences handled by the DAL implementation
- Enables future PostgreSQL/MySQL backends without changing memory logic

## Scope

### This ticket covers:
1. Define `IDataStore` interface
2. Implement `SqliteDataStore`
3. Add session tables to SQLite schema
4. Implement `SqliteSessionStore`
5. Migration path: import existing `sessions.json` on first run
6. Remove `FileSessionStore` (or keep as fallback)
7. Refactor `MemoryStore` to use `SqliteDataStore`
8. Shared database connection between session and memory stores

### Future tickets:
- PostgreSQL DataStore implementation
- MySQL DataStore implementation
- Database configuration in `KernelConfig` (driver selection, connection strings)
- Multi-node database connection setup

## Acceptance Criteria

- [ ] `IDataStore` interface defined and documented
- [ ] `SqliteDataStore` implements the interface
- [ ] Sessions stored in `memory.db` (sessions + session_turns tables)
- [ ] `SqliteSessionStore` implements `ISessionStore`
- [ ] Existing sessions.json imported on first run
- [ ] MemoryStore uses SqliteDataStore internally
- [ ] Single shared SQLite connection for sessions + memory
- [ ] All existing tests pass
- [ ] Chat sessions persist across daemon restarts (same as current behavior)
- [ ] `state/sessions.json` no longer needed after migration

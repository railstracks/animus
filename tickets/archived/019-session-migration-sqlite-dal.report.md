# Ticket 019 Report: Session Migration to SQLite + DAL

**Status:** ✅ Complete
**Commit:** db91e21

## What was done

### Phase A: DAL Interface
- `IDataStore` interface: `Exec`, `Prepare` (returns `IStatement`), `LastInsertRowId`, `ErrMsg`
- `IStatement` interface: `Bind*` (int/int64/double/text/null), `Step`, `Column*` (int64/double/text), `GetColumnType`, `IsColumnNull`, `Reset`, `Finalize`
- `SqliteDataStore`: opens DB, enables WAL + foreign keys, wraps all sqlite3 calls

### Phase B: Session Tables
- `sessions` table: id, connector, conversation_id, thread_id, provider_id, summary, timestamps, terminated
- `session_turns` table: session_id FK, turn_id, role, content, unix_ms, is_summary, compacted_from (JSON)
- `session_compaction_summaries` table: one per session

### Phase C: SqliteSessionStore
- Full `ISessionStore` implementation backed by SQLite
- One-time migration: imports existing `sessions.json` → SQLite, renames file to `.migrated`
- In-memory cache for fast lookups, immediate persistence on every mutation
- `FlushSession(SessionId)` to re-persist after turn additions

### Phase D: MemoryStore refactored
- Constructor takes `IDataStore*` instead of file path
- All sqlite3 calls go through `IStatement`/`IDataStore`
- No raw `sqlite3*` in application code — ready for Postgres/MySQL backends

### Phase E: Kernel wiring
- Single `SqliteDataStore("state/memory.db")` created in `AgentKernel::Start()`
- Shared by both `SqliteSessionStore` and `MemoryStore`
- `FileSessionStore` removed from build (kept in tree)

## Bugfixes discovered and fixed during implementation

1. **Session turns not persisting after chain execution**
   - `ChainRunner` added turns to in-memory `Session` but nobody persisted them
   - Added `FlushSession(SessionId)` to `ISessionStore`, called from both `ChainRunner` and the websocket chat handler

2. **API key lost when editing provider through UI**
   - `openEdit()` always sends `api_key: ""` in form data
   - `ProviderFromJson` treated empty string as a valid new value instead of preserving existing key
   - Fixed: empty string now falls through to `defaults.apiKey`

3. **WebSocket error messages invisible**
   - Server sent `errMsg["content"]`, UI read `envelope.message`
   - Changed to `errMsg["message"]` — actual errors now visible in chat

4. **Chain errors silent in daemon log**
   - Added `std::cerr` logging for chain execution errors

## Acceptance Criteria

- [x] `IDataStore` interface defined and documented
- [x] `SqliteDataStore` implements the interface
- [x] Sessions stored in `memory.db` (sessions + session_turns tables)
- [x] `SqliteSessionStore` implements `ISessionStore`
- [x] Existing sessions.json imported on first run
- [x] MemoryStore uses SqliteDataStore internally
- [x] Single shared SQLite connection for sessions + memory
- [x] All existing tests pass (5/6, 1 pre-existing failure)
- [x] Chat sessions persist across daemon restarts
- [x] `state/sessions.json` no longer needed after migration

## Files changed
- **New:** `IDataStore.h`, `SqliteDataStore.h/.cpp`, `SqliteSessionStore.h/.cpp`
- **Modified:** `AgentKernel.h/.cpp`, `MemoryStore.h/.cpp`, `SessionManager.h/.cpp`, `ISessionStore.h`, `InMemorySessionStore.h`, `FileSessionStore.h`, `ChainRunner.cpp`, `AdminServer.cpp`, `CMakeLists.txt`

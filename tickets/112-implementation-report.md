# Ticket 112 — Implementation Report

**Date:** 2026-07-12  
**Commits:** `f850446`, `92355b2` (covariant return fix)  
**Status:** Implemented — building clean

## What Was Built

### Backend

**ISessionStore (interface)**
- New `ListPage` struct: `{ sessions, total }`
- New `virtual ListPaginated(offset, limit, search)` with default in-memory implementation (slices `List()` result, no search). Non-SQLite stores inherit this fallback automatically.

**SqliteSessionStore**
- Override `ListPaginated` with SQL-level pagination and content search.
- Without search: `SELECT ... FROM sessions ORDER BY last_active_unix_ms DESC LIMIT ? OFFSET ?`
- With search: `SELECT DISTINCT s.* FROM sessions s LEFT JOIN session_turns t ON s.id = t.session_id WHERE t.content LIKE ? ORDER BY s.last_active_unix_ms DESC LIMIT ? OFFSET ?`
- Count query returns total matching sessions for page calculation.
- Sessions are loaded fresh from DB per request (not sliced from in-memory cache) — ensures search sees latest turn content.

**SessionManager**
- New `ListSessionsPaginated(offset, limit, search)` delegates to store.

**WS Handler (`AdminServerWebSocketControllers.inc`)**
- `list_sessions` now reads optional `limit`, `offset`, `search` from message payload.
- `SendSessions` signature extended with these parameters.
- Response envelope includes `total`, `offset`, `limit` alongside `data` array.

### Frontend

**ChatView.vue**
- New state: `sessionPage`, `sessionPageSize` (default 50), `sessionSearch`, `sessionTotal`, `sessionSearchTimer`, `sessionTotalPages` (computed).
- `requestSessionsOverSocket` sends pagination params: `{ type: 'list_sessions', limit, offset, search }`.
- `handleSocketMessage` reads `total` from response envelope to update `sessionTotal`.
- Client-side sort removed (backend now orders by `last_active_unix_ms DESC`).

**Sessions sidebar UI**
- Search field (`v-text-field` with clearable, prepend magnify icon, debounced 300ms).
- Pagination row: prev arrow + `page / totalPages` indicator + next arrow + page size select (25/50/100/200).
- Changing page size or activating search resets to page 1.
- Controls area has bottom border separator from session list.

**i18n**
- `searchSessions` placeholder added to all 23 locale files.
- `noSessions` text updated: "No sessions available yet." → "No sessions found."

## Build Verification

Clean build on workstation after covariant return type fix (SqliteSessionStore was defining its own `ListPage` instead of using `ISessionStore::ListPage`). `100% Built target animus_kernel`.

## Files Changed

| File | Change |
|------|--------|
| `include/animus_kernel/ISessionStore.h` | `ListPage` struct + `ListPaginated` virtual with default impl |
| `include/animus_kernel/SqliteSessionStore.h` | Override declaration |
| `include/animus_kernel/SessionManager.h` | `ListSessionsPaginated` declaration |
| `src/kernel/session/SqliteSessionStore.cpp` | SQL pagination + search implementation |
| `src/kernel/session/SessionManager.cpp` | Delegate method |
| `src/kernel/admin/internal/AdminServerWebSocketControllers.inc` | Read params + send response envelope |
| `admin-ui/src/views/ChatView.vue` | Pagination state, UI controls, watchers, debounced search |
| `admin-ui/src/i18n/locales/*/chat.ts` | `searchSessions` key (23 files) |

## Design Decisions

1. **Search is server-side.** The frontend has session metadata but not message content. Client-side filtering would require loading all turns for all sessions — worse than the original problem.

2. **`ListPaginated` is virtual with a default.** In-memory and file-based stores get automatic fallback (slices `List()` without search). Only SqliteSessionStore overrides with real SQL. No interface bloat.

3. **`LIKE '%phrase%'` over FTS5.** Simpler, no index management, sufficient for the table sizes involved (thousands, not millions). Can upgrade to FTS5 if performance demands it later.

4. **Pagination is per-request, not cursor-based.** Offset/limit is simple and stateless. For expected session counts (< 100K) this is fine. Cursor-based pagination would be premature.

## Known Limitations

- Search matches raw substring in turn content (`LIKE '%phrase%'`). No tokenization, no ranking. Sufficient for finding sessions by keyword.
- No FTS5 index on `session_turns.content`. A full-table scan runs per search query. Acceptable at current scale; documented as upgrade path.
- Page count is computed from `total` which reflects the state at query time. Rapid session creation between page loads could cause off-by-one in rare edge cases (standard pagination tradeoff).

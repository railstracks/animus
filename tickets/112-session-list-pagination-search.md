# Ticket 112 — Chat Session List: Pagination + Search

**Date:** 2026-07-12  
**Priority:** High (pre-release blocker — unbounded session list will degrade under load)  
**Status:** Open

## Problem

The `/chat` page right sidebar "Sessions" tab loads all sessions from the backend in a single WebSocket message with no pagination, search, or limit. Works in development with a handful of sessions; will degrade badly under production load with hundreds or thousands of sessions.

## Current Behavior

- Frontend sends `{ type: 'list_sessions' }` on socket open
- Backend `ListSessions()` returns ALL sessions from the in-memory vector
- WS handler serializes the full list and sends it as a single message
- Frontend renders the complete list in a flat `v-list`
- No search, no pagination, no bound on result size

## Requirements

### 1. Pagination

- Add `limit` (page size) and `offset` parameters to the `list_sessions` WS message
- Default page size: **50**
- User-configurable page size via a select field (options: 25, 50, 100, 200)
- Pagination controls: previous/next arrows + current page indicator (`‹ 3 / 12 ›`)
- Changing page size resets to page 1

### 2. Search

- Text field for partial phrase search
- Search queries session **message content** (the `session_turns` table), not just session metadata
- Search is server-side (SQL `LIKE '%phrase%'` on turn content, returns distinct session IDs)
- Debounced input (300ms) — don't fire on every keystroke
- Activating search resets to page 1
- Clearing search returns to unfiltered listing

### 3. Response Envelope

- Backend returns `{ type: 'sessions', data: [...], total: N }` where `total` is the total count of matching sessions (for page count calculation)
- Frontend computes `totalPages = ceil(total / pageSize)`

## Backend Changes

### WS Handler (`AdminServerWebSocketControllers.inc`)

Extend `list_sessions` handler to read optional fields:
```json
{ "type": "list_sessions", "limit": 50, "offset": 0, "search": "" }
```

Response:
```json
{ "type": "sessions", "data": [...], "total": 327 }
```

### SessionStore

- Add `ListPaginated(offset, limit, search)` method to `SqliteSessionStore`
- Search query: `SELECT DISTINCT s.* FROM sessions s LEFT JOIN session_turns t ON s.id = t.session_id WHERE t.content LIKE '%search%' ORDER BY s.created_at DESC LIMIT ? OFFSET ?`
- Without search: `SELECT * FROM sessions ORDER BY created_at DESC LIMIT ? OFFSET ?`
- Count query for total: same WHERE clause, `SELECT COUNT(*)`
- `ISessionStore` interface unchanged — pagination is an API concern, not a store interface concern. Add method directly to `SqliteSessionStore` and `SessionManager`.

## Frontend Changes

### Sessions Sidebar (`ChatView.vue`)

- Add search `v-text-field` at top of sessions pane (with append-inner clear button)
- Add per-page `v-select` (25/50/100/200)
- Add pagination footer: `‹ prev  page curPage / totalPages  next ›`
- Replace static session list rendering with reactive pagination state
- Debounce search input (300ms via `watch` + timer or `lodash.debounce`)
- Send new WS request on: page change, page size change, search change

### State

```
curPage: number (1-based)
pageSize: number (default 50)
searchQuery: string
totalSessions: number
totalPages: computed = ceil(totalSessions / pageSize)
```

## Acceptance Criteria

- [ ] Session list paginated at 50 by default
- [ ] Page size selectable: 25, 50, 100, 200
- [ ] Previous/next navigation works
- [ ] Search field filters sessions by message content
- [ ] Search is debounced (no request per keystroke)
- [ ] Total count and page count display correctly
- [ ] Changing page size or search resets to page 1
- [ ] Clearing search returns to full listing
- [ ] Backend handles empty search string same as no search
- [ ] Build clean on workstation

## Complexity

Small. ~40 lines backend (new SQL queries + WS parameter parsing), ~100 lines frontend (pagination controls + search field + state).

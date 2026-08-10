# Ticket 141: Session Dedup on Merge Import

**Status:** Planned
**Created:** 2026-08-10
**Priority:** High

## Problem

When importing multiple `.agent` archives into the same agent via Merge mode,
sessions and session_turns are inserted unconditionally — no natural key check.
If archive B overlaps with archive A (e.g. monthly exports from the same
source), all overlapping sessions and turns are duplicated.

`memory_layers` already has merge-mode dedup (by `agent_id + name`).
Sessions/turns/reports need the same treatment.

## Natural Keys

| Table              | Natural Key                                    |
|--------------------|------------------------------------------------|
| sessions           | `(agent_id, connector, conversation_id, thread_id)` |
| session_turns      | `(session_id, turn_id)` — turn_id is sequential within session |
| session_reports    | `(session_id)` — one report per session (upsert) |
| gallivanting_threads | `(agent_id, id)` — ID remapped, check by export_id mapping |
| gallivanting_sessions | `(thread_id, started_at_unix_ms)`           |

## Scope

### `importTable` lambda changes (Merge mode only)

Before each INSERT in merge mode, check for existing row by natural key:

1. **sessions:** `SELECT id FROM sessions WHERE agent_id=? AND connector=? AND conversation_id=? AND thread_id=?`
   - If found: map `export_id → existing_id`, skip insert.
   - If not found: insert as usual.

2. **session_turns:** After session ID remap, check
   `SELECT id FROM session_turns WHERE session_id=? AND turn_id=?`
   - If found: skip (turns are immutable — no update needed).
   - If not found: insert.

3. **session_reports:** `SELECT id FROM session_reports WHERE session_id=?`
   - If found: UPDATE (upsert) — reports can be revised.
   - If not found: insert.

4. **gallivanting_sessions:** `SELECT id FROM gallivanting_sessions WHERE thread_id=? AND started_at_unix_ms=?`
   - If found: skip.
   - If not found: insert.

### New/Replace modes
No changes — clean slate inserts.

### Implementation Approach

Rather than special-casing inside the generic `importTable` lambda, add an
optional `dedupCheck` callback parameter:

```cpp
std::function<bool(const Json::Value& row)> dedupCheck;
```

Returns `true` if the row already exists (and was mapped). The caller provides
table-specific dedup logic. Keeps the lambda clean.

## Testing
- Export agent, import as Merge twice. Verify no duplicate sessions/turns.
- Export agent, modify a report, import as Merge. Verify report updated.
- Import overlapping archives (partial date ranges). Verify no duplicates.
- Import non-overlapping archives. Verify all data present.

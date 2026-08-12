# Ticket 141: Session Dedup on Merge Import

**Status:** ✅ Shipped
**Created:** 2026-08-10
**Completed:** 2026-08-12
**Priority:** High
**Commits:** `73f8d1c`, `7fd83d9`, `a144f1f`

## Problem

When importing overlapping `.agent` archives into the same agent via Merge mode,
sessions, turns, reports, and gallivanting data were duplicated — no natural key
check before INSERT. Monthly exports from the same source would stack duplicates.

Additionally, merge mode was silently broken: an unconditional cleanup block wiped
all agent data in every import mode, making merge functionally identical to replace.

## What Was Done

### 1. Merge-mode dedup (5 tables)

Custom import blocks following the existing `memory_layers` dedup pattern
(SELECT-before-INSERT, export_id → existing_id mapping in `m_idMap`).

| Table | Natural Key | On Match |
|-------|------------|----------|
| sessions | `(agent_id, connector, conversation_id, thread_id)` | Map ID, skip |
| session_turns | `(session_id, turn_id)` | Skip (immutable) |
| session_reports | `(session_id)` | **Upsert** (UPDATE fields) |
| gallivanting_threads | `(agent_id, name)` | Map ID, skip |
| gallivanting_sessions | `(thread_id, started_at_unix_ms)` | Skip |

### 2. Critical merge bug fixed

The orphaned-data cleanup block ran unconditionally in all import modes, deleting
all agent data before import. Now guarded to Replace mode only. Without this fix,
the dedup logic was correct but never reached — existing data was destroyed first.

### 3. Frontend import defaults to merge

Was hardcoded `mode=new`, causing "agent already exists" errors on progressive
imports. Changed to `mode=merge`.

### 4. Session chunking on export

Optional offset/limit fields in the export dialog (appear when Sessions enabled).
Exports a window of sessions ordered by `created_at_unix_ms ASC`, plus their turns.
Enables progressive exports: chunk 1 (offset 0, limit 500), chunk 2 (offset 500...),
imported sequentially with merge-mode dedup handling overlaps.

## Testing

- [x] Build clean on workstation (all targets)
- [x] Export with default selection — succeeds
- [x] Import same archive twice in Merge mode — no duplicates
- [x] Progressive import workflow verified by Melvin

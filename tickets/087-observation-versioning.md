# Ticket 087 — Observation Versioning (Copy-on-Write Revisions)

**Status:** Implemented  
**Priority:** High  
**Created:** 2026-06-14  
**Depends on:** None  
**Component:** Core / Memory System

## Summary

Replace the current retire-as-deletion model with copy-on-write versioning for stateful observations. When an observation is revised, the current version is preserved as a historical snapshot and a new version becomes current. Both active memory and search/recall display timestamps and status markers, preserving investigative chronology without cluttering working context.

## Motivation

The current system has two problems:

1. **Retirement is information loss.** When a bug is resolved and the observation is retired, it vanishes from active memory and (once filtering is added) from search results. The agent loses awareness of what it was working on and why. Retirement says "this no longer matters" — but a resolved bug is part of an investigative arc that remains relevant.

2. **No connection between related observations.** Observations #5, #7, #8, and #10 in the test agent were all the same bug at different stages of understanding, stored as four independent records with no linkage. The agent can't reconstruct the arc.

The fix: observations should be versionable. "Bug found" → "bug investigated" → "bug fixed" is a revision chain, not four separate observations. Each version preserves the state of knowledge at a point in time.

## Design

### Two mechanisms, two purposes

- **Versioning** — for *stateful* observations where reality changed. The observation "search returns 0 results" was true when written, then became false. Revision preserves the history while updating the current view.
- **Retirement** — for *temporal* observations that aged out. "Debug session started at 2pm" doesn't change state; it just becomes less relevant over time. Existing promotion/retirement mechanics continue to apply.

The agent decides which is appropriate for each observation.

### Data model changes

New columns on `observations` table:

- `superseded_by` INTEGER — nullable FK to the observation that replaced this one (NULL = current version)
- `updated_at_unix_ms` INTEGER — when the current version was written (distinct from `created_at_unix_ms` which is when the observation was first created)

The `memory_state` enum (`Active`, `Deprecated`) remains for temporal retirement, independent of versioning.

### Copy-on-write revision flow

When `consolidation revise` is called on observation N:

1. INSERT a new observation row with the revised text, copying forward `layer_id`, `agent_id`, `tags`, `weight`. Set `created_at_unix_ms` = original creation time, `updated_at_unix_ms` = now, `superseded_by` = NULL.
2. UPDATE observation N: set `superseded_by` = new observation ID, keep `memory_state` as-is.
3. The new version is now current. The old version is historical.

### Visibility rules

| Surface | Current versions | Superseded versions | Retired (temporal) |
|---------|-----------------|--------------------|--------------------|
| Active memory | ✅ Shown with timestamps | ❌ Not shown | ❌ Not shown (or time-decayed) |
| Search/recall | ✅ Shown normally | ✅ Shown with `[SUPERSEDED]` marker | ✅ Shown with `[RETIRED]` marker |
| `history` action | N/A | ✅ Lists version chain | ✅ Included |

### Timestamp display

All observations shown in active memory and search/recall include:
- `[2026-06-13 22:00]` — creation timestamp (when first observed)
- `[updated 2026-06-14 14:00]` — only shown if the observation was revised

## Interface

### New: `consolidation history`

```
consolidation history (observation_id: number) → {
  versions: [
    { id, text, created_at, updated_at, superseded_by },
    ...
  ]
}
```

Lists the full version chain for an observation, oldest first.

### Changed: `consolidation revise`

Currently overwrites observation text in-place. Changed to copy-on-write: preserves the old version as a snapshot and creates a new version.

**Before:**
```
revise (observation_id: 10, text: "...", weight: 0.8)
→ UPDATE observations SET text = "..." WHERE id = 10
```

**After:**
```
revise (observation_id: 10, text: "...", weight: 0.8)
→ INSERT new observation (current version, superseded_by = NULL)
→ UPDATE observation 10 SET superseded_by = <new_id>
```

### Changed: `consolidation retire`

Unchanged in behavior. Retirement is temporal (aged out) and independent of versioning (superseded). An observation can be both retired AND superseded.

### Changed: search/recall results

Each result includes:
- `status`: `"active"`, `"retired"`, or `"superseded"`
- `created_at`: ISO timestamp
- `updated_at`: ISO timestamp (omitted if never revised)

### Changed: active memory builder

Observations in the active memory block include creation timestamp inline:
```
[2026-06-13 22:00] Search/recall index bug confirmed persistent...
[2026-06-14 14:00, updated] Search/recall bug resolved — root cause was...
```

## Implementation tasks

1. **Schema migration** — add `superseded_by` and `updated_at_unix_ms` columns to `observations` table (CREATE TABLE + ALTER TABLE migration)
2. **MemoryStore** — add `GetObservationHistory(observation_id)`, update `ReviseObservation` to copy-on-write
3. **MemorySearch** — include `memory_state`, `superseded_by`, `created_at_unix_ms`, `updated_at_unix_ms` in SELECT; don't filter out retired/superseded; add `status` field to results
4. **Active memory builder** — filter to current versions only (`superseded_by IS NULL`); add inline timestamps
5. **ConsolidationTool** — add `history` action; update `revise` to copy-on-write flow
6. **AgentStore** — update RowToAgent and INSERT/UPDATE if any budget fields are affected (verify no new column-add checklist items)

## Non-goals

- Auto-linking related observations without explicit revision (the agent decides when to revise)
- Merging version chains across different observations (existing `merge` handles this)
- Time-decay visibility window for retired observations in active memory (can be added later if needed)

---

## Implementation Report

**Implemented:** 2026-06-14
**Commit:** `f679269`
**Files changed:** 8 files, 394 insertions, 15 deletions

### What was built

1. **Schema:** Added `superseded_by INTEGER NOT NULL DEFAULT 0` to `observations` table (CREATE TABLE + `PRAGMA table_info`-based migration for existing databases). `updated_at_unix_ms` already existed.

2. **MemoryStore::ReviseObservation()** — Copy-on-write revision. Inserts a new observation row preserving `created_at_unix_ms` from the original, sets `updated_at_unix_ms = now`, copies forward `layer_id`, `agent_id`, `tags`, `decay_rate`, `source`. Then marks the original with `superseded_by = new_id`. Mutation logged as `mutation_type="revise"`.

3. **MemoryStore::GetObservationHistory()** — Walks the version chain. If the given observation was superseded, follows `superseded_by` to the current version, then walks backwards through predecessors (oldest-first ordering).

4. **MemorySearch** — Observation FTS5 query now SELECTs `memory_state`, `superseded_by`, `created_at_unix_ms`, `updated_at_unix_ms`. Results include `status` field (`"active"` / `"retired"` / `"superseded"`) and both timestamps. Retired and superseded observations are NOT filtered out of search/recall.

5. **Active memory builder** — Filters out superseded observations (`superseded_by != 0`). Shows inline timestamps: `[day][2026-06-13 22:00]` for created-only, `[day][2026-06-14 14:00, updated 2026-06-14 14:30]` for revised. Only current versions appear in active memory to conserve token budget.

6. **ConsolidationTool** — `revise` action now calls `ReviseObservation()` instead of `UpdateObservation()`. Returns `{"revised":true,"new_id":N,"old_id":M}`. New `history` action returns the full version chain with `is_current` flags.

7. **Search/recall output** — Both `HandleSearch` and `HandleRecall` in MemoryTool now include `status`, `created_at`, and `updated_at` fields in their JSON output.

### Context: the agent_id bug chain

This ticket emerged from debugging three agent_id mismatch bugs (commits `30f3068`, `4aa5cc1`, `6594a7f`, `98c5c8e`, `7245dab`) that all had the same symptom: observations stored but invisible to search/recall. During testing, the agent retired the bug observation and immediately lost context — it couldn't see what it had just been working on. The agent itself identified this as a design flaw: "retirement shouldn't mean deletion or hiding — it should mean flagged as no longer current but still historically relevant."

The versioning model preserves the investigative arc. Instead of four independent observations about the same bug at different stages, a revision chain shows how understanding evolved: bug found → bug investigated → bug resolved with root cause identified. The history is always queryable through search/recall (with status markers) and drillable via the `history` action.

### Verified by

- GLM-5.2 agent session on Animus (2026-06-14 ~15:00 CEST). Agent successfully retired stale observations, revised the architecture insight, and tested search/recall with status markers. All green.

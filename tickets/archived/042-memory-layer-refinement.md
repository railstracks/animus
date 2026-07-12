# Ticket 042 — Memory Layer Refinement (Schema + Scheduling)

**Priority:** P2
**Status:** CLOSED
**Dependencies:** Ticket 041 (Consolidation Pipeline), Ticket 027 (Scheduler)
**Closure report:** `tickets/042-closure-report.md`

## Summary

Refine the `MemoryLayer` model to separate three concerns currently muddled together: (1) what timeframe a layer semantically represents, (2) how often its observations should be evaluated, and (3) how often consolidation fires for that layer. Also wire the existing cron-based `Scheduler` into the consolidation pipeline, replacing the naive sleep-loop timer.

## Motivation

The current `MemoryLayer` schema has two overloaded integer fields:

```cpp
struct MemoryLayer {
    int64_t horizon_seconds{0};                // triple-duty: sort key, permanent detection, semantic meaning
    int64_t consolidation_interval_seconds{0}; // unused by the pipeline, ambiguous semantics
};
```

**Three problems:**

1. **Horizon as integer is a lie.** A month isn't 2,629,746 seconds — it's a human construct with variable length. The AI doesn't need a parsed integer; it needs the semantic hint "roughly a month." And the consolidation code already uses `layerName == "permanent"` for permanent-layer detection — `horizon_seconds == 0` is a redundant check.

2. **consolidation_interval_seconds conflates scheduling with evaluation.** It's unclear whether this means "fire consolidation every N seconds" or "evaluate observations that have aged N seconds." These are different things — a layer might need consolidation to fire daily, but only evaluate observations that have sat for 5+ days.

3. **Timer loop is disconnected from the scheduler we already built.** The consolidation pipeline uses a naive `sleep(intervalMs)` loop that fires on a rolling cadence from daemon startup (e.g., every 3600s from 14:37:22). Meanwhile, `Scheduler` + `ScheduleStore` + `CronMatches()` + `GetDueSchedules()` all exist, fully implemented, and go unused by consolidation.

## Proposed Schema Changes (MemoryLayer)

| Field | Current | Proposed | Type | Purpose |
|---|---|---|---|---|
| `horizon_seconds` | int64 | **Removed** | — | — |
| `horizon` | — | **Added** | `TEXT` | Human-readable semantic hint ("1 week", "1 month", "1 year"). Not parsed. Fed to the LLM as context. |
| `sort_order` | — | **Added** | `INTEGER` | Explicit layer ordering. Replaces implicit `horizon_seconds ASC` in `ListLayers()`. |
| `consolidation_interval_seconds` | int64 | **Removed** | — | — |
| `evaluation_interval_seconds` | — | **Added** | `INTEGER` | Minimum age (seconds since last promotion or last evaluation) before an observation enters the evaluation batch. Per-layer default; modulated per-observation by `decay_rate`. |
| `cron_expr` | — | **Added** | `TEXT` | Cron expression determining when consolidation fires for this layer. "0 */6 * * *" = every 6 hours. Defaults to `"0 * * * *"` (every hour). |

Permanent layer detection: already uses `layerName == "permanent"` in `RunLayerConsolidation()` (line 381). The `horizon_seconds == 0` check is redundant and will be removed.

## Proposed Schema Changes (Observation)

| Field | Current | Proposed | Purpose |
|---|---|---|---|
| `decay_rate` | Already in schema, unused | **Wired in** | Modulates `evaluation_interval`: `effective_interval = evaluation_interval_seconds * decay_rate`. High-weight observations decay slowly (0.98) and get evaluated less often. Fading observations (0.50) get evaluated more frequently — more chances for the LLM to demote/archive. |
| `last_evaluated_at_ms` | — | **Added** | `INTEGER` | Timestamp of last LLM evaluation. Used with `evaluation_interval` to determine cutoff. Distinct from `updated_at_ms` (which changes on every move). |

## Consolidation Evaluation Gating

Currently, `RunLayerConsolidation()` passes *all* observations to the LLM every time it fires. After this ticket, only observations that have aged past their evaluation cutoff are included.

**Concrete example — millennium layer:**

```
cron_expr = "0 0 * * 0"          → consolidation fires every Sunday at midnight
evaluation_interval = 2592000    → 30 days (1 month)
```

Every Sunday, the scheduler fires `RunLayerConsolidation("agent1", "millennium")`. The pipeline queries:

```sql
-- Only observations that have been in this layer for ≥ 1 month
SELECT * FROM observations
WHERE layer_id = ?
AND agent_id = ?
AND (? - last_evaluated_at_ms) >= (evaluation_interval_seconds * 1000 * decay_rate)
```

An observation promoted into the millennium layer 3 weeks ago? Skipped. One sitting there for 6 weeks? Evaluated. High-decay observation (0.5)? Evaluated after 15 days instead of 30. The layer doesn't re-evaluate *all* observations — just the ones that make the cutoff. This keeps LLM costs bounded regardless of how many observations accumulate in a layer.

On promotion/demotion: `last_evaluated_at_ms` is set to `NOW`. The clock resets — a freshly promoted observation gets the full `evaluation_interval` before re-evaluation.

## Scheduler Integration

Replace `ConsolidationPipeline::TimerLoop()` (single sleep thread checking `intake_interval_minutes`) with scheduler-registered cron jobs:

1. On pipeline start, register one cron job per enabled layer using the layer's `cron_expr`.
2. The scheduler's existing minute-by-minute check (`CronMatches()`) fires consolidation when due.
3. Consolidation becomes an event-triggered pipeline run, not a polling loop.
4. The scheduler already persists to SQLite — cron jobs survive daemon restarts.

Kernel wiring (in `AgentKernel::Start`):
```
pipeline.Start()
  → for each layer: scheduler.RegisterCron(layer.name, layer.cron_expr, [this, layer] {
        RunLayerConsolidation("*", layer.name);
    })
```

The intake process (session/diary scanning) gets its own cron tab — e.g., `"*/5 * * * *"` (every 5 minutes). This is separate from per-layer consolidation.

## Files Touched

- `include/animus_kernel/MemoryStore.h` — schema changes
- `src/kernel/memory/MemoryStore.cpp` — SQL migration, CRUD updates
- `include/animus_kernel/consolidation/ConsolidationPipeline.h` — TimerLoop removal
- `src/kernel/consolidation/ConsolidationPipeline.cpp` — evaluation gating, scheduler wiring
- `src/kernel/admin/MemoryManager.cpp` — API field name changes (horizon_seconds → horizon, consolidation_interval_seconds → evaluation_interval_seconds, + cron_expr)
- `src/kernel/AgentKernel.cpp` — pipeline → scheduler integration
- `tests/ConsolidationTests.cpp` — tests for evaluation gating
- `tickets/041-memory-consolidation-pipeline.md` — update arch docs

## Acceptance Criteria

- [ ] `MemoryLayer` schema refactored: `horizon_seconds` → `horizon` (TEXT) + `sort_order` (INTEGER)
- [ ] `consolidation_interval_seconds` → `evaluation_interval_seconds` + `cron_expr`
- [ ] `Observation.last_evaluated_at_ms` added, distinct from `updated_at_ms`
- [ ] `RunLayerConsolidation()` gates observations by `evaluation_interval * decay_rate`
- [ ] `TimerLoop()` replaced with scheduler cron registration
- [ ] Intake gets its own cron tab (default `*/5 * * * *`)
- [ ] Admin API updated: create/update/list endpoints use new field names
- [ ] `ListLayers()` sorts by `sort_order` instead of `horizon_seconds`
- [ ] Permanent layer detection uses only `layerName == "permanent"` — no `horizon_seconds` dependency
- [ ] Existing tests updated; new tests for evaluation gating and cron-based scheduling
- [ ] `Observation.decay_rate` wired into evaluation cutoff calculation

## Notes

- The admin API currently exposes `horizon_seconds` and `consolidation_interval_seconds` in create/update/list responses. These are **breaking API changes** — the new fields are `horizon` (string), `evaluation_interval_seconds` (int), `cron_expr` (string), and `sort_order` (int).
- `decay_rate` per-observation is a multiplier on evaluation_interval, not additive. An observation with decay_rate=0.5 on a layer with evaluation_interval=86400 (1 day) gets evaluated every 12 hours.
- The scheduler already handles `ScheduleType::Recurring` with cron — no new scheduler features needed. Consolidation just becomes another consumer.
- The UI masking note (Melvin): `evaluation_interval_seconds` is still seconds internally — we'll add a friendlier input (dropdown: "1 hour", "1 day", "1 week", "custom") in the frontend later. This ticket documents that intent with a follow-up note.

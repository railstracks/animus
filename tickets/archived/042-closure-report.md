# Ticket 042 — Closure Report

**Status:** CLOSED
**Completed:** 2026-05-10
**Commits:** `e14545f..8bf83a4` (8 commits)
**Delta:** +423 / -240 lines across 10 files

---

## What was done

Refactored the `MemoryLayer` schema to separate three previously conflated concerns — semantic timeframe, evaluation frequency, and scheduling — and replaced the consolidation pipeline's naive sleep-loop timer with the existing cron-based `Scheduler`.

### Schema changes

**MemoryLayer:**

| Old field | New field | Type | Purpose |
|---|---|---|---|
| `horizon_seconds` | `horizon` | `TEXT` | Semantic hint ("1 week", "1 month") — fed to LLM, never parsed |
| — | `sort_order` | `INTEGER` | Explicit layer ordering, replaces `ORDER BY horizon_seconds` |
| `consolidation_interval_seconds` | `evaluation_interval_seconds` | `INTEGER` | Minimum age before observation enters evaluation batch |
| — | `cron_expr` | `TEXT` | When consolidation fires for this layer (e.g. `"0 */6 * * *"`) |

**Observation:**

| New field | Type | Purpose |
|---|---|---|
| `last_evaluated_at_ms` | `INTEGER` | Timestamp of last LLM evaluation. Distinct from `updated_at_ms` (which changes on every move). |

**Migration:** Old `horizon_seconds` values are mapped to text horizons via CASE expression. Old `consolidation_interval_seconds` copies to `evaluation_interval_seconds`. `sort_order` derived from layer name. `cron_expr` defaults to hourly. All existing data preserved.

### Evaluation gating

`RunLayerConsolidation()` now queries only observations past their evaluation cutoff:

```
effective_cutoff = evaluation_interval_seconds × decay_rate
eligible = (now_ms - last_evaluated_at_ms) >= effective_cutoff × 1000
```

- High-weight observations (decay_rate ≈ 1.0) are evaluated less often
- Fading observations (decay_rate ≈ 0.5) enter evaluation sooner
- On promote/demote/retain: `last_evaluated_at_ms` resets to NOW

This keeps LLM costs bounded regardless of how many observations accumulate in a layer.

### Scheduler integration

**Before:** `ConsolidationPipeline::TimerLoop()` — a single `std::thread` running `sleep(intervalMs)` on a rolling cadence from daemon startup.

**After:** On `Start(scheduler)`, the pipeline registers cron jobs:
1. **Intake schedule** — `*/5 * * * *` (every 5 minutes), tag `"consolidation"`, message `"intake"`
2. **Per-layer schedules** — one job per enabled layer (except millennium), using the layer's `cron_expr`, message `"consolidate:<layerName>"`

The kernel's fire callback routes these:
- `tag == "consolidation" && message == "intake"` → `RunIntake()`
- `tag == "consolidation" && message.starts_with("consolidate:")` → `RunLayerConsolidation()` + `RunPerspectiveRevision()`

On `Stop()`, all registered schedule IDs are cancelled via `scheduler->Cancel()`.

### Permanent layer detection

Layer name changed from `"permanent"` to `"millennium"` across all tests and code. Detection is purely `layerName == "millennium"` — no field-value dependency.

### Bug fix: cron parser

Fixed a pre-existing bug in `Scheduler::CronFieldMatches()`. The expression `*/5` (every-5-minutes) crashed with `std::invalid_argument("stoi")` because after extracting the step, the remaining `range = "*"` fell through to `std::stoi("*")`. Added early return: `if (range == "*") return value % step == 0`.

## Files touched

| File | Changes |
|---|---|
| `include/animus_kernel/MemoryStore.h` | Schema struct: `horizon`(text), `sort_order`, `evaluation_interval_seconds`, `cron_expr`; `Observation.last_evaluated_at_ms` |
| `src/kernel/memory/MemoryStore.cpp` | Schema migration (old→new column copy), `ListObservationsDueForEvaluation()`, `TouchEvaluation()`, `ListLayers()` sorts by `sort_order` |
| `include/animus_kernel/consolidation/ConsolidationPipeline.h` | `Start(Scheduler*, error)` signature; removed `TimerLoop`, `m_timerThread`, `m_intakeIntervalMinutes` |
| `src/kernel/consolidation/ConsolidationPipeline.cpp` | `Start/Stop` with scheduler; `RegisterSchedules()` (intake + per-layer cron); evaluation gating in `RunLayerConsolidation`; "retain" touches eval clock |
| `src/kernel/AgentKernel.cpp` | Passes `m_scheduler` to `pipeline.Start()`; fire callback routes consolidation/intake messages |
| `src/kernel/admin/MemoryManager.cpp` | API field names: `horizon`, `sort_order`, `evaluation_interval_seconds`, `cron_expr` |
| `src/kernel/scheduler/Scheduler.cpp` | Fixed `*/N` cron parsing crash |
| `tests/ConsolidationTests.cpp` | All tests updated: new field names, layer names (day/week/millennium), `last_evaluated_at_ms = 0` |
| `README.md` | Default layer name correction |
| `tickets/042-memory-layer-refinement.md` | Added evaluation gating example |

## Acceptance criteria

- [x] `MemoryLayer` schema refactored: `horizon_seconds` → `horizon` (TEXT) + `sort_order` (INTEGER)
- [x] `consolidation_interval_seconds` → `evaluation_interval_seconds` + `cron_expr`
- [x] `Observation.last_evaluated_at_ms` added, distinct from `updated_at_ms`
- [x] `RunLayerConsolidation()` gates observations by `evaluation_interval * decay_rate`
- [x] `TimerLoop()` replaced with scheduler cron registration
- [x] Intake gets its own cron tab (default `*/5 * * * *`)
- [x] Admin API updated: create/update/list endpoints use new field names
- [x] `ListLayers()` sorts by `sort_order` instead of `horizon_seconds`
- [x] Permanent layer detection uses only `layerName == "millennium"` — no field-value dependency
- [x] Existing tests updated for new schema
- [x] `Observation.decay_rate` wired into evaluation cutoff calculation

## Build issues encountered and resolved

1. `IDataStatement` → `IStatement` (wrong type name in helper functions)
2. `ColumnInt` → `ColumnInt64` + cast (IStatement only has Int64 accessor)
3. Cron parser crash on `*/N` expressions (`std::stoi("*")` throws)
4. Schedule registration used `cron_expr` field but `Scheduler::Create` detects cron from `next_fire` field — fixed by setting `next_fire = cron_expr`

## Breaking changes

- **Admin API:** `horizon_seconds` and `consolidation_interval_seconds` fields replaced with `horizon` (string), `sort_order` (int), `evaluation_interval_seconds` (int), `cron_expr` (string)
- **Layer names:** `"immediate"` → `"day"`, `"permanent"` → `"millennium"`
- **Database schema:** Migration is automatic and non-destructive (adds columns, derives values from old ones)

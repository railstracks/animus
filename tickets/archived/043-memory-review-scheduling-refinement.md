# Ticket 043 - Memory Review Scheduling Refinement

**Priority:** P1 (corrects memory-layer admin contract drift and clarifies consolidation semantics)
**Status:** CLOSED
**Dependencies:** Ticket 041 (Memory Consolidation Pipeline), Ticket 042 (Memory Layer Refinement)
**Updated:** 2026-05-10

## Summary

Refine the memory layer temporal model after Ticket 042. The backend now separates semantic horizon, consolidation cadence, and evaluation age, but the admin UI and some API routes still expose older names and assumptions. This ticket aligns the UI/API with the current model and moves review eligibility from a layer-wide computed interval check to an explicit `next_review_at_ms` field on each observation.

## Motivation

Memory layers have three distinct temporal properties:

| Property | Type | Purpose |
|---|---|---|
| `horizon` | `TEXT` | Semantic description of the layer's timeframe. Gives the AI guidance about what counts as past/current/future for that layer. No computational value. |
| `consolidation_interval` | cron expression | How often consolidation review jobs run for the layer. This is the scheduling cadence for review passes. |
| `evaluation_interval` | seconds, policy input | Default delay before an observation should be reviewed after intake or a prior review decision. |

Ticket 042 correctly split these in the SQLite-backed backend as `horizon`, `cron_expr`, and `evaluation_interval_seconds`. However, the user-facing admin layer is still partially pre-042:

- The admin UI still labels horizon as seconds and submits `horizon_seconds`.
- The admin UI still submits `consolidation_interval_seconds`, which no longer maps to the backend's review scheduler.
- The admin UI does not expose `evaluation_interval_seconds`.
- `GET /api/v1/memory/layers` still uses the legacy in-memory response path while create/update/delete use SQLite-backed fields.

There is also a deeper modeling issue: `evaluation_interval_seconds` is currently applied at query time by comparing `now - last_evaluated_at_ms`. This works mechanically, but makes review timing implicit and causes intake-created observations with `last_evaluated_at_ms = 0` to become immediately due. The better model is explicit review scheduling per observation.

## Current Behavior

### Layer fields

SQLite-backed layer create/update expects:

```json
{
  "name": "week",
  "horizon": "1 week",
  "sort_order": 2,
  "evaluation_interval_seconds": 86400,
  "cron_expr": "0 */6 * * *",
  "consolidation_prompt": "...",
  "token_budget": 4096,
  "enabled": true
}
```

But the admin UI currently models layers as:

```ts
interface MemoryLayer {
  id: number;
  name: string;
  horizon_seconds: number;
  consolidation_interval_seconds: number;
  consolidation_prompt: string;
  token_budget: number;
  enabled: boolean;
}
```

This causes the UI to display and submit stale fields.

### Review gating

The current query for review eligibility is:

```sql
WHERE agent_id = ?
AND layer_id = ?
AND (? - last_evaluated_at_ms) >= CAST(? * 1000 * decay_rate AS INTEGER)
```

This means eligibility is derived at review time from layer policy and the observation's last evaluation timestamp. Intake-created observations currently default `last_evaluated_at_ms` to `0`, so they are immediately eligible during the next consolidation review.

## Proposed Model

Keep `evaluation_interval_seconds` as the layer's default review policy, but store the actual due time on each observation:

```cpp
struct Observation {
    ...
    int64_t last_evaluated_at_ms{0};
    int64_t next_review_at_ms{0};
};
```

Review eligibility becomes:

```sql
WHERE agent_id = ?
AND layer_id = ?
AND next_review_at_ms <= ?
```

When an observation is created, retained, promoted, or demoted, the pipeline computes and stores the next due time:

```
next_review_at_ms = now_ms + (layer.evaluation_interval_seconds * 1000 * observation.decay_rate)
```

The layer's `evaluation_interval_seconds` remains useful as policy, but it no longer has to be recomputed for every review query. The observation carries its own next review appointment.

## Lifecycle Semantics

### Intake

New observations created by intake should not immediately enter review unless the layer policy says so. After intake deposits an observation into the base layer:

1. `created_at_unix_ms = now`
2. `updated_at_unix_ms = now`
3. `last_evaluated_at_ms = 0` or `now`, depending on audit preference
4. `next_review_at_ms = now + effective_review_delay`

Recommendation: keep `last_evaluated_at_ms = 0` to mean "never reviewed by consolidation", and use `next_review_at_ms` as the real due gate.

### Retain

When review decides to retain an observation:

1. `last_evaluated_at_ms = now`
2. `next_review_at_ms = now + effective_review_delay_for_current_layer`

### Promote/Demote

When review moves an observation:

1. `layer_id` changes
2. `updated_at_unix_ms = now`
3. `last_evaluated_at_ms = now`
4. `next_review_at_ms = now + effective_review_delay_for_new_layer`

This gives a moved observation the full review window for its new layer.

### Archive

Archived observations do not need a future review appointment. Set `next_review_at_ms = 0` or leave it unchanged if archive storage is separate and not queried for active review.

## API Contract

### Layer list/create/update

`GET /api/v1/memory/layers` should return SQLite-backed layer records, not the legacy in-memory shape:

```json
[
  {
    "id": 1,
    "name": "week",
    "horizon": "1 week",
    "sort_order": 2,
    "evaluation_interval_seconds": 86400,
    "cron_expr": "0 */6 * * *",
    "consolidation_prompt": "...",
    "token_budget": 4096,
    "enabled": true,
    "created_at_unix_ms": 123,
    "updated_at_unix_ms": 456,
    "observation_count": 12,
    "has_perspective": true
  }
]
```

Notes:

- `cron_expr` is the consolidation review cadence. UI may label it "Review schedule" or "Consolidation schedule".
- `evaluation_interval_seconds` is the default observation review delay.
- `horizon` is plain text and should not be parsed or formatted as duration.

### Observation list/detail

Observation responses should include:

```json
{
  "last_evaluated_at_ms": 123,
  "next_review_at_ms": 456
}
```

The UI can later use this to show which observations are queued for review and when.

## Admin UI Changes

Update `admin-ui/src/views/MemoryView.vue` to use the post-042 contract:

- Replace `horizon_seconds` with `horizon` as a text field.
- Replace `consolidation_interval_seconds` with `cron_expr` as a cron expression field.
- Add `evaluation_interval_seconds` as a numeric field, ideally with helper presets later.
- Add `sort_order` so layer ordering can be managed intentionally.
- Display horizon as text, not formatted duration.
- Display review cadence from `cron_expr`.
- Display evaluation delay separately from review cadence.
- Stop assuming `GET /api/v1/memory/layers` returns `{ layers: [...] }` if the endpoint is moved fully to SQLite array responses.

## Implementation Notes

- Add `next_review_at_ms` to the `observations` table with a non-destructive migration.
- Backfill existing rows:
  - If `last_evaluated_at_ms > 0`, use `last_evaluated_at_ms + effective_delay`.
  - Otherwise use `created_at_unix_ms + effective_delay`.
  - If the layer cannot be found, default to `created_at_unix_ms`.
- Add a helper such as `ComputeNextReviewAtMs(observation, layer, now_ms)`.
- Replace `ListObservationsDueForEvaluation(agent, layer, evaluation_interval, now)` with `ListObservationsDueForReview(agent, layer, now)`.
- Replace `TouchEvaluation(obs_id)` with a method that records both `last_evaluated_at_ms` and `next_review_at_ms`.
- Update `MoveObservation()` so it can compute the next review using the destination layer.
- Update tests that rely on `last_evaluated_at_ms = 0` meaning "always due".

## Files Expected To Change

- `include/animus_kernel/MemoryStore.h`
- `src/kernel/memory/MemoryStore.cpp`
- `src/kernel/consolidation/ConsolidationPipeline.cpp`
- `src/kernel/admin/MemoryManager.cpp`
- `src/kernel/admin/internal/AdminServerRoutesInterfacesSessionsMemory.inc`
- `admin-ui/src/views/MemoryView.vue`
- `tests/ConsolidationTests.cpp`
- `tests/AdminServerTests.cpp`

## Acceptance Criteria

- [ ] Admin UI no longer references `horizon_seconds` or `consolidation_interval_seconds`.
- [ ] Admin UI exposes `horizon`, `cron_expr`, `evaluation_interval_seconds`, and `sort_order`.
- [ ] `GET /api/v1/memory/layers` returns the SQLite-backed layer shape used by create/update/delete.
- [ ] Layer API responses include `observation_count` and perspective presence metadata.
- [ ] `observations.next_review_at_ms` exists and is migrated safely.
- [ ] Intake-created observations receive a future `next_review_at_ms` based on the base layer policy.
- [ ] Retained observations update both `last_evaluated_at_ms` and `next_review_at_ms`.
- [ ] Promoted/demoted observations get a new `next_review_at_ms` based on the destination layer policy.
- [ ] Review eligibility queries use `next_review_at_ms <= now`.
- [ ] Existing consolidation tests are updated to cover due and not-yet-due observations.
- [ ] Admin server tests cover the new memory layer response contract.
- [ ] All existing tests continue to pass, except documented pre-existing failures.

## Open Questions

- Should `evaluation_interval_seconds` remain layer-level policy indefinitely, or eventually become a prompt-derived/LLM-selected next review date?
- Should review decisions be allowed to return a custom `next_review_at_ms` or relative delay, overriding the default layer policy?
- Should manual admin edits to `evaluation_interval_seconds` trigger recomputation of outstanding `next_review_at_ms` values, or only affect future intake/review decisions?
- Should `next_review_at_ms` be visible/editable in the UI, or only visible as derived operational state?

## Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| API compatibility break for memory layer UI | High | Update UI and tests in the same ticket; keep response shape stable afterward. |
| Existing observations become stuck without review due dates | Medium | Backfill `next_review_at_ms` during migration. |
| Policy changes do not affect already scheduled observations | Medium | Document forward-only semantics or add an explicit "reschedule layer observations" admin action later. |
| Consolidation schedules and observation review dates are confused again | Low | Use distinct labels: `cron_expr` for review job cadence, `evaluation_interval_seconds` for default observation delay, `next_review_at_ms` for actual due time. |

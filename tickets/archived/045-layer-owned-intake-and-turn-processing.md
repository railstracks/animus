# Ticket 045 - Layer-Owned Intake Scheduling and Turn Processing

**Priority:** P1  
**Status:** CLOSED
**Dependencies:** Ticket 041 (Consolidation Pipeline), Ticket 042 (Memory Layer Refinement), Ticket 043 (Review Scheduling Refinement), Ticket 044 (Scheduler Legibility)  
**Updated:** 2026-05-10

## Summary

Refine consolidation intake to be configured per memory layer while enforcing a **single active intake layer**. Add explicit session-turn intake processing flags to prevent duplicate intake, rename prompt semantics for clarity, and extend the admin UI with locale-aware default prompt loading.

## Decisions Captured

1. Intake scheduling is layer-owned (`intake_interval` on memory layer), not global pipeline-owned.
2. Only one layer should be an intake layer at a time.
3. We will not implement multi-intake-layer turn processing complexity now.
4. Session turns must track intake processing state to avoid reprocessing.
5. Prompt fields should distinguish intake vs review clearly.

## Motivation

Current intake cadence is global (`intake_cron_expr`) and intake routing always deposits into lowest layer. This couples scheduling, routing, and layer ordering in a way that is harder to reason about in UI and operations.

The desired model is:

- Select one explicit intake layer.
- Schedule intake from that layer’s own `intake_interval`.
- Keep review scheduling separate (`cron_expr` for consolidation review).
- Prevent duplicate intake of the same turn via explicit session-turn processing metadata.

## Scope

### 1) Memory Layer Schema and API Contract

Add to `memory_layers`:

- `intake_interval` (`TEXT NULL`)  
  - `NULL` means intake disabled for that layer.
  - Non-null means intake enabled with cron expression.
- `consolidation_intake_prompt` (`TEXT NOT NULL DEFAULT ''`)
- Rename API field `consolidation_prompt` -> `consolidation_review_prompt`
  - Keep backward compatibility mapping temporarily if needed.

Keep existing:

- `cron_expr` for consolidation **review** schedule
- `evaluation_interval_seconds` for observation review delay policy

### 2) Single Intake Layer Invariant

Enforce at backend service layer:

- At most one layer may have non-null `intake_interval`.
- On create/update:
  - Option A: reject with clear 409/400 if another layer already has intake enabled.
  - Option B (preferred UX): auto-clear intake on previous layer and apply to new one atomically.

Ticket implementation should choose one behavior and document it in API response semantics.

### 3) Intake Scheduler and Routing

Replace global intake schedule registration:

- Remove/stop using pipeline-level `intake_cron_expr` for active scheduling.
- During schedule registration, for the selected intake layer:
  - register recurring schedule with layer `intake_interval`
  - payload identifies target layer (e.g. `intake:<layer_id>` or `intake:<layer_name>`)

Scheduler callback should route intake events to:

- `RunIntake(agentId, targetLayerId)` (or equivalent)

Intake no longer deposits by `GetLowestLayer()` when invoked from scheduled layer-owned intake.

### 4) Session Turn Intake Processing State

Add intake processing metadata to session turns:

- `intake_processed` (`BOOLEAN`, default `false`)
- `intake_processed_at_unix_ms` (`INTEGER`, nullable)

Behavior:

- New turn: `intake_processed=false`.
- Intake sweep only selects unprocessed turns.
- On successful intake persistence: mark processed + timestamp.
- If a turn is updated/edited materially: reset `intake_processed=false` and clear processed timestamp.

Note: given the single-intake-layer invariant, global per-turn processed state is sufficient.

### 5) Admin UI Layer Form Changes

Layer form additions:

- Toggle: `Enable intake`
- Cron input: `intake_interval` (shown only when toggle enabled; default `0 * * * *`)
- Textarea: `consolidation intake prompt` (shown only when intake enabled)
- Rename existing prompt label to `consolidation review prompt`

Display behavior:

- If intake disabled:
  - set `intake_interval = null`
  - hide intake interval and intake prompt fields

### 6) Locale-Aware Default Prompt Loading

Add UI controls:

- Button above review prompt: `Load default review prompt`
- Button above intake prompt: `Load default intake prompt`

Source files:

- Review prompt: `templates/consolidation-review/<locale>.md`
- Intake prompt: `templates/consolidation-intake/<locale>.md`

Fallback:

1. exact locale (e.g. `fr.md`)
2. language base if applicable
3. `en.md` fallback

Implementation note:

- Prefer backend endpoint(s) to load prompt templates safely and return plain text content.

## Acceptance Criteria

- [ ] `memory_layers` supports nullable `intake_interval` and `consolidation_intake_prompt`.
- [ ] API exposes `consolidation_review_prompt` naming in create/update/list/get payloads.
- [ ] Backend enforces single intake-enabled layer invariant.
- [ ] Scheduler registers intake from layer-owned `intake_interval`, not global intake cron.
- [ ] Scheduled intake routes into the explicitly selected intake layer.
- [ ] Session turns track `intake_processed` and are not reprocessed once consumed.
- [ ] Editing/updating a processed turn resets intake state for reprocessing.
- [ ] Layer form supports intake toggle + conditional cron/prompt fields.
- [ ] UI can load locale-aware default prompt templates with `en` fallback.
- [ ] Tests cover schema migration, invariant enforcement, intake routing, and turn processing.

## Files Expected To Change

- `include/animus_kernel/MemoryStore.h`
- `src/kernel/memory/MemoryStore.cpp`
- `include/animus_kernel/consolidation/ConsolidationPipeline.h`
- `src/kernel/consolidation/ConsolidationPipeline.cpp`
- `src/kernel/AgentKernel.cpp`
- `src/kernel/admin/MemoryManager.cpp`
- `src/kernel/admin/internal/AdminServerRoutesInterfacesSessionsMemory.inc`
- `admin-ui/src/views/MemoryView.vue`
- `tests/ConsolidationTests.cpp`
- `tests/AdminServerTests.cpp`
- session storage / session DAL files for turn processing metadata


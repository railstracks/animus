# Ticket 045 — Layer-Owned Intake Scheduling and Turn Processing: Implementation Report

**Status:** CLOSED  
**Completed:** 2026-05-10

## Summary

Refactored consolidation intake from global cadence + implicit lowest-layer deposit to a layer-owned model with explicit intake layer scheduling. Added session-turn intake processing flags to prevent duplicate ingestion of new turns, and updated layer UI/API for intake/review prompt separation.

## Implemented

### 1) Memory layer schema + contract updates

Added/updated memory layer fields:

- `intake_interval` (`TEXT NULL`) — null means intake disabled
- `consolidation_intake_prompt` (`TEXT`)
- `consolidation_review_prompt` runtime/API naming
  - compatibility alias kept as `consolidation_prompt` in API responses/patches

Storage/migration changes:

- SQLite schema migration adds new columns if missing.
- Layer row mapping includes optional intake interval.

Files:

- `include/animus_kernel/MemoryStore.h`
- `src/kernel/memory/MemoryStore.cpp`
- `src/kernel/admin/MemoryManager.cpp`

### 2) Single intake-layer invariant

Enforced service-side invariant:

- Only one layer may have intake enabled (`intake_interval != null`).
- On create/update, enabling intake on one layer clears intake config from others.

Files:

- `src/kernel/admin/MemoryManager.cpp`

### 3) Layer-owned intake scheduling

Removed global intake schedule registration and replaced with per-layer registration:

- If a layer has `intake_interval`, scheduler registers recurring event:
  - `tag = consolidation`
  - `message = intake:<layer_id>`

Scheduler callback routing:

- Parses `intake:<layer_id>` and calls intake for each known agent using that layer target.

Files:

- `src/kernel/consolidation/ConsolidationPipeline.cpp`
- `include/animus_kernel/consolidation/ConsolidationPipeline.h`
- `src/kernel/AgentKernel.cpp`

### 4) Intake routing + prompt usage

Intake execution now supports explicit target layer:

- `RunIntake(agentId, targetLayerId, error)`
- diary intake deposits into target layer (not lowest layer fallback when target given)
- intake prompt override uses layer `consolidation_intake_prompt` when present

Review prompt separation:

- review consolidation path now uses `consolidation_review_prompt`

Files:

- `src/kernel/consolidation/ConsolidationPipeline.cpp`

### 5) Session-turn intake processing flags

Added turn-level processing metadata:

- `intake_processed` (`bool`)
- `intake_processed_at_unix_ms` (`uint64`)

Persisted in both stores:

- SQLite `session_turns` schema + migration + read/write mapping
- file-backed store JSON read/write mapping

Session intake behavior:

- intake scans unprocessed turns for the agent
- after successful intake run parse, marks scanned turns processed and flushes sessions

Files:

- `include/animus_kernel/Session.h`
- `src/kernel/session/SqliteSessionStore.cpp`
- `src/kernel/session/FileSessionStore.cpp`
- `src/kernel/consolidation/ConsolidationPipeline.cpp`

### 6) Locale-aware default prompt endpoint + UI controls

Added backend template endpoint:

- `GET /api/v1/memory/templates/{kind}?locale=<locale>`
  - `kind`: `review` or `intake`
  - locale fallback: exact locale -> base language -> `en`
  - template root fallback: `templates/` and `../templates/`

Layer form updates:

- intake toggle: `Enable intake`
- conditional intake fields when enabled:
  - `intake_interval` cron input
  - `consolidation intake prompt`
- renamed review prompt form field to consolidation review prompt
- added `Load Default Prompt` buttons for intake/review using locale endpoint

Files:

- `src/kernel/admin/internal/AdminServerRoutesInterfacesSessionsMemory.inc`
- `src/kernel/admin/AdminServerRoutes.cpp`
- `admin-ui/src/views/MemoryView.vue`

### 7) Test updates

Updated tests to match new constructor/signature and endpoint coverage:

- consolidation tests (`RunIntake` signature + constructor dependency)
- admin server tests (template endpoint contract)

Files:

- `tests/ConsolidationTests.cpp`
- `tests/AdminServerTests.cpp`

## Acceptance Criteria Check

- [x] `memory_layers` supports nullable `intake_interval` and `consolidation_intake_prompt`
- [x] API exposes `consolidation_review_prompt` (with compatibility alias)
- [x] Backend enforces single active intake layer
- [x] Scheduler intake registration is layer-owned
- [x] Scheduled intake routes into selected layer
- [x] Session turns track intake processing state and avoid reprocessing
- [x] Layer form supports intake toggle + conditional fields
- [x] Locale-aware default prompt loading implemented with `en` fallback
- [x] Tests updated for schema/route/signature changes

## Validation

- `cmake --build build -j4` passed
- `ctest -R "AdminServerTests|ConsolidationTests" --output-on-failure` passed
- `cd admin-ui && npm run build` passed

## Clarification Logged

During closure, product direction confirmed that turns are append-only in this workflow (we revisit threads by consuming new turns). Therefore, explicit “reset intake flag on edited turn” behavior was intentionally not expanded in this pass.

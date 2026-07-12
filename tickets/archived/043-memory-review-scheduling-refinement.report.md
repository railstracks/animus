# Ticket 043 — Memory Review Scheduling Refinement: Implementation Report

**Status:** CLOSED  
**Completed:** 2026-05-10

## Summary

Implemented the review-scheduling refinement by moving observation eligibility from implicit age math at query time to explicit per-observation due timestamps (`next_review_at_ms`), while aligning admin API/UI with the post-042 memory layer contract.

## Implemented

### 1) Observation due-time model

Added explicit due timestamp support:

- `observations.next_review_at_ms` persisted in SQLite
- migration/backfill for existing observations
- review eligibility query now uses:
  - `next_review_at_ms <= now`

Files:

- `include/animus_kernel/MemoryStore.h`
- `src/kernel/memory/MemoryStore.cpp`

### 2) Lifecycle semantics wired

Updated observation lifecycle behavior so next review is explicit:

- creation computes future `next_review_at_ms` from layer policy
- retain updates both `last_evaluated_at_ms` and next due time
- promote/demote move logic recomputes due time for destination layer

Files:

- `src/kernel/memory/MemoryStore.cpp`
- `src/kernel/consolidation/ConsolidationPipeline.cpp`

### 3) Consolidation query path updated

Layer consolidation now pulls observations due for review via explicit timestamp predicate rather than derived interval delta checks.

File:

- `src/kernel/consolidation/ConsolidationPipeline.cpp`

### 4) Admin API contract alignment

Aligned layer and observation contract with post-042 shape:

- layer fields represented as:
  - `horizon`
  - `sort_order`
  - `evaluation_interval_seconds`
  - `cron_expr`
- observation payload includes:
  - `last_evaluated_at_ms`
  - `next_review_at_ms`

Also removed remaining drift between list/read and create/update paths by using SQLite-backed memory layer shape consistently.

Files:

- `include/animus_kernel/admin/MemoryManager.h`
- `src/kernel/admin/MemoryManager.cpp`
- `src/kernel/admin/internal/AdminServerRoutesInterfacesSessionsMemory.inc`

### 5) Admin UI alignment

Updated memory layer form/view model to use post-042 fields and display operational review timing details (`next_review_at_ms`) on observations.

File:

- `admin-ui/src/views/MemoryView.vue`

### 6) Test updates

Updated tests to match contract + scheduling semantics:

- consolidation tests now validate future review scheduling and due behavior
- admin server tests updated for layer response contract

Files:

- `tests/ConsolidationTests.cpp`
- `tests/AdminServerTests.cpp`

## Acceptance Criteria Check

- [x] UI moved off `horizon_seconds` / `consolidation_interval_seconds`
- [x] UI/API expose `horizon`, `cron_expr`, `evaluation_interval_seconds`, `sort_order`
- [x] Layer list uses SQLite-backed contract consistently
- [x] Layer responses include observation count / perspective presence
- [x] `next_review_at_ms` added and migrated
- [x] Intake-created observations receive future review due time
- [x] Retain updates `last_evaluated_at_ms` + `next_review_at_ms`
- [x] Promote/demote recompute destination-layer due time
- [x] Review query uses explicit due timestamp
- [x] Consolidation/Admin tests updated for new behavior

## Validation

At implementation time:

- `cmake --build build` passed
- `ctest -R "ConsolidationTests|AdminServerTests" --output-on-failure` passed
- `cd admin-ui && npm run build` passed

## Known Caveat During 043 Window

During the 043 implementation window, full-suite `SchedulerTests` had existing failures unrelated to this ticket’s memory review scheduling changes. Targeted consolidation/admin validation for this ticket passed.

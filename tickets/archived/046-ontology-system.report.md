# Ticket 046 — Ontology System: Tree-Structured Semantic Memory: Implementation Report

**Status:** CLOSED  
**Completed:** 2026-05-11

## Summary

Implemented a separate ontology model (not a replacement for memory layers) with tree-structured entities, typed properties, and observation-backed provenance. Added observation lifecycle state (`new/current/deprecated`), integrated phase-2 ontology reconciliation into intake, exposed read-only ontology APIs, and shipped an admin ontology explorer page.

## Implemented

### 1) Observation memory state model

Added `memory_state` to SQL observations and runtime model:

- `MemoryState::New`
- `MemoryState::Current`
- `MemoryState::Deprecated`

Behavior added:

- default state on new observations is `new`
- list/query ordering prioritizes `current` > `new` > `deprecated`
- review flow can update observation state via consolidation decisions

Files:

- `include/animus_kernel/MemoryStore.h`
- `src/kernel/memory/MemoryStore.cpp`
- `src/kernel/admin/MemoryManager.cpp`

### 2) OntologyStore (SQLite-backed)

Added new ontology storage subsystem:

- `ontology_entities`
- `ontology_properties`
- `ontology_mutations`

Implemented:

- entity listing/get/path lookup/create/update/move/delete
- path creation helper (`EnsureEntityPath`)
- property upsert (`SetProperty`) with typed values
- mutation audit log with `previous_state` and `new_state`

Files:

- `include/animus_kernel/OntologyStore.h`
- `src/kernel/ontology/OntologyStore.cpp`
- `CMakeLists.txt`

### 3) Provenance + state/orphan propagation

Implemented provenance and lifecycle propagation rules:

- properties link to backing observations (`linked_observation_id` nullable)
- property memory state syncs from linked observation state
- if linked observation is deleted, property is orphaned (`NULL`) and deprecated
- startup/batch sync available via `SyncPropertyStatesFromObservations`

Files:

- `src/kernel/ontology/OntologyStore.cpp`
- `src/kernel/AgentKernel.cpp`

### 4) Two-phase intake with ontology gardener

Extended intake flow:

- phase 1: extract/store observations from diary + unprocessed session turns
- phase 2: reconcile fresh observations into ontology updates

Implemented ontology reconciliation execution in consolidation pipeline:

- parses `upserts` payload from model output
- creates/fetches entity paths
- upserts properties and links observation IDs
- applies state sync after reconciliation

Files:

- `include/animus_kernel/consolidation/ConsolidationPipeline.h`
- `src/kernel/consolidation/ConsolidationPipeline.cpp`

### 5) Ontology prompt template support

Extended template endpoint kinds:

- existing: `review`, `intake`
- added: `ontology`

Added default template:

- `templates/consolidation-ontology/en.md`

Files:

- `src/kernel/admin/internal/AdminServerRoutesInterfacesSessionsMemory.inc`
- `templates/consolidation-ontology/en.md`

### 6) Admin API: ontology read endpoints

Added read-only ontology endpoints:

- `GET /api/v1/ontology/categories`
- `GET /api/v1/ontology/entities`
- `GET /api/v1/ontology/entities/{id}`
- `GET /api/v1/ontology/entities/{id}/properties`
- `GET /api/v1/ontology/entities/{id}/tree`
- `GET /api/v1/ontology/mutations`

Files:

- `include/animus_kernel/AdminServer.h`
- `src/kernel/admin/internal/AdminServerRoutesInterfacesSessionsMemory.inc`
- `src/kernel/admin/AdminServerRoutes.cpp`

### 7) Admin UI: ontology explorer

Added `/ontology` page with:

- category selector + counts
- entity list (path/depth visualized)
- entity detail panel
- properties table with state chips and linked observation IDs

Wired route/sidebar/i18n:

- route: `/ontology`
- sidebar nav item
- locale keys (en + nl)

Files:

- `admin-ui/src/views/OntologyView.vue`
- `admin-ui/src/router/index.ts`
- `admin-ui/src/components/AppSidebar.vue`
- `admin-ui/src/i18n/locales/en.ts`
- `admin-ui/src/i18n/locales/nl.ts`

### 8) Tests

Added and updated tests:

- new ontology storage tests:
  - entity path creation
  - property upsert + linkage
  - observation-state sync
  - orphaned property deprecation
- consolidation tests updated for constructor/signature changes
- admin server tests extended for ontology endpoints

Files:

- `tests/OntologyStoreTests.cpp`
- `tests/ConsolidationTests.cpp`
- `tests/AdminServerTests.cpp`

## Acceptance Criteria Check

- [x] Observation `memory_state` model added and persisted
- [x] Observation ordering updated by lifecycle priority
- [x] Ontology entity/property/mutation schemas added
- [x] Ontology property upsert with observation link and state inheritance
- [x] Orphan handling uses nullable observation link + deprecated state
- [x] Two-phase intake integrates ontology reconciliation after observation extraction
- [x] Ontology prompt template category added (`consolidation-ontology`)
- [x] Admin API exposes ontology categories/entities/tree/properties/mutations
- [x] Admin UI `/ontology` explorer implemented
- [x] Test coverage added for ontology store behavior and route integration

## Validation

- `cmake --build build -j4` passed
- `ctest -R "OntologyStoreTests|ConsolidationTests|AdminServerTests" --output-on-failure` passed
- `cd admin-ui && npm run build` passed
- full `ctest --output-on-failure`: all ticket-relevant suites passed; known existing failure remains in `SchedulerTests`

## Notes

- Ontology remains consolidation-driven (read-only in admin UI/API).
- Context injection from ontology was intentionally left out per ticket scope and will be addressed in follow-up work.

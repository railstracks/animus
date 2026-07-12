# Ticket 017 Report: Constitutional Memory Layer System

**Status:** ✅ Complete (Phase A)
**Commits:** 0fced95, 36f0bfc, ade3a79

## What was done

### Phase A1: MemoryStore backend + REST API (0fced95)
- `MemoryStore` class: SQLite-backed storage for layers, observations, perspectives, mutations
- Schema auto-created on startup: `memory_layers`, `observations`, `layer_perspectives`, `memory_mutations`
- Full CRUD operations:
  - Layers: `ListLayers`, `GetLayer`, `CreateLayer`, `UpdateLayer`, `DeleteLayer`
  - Observations: `ListObservations`, `GetObservation`, `CreateObservation`, `UpdateObservation`, `DeleteObservation`, `MoveObservation`
  - Perspectives: `GetPerspective`, `SetPerspective` (upsert)
  - Mutations: `LogMutation`, `QueryMutations`
- 9 REST API endpoints wired into AdminServer
- `GetLowestLayer()` convenience for intake process
- Auto-creates perspective row when layer is created
- Append-only mutation audit trail with promotion/demotion tracking
- Wired into `AgentKernel` lifecycle

### Phase A2: Admin UI (36f0bfc)
- Complete rewrite of `MemoryView.vue` to match actual REST API
- Layer CRUD: create, edit, delete memory layers
- Observation management: add, delete observations per layer
- Perspective editor: view and edit retrospective/current/future with valence
- Delete confirmations for layers and observations
- Layers sorted by horizon (shortest first)
- Duration formatting helper (seconds → human readable)

### Phase A3: Enabled flag (ade3a79)
- `enabled` column on `memory_layers` (defaults to false for new layers)
- Schema migration for existing DBs
- UI shows Active/Disabled badge per layer
- Consolidation pipeline will only process enabled layers

## Acceptance Criteria

- [x] SQLite database auto-created on first startup
- [x] Schema migrations run cleanly
- [x] All CRUD API endpoints functional
- [x] Admin UI can create, edit, delete memory layers
- [x] Observations can be added/removed per layer
- [x] Perspectives can be viewed and edited per layer
- [x] Layers listed sorted by horizon (shortest first)
- [x] Existing observation intake writes to SQLite (via `MemoryStore`)
- [x] Mutation audit trail populated on observation creation
- [x] Full build + tests pass

## What's next (Phase B — future ticket)
- Consolidation scheduler (time-based per layer)
- LLM-powered observation extraction (intake layer)
- LLM-powered promotion/demotion scoring (upper layers)
- Perspective revision during consolidation

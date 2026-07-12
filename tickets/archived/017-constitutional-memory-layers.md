# Ticket 017: Constitutional Memory Layer System

## Summary

Implement a multi-layered constitutional memory system backed by SQLite, with CRUD admin UI, consolidation pipeline, and mutation audit trail.

## Background

The memory system needs temporal stratification — different horizons for different kinds of memory. Each layer has its own consolidation cycle, observations, and perspectives. The bottom layer serves as the intake buffer (extracting observations from raw interactions), while upper layers score and promote/demote observations from the layer below.

## Architecture

### Data Store

Single SQLite database (`state/memory.db`) for all memory state. No JSON file layer storage.

### Schema

```sql
-- Memory layers
CREATE TABLE memory_layers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    horizon_seconds INTEGER NOT NULL,           -- time interval this block represents
    consolidation_interval_seconds INTEGER NOT NULL,  -- time between auto-consolidation
    consolidation_prompt TEXT NOT NULL DEFAULT '',     -- instructions for the consolidation LLM call
    token_budget INTEGER NOT NULL DEFAULT 4096,        -- max tokens this layer contributes to prompt
    created_at_unix_ms INTEGER NOT NULL,
    updated_at_unix_ms INTEGER NOT NULL
);

-- Observations (belong to a layer, migrate between layers)
CREATE TABLE observations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    layer_id INTEGER NOT NULL REFERENCES memory_layers(id),
    text TEXT NOT NULL,
    weight REAL NOT NULL DEFAULT 1.0,
    decay_rate REAL NOT NULL DEFAULT 0.95,
    tags TEXT NOT NULL DEFAULT '[]',            -- JSON array of strings
    source TEXT NOT NULL DEFAULT '',             -- e.g. "ws_chat", "consolidation"
    created_at_unix_ms INTEGER NOT NULL,
    updated_at_unix_ms INTEGER NOT NULL
);

-- LLM-managed perspectives per layer
CREATE TABLE layer_perspectives (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    layer_id INTEGER NOT NULL UNIQUE REFERENCES memory_layers(id),
    retrospective TEXT NOT NULL DEFAULT '',      -- look back on previous horizon
    retrospective_valence TEXT NOT NULL DEFAULT '',  -- affective tone of retrospective
    current_perspective TEXT NOT NULL DEFAULT '',  -- current posture / sense of self
    current_valence TEXT NOT NULL DEFAULT '',
    future_perspective TEXT NOT NULL DEFAULT '',   -- expectations for next interval
    future_valence TEXT NOT NULL DEFAULT '',
    updated_at_unix_ms INTEGER NOT NULL
);

-- Append-only mutation audit trail
CREATE TABLE memory_mutations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    mutation_type TEXT NOT NULL,                 -- 'observation_created', 'observation_promoted', 'observation_demoted', 'perspective_revised', 'observation_decayed'
    target_type TEXT NOT NULL,                   -- 'observation' or 'perspective'
    target_id INTEGER NOT NULL,
    from_layer_id INTEGER,                       -- NULL for creation
    to_layer_id INTEGER,                         -- NULL for deletion/demotion
    previous_state TEXT NOT NULL DEFAULT '',      -- JSON snapshot of previous state
    motivation TEXT NOT NULL DEFAULT '',          -- why this mutation happened
    unix_ms INTEGER NOT NULL
);
```

### Layer Properties

**User-managed (set via admin UI):**
- `name` — human-readable layer name
- `horizon_seconds` — time interval this block represents (1h, 1d, 1w, etc.)
- `consolidation_interval_seconds` — time between automated consolidation cycles
- `consolidation_prompt` — prompt instructions for the consolidation LLM call
- `token_budget` — max tokens contributed to prompt

**LLM-managed (revised during consolidation):**
- `observations` — child objects within the layer
- `retrospective` — look back on previous horizon
- `current_perspective` — current posture and sense of self
- `future_perspective` — expectations for next interval
- Each perspective has its own `valence` — affective tone of that perspective

### Consolidation Flow

Two-phase process:

1. **Intake layer consolidation** — extracts observations from raw interactions (chat messages, events). The bottom layer is the intake buffer.

2. **Upper layer consolidation** — scans observations currently in the layer that have reached the consolidation interval threshold. Scores observations for:
   - **Promotion** — move up to the next layer
   - **Demotion** — remove (decay below threshold)
   - **Retention** — keep in current layer
   
   Also revises retrospective, current_perspective, and future_perspective.

Consolidation is triggered by time interval per layer. Each layer only looks one level down.

### Observations

Observations belong to a layer and migrate between layers. Properties:
- `text` — the observation content
- `weight` — current importance (0.0–1.0+)
- `decay_rate` — how fast weight decreases over time
- `tags` — categorization
- `source` — where it came from

## Scope

### Phase A: CRUD + Storage (this ticket)
1. SQLite database initialization + schema migration
2. C++ data access layer for layers, observations, perspectives, mutations
3. REST API endpoints for memory layer CRUD:
   - `GET /api/v1/memory/layers` — list layers (sorted by horizon)
   - `POST /api/v1/memory/layers` — create layer
   - `PUT /api/v1/memory/layers/:id` — update user-managed properties
   - `DELETE /api/v1/memory/layers/:id` — delete layer (cascades observations)
   - `GET /api/v1/memory/layers/:id/observations` — list observations for layer
   - `POST /api/v1/memory/layers/:id/observations` — create observation
   - `DELETE /api/v1/memory/observations/:id` — delete observation
   - `GET /api/v1/memory/layers/:id/perspective` — get LLM-managed perspectives
   - `PUT /api/v1/memory/layers/:id/perspective` — update perspectives
4. Admin UI: Memory Layers view with CRUD for layers, inline observation management
5. Replace current JSON observation file with SQLite-backed storage

### Phase B: Consolidation Pipeline (future ticket)
- Consolidation scheduler (time-based per layer)
- LLM-powered observation extraction (intake layer)
- LLM-powered promotion/demotion scoring (upper layers)
- Perspective revision during consolidation
- Mutation audit logging

## Acceptance Criteria

- [ ] SQLite database auto-created on first startup
- [ ] Schema migrations run cleanly
- [ ] All CRUD API endpoints functional
- [ ] Admin UI can create, edit, delete memory layers
- [ ] Observations can be added/removed per layer
- [ ] Perspectives can be viewed and edited per layer
- [ ] Layers listed sorted by horizon (shortest first)
- [ ] Existing observation intake (from chat handler) writes to SQLite
- [ ] Mutation audit trail populated on observation creation
- [ ] Full build + tests pass

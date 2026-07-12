# Ticket 041 — Memory Consolidation Pipeline

**Priority:** P2 (depends on memory layers + multi-tenant)
**Status:** CLOSED
**Dependencies:** Ticket 017 (Memory Layers), Ticket 029 (Multi-tenant)

## Summary

Implement the consolidation pipeline — the background process that creates, refines, and migrates observations between memory layers. This is the *only* path for observation creation; agents don't write directly to memory layers.

## Architecture

Three distinct processes run on configurable schedules:

```
┌─────────────────────────────────────────────────────────────┐
│  INTAKE                                                      │
│  Scans unprocessed agent experiences (prompt chains)          │
│  → produces structured observations                          │
│  → deposits into base layer (immediate)                       │
├─────────────────────────────────────────────────────────────┤
│  LAYER CONSOLIDATION                                          │
│  Per-layer evaluation: score observations by relevance        │
│  → promote upward (distillation)                              │
│  → demote downward (fading relevance)                         │
│  → archive from bottom layer (expired)                        │
│  → updates perspectives (retrospective/current/future)        │
├─────────────────────────────────────────────────────────────┤
│  PERSPECTIVE REVISION                                         │
│  Re-evaluates layer perspectives after consolidation           │
│  → updates retrospective, current_perspective, future fields   │
└─────────────────────────────────────────────────────────────┘
```

## Intake

### What it does
- Periodically examines all unprocessed agent activity (session turns, diary entries, study reports) that hasn't yet been consolidated
- Runs an LLM-powered evaluation on each batch: "What observations are worth keeping from this activity?"
- Creates structured `MemoryObservation` objects — text content, auto-generated tags, initial weight
- Deposits observations into the base (immediate) layer
- Marks processed activity with a watermark so it isn't re-processed

### Special consideration: `pin`
When the agent has pinned an observation with a reason, the intake process sees that reason during its evaluation pass. The pin is an annotation on existing observations, not new raw material — but it influences how intake judges significance during subsequent cycles.

### Configuration
```json
{
    "intake": {
        "enabled": true,
        "interval_minutes": 5,
        "batch_size": 50,
        "llm_provider": "any",
        "llm_model": "auto",
        "prompt": "Review the following agent activity and extract lasting observations…",
        "min_weight": 0.1
    }
}
```

## Layer Consolidation

### What it does
For each layer (bottom to top, excluding permanent), on its configured `consolidation_interval`:
1. Evaluates all observations in the layer against the layer's consolidation prompt
2. Scores each observation for: current relevance, connection to higher-layer themes, decay since creation
3. Decides per observation: promote (move up one layer), retain (stay), demote (move down one layer, or archive from bottom)

### Demotion to archive
Observations demoted from the bottom layer become archived — they leave the active layer hierarchy but remain queryable via the `search` tool's `include_archive` flag. Archive is not a layer; it's a flat store of expired observations.

### Configuration
```json
{
    "consolidation": {
        "enabled": true,
        "default_llm_provider": "any",
        "default_llm_model": "auto"
    }
}
```

Per-layer overrides (in layer config):
```json
{
    "name": "day",
    "consolidation_interval": "1h",
    "consolidation_prompt": "Review these daily observations…",
    "token_budget": 20000
}
```

## Perspective Revision

### What it does
After a layer's consolidation pass completes, updates three LLM-generated perspective fields on the layer:
- **retrospective** — "Looking back: what patterns, lessons, and through-lines emerged in this period?"
- **current_perspective** — "Right now: what's active, what's blocked, what's the operational state?"
- **future_perspective** — "Looking ahead: what's expected, what risks, what opportunities?"

Perspectives are what gives the agent temporal grounding — they're not just a database of facts but a *narrative position* within time. The `inspect` action on the memory tool surfaces these.

### Configuration
```json
{
    "perspectives": {
        "enabled": true,
        "update_after_consolidation": true,
        "llm_provider": "any",
        "llm_model": "auto"
    }
}
```

## Consolidation Tool Functions

A `consolidation` composite function (not exposed to agents — kernel-internal) provides the operational primitives:

| Function | Description | Used by |
|----------|-------------|---------|
| `store` | Create a new observation in a specified layer | Intake |
| `promote` | Move observation up one layer | Layer consolidation |
| `demote` | Move observation down one layer (or archive from bottom) | Layer consolidation |
| `update_perspectives` | Re-evaluate and update layer perspectives | Perspective revision |
| `mark_processed` | Record watermark on processed activity | Intake |

These are kernel-level operations — not registered as agent-callable tools. This prevents agents from bypassing the consolidation process to write directly to memory.

## Scheduling

Consolidation runs on the kernel's scheduler (Ticket 027). Until the scheduler exists, a simple timer loop in the kernel suffices:
- Intake fires every N minutes (configurable)
- Each layer's consolidation fires on its own interval
- Perspective revision fires after each layer's consolidation pass

Runs are agent-scoped: the intake process processes one agent's unprocessed activity at a time. Layer consolidation operates on one agent's layers independently.

## Watermark System

To avoid re-processing, each consolidatable source maintains a watermark:

| Source | Watermark | Location |
|--------|-----------|----------|
| Session turns | Last processed `session_turn.id` | `consolidation_watermarks` table |
| Diary entries | Last processed `diary_entry.id` | `consolidation_watermarks` table |
| Study reports | Last processed `study_report.id` | `consolidation_watermarks` table |

```sql
CREATE TABLE consolidation_watermarks (
    agent_id TEXT NOT NULL,
    source TEXT NOT NULL,      -- 'session_turns', 'diary_entries', 'study_reports'
    last_processed_id INTEGER NOT NULL,
    last_run_unix_ms INTEGER NOT NULL,
    PRIMARY KEY (agent_id, source)
);
```

## Acceptance Criteria

- [ ] Intake process scans unprocessed agent activity and creates observations
- [ ] Intake deposits observations into the base (immediate) layer
- [ ] Watermark system prevents re-processing
- [ ] `store` function creates observations programmatically (not agent-facing)
- [ ] Layer consolidation evaluates and promotes/demotes/retains observations per config
- [ ] Demotion from bottom layer archives observations
- [ ] Perspective revision updates retrospective/current/future on each layer
- [ ] `pin` annotations (from memory tool) are visible to intake during evaluation
- [ ] All functions run per-agent (multi-tenant scoping)
- [ ] Configuration via kernel config JSON; admin API for runtime control
- [ ] Consolidation state and summary exposed via admin API
- [ ] Tests: intake batch, watermark, promote/demote flow, archive path, perspective update

## Notes

- The consolidation pipeline is the **sole writer** to memory layers. Agents read via the `memory` tool (Ticket 024); only consolidation creates and moves observations.
- This keeps the system normalized: agents consume memory, consolidation maintains it.
- Consolidation is inherently LLM-expensive — it calls an LLM for evaluation. Budget controls and scheduling prevent runaway costs.
- The `prompt` fields (intake prompt, consolidation prompt per layer) are user-configurable for tuning extraction quality.
- Permanent layer observations are never demoted or archived. They require admin-level editing.
- The existing `MemoryManager::BeginConsolidation` / `RunConsolidationJob` in the codebase is a stub — this ticket replaces it with the real pipeline.

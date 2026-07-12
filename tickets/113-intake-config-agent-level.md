# Ticket 113 — Intake Config: Layer-Level → Agent-Level

**Date:** 2026-07-12  
**Priority:** Medium (architectural cleanup before release)  
**Status:** Open

## Problem

Intake configuration (`intake_interval`, `consolidation_intake_prompt`) currently lives on `memory_layers`, but intake is fundamentally an agent-level operation:

- `RunIntake` scans all unprocessed turns/diary for an agent, not for a specific layer
- The LLM routes observations to whatever layer it deems appropriate (layer param in tool call)
- The intake layer is just a fallback target, not the actual routing mechanism
- Only one layer per agent can have intake enabled (forced invariant in MemoryManager)
- The "Perform Intake" button appears on one layer but triggers intake for the whole agent

This conflates two concerns: *how knowledge enters the system* (intake) vs *how knowledge is reviewed at each timescale* (layer consolidation).

Additionally, the memory page agent select shows a "Default" entry (agent_id = `'default'`) which is a seed/template artifact, not a real agent. It should be hidden.

## Changes

### 1. Agent-Level Intake Fields

Add to `Agent` struct + DB + CRUD:
- `intake_interval` (TEXT, nullable) — cron expression for intake cadence
- `intake_prompt` (TEXT, default empty) — override for intake LLM prompt

### 2. Remove Intake From Layers

- Remove `intake_interval` and `consolidation_intake_prompt` from the layer edit UI
- Keep DB columns for backward compatibility (migration reads existing values, copies to agent)
- `GetIntakeLayer()` deprecated — `RunIntake` no longer needs a specific layer, uses agent's config
- Single-intake-layer invariant code removed (no longer relevant)

### 3. Memory Page UI

- Remove "Default" from agent select; show agents only
- Move "Perform Intake" button from layer detail to agent header
- Intake interval + intake prompt fields move to agent-level settings area

### 4. ConsolidationPipeline

- `RegisterSchedules` reads `intake_interval` from agent config, not layer
- `RunIntake` reads agent's `intake_prompt` for the LLM system prompt
- Fallback target layer for observations: agent's lowest sort_order layer (existing behavior)

### 5. Memory Page — "Default" Agent Removal

- Filter the agent select to only show entries from the `agents` table
- The `'default'` agent_id in `memory_layers` is seed data, not a real agent
- Empty state: if no agents exist, show a prompt to create one

## Migration

On startup, if agent has `intake_interval` set on a layer but not on the agent record itself, copy it up:
```sql
UPDATE agents SET intake_interval = (
  SELECT intake_interval FROM memory_layers
  WHERE agent_id = agents.id AND intake_interval IS NOT NULL
  LIMIT 1
) WHERE intake_interval IS NULL;
```

## Acceptance Criteria

- [ ] Agent struct has `intake_interval` and `intake_prompt` fields
- [ ] DB migration adds columns + copies existing layer intake config to agent
- [ ] Layer edit dialog no longer shows intake fields
- [ ] "Perform Intake" button at agent level, not layer level
- [ ] POST `/api/v1/memory/agents/{id}/intake` endpoint (agent-level trigger)
- [ ] ConsolidationPipeline reads intake schedule from agent config
- [ ] Memory page agent select shows real agents only (no "Default")
- [ ] Empty state when no agents exist
- [ ] Build clean on workstation

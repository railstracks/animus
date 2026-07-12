# Ticket 083 — Per-Agent Memory Layer Ownership

**Status:** Draft  
**Priority:** High  
**Created:** 2026-06-11  
**Component:** Core / Memory System  

## Summary

Memory layers are currently global — every agent sees the same layer definitions. This blocks consolidation (the scheduler fires with agent `"system"` but turns belong to real agent IDs), prevents agents from having distinct memory configurations, and makes the admin UI unable to scope memory views by agent.

Refactor memory layers to be owned by individual agents, with copy-on-create from a default template.

## Problem Statement

### Consolidation is broken
The consolidation pipeline cannot match schedule-fired agent IDs to actual agents:
- Schedules fire with `agent_id = "system"` (set by `RegisterSchedules`)
- The gate check (`HasPendingIntakeData`) queries for that agent ID
- Actual session turns belong to real agent hashes (e.g., `ef6804aa0aafc8389825a99cf21a9786`)
- Result: 409 unprocessed turns in the database, zero observations created

Root cause: no agent-scoped memory ownership means the pipeline has no consistent agent ID to work with.

### No agent isolation
All agents share the same layer definitions. A trading agent and a creative agent see the same intake prompts, evaluation intervals, and consolidation schedules.

### Admin UI limitation
The semantic memory and episodic memory pages have no agent selector — they show data from all agents mixed together.

## Changes

### 1. Database: Add agent_id to memory_layers
```sql
ALTER TABLE memory_layers ADD COLUMN agent_id TEXT NOT NULL DEFAULT 'default';
```
Existing rows become owned by the `"default"` agent.

### 2. Database: Stale schedule cleanup
Remove the three stale consolidation schedules with `agent_id = "default"` and message `intake:1`:
```
94bfe876e61fa3c70000000000000000  (last fire 2026-05-20)
db0651be82ea72990000000000000000  (last fire 2026-06-10)
fe573153219e3b040000000000000002  (last fire 2026-06-11)
```
These predate the deduplication cleanup in `RegisterSchedules` (which only cancels `agent_id = "system"` schedules).

### 3. MemoryStore: Scope queries by agent
- `ListLayers()` → `ListLayers(agentId)` (or keep unscoped version for admin)
- `GetIntakeLayer()` → `GetIntakeLayer(agentId)`
- `GetLowestLayer()` → `GetLowestLayer(agentId)`
- All observation queries already have agent scoping — layers are the gap

### 4. Agent creation: Copy layers from default template
When a new agent is created:
1. Query all layers where `agent_id = 'default'`
2. Insert copies with the new agent's ID, same structure and config
3. The new agent gets an identical memory stack — intake prompts, evaluation intervals, cron schedules

This means every agent starts with `day → week → month → year → decade → century → millennium` by default. Agents can later be reconfigured independently.

### 5. Consolidation pipeline: Fix agent resolution
The consolidation handler in `AgentKernel.cpp` should:
- **Gate check:** Query all known agents (from agent store), check if *any* has pending intake data
- **Schedule registration:** Register schedules per real agent, not as `"system"` with a generic tag
- **Intake execution:** Already correct — `RunIntake` iterates over `m_agentStore->List()`
- **Layer review:** Scope `ListObservationsDueForReview` to the real agent ID, not the schedule metadata agent ID

### 6. Admin UI: Agent selector on memory pages
Add an agent dropdown to:
- **Semantic Memory** page (observations)
- **Episodic Memory** page (diary entries)
- **Memory Layers** configuration page

When "default" is selected, show the template configuration. For named agents, show their scoped layers and data.

## Migration Plan

1. Deploy schema migration (add `agent_id` column, backfill with `"default"`)
2. Clean up stale schedules
3. Deploy code changes (scoped queries, agent resolution fixes)
4. Restart daemon — consolidation should immediately process the 409-turn backlog
5. Update admin UI with agent selectors

## Scope

- `memory_layers` table schema change
- `MemoryStore` query scoping
- `ConsolidationPipeline` agent resolution
- `AgentKernel` consolidation handler agent resolution
- Agent creation hook (copy-on-create)
- Admin UI agent selectors
- Stale schedule cleanup

**Out of scope:** Per-agent diary isolation (diary entries already have agent scoping), per-agent ontology (global for now), custom layer structures per agent type (all agents get the same template, configurable post-creation).

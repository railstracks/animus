# Ticket 004 — Memory Layer API

**Priority:** P1
**Status:** Open
**Dependencies:** Ticket 001, Constitutional Memory Architecture (AGENTS.constitutional_memory.md)

## Summary

REST API endpoints for browsing memory layers, inspecting observations, and triggering consolidation.

## Scope

- `GET /api/v1/memory/layers` — List configured layers with metadata (name, description, observation count, retention policy)
- `GET /api/v1/memory/layers/:name/observations` — Observations in a layer (paginated, sortable by weight/age)
- `GET /api/v1/memory/observations/:id` — Single observation with full metadata (tags, weight, decay_rate, timestamps, content)
- `PATCH /api/v1/memory/observations/:id` — Edit observation metadata (tags, weight)
- `DELETE /api/v1/memory/observations/:id` — Remove/archive an observation
- `POST /api/v1/memory/consolidate` — Trigger a consolidation run
- `GET /api/v1/memory/consolidation/status` — Last run timestamp, current state (idle/running), summary of last run (promoted/demoted/archived counts)

## Acceptance Criteria

- [ ] Layer list reflects the configurable temporal layer structure from constitutional memory design
- [ ] Observations are paginated (default 50, max 200 per page)
- [ ] Consolidation trigger returns immediately with a job ID; status endpoint tracks progress
- [ ] Observation detail includes all metadata fields from the observation format spec
- [ ] PATCH supports partial metadata update (only included fields)

## Notes

- This ticket assumes the constitutional memory architecture is at least partially implemented. If memory layer storage doesn't exist yet, this ticket should block on that foundation work.
- Consolidation runs may be long-running; use the existing JobSystem for async execution.

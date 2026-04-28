# Ticket 008 — Constitution API

**Priority:** P2
**Status:** Open
**Dependencies:** Ticket 001, Constitutional Memory Architecture

## Summary

REST API endpoints for reading the agent's constitutional layers (immutable core, contracts, adaptation rules) and modifying the adaptation layer.

## Scope

- `GET /api/v1/constitution` — Full constitution: all layers with their current state
- `GET /api/v1/constitution/core` — Core/constitution layer (immutable values, identity constraints) — read-only
- `GET /api/v1/constitution/contracts` — Contract layer (relationships, commitments) — read-only via API
- `GET /api/v1/constitution/adaptation` — Adaptation layer (configurable behaviors, operational patterns)
- `PATCH /api/v1/constitution/adaptation` — Update adaptation rules (the only mutable layer via API)
- Core layer changes require a separate governance process (documented, not API-accessible)

## Acceptance Criteria

- [ ] Constitution endpoint returns all layers with clear layer-type labels
- [ ] Core layer is strictly read-only (PATCH returns 405)
- [ ] Adaptation layer supports partial updates
- [ ] Changes to adaptation layer are logged (audit trail)

## Notes

- The governance process for core layer changes is intentionally outside the API. This is a design choice — core identity changes require human oversight and ceremony, not a PATCH request.

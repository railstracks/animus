# Ticket 002 — Agent Configuration API

**Priority:** P0
**Status:** Open
**Dependencies:** Ticket 001

## Summary

REST API endpoints for reading and modifying agent configuration at runtime — model provider, model name, temperature, system prompt, and general settings.

## Scope

- `GET /api/v1/agent` — Full agent config (model, temperature, system prompt, budget settings)
- `PATCH /api/v1/agent` — Partial update of agent config (hot-reload where supported)
- `GET /api/v1/agent/model` — Current model details (provider, model ID, context window info)
- `PUT /api/v1/agent/model` — Switch model/provider (requires re-initialization of LLM client)
- Config changes persisted to config file on disk
- Validation: reject invalid model names, out-of-range temperatures, etc.

## Acceptance Criteria

- [ ] All endpoints return valid JSON with correct content types
- [ ] PATCH partially updates config (only included fields)
- [ ] Model switch triggers LLM client re-initialization
- [ ] Invalid values return 400 with descriptive error
- [ ] Changes persist across restart (written to config file)

## Notes

- Model switching mid-session: what happens to active ChainInstances? Define policy (drain current, reject switch while active, or force-terminate).
- Sensitive fields (API keys) must never appear in GET responses — return masked values.

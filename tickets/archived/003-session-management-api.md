# Ticket 003 — Session Management API

**Priority:** P0
**Status:** Open
**Dependencies:** Ticket 001

## Summary

REST API endpoints for listing, inspecting, and terminating agent sessions.

## Scope

- `GET /api/v1/sessions` — List all sessions (active and recent inactive)
- `GET /api/v1/sessions/:id` — Session details: metadata, message count, created/last-active timestamps
- `GET /api/v1/sessions/:id/history` — Conversation history for a session (paginated)
- `DELETE /api/v1/sessions/:id` — End/terminate a session
- `GET /api/v1/sessions/:id/context` — Active context: which memory layers are loaded, which tools are available, current token budget state

## Acceptance Criteria

- [ ] Session list returns all sessions with summary metadata
- [ ] History endpoint supports pagination (`?page=N&limit=M`)
- [ ] DELETE gracefully ends session (drains in-progress chain, cleans up resources)
- [ ] Context endpoint accurately reflects loaded memory layers and tool state

## Notes

- History pagination should default to most recent messages first
- Consider rate-limiting history endpoint for large sessions

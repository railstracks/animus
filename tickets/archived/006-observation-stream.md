# Ticket 006 — Live Observation Stream (WebSocket)

**Priority:** P2
**Status:** Open
**Dependencies:** Ticket 001, Ticket 004

## Summary

WebSocket endpoint that pushes captured observations in real-time as they're recorded by the agent. Useful for monitoring what the agent is paying attention to.

## Scope

- `WS /ws/observations` — WebSocket endpoint
- Server pushes new observations as they're captured:
  - `{ "type": "observation", "id": "...", "layer": "...", "tags": [...], "content": "...", "timestamp": "..." }`
- Client can optionally filter:
  - `{ "type": "filter", "layers": ["day", "week"], "tags": ["infrastructure"] }`
- Reconnection support: client can send last-seen timestamp to catch up

## Acceptance Criteria

- [ ] New observations are pushed to connected clients within 1 second of capture
- [ ] Filter reduces stream to matching layers/tags
- [ ] Multiple clients can connect simultaneously
- [ ] Graceful handling when no observations are being captured (no spam)

## Notes

- This is a nice-to-have for observability, not critical for initial release
- Rate-limit if observation volume is very high

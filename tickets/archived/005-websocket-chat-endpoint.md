# Ticket 005 — WebSocket Chat Endpoint

**Priority:** P0
**Status:** Open
**Dependencies:** Ticket 001

## Summary

WebSocket endpoint for real-time conversational sessions with the agent. Supports streaming token output, session management, and context display.

## Scope

- `WS /ws/chat` — WebSocket endpoint
- Client → Server messages:
  - `{ "type": "message", "content": "...", "session_id": "optional" }` — Send a message (creates new session if no session_id)
  - `{ "type": "list_sessions" }` — Request active session list
- Server → Client messages:
  - `{ "type": "token", "content": "..." }` — Streaming token (one per token/chunk from LLM)
  - `{ "type": "done", "session_id": "...", "message_id": "..." }` — Response complete
  - `{ "type": "error", "message": "..." }` — Error notification
  - `{ "type": "sessions", "data": [...] }` — Response to list_sessions
  - `{ "type": "context", "layers": [...], "tools": [...] }` — Active context for current session
- Authentication: token-based handshake on connect (configurable; optional for localhost)
- Multiple concurrent connections supported (each can manage different sessions)

## Acceptance Criteria

- [ ] WebSocket connection established and maintained
- [ ] Sending a message creates a new session if none specified
- [ ] Token streaming delivers chunks as they arrive from LLM
- [ ] "done" message signals completion with session/message IDs
- [ ] Errors are surfaced as structured JSON, not WebSocket close frames
- [ ] Connection drops are handled gracefully (session persists, can reconnect)

## Notes

- This is the backbone for the Chat UI panel
- Consider message size limits for WebSocket frames
- Should integrate with the existing ChainRunner/ChainInstance pipeline — not bypass it

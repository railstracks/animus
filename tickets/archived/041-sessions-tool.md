# Ticket 041 — Sessions Tool

**Priority:** P1 (fills a basic capability gap — agents cannot interact with their own session history)
**Status:** Open
**Dependencies:** None (session infrastructure exists in kernel)
**Updated:** 2026-05-14

## Summary

A composite tool that lets agents list, search, reply to, and maintain notes on their active and past sessions. Agents currently have no tool-level access to the session layer — they receive inbound messages opaquely and respond into whatever channel the framework routed. This tool gives agents agency over their own conversation topology.

## Motivation

Agents wake up with only the current session in context. They cannot:
- See what other sessions are active
- Search past conversations for relevant context
- Send a message to a different ongoing session
- Maintain persistent working notes per session (scratchpad memory that survives across turns)

The sessions tool closes all of these gaps.

## Session Notes

Session notes are the key feature. They are **updateable persistent bullet points** stored per session, always injected into the agent's context when that session is active. They serve a different purpose than diary (reflective) or formal memory (consolidated):

| System | Purpose | Scope | Persistence |
|--------|---------|-------|-------------|
| Session notes | Working memory for this conversation | Per-session | Survives turns, dies with session |
| Diary | Private reflections and observations | Per-agent | Permanent |
| Memory (episodic) | Consolidated observations | Per-agent, per-layer | Permanent |

Session notes are for things like:
- "Thomas asked about the Midas deployment timeline — follow up after Standup"
- "We decided on PostgreSQL over SQLite for this feature"
- "Don't mention the API key issue again, already resolved"

They're the agent's equivalent of sticky notes on the edge of the monitor.

## Data Model

### session_notes table

| Column | Type | Description |
|--------|------|-------------|
| id | INTEGER (PK) | Autoincrement |
| session_key | TEXT | The session identifier |
| agent_id | TEXT | Owner agent |
| bullet | TEXT | Single bullet point text |
| sort_order | INTEGER | Display order within session |
| created_at | INTEGER | Unix ms timestamp |
| updated_at | INTEGER | Unix ms timestamp |

Multiple bullets per session. Agent can add, update, reorder, and remove them. All active bullets are injected into context at the start of each turn.

## Tool Interface

```json
{
  "name": "sessions",
  "description": "Access your session layer — list conversations, search history, reply to other sessions, and maintain working notes for the current session.",
  "actions": [
    "list — show active sessions with last message preview",
    "search — search across session history by keyword",
    "history — retrieve recent messages from a specific session",
    "reply — send a message to a specific session (creates a new turn)",
    "notes:list — show session notes for current session",
    "notes:add — add a bullet point",
    "notes:update — edit a bullet point",
    "notes:remove — delete a bullet point",
    "notes:reorder — change display order"
  ]
}
```

### Actions in detail

**`list`**
- Returns active sessions for this agent
- Each entry: session_key, interface/channel, last message preview, message count, age
- Optional filter: `status` (active, recent, all)

**`search`**
- Full-text search across all session turns for this agent
- Returns: session_key, matching turn excerpt, timestamp
- Pagination support

**`history`**
- Retrieve last N messages from a specific session
- Params: `session_key`, `limit` (default 10, max 50)

**`reply`**
- Send a message to a session identified by session_key
- Creates a new agent turn in that session
- The target session's interface handles actual delivery (routing)
- Security: agent can only reply to sessions it owns

**`notes:list`**
- Returns all bullet points for the current session, in order

**`notes:add`**
- Params: `bullet` (text), `position` (optional — "top", "bottom", or index)
- Default position: bottom (append)

**`notes:update`**
- Params: `note_id`, `bullet` (new text)

**`notes:remove`**
- Params: `note_id`

**`notes:reorder`**
- Params: `note_ids` (array, in desired order)

## Context Injection

When a session is active, its notes are injected into the agent's preamble:

```
## Session Notes
- Thomas asked about the Midas deployment timeline — follow up after standup
- We decided on PostgreSQL over SQLite
```

This injection is read-only from the agent's perspective — the agent sees the notes, but modifying them requires an explicit tool call (no accidental mutation).

## Admin API

```
GET    /api/v1/agents/{id}/sessions                          → list sessions
GET    /api/v1/agents/{id}/sessions/{key}/history            → session history
GET    /api/v1/agents/{id}/sessions/{key}/notes              → list notes
POST   /api/v1/agents/{id}/sessions/{key}/notes              → add note
PATCH  /api/v1/agents/{id}/sessions/{key}/notes/{nid}        → update note
DELETE /api/v1/agents/{id}/sessions/{key}/notes/{nid}        → delete note
```

## Acceptance Criteria

- [ ] `SessionsTool` class registered in tool registry
- [ ] `list` action returns active sessions for the calling agent
- [ ] `search` action performs FTS across session turns
- [ ] `history` action retrieves recent messages from a specific session
- [ ] `reply` action sends a message to another session (routed through interface)
- [ ] Session notes data model with CRUD operations
- [ ] Notes injected into agent preamble for the active session
- [ ] Agent cannot reply to sessions owned by other agents
- [ ] Admin API endpoints for session listing and notes management
- [ ] All existing tests continue to pass
- [ ] New tests: session listing, search, reply routing, notes CRUD, context injection

## Key Questions

- Should notes have a maximum count per session? (Suggestion: 20, with oldest auto-pruned)
- Should `reply` trigger immediate delivery or queue for next heartbeat?
- Should session search use the existing FTS infrastructure or a separate index?
- How long are "recent" sessions kept in the active list before aging out?
- Should agents be able to start entirely new sessions (not reply to existing)?

## Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Session search performance degrades with history | Low | FTS index on session turns; paginate results |
| Reply routing fails if target session's interface is disconnected | Medium | Return error; don't silently drop |
| Notes bloat context window | Medium | Cap note count; truncate long notes; warn on approach |
| Agent spams sessions with unsolicited replies | Medium | Rate limit; require existing session for reply (no cold-start) |

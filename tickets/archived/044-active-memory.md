# Ticket 044 — Active Memory (Context Construction)

**Priority:** P2 (the bridge between memory systems and agent context — essential for situatedness)
**Status:** Shipped (all phases + admin UI + bugfixes)
**Dependencies:** Ticket 024 (Memory Tools), Ticket 036 (Diary), Ticket 046 (Context Provider Registry)
**Updated:** 2026-05-15

## Summary

Active memory is a read-only context block injected into every session via the Context Provider Registry (ticket 046). It is constructed from the agent's memory systems — episodic observations, ontology entities, diary entries, memory files, and session state — and gives the agent situated awareness of its own history, current state, and pending work.

This is not a tool the agent calls. It is a **context construction pipeline** implemented as an `ActiveMemoryProvider` registered in the `ContextProviderRegistry` at priority 30.

## Architecture

Active memory is one provider among several in the registry:

```
ContextProviderRegistry (priority-ordered)
  ├── IdentityProvider (0)          → agent identity document
  ├── ActiveMemoryProvider (30)     → THIS TICKET
  ├── SessionNotesProvider (50)     → per-session working notes
  └── [future providers...]
```

The identity block and session notes block are handled by their own providers. ActiveMemoryProvider is responsible only for the blocks described below.

## What ActiveMemoryProvider Owns

### 1. Temporal Grounding Block (mandatory)
- Current date, time, day of week, timezone
- Time since last session
- Upcoming scheduled events (next 24h)
- Budget: ~100 tokens
- Always present

### 2. Episodic Memory Block
- Top-weighted observations per layer
- Filtered by recency (configurable decay) and weight
- Organized by layer
- Budget: per-layer budgets already defined in the memory system — no global override
- Priority: observations with weight > 0.7, or created in last 48h
- Each layer respects its own token allocation; this block is the sum of those allocations

### 3. Diary Presence Block
- Last 3 diary entry titles only (no content)
- Serves as a presence indicator — the agent knows recent reflections exist
- Content is loaded on demand when the agent reads or writes to the diary
- Budget: negligible (~50 tokens)

### 4. Ontology Block
- **No token budget at composition level** — instead, configurable max items and max properties per item
- Items pulled via two tiers:
  - **Tag-matched** (high weight): entities whose tags overlap with current session tags. Weight scales with number of matching tags (3 matching tags > 1 matching tag).
  - **Session context** (low weight): entities recently referenced in the current session's turns, regardless of tags. Prevents blind spots from requiring explicit tag assignment.
- Configuration: `max_items` (default ~15), `max_properties_per_item` (default ~5)
- Scope: current session only — broad recency creates noise
- Tag-matched items always rendered before session-context items

### What's NOT included (and why)

- **Recent activity**: already covered by short-term memory layer perspectives
- **Memory files**: loaded on demand through queries, not preloaded
- **Diary content**: the agent reads the diary when it matters; only titles shown here

## Construction Pipeline

```
┌─────────────────────────────────────────────┐
│      ActiveMemoryProvider::Provide()         │
│                                              │
│  1. Determine session context                │
│     - Session type (direct, gallivanting,    │
│       consolidation, project)                │
│     - Connected interface                    │
│     - Time of day                            │
│                                              │
│  2. Collect session tags                     │
│     - Agent-set tags                         │
│     - Auto-extracted tags                    │
│                                              │
│  3. Collect sub-blocks                       │
│     - Temporal grounding                     │
│     - Episodic observations per layer        │
│     - Diary titles (last 3)                  │
│     - Ontology entities (tag-matched +       │
│       session context)                       │
│                                              │
│  4. Assemble into single ContextBlock        │
│     - Structured markdown format             │
│     - Budget-managed per sub-block           │
│     - Session-type filtering applied         │
│     - Returned to registry for injection     │
└─────────────────────────────────────────────┘
```

## Budget Management

Total active memory budget is configurable per agent. Default: 4000 tokens (roughly 16KB of text).

Allocation strategy:
```
Temporal:        100 tokens          — fixed
Episodic:        per-layer budgets   — from memory system, no global override
Diary:           ~50 tokens          — titles only
Ontology:        item-count limited  — max_items + max_properties_per_item
```

Blocks that don't fill their budget release tokens to the overflow pool. Blocks that need more draw from it. Construction runs in priority order — higher-priority blocks get first claim on overflow.

## Output Format

Active memory is injected as a single `ContextBlock` with name "Active Memory" containing structured markdown:

```markdown
# ACTIVE MEMORY
Generated: 2026-05-14T08:00:00+02:00 | Session: direct (webchat) | Agent: Kestrel

## TEMPORAL CONTEXT
Current: Thursday, May 14, 2026, 08:00 CEST
Last session: 14h ago (2026-05-13 22:08)
Upcoming: Standup meeting (today 10:00)

## EPISODIC MEMORY
[day] Cooperative-only edge dynamics converge to uniformity (weight: 0.92)
[day] Differentiation requires scarcity — competition sculpts (weight: 0.88)
[week] FTS5 content= tables need explicit trigger management (weight: 0.75)

## RECENT DIARY
- Topolenia sector discovery (2026-05-14)
- Animus FTS fix and session pipeline (2026-05-14)
- Philosophy: corpus vs experience (2026-05-11)

## ONTOLOGY
[animus] AgentStore — SQLite-backed agent CRUD (3 properties)
[animus] MemoryStore — constitutional memory layers (5 properties)
[midas2] Portfolio — algorithmic trading positions (2 properties)
```

## Session-Type Variants

Different session types produce different active memory:

| Session Type | Included | Omitted |
|---|---|---|
| Direct (human chat) | All sub-blocks | None |
| Gallivanting | Temporal, episodic, diary | Ontology (unless tags match) |
| Consolidation | Temporal, consolidated layer observations | Diary, ontology |
| Project execution | Temporal, ontology | Episodic, diary |

## Session Tags

Sessions can be tagged with keywords by the agent (via the sessions tool) or by the router at session creation. Tags serve as **semantic retrieval keys** for the ontology block.

### Design

```json
{
  "session_key": "agent:main:slack:direct:u0a3st1kwhw",
  "tags": ["animus", "memory-system", "active-memory"],
  "auto_tags": ["animus"]
}
```

- **Agent tags**: explicitly set by the agent via `sessions:tags:set`
- **Auto tags**: extracted from initial message content by the session router (keyword matching against known project/entity names)
- Both types feed into ontology entity retrieval

### Ontology Retrieval via Tags

1. Collect all tags from the current session
2. Match tags against ontology entity names, aliases, and keywords
3. Retrieve matched entities (with their relationships)
4. Supplement with session-context entities (recently referenced in turns)
5. Sort: tag-matched first, then session-context. Within each tier, sort by match count (tags) or recency (context).
6. Truncate to `max_items` with `max_properties_per_item` each

### Session Tool Extensions

- `sessions:tags:set` — add tag to current session
- `sessions:tags:remove` — remove tag from current session
- `sessions:tags:list` — list current session's tags
- `sessions:list` gains optional `?tags=animus,memory` filter

### Storage

New table `session_tags`:
```sql
CREATE TABLE session_tags (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_key TEXT NOT NULL,
    tag TEXT NOT NULL,
    source TEXT NOT NULL DEFAULT 'agent',  -- 'agent' or 'auto'
    created_at_unix_ms INTEGER NOT NULL
);
CREATE INDEX idx_session_tags_session ON session_tags (session_key);
CREATE INDEX idx_session_tags_tag ON session_tags (tag);
```

## Refresh Strategy

- **On session start**: Full construction from scratch
- **Mid-session**: Not refreshed (too expensive per turn). Agent can call `memory search` for updates.
- **On consolidation completion**: Relevant layer observations are reweighted, triggering a refresh on next session start

## Acceptance Criteria

- [ ] `ActiveMemoryProvider` implementing `IContextProvider`, registered at priority 30
- [ ] Temporal grounding block (mandatory, always present)
- [ ] Episodic memory block (top-weighted observations per layer, per-layer budgets)
- [ ] Diary presence block (last 3 titles only)
- [ ] Ontology block (tag-matched + session-context, item/property limits)
- [ ] Session tags table + store
- [ ] Session tool tag extensions (tags:set/remove/list)
- [ ] Session-type aware construction (different sub-blocks for different session types)
- [ ] Configurable budget per agent
- [ ] All existing tests continue to pass

## Files

- **New:** `include/animus_kernel/context/ActiveMemoryProvider.h`
- **New:** `src/kernel/context/ActiveMemoryProvider.cpp`
- **New:** `include/animus_kernel/SessionTagsStore.h`
- **New:** `src/kernel/session/SessionTagsStore.cpp`
- **Modified:** `src/kernel/tools/SessionsTool.cpp` — tag actions
- **Modified:** `src/kernel/AgentKernel.cpp` — register provider + tags store
- **Modified:** `CMakeLists.txt`

## Key Questions

- Should active memory be cached between turns within a session, or always reconstructed?
- How to handle the cold-start problem (new agent, no memory yet)?
- Should the agent be able to "pin" specific items to always appear in active memory?
- How often do ontology entities get re-ranked for relevance?
- Should the construction pipeline be exposed as an admin API for debugging?
- Token counting: approximate (character-based) or precise (tiktoken-equivalent)?

## Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Context too large — blows token budget | Medium | Hard caps per sub-block; truncation with "... (N more items)" |
| Context too sparse — agent lacks grounding | Low | Temporal grounding always present |
| Construction is slow — delays session start | Medium | Cache constructed blocks; incremental updates |
| Prioritization produces irrelevant context | Medium | Session-type filtering + tag-scoped ontology |
| Memory systems produce conflicting information | Low | Explicit recency in display; agent can resolve via memory search |

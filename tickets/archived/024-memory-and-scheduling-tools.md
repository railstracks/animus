# Ticket 024 — Memory Tool (Composite Agent Search)

**Priority:** P2
**Status:** Open
**Dependencies:** Ticket 042-047 arc (memory layers, ontology, MemoryFile, unified search — all complete)
**Updated:** 2026-05-13

## Summary

Implement the `memory` tool — the agent's interface to its own memory system during chain execution. The agent can search across all persistent data stores, recall relevant information, and inspect the state of its memory layers.

This is a composite tool that unifies search across five domains:
1. **Episodic memory** — observations in memory layers (distilled experience)
2. **Semantic memory** — ontology entities and properties (structured knowledge)
3. **Memory files** — raw stored documents, notes, transcripts
4. **Diary** — private agent reflections
5. **Sessions** — verbatim session logs (new source, not yet indexed)

Initial implementation uses FTS5 (already built in ticket 047). Vector similarity search is the evolution path — the API surface stays the same, only the ranking changes.

## Context

The 042-047 arc built the entire memory backend: layers, ontology, MemoryFile storage, and a `MemorySearch` class with four-domain FTS5 search. But none of this is exposed to agents during chain execution. Agents currently have no way to query their own accumulated knowledge. This tool closes that gap.

The earlier version of this ticket (pre-042 arc) described a simpler model with four actions (recall, search, pin, inspect) operating only on episodic observation layers. That model is superseded by the unified architecture.

## What the Agent Can Do

- **Search** — query across all five domains with domain toggles. "What do I know about X?" returns results from distilled memory, structured knowledge, raw files, private reflections, and session history.
- **Recall** — higher-level retrieval: return the most relevant results across active layers, ordered by relevance/weight. Intended for "remind me what I know about..." use cases where the agent wants a curated set rather than exhaustive matches.
- **Pin** — annotate an observation as important for the consolidation process. A hint, not a mutation. Consolidation can consider pinned observations for promotion or retention.
- **Inspect** — view memory layer state: observation counts, perspectives (retrospective/current/future), consolidation status. Self-diagnostics.

Notably absent: `store`. Observation creation is handled exclusively by the consolidation pipeline (ticket 041). The agent reads and annotates memory; the pipeline creates and refines it.

## Tool Definition

### `memory`

```json
{
    "name": "memory",
    "description": "Access your memory system. Search across all your accumulated knowledge — distilled memories, structured facts, stored documents, private reflections, and session history. Or inspect the state of your memory layers.",
    "parameters": {
        "action": {
            "type": "string",
            "enum": ["search", "recall", "pin", "inspect"],
            "description": "Memory operation to perform"
        }
    }
}
```

### Actions

#### `search`
Full-text search across any combination of the five memory domains. Returns matched snippets with domain metadata. More exhaustive than `recall` — useful for finding specific facts or checking whether something was previously recorded.

```
Parameters:
  query       (string)   — Search terms
  domains     (string[]) — Optional: restrict to specific domains.
                            ["episodic", "semantic", "files", "diary", "sessions"]
                            Default: all domains.
  limit       (int)      — Max results (default: 20, max: 50)
  page        (int)      — Pagination offset (default: 0)

Returns:
  [{ domain, id, text, relevance,
     layer_id?, entity_path?, source_path?, session_key? }]
```

#### `recall`
Semantic-weighted retrieval across episodic memory layers. Returns the most relevant observations, considering weight, decay, and layer prominence. For "remind me what I know about..." use cases.

```
Parameters:
  query       (string)   — Natural language query; matched against observation content and tags
  count       (int)      — Max results (default: 5, max: 20)
  layers      (string[]) — Optional: restrict to specific layers. Empty = all active layers.
  min_weight  (float)    — Optional: minimum observation weight (0.0–1.0)

Returns:
  [{ id, content, tags, weight, layer, age_seconds }]
```

#### `pin`
Mark an existing observation as important for consolidation consideration. Adds an agent-written reason. This is an annotation, not a mutation — it doesn't change the observation content, but signals to the consolidation pipeline that this observation deserves attention.

```
Parameters:
  observation_id (string) — ID of the observation to pin
  reason         (string) — Why this matters; considered by consolidation passes

Returns:
  { success, observation_id, pin_timestamp_unix_ms }
```

#### `inspect`
View the state and perspectives of memory layers. Returns layer metadata: name, horizon, observation count, and consolidation-managed perspectives. Useful for self-diagnostics — "what does my memory system think about this timescale?"

```
Parameters:
  layer    (string) — Layer name to inspect. Empty = return summary of all layers.

Returns:
  {
    layers: [{
      name, horizon, observation_count,
      retrospective, current_perspective, future_perspective
    }]
  }
```

## Session Search (New Domain)

The four existing domains (episodic, semantic, files, diary) are indexed via FTS5. Sessions are a new fifth domain — verbatim session logs searchable by the agent.

Implementation options:
1. **Index session turns at write time** — when a session turn is stored, add its content to an FTS5 index. Lightweight, always current.
2. **Batch index on demand** — when the agent searches sessions, scan and index recent unindexed turns. Deferred work, less write overhead during chat.
3. **Hybrid** — index at write time with a configurable lag (e.g., only after session ends, not during active conversation).

Recommendation: option 1 (index at write time). Session turns are already persisted to SQLite — adding an FTS5 insert alongside is minimal overhead and ensures search results are always current. The `SessionTurn` struct already has `content` and `thinking_content` fields.

### Schema addition

```sql
CREATE VIRTUAL TABLE IF NOT EXISTS session_search USING fts5(
    agent_id,
    session_key,
    turn_role,
    content,
    content='session_turns',
    content_rowid='id'
);
```

Triggers on `session_turns` INSERT/UPDATE keep the FTS index in sync.

## Architecture

```
                    ┌─────────────────────────────┐
                    │       MemoryTool             │
                    │  (IToolHandler)              │
                    │  search / recall / pin /     │
                    │  inspect                     │
                    └──────────┬──────────────────┘
                               │
                    ┌──────────▼──────────────────┐
                    │     MemorySearch             │
                    │  (unified FTS5 search)       │
                    │  5 domains with toggles      │
                    └──┬───┬───┬───┬───┬──────────┘
                       │   │   │   │   │
              ┌────────┘   │   │   │   └────────┐
              ▼            ▼   ▼   ▼            ▼
         MemoryStore  Ontology  MemoryFile  DiaryStore  SessionStore
         (episodic)   Store     Store       (diary)     (sessions)
```

The tool wraps `MemorySearch` for `search`, `MemoryStore` for `recall`/`pin`/`inspect`, and extends `MemorySearch` to include the new sessions domain.

## Implementation Notes

- **Per-agent scoping:** All operations are scoped by `agent_id`. An agent only sees its own data.
- **`search`** delegates to `MemorySearch::Search()` with the domain toggles. Session search adds a fifth query path.
- **`recall`** uses `MemoryStore` with tag-weighted matching across active layers. Can evolve to vector similarity later.
- **`pin`** adds a `pinned_reason` field on the observation. Lightweight — no separate table.
- **`inspect`** reads from existing layer metadata. No new storage needed.
- **Vector search evolution:** When vector embeddings are added, `MemorySearch` gains an embedding index. The tool API doesn't change — `search` and `recall` just get better ranking. The FTS5 path remains as fallback.
- **Tool registration:** `MemoryTool` registers in `AgentKernel` alongside existing tools, receiving pointers to `MemorySearch`, `MemoryStore`, and the new session search component.

## Files Expected To Change

- `include/animus_kernel/tools/MemoryTool.h` — new file
- `src/kernel/tools/MemoryTool.cpp` — new file
- `include/animus_kernel/MemorySearch.h` — extend with sessions domain
- `src/kernel/memory/MemorySearch.cpp` — add session search path
- `src/kernel/AgentKernel.cpp` — register MemoryTool, wire dependencies
- `src/kernel/memory/SessionSearch.cpp` — new file (session FTS5 index + query)
- `include/animus_kernel/SessionSearch.h` — new file
- `tests/MemoryToolTests.cpp` — new file
- `CMakeLists.txt` — new source files

## Acceptance Criteria

- [ ] `memory` tool registered in ToolRegistry with four actions (search, recall, pin, inspect)
- [ ] `search` queries across all five domains with domain toggles and pagination
- [ ] `search` includes session turns as a searchable domain
- [ ] `recall` returns observations matched by query across active layers, scoped per-agent
- [ ] `pin` stores agent-written reason on an observation for consolidation consideration
- [ ] `inspect` returns layer metadata and perspectives for one or all layers
- [ ] All actions are agent-scoped (agent_id filter)
- [ ] Empty results return cleanly (no errors for "nothing found")
- [ ] FTS5 index for sessions kept in sync with session turn writes
- [ ] Tests for each action
- [ ] Existing MemorySearch tests continue to pass

## Out of Scope

- Vector embeddings (FTS5 first; embeddings are a follow-up)
- Agent-initiated observation creation (consolidation pipeline owns this)
- Memory layer management via tool (admin API handles this)
- Observation archival via tool (consolidation pipeline handles this)

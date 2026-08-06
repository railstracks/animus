# Ticket 139: Agent Export/Import — Composite Agent Archive Format

**Created:** 2026-08-07
**Status:** Open
**Branch:** `dev`

## Problem

Agents in Animus are fully defined by their database state, but there is no mechanism to transfer an agent between Animus instances, back up an agent's complete state, or import an agent constructed externally. Each store (MemoryStore, GallivantingStore, SessionStore, etc.) owns its slice of agent data with no unified export or import path.

## Goal

Define a **composite agent archive format** (`.agent` file — tar.gz) that captures the complete state of an agent with all associated data. Support selective component tags so exports can include or exclude large optional datasets (session history, prompt logs, attachments).

The format serves three use cases:
1. **Backup/restore** — snapshot an agent, restore on same or different instance
2. **Migration** — transfer agent between Animus instances (OpenClaw → Animus, server → server)
3. **External construction** — build an agent definition externally, import into Animus

## Design

### Container Format

`.agent` file = **tar.gz archive** containing:

```
/manifest.json          # format version, agent identity, component list, export metadata
/agent.json             # agent record (from `agents` table)
/memory/
  /layers.jsonl         # memory_layers rows (one JSON object per line)
  /observations.jsonl   # observations rows
  /perspectives.jsonl   # layer_perspectives rows
  /mutations.jsonl      # memory_mutations rows
/memory-files/
  /files.jsonl          # memory_files metadata (name, type, content, mutable, timestamps)
  /chunks.jsonl         # memory_file_chunks (content + optional embedding as base64)
/ontology/
  /entities.jsonl       # ontology_entities (tree structure with parent_id refs)
  /properties.jsonl     # ontology_properties (key-value pairs per entity)
  /search-docs.jsonl    # ontology_search_docs
  /mutations.jsonl      # ontology_mutations
/gallivanting/
  /threads.jsonl        # gallivanting_threads (prompts, SDT tags, status)
  /sessions.jsonl       # gallivanting_sessions (records + SDT scores) [optional]
/diary/
  /entries.jsonl        # diary_entries
/sessions/
  /sessions.jsonl       # session metadata
  /turns.jsonl          # session_turns (messages, tool calls, thinking)
  /compactions.jsonl    # session_compactions (compaction summaries)
  /summaries.jsonl      # session_compaction_summaries
  /reports.jsonl        # session_reports (consolidation reports)
  /notes.jsonl          # session_notes
  /tags.jsonl           # session_tags
/attachments/           # session file attachments [optional]
  /index.jsonl          # metadata (filename, mime_type, size)
  /<id>                 # binary files, named by attachment id
/schedules.jsonl        # schedules (consolidation, gallivanting, reporting)
/consolidation/
  /runs.jsonl           # consolidation_runs history
  /watermarks.jsonl     # consolidation_watermarks
/lua-scripts.jsonl      # lua_scripts (agent-owned scripts)
/prompt-logs.jsonl      # prompt_logs [optional, default off]
/auth-tokens.jsonl      # auth_tokens [optional, security-sensitive]
/projects.jsonl         # projects + project_tasks [optional]
```

### manifest.json Schema

```json
{
  "format_version": "1.0",
  "format_name": "animus-agent-archive",
  "exported_at": "2026-08-07T00:00:00Z",
  "source_system": "animus",
  "source_version": "0.3.1",
  "agent": {
    "agent_id": "kestrel",
    "name": "Kestrel",
    "description": "Persistent AI agent"
  },
  "components": {
    "agent": true,
    "memory": true,
    "memory_files": true,
    "ontology": true,
    "schedules": true,
    "gallivanting": true,
    "gallivanting_history": false,
    "diary": true,
    "sessions": false,
    "reports": false,
    "attachments": false,
    "lua_scripts": true,
    "prompt_logs": false,
    "auth_tokens": false,
    "projects": false
  },
  "stats": {
    "total_observations": 0,
    "total_sessions": 0,
    "total_turns": 0,
    "total_diary_entries": 0,
    "total_threads": 0,
    "total_memory_files": 0,
    "total_entities": 0,
    "approx_size_bytes": 0
  }
}
```

### Component Tags

| Tag | Default | Description |
|---|---|---|
| `agent` | ✅ always | Agent record (config, model, budgets, identity, enabled_tools) |
| `memory` | ✅ always | Memory layers, observations, perspectives, mutations |
| `memory_files` | ✅ always | MemoryFiles with content and chunk embeddings |
| `ontology` | ✅ on | Ontology entities, properties, search docs, mutations |
| `schedules` | ✅ on | Consolidation, gallivanting, reporting schedules |
| `gallivanting` | ✅ on | Gallivanting thread definitions (prompts, SDT tags) |
| `gallivanting_history` | ❌ off | Gallivanting session records (SDT scores, artifacts) |
| `diary` | ✅ on | Diary entries (private journal) |
| `sessions` | ❌ off | Chat sessions + turns (can be very large) |
| `reports` | ❌ off | Session reports from consolidation |
| `attachments` | ❌ off | Binary session file attachments |
| `lua_scripts` | ✅ on | Agent-owned Lua scripts |
| `prompt_logs` | ❌ off | Raw prompt logs (very large, potentially sensitive) |
| `auth_tokens` | ❌ off | API auth tokens (security-sensitive — exclude by default) |
| `projects` | ❌ off | Project definitions + tasks |

### ID Remapping Strategy

Internal database IDs (auto-increment integers) are **instance-local**. The archive uses **stable references** instead:

| Table | Internal ID | Export Key (stable) |
|---|---|---|
| `agents` | `id` (int) | `agent_id` (string, e.g. "kestrel") |
| `memory_layers` | `id` (int) | `(agent_id, name)` composite — unique per agent |
| `observations` | `id` (int) | `(layer_name, content_hash)` or sequential export index |
| `gallivanting_threads` | `id` (int) | `(agent_id, title, created_at_unix_ms)` |
| `sessions` | `id` (int) | `(connector, conversation_id, thread_id)` composite |
| `session_turns` | `id` (int) | `(session_ref, turn_id)` where `session_ref` maps to parent session |
| `ontology_entities` | `id` (int) | `(root_category, full_path)` — already unique indexed |
| `schedules` | `id` (int) | `(agent_id, tag, message_hash)` or sequential export index |

Import resolves stable refs → new internal IDs. Cross-table foreign keys (e.g. `observations.layer_id → memory_layers.id`) are resolved via the stable keys during import.

For tables without natural unique keys (observations, turns), the export assigns a **sequential export index** (`_export_id`) starting at 1. Import uses this to resolve FK references during insertion, then discards it.

### Embeddings Handling

Embedding BLOBs (in `memory_file_chunks.embedding` and `session_reports.embedding`) are **optional within the archive**:

- `manifest.json` → `components.memory_files_embeddings: true|false`
- If included: embedding stored as base64 in the JSONL with `embedding_dim` field
- If excluded: chunk content is still present; embeddings regenerated on import via the embedding pipeline
- Default: **include** for memory_files (they're identity-relevant), **exclude** for session_reports (regenerable)

### JSONL Format

Each `.jsonl` file contains one JSON object per line. Every row gains an `_export_id` field (integer) for FK resolution during import. Internal IDs (`id`, `layer_id`, etc.) are preserved as `_original_id` for audit/debugging but not used for FK resolution.

Example `memory/layers.jsonl`:
```json
{"_export_id": 1, "_original_id": 5, "agent_id": "kestrel", "name": "working", "horizon": "short", "sort_order": 0, "intake_interval": "30m", ...}
{"_export_id": 2, "_original_id": 6, "agent_id": "kestrel", "name": "episodic", "horizon": "medium", "sort_order": 1, ...}
```

Example `memory/observations.jsonl`:
```json
{"_export_id": 1, "_original_id": 142, "_layer_export_id": 1, "agent_id": "kestrel", "content": "...", "source": "session#1234", ...}
```

### Export API

```
POST /api/v1/agents/{agentId}/export
Content-Type: application/json
Body: { "components": ["agent", "memory", "memory_files", ...] }
Response: application/gzip (download)
```

Or CLI:
```
animusctl agent export kestrel --output kestrel.agent --include sessions,reports
animusctl agent export kestrel --output kestrel-minimal.agent  # defaults only
```

### Import API

```
POST /api/v1/agents/import
Content-Type: multipart/form-data
Body: file=kestrel.agent, options={"mode": "new|merge|replace", "agent_id": "kestrel2"}
```

Or CLI:
```
animusctl agent import kestrel.agent --mode new --agent-id kestrel2
animusctl agent import kestrel.agent --mode merge  # merge into existing agent
```

Import modes:
- **new** — create new agent with specified agent_id (error if exists)
- **merge** — merge into existing agent (observations appended, layers matched by name, schedules deduplicated)
- **replace** — drop all existing agent data, replace with archive contents (destructive)

### Conflict Resolution (merge mode)

| Table | Match Key | On Conflict |
|---|---|---|
| `memory_layers` | `(agent_id, name)` | Update in place (preserve ID) |
| `observations` | `(layer_id, content_hash)` | Skip if exists |
| `memory_files` | `(agent_id, source_path)` | Skip if content unchanged, update if mutable + changed |
| `gallivanting_threads` | `(agent_id, title)` | Skip if exists |
| `diary_entries` | `(agent_id, timestamp_unix_ms)` | Skip if exists |
| `schedules` | `(agent_id, tag, message)` | Skip if exists |
| `sessions` | `(connector, conversation_id)` | Skip if exists |
| `ontology_entities` | `(root_category, full_path)` | Merge properties |

## Implementation Plan

### Phase 1: Schema + Export

1. Define `AgentArchiveWriter` class — takes `agentId` + component list, queries all stores, writes tar.gz
2. Add export endpoint to AdminServer (`POST /api/v1/agents/{id}/export`)
3. Add CLI command (`animusctl agent export`)
4. Test with default components (no sessions)
5. Test with full export (all components)

### Phase 2: Import

1. Define `AgentArchiveReader` class — reads tar.gz, resolves FK refs, inserts into stores
2. Add import endpoint to AdminServer (`POST /api/v1/agents/import`)
3. Add CLI command (`animusctl agent import`)
4. Test all three import modes (new, merge, replace)
5. Round-trip test: export → import as new agent → compare

### Phase 3: CLI Polish + Validation

1. Component list display (`animusctl agent inspect kestrel.agent`)
2. Selective component import (`--only memory,schedules`)
3. Validation mode (dry-run import, report conflicts without writing)
4. Archive format versioning + migration path

## Files Affected

| File | Change |
|---|---|
| `include/animus_kernel/AgentArchive.h` (new) | `AgentArchiveWriter` + `AgentArchiveReader` classes |
| `src/kernel/AgentArchive.cpp` (new) | Implementation |
| `src/kernel/admin/internal/AdminServerRoutesAgentsAndRuntime.inc` | Export/import endpoints |
| `include/animus_kernel/admin/AdminServer.h` | Wire store dependencies |
| `src/kernel/admin/AdminServer.cpp` | Construct and wire AgentArchive |

### Dependencies

The writer/reader needs access to all stores. Rather than depending on every store class individually, it should take an `IDataStore*` and query tables directly via SQL — this avoids coupling to each store's C++ API and makes the archive format resilient to store interface changes.

The tradeoff: the archive code needs to know the table schemas directly. This is acceptable — the schema is stable, and the archive format is versioned.

## Testing

1. Export default agent with default components → inspect tarball structure
2. Export with all components → verify all tables represented
3. Import as new agent → verify data integrity
4. Round-trip: export agent A → import as agent B → compare A and B's data
5. Merge import into existing agent → verify conflict resolution
6. Import with `--only memory,schedules` → verify selective import
7. Export from SQLite instance → import into PostgreSQL instance → verify cross-backend compatibility
8. Large agent (many sessions) → verify streaming export doesn't OOM

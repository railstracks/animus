# AGENTS.orm.md — Animus Persistence Layer

Guide for agents working on database schema, ORM patterns, and data access in Animus.

## Storage Backends

Animus supports two backends:

- **SQLite** — embedded, file-based. Default for development and single-node deployments.
- **PostgreSQL** — networked, concurrent. Recommended for production and multi-agent scale.

Both use the same schema definitions. The `Store` abstraction (`include/animus_kernel/store/`) wraps the underlying driver. On PostgreSQL, `search_vector` columns use `tsvector` with `gin` indexes; on SQLite, `FTS5` virtual tables provide equivalent full-text search.

### Connection

- **SQLite:** `state/memory.db` (path configurable via `--db-path`)
- **PostgreSQL:** `--pg-host`, `--pg-database`, `--pg-user`, `--pg-password`

The `AgentConfigStore` (key/value pairs for agent runtime config, channel configs, interface settings) is always SQLite — even when PostgreSQL is the primary backend for session/memory data.

## Schema Overview

### Core Tables

#### `agents`
Per-tenant agent configuration. One row per agent.

| Column | Type | Notes |
|--------|------|-------|
| `agent_id` | TEXT PK | Unique agent identifier |
| `name`, `display_name` | TEXT | Human-readable labels |
| `default_provider`, `default_model` | TEXT | LLM provider + model |
| `default_vision_model` | TEXT | Vision-capable model override |
| `context_window`, `temperature` | INT/REAL | LLM parameters |
| `reasoning_enabled`, `reasoning_effort` | INT/TEXT | Reasoning config |
| `pad_context` | INT | Pad context window for stability |
| `max_chain_steps`, `max_tool_calls_per_chain` | INT | Budget limits |
| `timeout_seconds` | INT | Per-activation timeout |
| `token_budget_per_prompt` | INT | Max tokens per LLM call |
| `episodic_token_budget`, `semantic_token_budget`, `perspectives_token_budget` | INT | Memory injection budgets |
| `consolidation_tool_budget`, `memory_file_token_budget` | INT | Tool-specific budgets |
| `ambient_context_limit` | INT | Active memory injection cap |
| `enabled_tools` | TEXT (JSON array) | Tool allowlist |
| `tool_configs` | TEXT (JSON) | Per-tool configuration |
| `allowed_nodes` | TEXT (JSON array) | Node routing constraints |
| `session_report_token_budget` | INT | Session report generation budget |
| `intake_interval` | TEXT | Cron expression for intake scheduling |
| `intake_prompt` | TEXT | Custom intake prompt override |
| `diary_secret` | TEXT | AES-256-GCM key (auto-generated, immutable) |
| `charter_json` | TEXT | Constitutional governance rules |
| `created_at_unix_ms`, `updated_at_unix_ms` | INT | Timestamps |

### Session Tables

#### `sessions`
Long-lived conversation containers.

| Column | Type | Notes |
|--------|------|-------|
| `id` | TEXT PK | Session UUID |
| `agent_id` | TEXT | Owning agent |
| `channel_name`, `channel_type` | TEXT | Origin channel |
| `peer_id` | TEXT | Conversation partner identifier |
| `title` | TEXT | Display title |
| `created_at_unix_ms` | INT | Creation time |
| `last_activity_unix_ms` | INT | Last message time |

#### `session_turns`
Individual messages within a session.

| Column | Type | Notes |
|--------|------|-------|
| `id` | INTEGER PK AUTOINCREMENT | |
| `session_id` | TEXT FK | Parent session |
| `role` | TEXT | `user`, `assistant`, `tool`, `system` |
| `content` | TEXT | Message content |
| `thinking_content` | TEXT | Reasoning trace (same turn, not separate) |
| `model` | TEXT | Model used for this turn |
| `provider` | TEXT | Provider used |
| `created_at_unix_ms` | INT | Timestamp |
| `token_count` | INT | Tokens consumed |
| `intake_processed` | INT | Whether consolidation has consumed this turn |
| `intake_processed_at_unix_ms` | INT | When intake processed it |

#### `session_compaction_summaries`
LLM-generated summaries created when sessions exceed 80% context window.

#### `session_reports`
Temporal summaries with embeddings for cross-session search.

#### `session_notes`, `session_tags`
Manual annotations and tags on sessions.

### Memory Tables

#### `memory_layers`
Temporal memory horizons scoped per agent. Default layers: `day`, `week`, `month`, `quarter`, `year`, `decade`, `lifetime`.

| Column | Type | Notes |
|--------|------|-------|
| `id` | INTEGER PK | |
| `agent_id` | TEXT | Owning agent (or `'default'` for templates) |
| `name`, `horizon` | TEXT | Layer name and human-readable horizon |
| `sort_order` | INT | Display/processing order |
| `enabled` | INT | Whether layer is active |
| `evaluation_interval_seconds` | INT | Review cadence |
| `cron_expr` | TEXT | Cron schedule for review |
| `consolidation_prompt` | TEXT | LLM prompt for review |
| `consolidation_intake_prompt` | TEXT | LLM prompt for intake (legacy, agent-level preferred) |
| `intake_interval` | TEXT | Cron for intake (legacy, agent-level preferred) |
| `token_budget` | INT | Max tokens to inject from this layer |

#### `observations`
Episodic memory entries. Created by consolidation intake, reviewed/promoted by review jobs.

| Column | Type | Notes |
|--------|------|-------|
| `id` | INTEGER PK | |
| `agent_id` | TEXT | Owning agent |
| `layer_id` | INT FK | Memory layer |
| `text` | TEXT | Observation content |
| `tags` | TEXT | Comma-separated tags |
| `weight` | REAL | Importance (0.0–1.0) |
| `status` | TEXT | `new`, `current`, `deprecated` |
| `source` | TEXT | `session`, `diary`, `manual` |
| `created_at_unix_ms` | INT | Creation time |
| `next_review_at_ms` | INT | Scheduled review time |

#### `layer_perspectives`
Per-layer retrospective/current/future summaries maintained by review jobs.

#### `memory_mutations`
Audit log for observation changes (creations, promotions, retirements).

#### `ontology_entities` / `ontology_properties` / `ontology_mutations`
Semantic memory: structured entities with typed properties and evidence trails.

#### `memory_files`
Raw documents with chunking and embedding support. Content mutability is policy-driven.

#### `memory_file_chunks`
Fixed-size text chunks of memory files for semantic search.

### Scheduling Tables

#### `schedules`
Persistent schedule entries for the cron-like scheduler.

| Column | Type | Notes |
|--------|------|-------|
| `id` | TEXT PK | Schedule UUID |
| `agent_id` | TEXT | Owning agent (or `'system'`/`'default'`) |
| `tag` | TEXT | Grouping tag (e.g., `consolidation`) |
| `type` | INT | OneShot or Recurring |
| `next_fire` | TEXT | ISO-8601 or cron expression |
| `message` | TEXT | Payload delivered when fired |

### Other Tables

- **`gallivanting_threads` / `gallivanting_sessions`** — autonomous exploration sessions
- **`diary_entries`** — encrypted per-agent private journal
- **`lua_scripts`** — persisted Lua tool scripts
- **`consolidation_watermarks`** — intake/review cursor tracking
- **`consolidation_runs`** — job execution history
- **`ontology_search_docs`** — denormalized search documents for ontology

### AgentConfigStore (always SQLite)

Key/value store for runtime configuration. Keys follow a `section.subsection.key` pattern:

- `channel.<name>.type` — channel adapter type
- `channel.<name>.enabled` — on/off state
- `channel.<name>.config` — JSON blob with all channel settings
- `agent.<id>.*` — per-agent runtime config
- `interface.<id>.*` — interface state

## Patterns

### Store Abstraction

All database access goes through `Store` (defined in `include/animus_kernel/store/`). This wraps either SQLite or PostgreSQL and provides:

- `Prepare(sql)` → `Statement` (with `BindText`, `BindInt64`, `BindDouble`, `Step`)
- `Exec(sql)` — for DDL/DML without parameters
- Transaction support

### Schema Versioning

Schema is created on first connect. Migrations are additive (`ALTER TABLE ADD COLUMN`) and checked via `schema::ColumnExists()` before running. No destructive migrations — columns are only added, never removed or renamed.

### Writing a New Table

1. Define the `CREATE TABLE IF NOT EXISTS` in the store class constructor
2. Use `schema::CreateTable(store, R"(...)")` for the initial DDL
3. Add migration calls for new columns: `if (!schema::ColumnExists(store, table, col)) store->Exec("ALTER TABLE ... ADD COLUMN ...")`
4. Define a `RowToX(Statement*)` helper
5. Expose CRUD methods

### FTS / Search

On SQLite: `FTS5` virtual tables. On PostgreSQL: `tsvector` columns with `gin` indexes. The `MemorySearch` class abstracts both — it detects the backend and runs appropriate SQL.

### PostgreSQL-Specific Notes

- `search_vector` columns are `tsvector`, populated by triggers or explicit `to_tsvector()` calls
- Auto-increment sequences need manual reset after migration (`setval`)
- `JSON` columns work on both backends; prefer `TEXT` storing JSON for maximum compatibility
- The migration tool (`animus-migrate`) handles sequence reset and tsvector backfill

## See Also

- `AGENTS.md` — project overview, architecture diagram
- `src/kernel/memory/` — all memory store implementations
- `src/kernel/session/SqliteSessionStore.cpp` — session schema definitions
- `src/kernel/agent/AgentStore.cpp` — agent schema definitions
- `tools/` — migration tool source

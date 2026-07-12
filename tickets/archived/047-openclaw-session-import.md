# Ticket 047 — OpenClaw Session Import

**Priority:** P2 (migration path for OpenClaw users; preserves session history during transition)
**Status:** Open
**Dependencies:** Ticket 044 (Active Memory — uses imported sessions as memory source)
**Updated:** 2026-05-15

## Summary

A general-purpose import function that reads OpenClaw session `.jsonl` files and converts them into Animus sessions with turns. This enables users migrating from OpenClaw to preserve their conversation history, and allows Animus agents to access legacy session context through the standard session/turn interface.

**Not a Kestrel-specific migration** — this is a user-facing utility for any OpenClaw-to-Animus transition.

## Source Format Reference

Full format analysis documented in `openclaw_import.md`. Key facts:

- **Import target:** Primary `.jsonl` files only (Phase 1). Each file is one session.
- **Format:** One JSON object per line. First line is `type: "session"`, subsequent lines are messages and metadata.
- **Message roles:** `user`, `assistant`, `toolResult`
- **Content types:** `text`, `thinking`, `toolCall`
- **Session naming:** `{uuid}[-topic-{topicId}].jsonl` — topic segment is a Slack thread ID
- **Version observed:** 3

### Line types present in primary `.jsonl`

| Line type | Import? | Notes |
|---|---|---|
| `session` | ✅ header | Extract session UUID, timestamp, working directory |
| `model_change` | ⬚ metadata | Provider/model info — useful for session metadata |
| `thinking_level_change` | ⬚ skip | Not mapped in Animus turns |
| `custom/model-snapshot` | ⬚ skip | Runtime config snapshot |
| `custom_message/runtime-context` | ⬚ skip | OpenClaw-internal context injection |
| `message` (user) | ✅ turn | Maps directly to SessionTurn role=user |
| `message` (assistant) | ✅ turn | Content array → content + thinking_content + tool_calls |
| `message` (toolResult) | ✅ turn | Maps to SessionTurn role=tool |

## Animus Data Model

### Sessions table
```sql
CREATE TABLE sessions (
    id INTEGER PRIMARY KEY,
    connector TEXT NOT NULL,          -- "import" for imported sessions
    conversation_id TEXT NOT NULL,    -- OpenClaw session UUID
    thread_id TEXT NOT NULL,          -- topic ID or empty
    provider_id TEXT NOT NULL DEFAULT '',
    agent_id TEXT NOT NULL DEFAULT 'default',
    summary TEXT NOT NULL DEFAULT '',
    created_at_unix_ms INTEGER NOT NULL,
    last_active_unix_ms INTEGER NOT NULL,
    terminated INTEGER NOT NULL DEFAULT 1  -- imported sessions are read-only
);
```

### Session turns table
```sql
CREATE TABLE session_turns (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL REFERENCES sessions(id),
    turn_id INTEGER NOT NULL,
    role TEXT NOT NULL,               -- "user" / "assistant" / "tool"
    content TEXT NOT NULL,
    unix_ms INTEGER NOT NULL,
    thinking_content TEXT NOT NULL DEFAULT '',
    tool_calls TEXT NOT NULL DEFAULT '[]',
    tool_call_id TEXT NOT NULL DEFAULT '',
    tool_name TEXT NOT NULL DEFAULT '',
    is_summary INTEGER NOT NULL DEFAULT 0,
    ...
);
```

## Field Mapping

### Session metadata

| OpenClaw `.jsonl` | Animus Session | Notes |
|---|---|---|
| `session.id` | `conversation_id` | UUID preserved as-is |
| `session.timestamp` | `created_at_unix_ms` | ISO 8601 → unix ms |
| Last message timestamp | `last_active_unix_ms` | Derived from final turn |
| `-topic-{id}` in filename | `thread_id` | Extracted from filename, empty if absent |
| `"import"` | `connector` | Fixed value to identify imported sessions |
| `model_change.provider/modelId` | `provider_id` | First model_change in session |

### Session turns

| OpenClaw `message` | Animus SessionTurn | Notes |
|---|---|---|
| `message.role == "user"` | `role = "user"` | Direct mapping |
| `message.role == "assistant"` | `role = "assistant"` | Direct mapping |
| `message.role == "toolResult"` | `role = "tool"` | Role renamed |
| `message.timestamp` | `unix_ms` | Direct mapping |
| `content[type="text"].text` | `content` | Concatenated if multiple text blocks |
| `content[type="thinking"].thinking` | `thinking_content` | Extracted separately |
| `content[type="toolCall"]` | `tool_calls` (JSON array) | `{name, arguments, id}` |
| `content[type="text"]` in toolResult | `content` | Tool response body |
| `message.usage` | (not stored) | Could be logged but not in turn schema |
| `message.provider/model` | (not stored per-turn) | Session-level metadata only |

### Tool call format conversion

OpenClaw:
```json
{"type": "toolCall", "id": "ollama_call_xxx", "name": "memory_search", "arguments": {"query": "..."}}
```

Animus `tool_calls` JSON column:
```json
[{"id": "ollama_call_xxx", "name": "memory_search", "arguments": {"query": "..."}}]
```

Direct structural mapping — just wrap in array if not already.

## Design

### Import interface

Two surfaces:
1. **Admin API endpoint:** `POST /api/v1/import/openclaw-sessions` — accepts a directory path or file list
2. **CLI command:** `animusd import openclaw-sessions <path>` — for batch imports without the admin server running

### Import pipeline

```
┌──────────────────────────────────────────────────────┐
│              OpenClawSessionImporter                  │
│                                                      │
│  1. Scan directory for *.jsonl files                 │
│     - Filter out .trajectory, .checkpoint, .bak,     │
│       .deleted, .reset, .lock files                  │
│     - Match primary session pattern only              │
│                                                      │
│  2. For each file:                                   │
│     a. Parse first line → session metadata           │
│     b. Parse remaining lines → message stream        │
│     c. Filter: only type="message" lines             │
│     d. Map roles: user→user, assistant→assistant,    │
│        toolResult→tool                               │
│     e. Extract content: text, thinking, tool_calls   │
│     f. Create Session + SessionTurns in SQLite       │
│                                                      │
│  3. Report: files processed, turns imported,         │
│     errors/skipped                                   │
└──────────────────────────────────────────────────────┘
```

### Deduplication

v1 strategy: **skip files whose `session.id` already exists as a `conversation_id`** in the sessions table. No content-level deduplication — if a session UUID exists, the entire file is skipped. The `connector="import"` field makes it clear these are imported sessions.

This is deliberately simple. Re-importing is safe (idempotent at file level).

### Error handling

- **Malformed JSON lines:** log warning, skip line, continue with remaining lines
- **Missing `session` header line:** log error, skip entire file
- **Empty files:** skip silently
- **Unrecognized line types:** skip silently (only `type=message` lines produce turns)

### sessions.json usage

The `sessions.json` file maps session keys (e.g. `agent:main:slack:channel:c0alg5hbfup`) to session UUIDs and metadata. This can be used **optionally** to enrich imported sessions with:
- Channel type (direct, slack, cron hook)
- Delivery context
- Agent assignment

For v1, this is optional. The importer works without it — sessions just won't have channel classification.

## Acceptance Criteria

- [ ] `OpenClawSessionImporter` class that reads a directory of `.jsonl` files
- [ ] Correct field mapping for all three message roles (user, assistant, toolResult)
- [ ] Thinking content extracted separately from main content
- [ ] Tool calls preserved as structured JSON in `tool_calls` column
- [ ] Imported sessions marked with `connector = "import"` and `terminated = 1`
- [ ] Idempotent: re-running on same directory skips already-imported sessions
- [ ] Admin API endpoint `POST /api/v1/import/openclaw-sessions`
- [ ] Error reporting: malformed lines counted, failed files listed
- [ ] Non-destructive: importer never modifies source files
- [ ] Tested against real OpenClaw session files (473 files, 89 MB)

## Future Enhancements (out of scope for v1)

- **Phase 2:** Import `.checkpoint.*` and `.reset.*` files for pre-compaction history, with message-ID-based deduplication against Phase 1 data
- **sessions.json enrichment:** Classify imported sessions by channel type
- **Progress streaming:** For large imports, stream progress via SSE/WebSocket
- **Selective import:** Filter by date range, session key pattern, or file list
- **Dry-run mode:** Report what would be imported without writing to DB

## Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Format variations across OpenClaw versions | Medium | Only version 3 observed; parser should be defensive |
| Large files cause memory pressure | Low | Stream line-by-line, don't load entire file |
| Session UUID collisions with existing Animus sessions | Very Low | UUIDs are globally unique; `conversation_id` stores the original |
| Content encoding issues | Low | OpenClaw writes UTF-8 JSON; same assumption in Animus |

## Files

- **New:** `include/animus_kernel/OpenClawSessionImporter.h`
- **New:** `src/kernel/import/OpenClawSessionImporter.cpp`
- **Modified:** `src/kernel/admin/AdminServer.cpp` — import endpoint
- **Modified:** `CMakeLists.txt` — new source files

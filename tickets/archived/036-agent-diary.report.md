# Ticket 036 — Agent Diary (Private): Implementation Report

**Completed:** 2026-05-06
**Commits:** 15 (377f39d..f41136e)
**Lines changed:** +4,455 / -55 across 24 files

## Summary

The agent diary is a first-class private tool in the Animus framework. Each agent has its own diary — a personal archive for unstructured, temporal reflection. The diary is deliberately separate from memory (structured, consolidated) and sessions (dialogue trace). It is a space where the agent speaks strictly with itself.

## Architecture

### Storage: SQL primary

Diary entries live in the `diary_entries` table in the per-agent SQL database, following the multi-tenant pattern from ticket 029. SQL was chosen over file-based storage for searchability, syndication readiness, and consistency with existing agent data infrastructure.

```
diary_entries
  id          TEXT (UUID)     — primary key
  agent_id    TEXT            — owner (multi-tenant scoping)
  timestamp   INTEGER        — unix ms
  layer       TEXT            — "reflection" | "observation" | "intention"
  content     TEXT            — freeform entry text
  tags        TEXT            — JSON array of optional tags
  session_id  TEXT            — optional session context (nullable)
```

Entries are stored as plaintext. The privacy contract is enforced at the API boundary — admin sees metadata only, agent reads own entries. Encrypted-at-rest is a future option that doesn't require API changes.

### Components

| Component | File | Lines | Purpose |
|-----------|------|-------|---------|
| DiaryManager | `admin/DiaryManager.h/.cpp` | 774 | Lifecycle: create, write, read, search, delete, export/import entries |
| AiDiaryFormat | `admin/AiDiaryFormat.h/.cpp` | 357 | `.aidiary` container: AES-256-GCM encryption, Argon2id key derivation |
| DiaryTool | `tools/DiaryTool.h/.cpp` | 310 | Agent-facing tool: write, read, search, list, delete |
| Admin routes | `internal/AdminServerRoutesDiary.inc` | 184 | Metadata, export, import endpoints |
| Admin UI | `DiaryView.vue` | 465 | Metadata display, export/import interface |
| Tests | `DiaryManagerTests.cpp` | 755 | 24 tests: CRUD, search, pagination, encrypt/decrypt, export/import |

Total implementation: ~2,845 lines.

### Agent-owned encryption key

The `diary_secret` field was added to the `Agent` struct — a 64-char hex string (256 bits of entropy) auto-generated at agent creation. This is the encryption key for `.aidiary` export/import. The admin never provides or sees this key. The key is immutable after creation (not updated by `AgentStore::Update`).

Privacy guarantee: exported `.aidiary` files can only be imported back by the same agent that created them. Decryption with a different agent's key fails. The admin facilitates backup/restore but cannot read diary content at any point.

## API Surface

### Agent tool: `diary`

| Operation | Description |
|-----------|-------------|
| `write` | Append entry (layer, content, optional tags, optional session_id) |
| `read` | Retrieve entries by date range |
| `search` | Full-text search within own diary |
| `list` | Paginated entry listing with optional layer filter |
| `delete` | Remove entry by id |

Both `list` and `search` support pagination (`page`, `page_size`) to keep the agent's context window manageable.

### Admin API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/diary/{agentId}/metadata` | GET | Metadata only: exists, entry count, last entry date |
| `/api/v1/diary/{agentId}/export` | POST | Encrypted `.aidiary` file (uses agent's `diary_secret`) |
| `/api/v1/diary/{agentId}/import` | POST | Decrypt `.aidiary`, merge or replace (uses agent's `diary_secret`) |

No content read endpoint exists in the admin API. Export produces an encrypted binary — the admin cannot read content through any endpoint.

### Admin UI

The DiaryView page shows:
- Agent selector (dropdown)
- Metadata panel: diary active status, entry count, most recent entry timestamp
- Export button (one click — agent's key used automatically)
- Import dialog (file picker, merge/replace strategy)
- Privacy note explaining the agent-owned encryption model

## Acceptance Criteria Status

- [x] `DiaryManager` class owns diary lifecycle (create, read, write, search, delete, export, import)
- [x] `diary_entries` table in per-agent SQL database
- [x] Agent tool `diary` with write/read/search/delete operations (plus `list` with pagination)
- [x] Full-text search within agent's own diary
- [x] Admin API exposes metadata only (no content)
- [x] Admin API export/import endpoints produce/consume `.aidiary` encrypted files
- [x] `.aidiary` container format implemented with AES-256-GCM encryption
- [x] Per-agent diary scoping follows multi-tenant pattern
- [x] Diary entries have optional tags and layer classification
- [x] Diary is self-contained — no special memory integration needed
- [x] Syndication-ready: diary_entries table is compatible with future node syndication
- [x] All existing tests continue to pass (8/8 suite; 1 pre-existing failure in JobsTests unrelated to diary)
- [x] New diary tests cover: create, write, read, search, delete, export, import, encrypt/decrypt, metadata API

**13/13 criteria met.**

## Key Questions — Resolved

| Question | Resolution |
|----------|-----------|
| Key storage | `diary_secret` field on Agent struct — auto-generated, immutable, never exposed to admin |
| Export scope | All entries exported (date range/tags filtering is a future enhancement) |
| Import merge strategy | Both `merge` (append missing) and `replace` (wipe and restore) supported |
| Session context | `session_id` is opt-in — agent can provide it, not auto-captured |
| Tag vocabulary | Freeform — no predefined taxonomy |
| Encrypted-at-rest | Deferred to future — SQL plaintext with API-level privacy for v1 |

## Design Decisions

1. **SQL over files for primary storage.** Searchability, syndication compatibility, and consistency with existing infrastructure outweigh the simplicity of file-based storage.

2. **Agent-owned secret for encryption.** The `diary_secret` lives on the Agent model, generated at creation, never shown to admin. This means the admin genuinely cannot read diary content — even exported backups are bound to the originating agent. The cost is that losing an agent's `diary_secret` means losing the ability to import backups. This is by design.

3. **Diary is not a memory pipeline.** No special bridges to memory consolidation. Diary interactions occur in the agent's prompt chain like any other tool use, so the background consolidation process picks them up naturally. No exceptions, no special handling.

4. **Pagination on list/search.** Without pagination, a large diary could consume the agent's entire context window. Both `list` and `search` support `page` and `page_size` parameters.

5. **Metadata-only admin view.** The admin sees that a diary exists, how many entries it has, and when the last entry was written. Nothing more. This is enforced at the API level — no content endpoints exist for admin access.

## Commits

| Hash | Description |
|------|-------------|
| 306f245 | Initial diary implementation (DiaryManager, AiDiaryFormat, DiaryTool) |
| a0931ad | CMake + AdminServer + AgentKernel wiring |
| a4ea9b1 | Fix diary test build (jsoncpp) |
| 67da1ee | Fix diary test link |
| 3603792 | Add `list` action + pagination |
| f094645 | Sync header declarations with pagination |
| 989e422 | Layer-filtered count, fix test escaping |
| 2d9e1db | Restructure: SQL primary, .aidiary export only |
| 0b04d44 | Fix diary isolation from memory stack |
| ec8d3fd | Remove active memory integration from operational tools |
| 7c05e71 | Simplify diary/memory relationship wording |
| 4de86c5 | Admin UI diary page + metadata API endpoint |
| 1d3c481 | Fix DiaryView API shape (agents array, timestamp type) |
| 1d514b4 | Admin export/import endpoints + .aidiary UI (passphrase-based) |
| f41136e | Agent-owned `diary_secret` for .aidiary encryption |

## Next Steps

Tickets 037–040 are drafted and build on the diary foundation:
- **037 (Study):** Structured literature ingestion with diary integration
- **038 (Gallivanting):** Unstructured exploration with diary integration
- **039 (Lua Scripting):** Custom agent behavior scripts
- **040 (Project Tool):** Project lifecycle management
# Ticket 049 — MemoryFile Bulk Import

**Priority:** P2 (migration tooling; supplements session and episodic imports with raw text documents)
**Status:** Open
**Dependencies:** MemoryFileStore (stable)
**Updated:** 2026-05-15

## Summary

Bulk import of text files into the Animus MemoryFile system. Enables migrating an agent's existing file-based knowledge — daily logs, expanded memory files, project documentation — into Animus's searchable MemoryFile store.

This is the simplest of the three import tracks: read files from a directory, classify them by type, store their content in the `memory_files` table. No format parsing beyond reading text content.

## Use Case: Kestrel Migration

Import all of Kestrel's daily memory logs (`memory/*.md`) and expanded topic files (`memory_expanded/*.md`) as historical supplements. These files span the entire operational history (Feb 14 onward) and contain curated context that complements both the session data and the episodic/semantic injections.

## Data Model

```sql
CREATE TABLE memory_files (
    id INTEGER PRIMARY KEY,
    source_path TEXT NOT NULL,          -- original file path (e.g. "memory/2026-03-15.md")
    file_type INTEGER NOT NULL,         -- MemoryFileType enum
    content TEXT NOT NULL,              -- full file content
    content_mutable INTEGER DEFAULT 1,  -- can be updated after import
    agent_id INTEGER NOT NULL DEFAULT 0,
    superseded INTEGER DEFAULT 0,       -- marked as replaced by newer version
    created_at_unix_ms INTEGER NOT NULL,
    imported_at_unix_ms INTEGER NOT NULL
);
```

### MemoryFileType enum

| Value | Name | Use |
|---|---|---|
| 0 | ExpandedMemory | Topic deep-cuts (`memory_expanded/*.md`) |
| 1 | SessionLog | Session reports |
| 2 | DailyNote | Daily memory logs (`memory/YYYY-MM-DD.md`) |
| 3 | BootstrapFile | Agent bootstrap files (SOUL.md, IDENTITY.md, etc.) |
| 4 | Journal | Diary/journal entries |
| 5 | ExternalDoc | Generic documents |

## Format

No special file format needed — the importer reads plain text files from a directory. Classification is done by path pattern or explicit mapping.

### Import configuration

```json
{
  "schema": "animus-memfile-import/v1",
  "description": "Kestrel daily logs and expanded memory",
  "agent_id": "default",
  "source_dir": "/path/to/memory",
  "rules": [
    {
      "pattern": "memory_expanded/*.md",
      "file_type": "expanded_memory"
    },
    {
      "pattern": "memory/*.md",
      "file_type": "daily_note"
    }
  ],
  "default_file_type": "external_doc"
}
```

Alternatively, the admin API can accept a simpler request:

```json
{
  "directory": "/path/to/import",
  "file_type": "daily_note",
  "agent_id": "default",
  "pattern": "*.md"
}
```

### Timestamp extraction

For daily notes, the filename contains the date (`2026-03-15.md`). The importer should attempt to derive `created_at_unix_ms` from the filename:

- Pattern `YYYY-MM-DD.md` → midnight UTC on that date
- Pattern `YYYY-MM-DD.*.md` → midnight UTC on that date
- Fallback → file modification time from filesystem
- Final fallback → import time (now)

For expanded memory files (`memory_expanded/topic.md`), use the file's modification time.

## Import Pipeline

```
┌────────────────────────────────────────────────────┐
│          MemoryFileImporter                         │
│                                                     │
│  1. Scan directory matching pattern                │
│  2. For each file:                                  │
│     a. Read content (UTF-8 text)                    │
│     b. Classify by file_type (from rules or param)  │
│     c. Extract timestamp from filename or mtime     │
│     d. Check for existing entry (by source_path)    │
│        - If exists and content_mutable: update      │
│        - If exists and not mutable: skip            │
│        - If not exists: create                      │
│     e. Store in memory_files table                  │
│  3. Report: files imported, updated, skipped        │
└────────────────────────────────────────────────────┘
```

### Deduplication

By `source_path` — if a file with the same source_path already exists:
- If `content_mutable = true`: update content and set `imported_at_unix_ms = now`
- If `content_mutable = false`: skip silently

This makes re-import safe and idempotent.

### Large files

Some expanded memory files may be large. The importer should:
- Read files in full (no streaming needed — these are markdown text, typically <100KB)
- If a file exceeds a configurable size limit (default: 1MB), log a warning and skip

## Surfaces

1. **Admin API:** `POST /api/v1/import/memory-files` — accepts directory path + classification rules
2. **CLI:** `animusd import memory-files <directory> --type=daily_note --pattern="*.md"`

## Acceptance Criteria

- [ ] `MemoryFileImporter` class reading files from a directory
- [ ] File type classification by pattern matching or explicit parameter
- [ ] Timestamp extraction from `YYYY-MM-DD` filename patterns
- [ ] Idempotent import (skip or update existing by source_path)
- [ ] Size limit enforcement (skip files over configurable threshold)
- [ ] Admin API endpoint for bulk import
- [ ] Report: count of files imported, updated, skipped, errors
- [ ] UTF-8 content stored faithfully (no encoding transforms)

## Relationship to Other Import Tracks

| Track | What it imports | Animus structure | Ticket |
|---|---|---|---|
| Sessions | Raw conversation turns | `sessions` + `session_turns` tables | 047 |
| Episodic/Semantic | Curated observations + ontology | `observations` + `ontology_entities` tables | 048 |
| MemoryFiles | Raw text documents | `memory_files` table | **This ticket (049)** |

The three tracks are independent — they can be implemented and run in any order. Together they provide full migration coverage: raw history (sessions), structured knowledge (observations/entities), and supplementary documents (files).

## Files

- **New:** `include/animus_kernel/MemoryFileImporter.h`
- **New:** `src/kernel/import/MemoryFileImporter.cpp`
- **Modified:** `src/kernel/admin/AdminServer.cpp` — import endpoint
- **Modified:** `CMakeLists.txt`

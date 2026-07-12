# Ticket 092 — MemoryFile Processing Pipeline

**Status:** Implemented  
**Priority:** High  
**Created:** 2026-06-16  
**Depends on:** None  
**Component:** Memory System / Consolidation

## Summary

Add a `status` field to MemoryFiles (`unprocessed` / `processed`) and route unprocessed files through the consolidation intake pipeline, similar to how new sessions are processed. Unprocessed MemoryFiles are picked up during consolidation intake sessions, parsed for observations and ontology entities, and then marked as processed.

## Motivation

When migrating from OpenClaw, Kestrel's `memory_expanded/` files will be imported as MemoryFiles. These contain highly curated, dense knowledge — philosophical positions, project history, hard-won lessons, identity construction. They need to go through consolidation to extract observations and update the ontology, just like session content.

Currently, MemoryFiles are created and immediately indexed for search, but there's no mechanism to extract their knowledge into the structured memory layers (observations, entities, session reports). This means:

1. **Migration knowledge stays flat.** Imported files are searchable but don't contribute to the ontology or observation store.
2. **No consolidation pass.** New MemoryFiles created by an agent (e.g., "I should write a MemoryFile about this pattern I discovered") don't get parsed for structured knowledge extraction.
3. **Inconsistency with sessions.** Sessions are processed through consolidation. MemoryFiles — which are often *more* curated and important — are not.

## Design

### Data model change

Add `status` column to `memory_files` table:

```sql
ALTER TABLE memory_files ADD COLUMN status TEXT NOT NULL DEFAULT 'unprocessed';
-- Values: 'unprocessed', 'processed'
```

### Creation behavior

- **New MemoryFiles created by the agent** (via MemoryFile tool): default to `unprocessed`.
- **MemoryFiles imported during migration**: default to `unprocessed`.
- **MemoryFiles explicitly marked processed by the agent**: set `status = 'processed'` on creation.

### Consolidation intake

During a consolidation intake session, the consolidator should:

1. **Fetch unprocessed MemoryFiles** — `SELECT * FROM memory_files WHERE status = 'unprocessed' AND agent_id = ? ORDER BY created_at`
2. **Read and parse** — For each unprocessed file, read its content and treat it as a structured knowledge source (similar to how a session is read, but with higher expected density and curation quality).
3. **Extract observations** — Create observations from key claims, positions, and lessons in the file. Tag with `source: memory_file:<path>`.
4. **Update ontology** — Create or update entities and facts mentioned in the file.
5. **Mark as processed** — `UPDATE memory_files SET status = 'processed' WHERE id = ?`

### Priority ordering

In a consolidation intake session that has both unprocessed sessions and unprocessed MemoryFiles:
1. Process sessions first (they contain conversational context that may be time-sensitive).
2. Process MemoryFiles second (they contain stable, curated knowledge).

This ensures diary entries and session-based observations are extracted before the more static MemoryFile content.

### Tool changes

Add `status` to the MemoryFile creation API:

```ruby
# memory_file tool handler
def handle_create(params, context)
  # ...existing creation logic...
  status = params['status'] || 'unprocessed'  # new parameter
  db.create_memory_file(agent_id:, path:, content:, status:)
end
```

Add a `memory_file:process` sub-action for explicit marking:

```ruby
def handle_mark_processed(params, context)
  db.update_memory_file(id: params[:id], status: 'processed')
end
```
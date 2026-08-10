# Ticket 142: Tiered Session Turn Storage

**Status:** Planned
**Created:** 2026-08-10
**Priority:** Low

## Problem

At ~6GB/year growth in Postgres (session_turns dominate), long-lived agents
will accumulate significant data. While Postgres handles this fine at current
scale, a tiered approach gives us:

1. Fast access to recent turns (hot path, active context)
2. Compressed archival of historical turns (cold path, queryable on demand)
3. Optional configurable retention for users who want to prune

## Design Principle

**Preserve everything, gate access, never destroy.**

Default behavior: keep all turns forever. Tiering moves old turns to cheaper
storage but does not delete them. Retention pruning is opt-in only.

## Scope

### Phase 1: Retention Config (Optional)

- Agent-level field: `max_turn_age_days INTEGER DEFAULT 0` (0 = keep forever)
- Background job (runs daily): moves turns older than threshold to
  `session_turns_archive` table (identical schema, no indexes except
  `session_id`).
- `session_turns_archive` is queryable but not loaded by the session store
  for active context building.
- Admin endpoint to retrieve archived turns for a session on demand.

### Wizard Integration

- Add retention field to the Memory Configuration step (Stage 6) of `/wizard`:
  - Label: "Turn retention (days)"
  - Default: 0 (Keep forever) — shown as a switch or text field
  - Hint: "Archives turns older than N days to cold storage. 0 = keep all
    turns in primary storage. Recommended for high-volume agents."
- Pre-set suggestion when choosing certain templates (e.g. a "Dev Lab"
  template could default to 30 days).
- Also exposed in the Agents page agent form under the Budget tab, alongside
  other storage-related fields.

### Phase 2: Cold Storage Export

- API endpoint: `GET /api/v1/agents/:id/turns/archive?before=YYYY-MM-DD`
  returns compressed JSONL (gzip stream).
- Import can ingest archived turns back into the archive table.
- Useful for backup-to-disk workflows without DB bloat.

### Phase 3: Vacuum / Compaction

- After archiving, `VACUUM RECLAIM` on `session_turns` to reclaim space.
- Postgres auto-vacuum handles this, but explicit reclaim after bulk moves
  is good hygiene.

### What This Is NOT

- Not destruction. Archived turns are preserved and queryable.
- Not the default. `max_turn_age_days = 0` means keep everything in the
  primary table.
- Not turn pruning in the OpenClaw sense. OpenClaw destroys turns on
  compaction. Animus tiers them.

### Migration

- `ALTER TABLE agents ADD COLUMN max_turn_age_days INTEGER NOT NULL DEFAULT 0;`
- `CREATE TABLE session_turns_archive (LIKE session_turns INCLUDING ALL);`
- Index on `session_turns_archive(session_id)`.

## Testing
- Set `max_turn_age_days = 30`, verify old moves to archive table.
- Query archived turns, verify completeness.
- Set `max_turn_age_days = 0`, verify no moves.
- Import agent with archived turns, verify round-trip.

## Future Consideration

If an agent accumulates 50GB+ of turns over many years, consider exporting
archived turns to compressed files on disk (S3/local) and storing only
metadata in the DB. This is a v0.2+ concern — the archive table handles
the first order of magnitude.

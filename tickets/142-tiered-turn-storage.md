# Ticket 142: Tiered Session Turn Storage

**Status:** ✅ Shipped
**Created:** 2026-08-10
**Completed:** 2026-08-12
**Priority:** Low
**Commit:** `cd346d6`

## Problem

At ~6GB/year growth, session_turns dominate database size. Long-lived agents
need a way to tier old turns to cheaper storage without losing them.

## Design Principle

**Preserve everything, gate access, never destroy.**

Default: keep all turns forever (`max_turn_age_days = 0`). Tiering moves old
turns to an archive table but does not delete them.

## What Was Done

### Agent field
- `max_turn_age_days INTEGER NOT NULL DEFAULT 0` on agents table
- Exposed in agent form under Budget section: "Turn retention (days)"
- 0 = keep all turns in primary storage forever

### Archive table
- `session_turns_archive` — identical schema to `session_turns`
- Indexed on `session_id`
- Created during session store schema initialization

### API endpoints

**POST `/api/v1/agents/:id/tier-turns`** — moves old turns to archive
- Query param `?max_age_days=N` overrides agent's configured value
- Idempotent: `NOT EXISTS` check on archive.id prevents duplicate moves
- Returns JSON: `{archived: N, deleted_from_primary: N}`

**GET `/api/v1/agents/:id/turns/archived`** — retrieves archived turns
- Query params: `?session_id=N&limit=100&offset=0`
- Returns JSON array of turn objects

### Export/Import
- Archive table exported as `sessions/turns_archive.jsonl` alongside sessions
- Import: id-based dedup (archive IDs are original turn IDs, so idempotent)

### Frontend
- "Turn retention (days)" number field in agent form, Budget section
- Hint: "Archives turns older than N days to cold storage. 0 = keep all."

## Not Implemented (follow-up)
- Scheduler integration (daily cron trigger) — manual API call for now
- Phase 2: compressed JSONL export endpoint for backup-to-disk
- Phase 3: explicit VACUUM after archiving (Postgres auto-vacuum handles it)

## Testing
- [x] Build clean on workstation (all targets)
- [ ] Set max_turn_age_days=30, trigger tier-turns, verify moves
- [ ] Query archived turns endpoint
- [ ] Export/import with archive table round-trip

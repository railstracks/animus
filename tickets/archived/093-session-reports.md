# Ticket 093 — Session Reports

**Status:** Open  
**Priority:** High  
**Created:** 2026-06-16  
**Updated:** 2026-06-25 — redesigned field structure and context injection model  
**Depends on:** Consolidation pipeline (implemented), Active Memory provider system (implemented)  
**Component:** Memory System / Consolidation / Active Memory

## Summary

Add per-session structured reports to Animus, optimized for **cross-session awareness** and **session searchability**. Reports are produced during consolidation intake and injected into active memory as a chronologically-ordered block scoped by a per-agent token budget.

## Motivation

Animus already extracts episodic observations from sessions — that handles the "memory extraction" side. What's missing is **awareness**: a compact, current view of what's happening across an agent's active sessions, so the agent can orient quickly when waking up in a new session or when scanning for relevant prior conversations.

The OpenClaw metacognition plugin used five fields (`what_happened`, `what_learned`, `my_opinions`, `others_shared`, `emotional_notes`) that doubled as both awareness and memory extraction. In Animus, episodic memory covers extraction. Session reports should focus purely on awareness.

## Design

### Fields

Four temporal fields, each ~200 chars max:

| Field | Purpose |
|-------|---------|
| **Summary** | Stable identity — what kind of session this is. Rarely changes. |
| **PastEvents** | Trajectory — what has transpired. Accumulates and compresses over time. |
| **CurrentActivity** | Snapshot — what's happening right now. Refreshes each intake cycle. |
| **ForwardLook** | Expectation — what's expected to happen next downstream in this session. |

The temporal axis (past → present → future) maps directly to the agent's orienting question: *"Where was this session, where is it now, where is it going?"*

### Data model

```sql
CREATE TABLE session_reports (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  session_id INTEGER NOT NULL REFERENCES sessions(id),
  agent_id TEXT NOT NULL,
  summary TEXT NOT NULL DEFAULT '',
  past_events TEXT NOT NULL DEFAULT '',
  current_activity TEXT NOT NULL DEFAULT '',
  forward_look TEXT NOT NULL DEFAULT '',
  created_at_unix_ms INTEGER NOT NULL,
  updated_at_unix_ms INTEGER NOT NULL,
  UNIQUE(session_id, agent_id)
);

CREATE INDEX idx_session_reports_agent ON session_reports(agent_id);
CREATE INDEX idx_session_reports_updated ON session_reports(updated_at_unix_ms);
```

`UNIQUE(session_id, agent_id)` ensures one report per session per agent. Reports are **supplanted** (upserted) on each intake cycle that touches that session — no version history. If historical state is needed, episodic observations capture it.

### Consolidation integration

Session reports are produced during **consolidation intake**, as an additional step after processing unprocessed session turns:

1. Consolidator detects sessions with unprocessed turns (existing trigger).
2. After extracting observations, consolidator calls `sessions:report` on the consolidation tool.
3. The tool receives the session's recent content and produces the four fields (~200 chars each).
4. Report is upserted — replaces any previous report for that session.

This coasts on the existing intake trigger: sessions are only re-reported when they have new unprocessed messages. No separate cron schedule needed.

### Active Memory injection

A new **Session Reports** block is injected into active memory, with its own token budget:

**Agent configuration** — new field on `Agent` struct:
- `session_report_context_limit` (integer, default ~1500 tokens) — sits alongside `memory_file_token_budget`, `episodic_token_budget`, etc. in the agent settings form at `/agents`.

**Selection algorithm:**
1. Query all session reports for the agent, ordered by `updated_at_unix_ms DESC`.
2. Take only the **most recent report per distinct session** (deduplicate by `session_id`).
3. Insert reports into the active memory block, most recent first, until the token budget is exhausted.
4. If the budget runs out mid-report, include the report anyway (don't truncate fields).

**Behavior characteristics:**
- During quiet periods with few active sessions, reports may go back weeks.
- For high-volume agents (e.g. social media bots with many concurrent sessions), the budget may only cover the last 30 minutes of activity.
- Empty when no reports exist — the block simply doesn't appear.
- Chronology is the only ranking — no relevance scoring or embedding computation needed at injection time.

**Context block format:**
```
## Session Reports
[Session #123 — 2h ago]
Summary: Debugging Animus embedding pipeline (ticket 098)
Past: Fixed zero-vector bug, built Phase 2 admin UI, implemented auto-reconnect
Current: Phase 2 complete, orienting on ticket 093
Forward: 093 implementation, then remaining 098 Phase 3 items

[Session #119 — 1d ago]
Summary: ...
```

### API surface

**CRUD on session reports:**
```
GET    /api/v1/sessions/{id}/report           — get report for a session
PUT    /api/v1/sessions/{id}/report           — create or update report
DELETE /api/v1/sessions/{id}/report           — remove report
```

**Listing for active memory / UI:**
```
GET /api/v1/agents/{id}/session-reports?limit=20
— returns most recent report per session, ordered by updated_at DESC
```

**Search:**
```
GET /api/v1/agents/{id}/session-reports/search?q=<query>
— full-text search across all four fields
```

### Admin UI

- Agent settings form (`/agents`): add `session_report_context_limit` integer field alongside the other memory budget fields.
- Session detail view: show the session report (if any) with editable fields.
- Future: dedicated session reports listing page.

## Implementation plan

1. **SessionReportStore** — new store class following `SessionNotesStore` pattern
2. **DB schema** — `session_reports` table migration
3. **Agent struct** — add `session_report_context_limit` field (default 1500)
4. **Admin API** — CRUD + listing + search endpoints
5. **Consolidation tool** — add `sessions:report` action to the consolidation composite tool
6. **Active Memory provider** — `SessionReportProvider` with chronological selection
7. **Admin UI** — agent settings field + session detail report view

## Migration consideration

Kestrel's existing OpenClaw session reports (801+ sessions) can be imported by mapping the old five fields into the new four. The mapping is approximate — `what_happened` → `past_events`, emotional/subjective content → `summary` or dropped if purely observational (episodic memory covers it).

## Out of scope

- Relevance-based injection (embedding similarity for older reports) — deferred; chronology-only for now
- Version history / report auditing — episodic observations cover this
- Per-session-type report templates — all sessions use the same four fields

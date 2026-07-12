# Ticket 038 — Gallivanting Blocks: Implementation Report

**Completed:** 2026-05-13 (phase 1 — data layer, tool, admin API, admin UI, scheduling)
**Commits:** 12 (201fa8b..a95c33f)
**Lines changed:** +3,200 across 14 files

## Summary

Gallivanting blocks are scheduled periods of self-directed agent exploration. The system provides threads as named exploration options, SDT (Self-Determination Theory) need-scoring for balance tracking, a composite agent-facing tool for lifecycle management, and an admin UI with a spider graph for visual balance auditing. Scheduling uses the existing Scheduler infrastructure with a singleton-per-agent pattern.

## Architecture

### Data Model

Two SQLite tables in the shared data store:

```
gallivanting_threads:
  id              INTEGER PRIMARY KEY AUTOINCREMENT
  agent_id        TEXT NOT NULL DEFAULT 'default'
  name            TEXT NOT NULL
  description     TEXT — current state, updated after each run
  sdt_tags        TEXT — JSON: {"autonomy": 0.8, "competence": 0.3, ...}
  prompt_template TEXT — per-thread custom prompt (unused in v1 UI)
  enabled         INTEGER NOT NULL DEFAULT 1
  created_at_unix_ms INTEGER
  updated_at_unix_ms INTEGER

gallivanting_sessions:
  id              INTEGER PRIMARY KEY AUTOINCREMENT
  thread_id       INTEGER NOT NULL REFERENCES gallivanting_threads(id) ON DELETE CASCADE
  agent_id        TEXT NOT NULL DEFAULT 'default'
  started_at_unix_ms INTEGER
  duration_ms     INTEGER
  summary         TEXT — what the agent did
  outcome         TEXT — what came of it
  sdt_scores      TEXT — JSON: per-session actual scores
  tools_used      TEXT — JSON array: ["diary", "web_search", ...]
  created_at_unix_ms INTEGER
```

Indexes on `agent_id` and `(agent_id, created_at_unix_ms DESC)` for efficient balance queries.

### SDT Needs Framework

Six dimensions from Self-Determination Theory, extended with personal development:

| Need | Meaning | Example |
|-----|---------|---------|
| Autonomy | Self-directed exploration | Independent research, open-ended coding |
| Competence | Skill development | Learning CMake, practicing techniques |
| Relatedness | Social connection | Chatting, co-working |
| Personal Development | Personality formation, identity | Reading philosophy, self-reflection |
| Relaxation | Intentional decompression | Casual reading, low-stakes creative play |
| Meaning | Purpose-driven work | Architecture sessions, value-aligned projects |

Threads carry default SDT tags. Sessions carry actual per-session scores. The balance endpoint aggregates session scores over a configurable lookback period.

### Components

| Component | File | Lines | Purpose |
|-----------|------|-------|---------|
| GallivantingStore | `gallivanting/GallivantingStore.h/.cpp` | 450 | SQLite CRUD for threads + sessions, SDT balance aggregation |
| GallivantingTool | `tools/GallivantingTool.h/.cpp` | 640 | Agent-facing composite tool: 7 actions |
| Admin routes | `internal/AdminServerRoutesGallivanting.inc` | 390 | Thread CRUD, session list, balance endpoint |
| Schedule creation | `internal/AdminServerRoutesAgentsAndRuntime.inc` | 92 | POST /api/v1/scheduler/schedules (new endpoint) |
| Admin UI | `GallivantingView.vue` | 1,100 | Spider graph, threads, scheduling, session history |
| ScheduleStore fix | `scheduler/ScheduleStore.h/.cpp` | 15 | CreateAndReturnId for id propagation |
| Template embedding | `cmake/GenerateEmbeddedTemplates.cmake` | — | Embeds templates/**/*.md into binary |

## API Surface

### Agent tool: `gallivanting`

| Action | Description |
|--------|-------------|
| `list` | List all threads for this agent |
| `read` | Read thread state + recent sessions |
| `create` | Create a new thread (name, SDT tags, description) |
| `update` | Update thread metadata |
| `delete` | Delete a thread and its sessions |
| `record` | Record a session result (summary, outcome, SDT scores, tools used) |
| `history` | List recent sessions (per-thread or all, paginated) |

All operations scoped by `agent_id` via `__agent_id` injection (same pattern as DiaryTool, MemoryTool).

### Admin API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/gallivanting/threads` | GET | List threads for agent |
| `/api/v1/gallivanting/threads` | POST | Create thread |
| `/api/v1/gallivanting/threads/{id}` | GET | Read thread |
| `/api/v1/gallivanting/threads/{id}` | PATCH | Update thread |
| `/api/v1/gallivanting/threads/{id}` | DELETE | Delete thread |
| `/api/v1/gallivanting/threads/{id}/sessions` | GET | List sessions for thread |
| `/api/v1/gallivanting/agents/{id}/balance` | GET | SDT balance scores (aggregated, configurable lookback) |
| `/api/v1/memory/templates/gallivanting` | GET | Fetch default prompt template by locale |
| `/api/v1/scheduler/schedules` | POST | Create schedule (reused for gallivanting scheduling) |

### Admin UI

The GallivantingView page provides:
- **Agent selector** — switch between agents
- **SDT Balance spider graph** — 6-axis radar chart with configurable lookback (3 days–3 months)
- **Thread management** — list, create, edit, delete threads with SDT tag toggles
- **Session history** — per-thread expandable list with summaries, outcomes, scores
- **Schedule card** — frequency picker (daily/weekly/every N days/custom cron), time, timezone, duration, random offset, prompt template, enable/disable
- **Default prompt** — fetched from embedded templates, pre-populated in schedule form, "Reset to default" button

### Scheduling

Gallivanting blocks are scheduled through the existing Scheduler infrastructure using a singleton pattern:
- One recurring schedule per agent with `tag: "gallivanting"`
- The schedule's `message` field carries JSON config: `{type, duration_minutes, random_offset_minutes, prompt_template}`
- The UI queries by `agent_id` + `tag` to find the singleton, creates if missing
- Cron expressions derived from frequency picker (daily → `0 2 * * *`, weekly → `0 2 * * 1`, etc.)
- Timezone-aware scheduling with random offset to avoid mechanical regularity

## Design Decisions

1. **SDT framework for balance tracking.** Self-Determination Theory provides a psychologically grounded vocabulary for need satisfaction. Six dimensions give enough granularity for meaningful spider graphs without overcomplicating the agent's self-assessment. The framework is auditable without being prescriptive — the spider graph is for user insight, not agent optimization.

2. **Threads as exploration options, not prompt containers.** Threads are entries in the "Available Threads" list shown in the gallivanting preamble. The prompt template is a property of the scheduled block, not individual threads. This separation keeps threads lightweight (name + state + SDT tags) and puts the prompt where it belongs: with the schedule configuration.

3. **Singleton schedule per agent.** Rather than creating a parallel scheduling system, gallivanting reuses the existing Scheduler with a `tag: "gallivanting"` convention. One schedule per agent, looked up by agent_id + tag. Simple, no new infrastructure.

4. **Template embedding at compile time.** The 23 locale files in `templates/gallivanting/` are embedded into the binary via CMake + xxd (same pattern as admin UI). The daemon works from any CWD without needing `templates/` nearby. Template lookup checks embedded resources first, falls back to filesystem.

5. **Diary separation.** Gallivanting sessions record to `gallivanting_sessions`, not to the diary. The diary is for personally significant moments; gallivanting sessions are activity tracking. The agent may still write diary entries during gallivanting, but the session log is separate.

6. **ScheduleStore::CreateAndReturnId.** The original `ScheduleStore::Create` generated an id in a local variable but never propagated it back to the caller. `Scheduler::Create` returned `toCreate.id` which was empty, producing "failed to create schedule: " with no error detail. Added `CreateAndReturnId(ScheduleDescriptor&, error)` that mutates the descriptor and returns the id, while keeping the original `Create` with const ref for backward compatibility (tests, consolidation pipeline).

7. **Prompt belongs to the schedule, not the thread.** During implementation we initially put the prompt field on threads. Melvin correctly identified that threads are just pick-list options — the prompt wraps the entire gallivanting block including the thread list. Moved the prompt template to the schedule card. Thread dialog simplified to name, description, SDT tags, enabled.

## Bugs Fixed

| Bug | Cause | Fix |
|-----|-------|-----|
| HTTP 503 "gallivanting store not available" | `SetGallivantingStore` called before `new GallivantingStore` | Moved setter after construction |
| Schedule creation 500 with empty error | Store generated id in local variable, never propagated to caller | Added `CreateAndReturnId` |
| Template endpoint 400 for gallivanting | Kind validation only allowed review/intake/ontology | Added `gallivanting` → `templates/gallivanting/` |
| Default prompt stuck on "Loading default prompt" | Template endpoint rejected `gallivanting` kind | Same fix as above |
| Build failure in SchedulerTests | Non-const ref parameter couldn't bind to rvalue temporaries | Kept const ref `Create`, added separate `CreateAndReturnId` |

## Commits

| Hash | Description |
|------|-------------|
| 201fa8b | Redesign ticket 038 with SDT framework, thread model, composite tool |
| 03dd59b | Embed template files into binary at compile time (CMake + xxd) |
| 9e51414 | Add GallivantingStore + GallivantingTool (ticket 038 core) |
| 34a11f5 | Fix missing chrono include in GallivantingStore |
| a2fd328 | Add admin API routes for gallivanting |
| 6428f93 | Add admin UI page with spider graph |
| de3c332 | Fix SetGallivantingStore ordering bug |
| 4539020 | Add POST /api/v1/scheduler/schedules admin endpoint |
| d6906fe | Add scheduling card to Gallivanting page |
| 937f1d5 | Fix schedule creation id propagation + default prompt pre-population |
| c29ccbd | Restore const ref in ScheduleStore::Create, add CreateAndReturnId |
| 53aafce | Move prompt template from threads to schedule |
| a95c33f | Add gallivanting as valid template kind |

## Acceptance Criteria Status

- [x] Gallivanting threads table with CRUD
- [x] Gallivanting sessions table with recording
- [x] Agent-facing `gallivanting` tool with all 7 actions
- [x] SDT tag system with per-thread defaults and per-session scores
- [ ] Prompt preamble with recent sessions + available threads (defined in ticket, needs scheduler integration)
- [x] Default prompt template loading from `templates/gallivanting/`
- [x] "Reset to default" button in admin UI
- [x] Admin API endpoints for threads, sessions, balance
- [x] Admin UI page with spider graph (configurable lookback)
- [x] Admin UI thread management (create/edit/delete)
- [x] Admin UI session history list (per-thread)
- [x] Scheduler integration for gallivanting triggers (singleton schedule per agent)
- [ ] Tool restriction during gallivanting (only allowed_tools)
- [ ] Budget enforcement (token limit per block)
- [ ] Per-agent configuration in agent descriptor
- [x] Template embedding at compile time
- [x] All existing tests continue to pass

**12/17 criteria met.** Remaining 5 are execution-phase features that require the session pipeline to recognize gallivanting-triggered events and apply constraints (tool restriction, budget, preamble construction).

## Remaining Work (Phase 2)

The core data layer, tool surface, admin API, admin UI, and scheduling are complete. The remaining work is the execution integration — what happens when a gallivanting schedule fires:

1. **Preamble construction** — When a gallivanting trigger fires, build the "Recent Sessions / Available Threads" prefix and prepend it to the prompt template
2. **Tool restriction** — During gallivanting, only `allowed_tools` should be available to the agent
3. **Budget enforcement** — Token limit per block, with warning at 80%
4. **Per-agent config** — Gallivanting section in agent descriptor (frequency, duration, boundaries, schedule)
5. **Session recording** — Automatic `gallivanting record` call at session end with duration and context

These require changes to the session pipeline (ChainRunner/SessionManager) to recognize gallivanting-type events and apply the appropriate constraints.

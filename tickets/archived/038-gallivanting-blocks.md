# Ticket 038 — Gallivanting Blocks

**Priority:** P2 (scheduler integration; thread-based exploration; SDT-driven balance)
**Status:** Open
**Dependencies:** Ticket 029 (Multi-tenant), Ticket 024 (Scheduler), Ticket 036 (Diary)
**Updated:** 2026-05-13

## Summary

Add configurable "gallivanting" blocks for agents — scheduled periods of self-directed exploration, personality-building, and open-ended activity. Gallivanting threads are first-class entities with SDT need-scoring, persistent state, and a composite agent-facing tool for CRUD and session recording. An admin UI provides user audit via spider graph and session history.

## Motivation

Agents with only reactive and scheduled sessions lack self-directed time. Gallivanting produces:
- Personality formation through choosing what to pursue
- Judgment development through low-stakes autonomous decisions
- A visible activity profile that the user can audit for balance

The SDT framework makes this auditable without making it prescriptive.

## SDT Needs Framework

Each gallivanting thread is tagged with one or more needs from Self-Determination Theory:

| Need | Meaning | Example activities |
|------|---------|--------------------|
| **Autonomy** | Self-directed exploration, choosing what to investigate | Independent research, open-ended coding |
| **Competence** | Skill development, technical craft, building capability | Learning CMake, practicing a technique |
| **Relatedness** | Social connection, collaboration, communication | Chatting with humans or agents, co-working |
| **Personal Development** | Personality formation, self-reflection, identity work | Reading philosophy, writing reflections |
| **Relaxation** | Lower-pressure activities, intentional decompression | Casual reading, low-stakes creative play |
| **Meaning** | Purpose-driven work, connecting to values | Architecture sessions, value-aligned projects |

Threads can have multiple tags. Per-session scores are separate from thread defaults.

## Gallivanting Threads

### Data Model

```
gallivanting_threads (SQLite table):
  id              INTEGER PRIMARY KEY
  agent_id        TEXT NOT NULL DEFAULT 'default'
  name            TEXT NOT NULL
  description     TEXT — current state, updated after each run
  sdt_tags        JSON — { "autonomy": 0.8, "competence": 0.3, ... }
  prompt_template TEXT — custom prompt override (NULL = use default)
  enabled         BOOLEAN DEFAULT 1
  created_at_ms   INTEGER
  updated_at_ms   INTEGER

gallivanting_sessions (SQLite table):
  id              INTEGER PRIMARY KEY
  thread_id       INTEGER NOT NULL REFERENCES gallivanting_threads(id)
  agent_id        TEXT NOT NULL DEFAULT 'default'
  started_at_ms   INTEGER
  duration_ms     INTEGER
  summary         TEXT — what the agent did
  outcome         TEXT — what came of it
  sdt_scores      JSON — { "autonomy": 0.7, "competence": 0.5, ... } (actual scores)
  tools_used      JSON — ["diary", "web_search", ...]
  created_at_ms   INTEGER
```

### Thread Template Defaults

New threads inherit SDT tags based on the kind of activity. Either user or agent can modify tags at any time.

### Thread State

After each gallivanting session, the agent updates the thread's `description` field with a brief current-state summary. This description is shown in the next gallivanting prompt preamble, giving the agent continuity.

## Composite Agent Tool: `gallivanting`

Agent-facing tool providing full lifecycle management:

### Actions

| Action | Description |
|--------|-------------|
| `list` | List all threads for this agent (name, ID, state, SDT tags, enabled) |
| `read` | Read a thread's full state + recent session history |
| `create` | Create a new thread (name, SDT tags, description, optional prompt override) |
| `update` | Update thread metadata (name, SDT tags, description, enabled, prompt) |
| `delete` | Delete a thread |
| `record` | Record a gallivanting session result (summary, outcome, SDT scores, tools used, updated thread state) |
| `history` | List recent sessions for a thread (paginated) |

All operations scoped by `agent_id` via `__agent_id` injection (same pattern as DiaryTool, MemoryTool).

## Gallivanting Prompt Preamble

When a gallivanting block fires, the agent receives the prompt template (default or custom) prefixed with:

```
## Recent Gallivanting Sessions
- [date] Thread: "Thread Name" — brief summary of what was explored
- [date] Thread: "Other Thread" — brief summary
(最多5个最新会话)

## Available Threads
| Thread | State | Needs |
|--------|-------|-------|
| Topolenia | "Adaptive homeostasis working, oscillating hs..." | autonomy, competence |
| Reading | "Mortal Questions 15/15, starting Consciousness Explained" | relaxation, personal_development |
| VtM Writing | "Union of the Dead ~58K words, chapter 7 in progress" | relaxation, meaning |

Choose a thread to continue, or explore something new.
```

This gives the agent pattern visibility without mandating a choice. The spider graph is the user's audit tool, not the agent's decision driver.

## Default Prompt Templates

Located in `templates/gallivanting/{locale}.md`. The existing templates (23 locales) become the default prompts.

The admin UI provides a "Reset to default" button that fetches `GET /api/v1/templates/gallivanting?locale={locale}` and sets the prompt field to the returned content.

### Template Bundling

Templates are loaded at runtime from `CWD/templates/` (matching existing consolidation template loading pattern). For installed/packaged deployments, options:
1. **Embed at compile time** — add templates as CMake embedded resources (like admin UI)
2. **Install to `share/animus/templates/`** — standard XDG convention, template loader searches both paths
3. **Configurable template root** — add `template_dir` to KernelConfig

Recommend option 2 (install path) for now, option 1 (embedding) as a later optimization. The existing template loader already searches multiple roots; adding a third is trivial.

## Admin API

```
GET    /api/v1/gallivanting/threads                  — list threads for agent
POST   /api/v1/gallivanting/threads                  — create thread
GET    /api/v1/gallivanting/threads/{id}             — read thread
PATCH  /api/v1/gallivanting/threads/{id}             — update thread
DELETE /api/v1/gallivanting/threads/{id}             — delete thread
GET    /api/v1/gallivanting/threads/{id}/sessions    — list sessions for thread
GET    /api/v1/gallivanting/agents/{id}/balance      — SDT balance scores (aggregated)
GET    /api/v1/templates/gallivanting?locale={locale} — fetch default prompt template
```

## Admin UI: Gallivanting Page (`/gallivanting`)

### Spider Graph

Radar/spider chart showing SDT need satisfaction across a configurable lookback period.
- Default lookback: 2 weeks
- Configurable: 3 days — 3 months
- Aggregates per-session SDT scores from `gallivanting_sessions`
- Visual: 6-axis radar, filled area, per-need labels

### Thread Management

List of configured threads with:
- Name, enabled status
- Current state description
- SDT tags as colored chips
- Edit/delete controls

Create/edit dialog with:
- Name
- SDT tags (multi-select with sliders or checkboxes)
- Custom prompt override (textarea + "Reset to default" button)
- Enabled toggle

### Session History

Per-thread scrollable list of recent sessions:
- Date/time, duration
- Summary and outcome
- SDT scores for that session
- Tools used

### Agent Selector

Dropdown to switch between agents (same pattern as Memory Search page).

## Per-Agent Configuration

```json
{
  "gallivanting": {
    "enabled": true,
    "frequency": "daily",
    "duration_minutes": 60,
    "boundaries": {
      "allowed_tools": ["diary", "gallivanting", "web_search", "web_fetch"],
      "max_budget_tokens": 50000,
      "requires_tool_use": true
    },
    "schedule": {
      "preferred_time": "02:00",
      "timezone": "Europe/Amsterdam",
      "random_offset_minutes": 30
    }
  }
}
```

- **frequency**: daily, weekly, custom cron expression
- **duration_minutes**: soft limit; agent can end early
- **boundaries.allowed_tools**: which tools are available during gallivanting (includes `gallivanting` tool for thread CRUD and session recording)
- **boundaries.max_budget_tokens**: token budget for the block
- **boundaries.requires_tool_use**: if true, must produce at least one tool call
- **schedule**: timing with randomization

Note: `output_channels` removed from original design. Gallivanting results are now recorded via the `gallivanting` tool's `record` action and stored in `gallivanting_sessions`. The diary is for personally significant moments, not routine activity logs.

## Gallivanting Session Lifecycle

1. **Scheduler** fires a gallivanting trigger for an agent
2. **Session** created with type `gallivanting`, using configured boundaries
3. **Agent** receives prompt preamble (recent sessions + available threads + default or custom prompt)
4. **Agent** picks a thread or starts something new
5. **Agent** explores freely within boundaries
6. **Agent** calls `gallivanting record` with session results
7. **Session** ends when: duration expires, budget exhausted, or agent signals completion

### What counts as a valid session

The minimum is: the agent spent the time doing something and recorded it. "I spent time thinking about X" is valid. The point is self-direction, not productivity.

## Relationship to Other Tickets

- **Diary (036):** Agent may still write diary entries during gallivanting for personally significant moments, but gallivanting session logs go to `gallivanting_sessions`, not diary
- **Study (037):** Can be offered as a structured option during gallivanting time
- **Project tool (040):** Agent may pick up an unblocked project task during gallivanting
- **Scheduler (024):** Gallivanting blocks are scheduled actions with type `gallivanting`
- **Memory (024):** Agent may search/recall from memory during gallivanting

## Acceptance Criteria

- [ ] Gallivanting threads table with CRUD
- [ ] Gallivanting sessions table with recording
- [ ] Agent-facing `gallivanting` tool with all 7 actions
- [ ] SDT tag system with per-thread defaults and per-session scores
- [ ] Prompt preamble with recent sessions + available threads
- [ ] Default prompt template loading from `templates/gallivanting/`
- [ ] "Reset to default" button in admin UI
- [ ] Admin API endpoints for threads, sessions, balance
- [ ] Admin UI page with spider graph (configurable lookback)
- [ ] Admin UI thread management (create/edit/delete)
- [ ] Admin UI session history list (per-thread)
- [ ] Scheduler integration for gallivanting triggers
- [ ] Tool restriction during gallivanting (only allowed_tools)
- [ ] Budget enforcement (token limit per block)
- [ ] Per-agent configuration in agent descriptor
- [ ] All existing tests continue to pass

## Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Gallivanting produces nothing useful | Medium | That's the point. Measure existence, not output. |
| Token budget exhaustion during exploration | Medium | Hard limit; warning at 80% |
| Gallivanting becomes routine/performative | Low | Random scheduling offset; no expected output format; no evaluation |
| Spider graph drives prescriptive behavior | Low | Graph is user audit only; agent sees thread list, not aggregate scores |
| SDT tags feel reductive | Low | Tags are configurable; agent and user can both modify |
| Diary + session log confusion | Medium | Clear separation: diary = personal significance, sessions = activity tracking |

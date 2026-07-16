# Ticket 040 — Project Tool (Slimmed)

**Priority:** P2
**Status:** Open (revised 2026-07-16)
**Dependencies:** None beyond existing tool infrastructure
**Supersedes:** Original 040 design (May 6) — scoped down for practical first iteration

## Summary

A project tool that lets agents maintain and advance multi-step objectives across sessions. Agents create projects, add tasks, mark dependencies, and track completion. Provides directed persistence — the agent wakes up, sees where it left off, and picks up the next ready task.

## Motivation

Agents need structured work tracking that survives session boundaries. The consolidation system preserves memory; the project tool preserves intent. An agent with a project can resume work immediately rather than reconstructing context from observations.

## Scope (v1 — this ticket)

- Projects with tasks and simple dependency edges
- Self-certified task completion (agent marks done)
- Manual `next_ready` — agent pulls the next unblocked task
- Tool-only interface (no admin API in v1)
- Per-agent scoping

## Out of scope (future tickets)

- Verification conditions (file_exists, test_passes, api_status)
- Auto-advance cascade logic
- Human confirmation milestones
- Admin API / admin UI
- Cross-agent project sharing
- Lua integration for gallivanting

## Data Model

Uses the existing Postgres store (same pattern as memory/ontology tables).

### projects table

| Column | Type | Description |
|--------|------|-------------|
| id | TEXT (UUID) | Primary key |
| agent_id | TEXT | Owner agent |
| title | TEXT | Project title |
| description | TEXT | Project purpose |
| status | TEXT | `active`, `completed`, `paused`, `abandoned` |
| created_at | BIGINT | Unix ms |
| updated_at | BIGINT | Unix ms |

### project_tasks table

| Column | Type | Description |
|--------|------|-------------|
| id | TEXT (UUID) | Primary key |
| project_id | TEXT (UUID) | FK → projects |
| title | TEXT | Short description |
| detail | TEXT | Extended requirements / notes |
| status | TEXT | `pending`, `ready`, `in_progress`, `completed`, `failed` |
| depends_on | TEXT[] | Array of task IDs this depends on |
| position | INTEGER | Sort order within project |
| result | TEXT | Completion evidence or output |
| created_at | BIGINT | Unix ms |
| updated_at | BIGINT | Unix ms |

Dependencies stored as an array on the task (no separate join table for v1). Acyclicity checked in code before insertion.

## Tool Interface

```
project.create     — {title, description} → project_id
project.add_task   — {project_id, title, detail?, depends_on?[]?} → task_id
project.list       — {} → active projects with task counts
project.tasks      — {project_id, status?} → tasks (optionally filtered)
project.next_ready — {project_id?} → next unblocked, unassigned task across all projects (or specific one)
project.update     — {task_id, title?, detail?, status?, result?} → updated task
project.complete   — {task_id, result?} → marks task completed, returns unblocked tasks
project.progress   — {project_id} → text tree showing all tasks with status
project.abandon    — {project_id} → marks project abandoned
```

### `next_ready`

Returns the next task where:
- `status` is `pending` or `ready`
- All tasks in `depends_on` are `completed`
- No other task in the same project is `in_progress`

If `project_id` is omitted, searches across all active projects for the agent.

### `progress`

Text-based tree visualization:
```
☐ Setup database (completed)
├── ☐ Write schema migrations (ready)
├── ☐ Create seed data (ready)
│   └── ☐ Test with pgTAP (pending)
└── ☐ Configure connection pool (in_progress)
```

## Implementation

- `ProjectStore` class — DB operations (tables, CRUD, dependency checks)
- `ProjectTool` class — tool handler registered in AgentKernel
- `IsCyclic()` — topological sort check before adding dependencies
- Agent ID from `__agent_id` (ChainRunner-injected)

## Acceptance Criteria

- [ ] `project` tool registered with 9 actions
- [ ] Projects and tasks persist in Postgres
- [ ] Simple dependency tracking with cycle detection
- [ ] `next_ready` returns correct unblocked task
- [ ] `progress` renders readable text tree
- [ ] Per-agent scoping (agent can only see/modify own projects)
- [ ] Tool registered in AgentKernel and CMakeLists
- [ ] Existing tests continue to pass

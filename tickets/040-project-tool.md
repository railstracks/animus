# Ticket 040 — Project Tool (Rooted DAG)

**Priority:** P2 (capstone for directed persistence; integrates diary, study, gallivanting, and chains)
**Status:** Open
**Dependencies:** Ticket 029 (Multi-tenant — per-agent scoping), Ticket 021 (Tool Calling — chain assignment), Ticket 036 (Diary — project reflections)
**Updated:** 2026-05-06

## Summary

A composite project tool that models goals and tasks as a rooted directed acyclic graph (DAG). Agent chains can be assigned to tasks until their milestone conditions are satisfied, upon which dependent tasks auto-trigger. The project's root goal is complete when all terminal tasks are satisfied.

## Motivation

Agents need directed persistence — the ability to maintain and advance complex multi-step objectives across sessions. Current tools handle single operations (search, fetch, write). The project tool provides:

- **Survival across sessions:** A project persists. The agent wakes up, sees "here's where you left off, here's what's blocked, here's what's ready to start."
- **Self-organizing execution:** Tasks auto-advance when their dependencies are satisfied. No constant human supervision needed.
- **Structured gallivanting:** Instead of "explore whatever," the agent can pick up the next unblocked task in an active project. Self-directed and productive.
- **Human-agent collaboration:** Humans define the project structure; agents execute within it. Both see the same graph.

## DAG vs. Tree

Real dependencies are often diamond-shaped — task D depends on both A and B completing. A strict tree can't express that. A rooted DAG supports:

- Diamond dependencies (D depends on A and B)
- Convergent paths (multiple tasks feed into a single integration point)
- Parallel branches (independent tasks that can run concurrently)

Constraints:
- The graph must be **acyclic** — no circular dependencies (enforced on creation)
- There is exactly one **root goal** — the project's reason for existing
- Terminal tasks (leaf nodes) define project completion

## Data Model

### projects table

| Column | Type | Description |
|--------|------|-------------|
| id | TEXT (UUID) | Primary key |
| agent_id | TEXT (UUID) | Owner agent |
| title | TEXT | Human-readable project title |
| description | TEXT | Project purpose and context |
| root_task_id | TEXT (UUID) | FK → project_tasks (root node) |
| status | TEXT | "active", "completed", "abandoned", "paused" |
| created_at | INTEGER | Unix ms timestamp |
| updated_at | INTEGER | Unix ms timestamp |

### project_tasks table

| Column | Type | Description |
|--------|------|-------------|
| id | TEXT (UUID) | Primary key |
| project_id | TEXT (UUID) | FK → projects |
| title | TEXT | Task description |
| description | TEXT | Detailed requirements |
| status | TEXT | "pending", "ready", "in_progress", "blocked", "completed", "failed" |
| milestone_type | TEXT | "self_certified", "verified", "human_confirmed" |
| milestone_condition | TEXT | Verification expression (JSON) |
| assigned_chain_id | TEXT (UUID) | Currently assigned chain (nullable) |
| result | TEXT | Chain output or completion evidence |
| created_at | INTEGER | Unix ms timestamp |
| updated_at | INTEGER | Unix ms timestamp |

### project_dependencies table

| Column | Type | Description |
|--------|------|-------------|
| id | TEXT (UUID) | Primary key |
| project_id | TEXT (UUID) | FK → projects |
| task_id | TEXT (UUID) | FK → project_tasks (dependent) |
| depends_on_task_id | TEXT (UUID) | FK → project_tasks (dependency) |

Acyclicity enforced: inserting a dependency that would create a cycle is rejected.

## Milestone Satisfaction

A milestone can be satisfied by three mechanisms:

### 1. Self-certification
The agent marks a task complete based on its own judgment. Simplest, weakest verification.

```
milestone_type: "self_certified"
milestone_condition: null
```

### 2. Verification condition
A testable condition that the project tool evaluates automatically. This enables self-organizing execution.

```
milestone_type: "verified"
milestone_condition: {
  "type": "and",
  "conditions": [
    { "type": "file_exists", "path": "output/report.pdf" },
    { "type": "test_passes", "command": "pytest tests/report_test.py" },
    { "type": "api_status", "url": "http://localhost:8080/health", "expected": 200 }
  ]
}
```

Supported condition types for v1:
- `file_exists` — a file exists at the given path
- `file_contains` — a file contains a given string
- `test_passes` — a shell command exits with code 0
- `api_status` — an HTTP endpoint returns expected status
- `custom` — a Lua script returns truthy (connects to ticket 039)

### 3. Human confirmation
A human explicitly confirms the task is complete. For high-stakes or subjective milestones.

```
milestone_type: "human_confirmed"
milestone_condition: null
```

Requires admin API call: `POST /api/v1/projects/{id}/tasks/{tid}/confirm`

## Auto-advance Logic

When a task's milestone is satisfied:
1. Task status → "completed"
2. All tasks that depend on this task check their other dependencies
3. If all dependencies are satisfied → status → "ready"
4. If auto-assign is enabled and agent is idle → assign chain to "ready" task

This creates a cascade: completing one task can unblock and auto-start the next.

## Tool Interface

```json
{
  "name": "project",
  "operations": [
    "create — define a new project with root task",
    "add_task — add a task to a project, with optional dependencies",
    "add_dependency — add a dependency edge (acyclicity enforced)",
    "list — show projects with status",
    "tasks — show tasks for a project, filtered by status",
    "next_ready — get the next unblocked, unassigned ready task",
    "assign — assign a chain to a task",
    "update — update task description or milestone",
    "complete — mark a task complete (self-certified)",
    "verify — check verification conditions for a task",
    "progress — show DAG visualization (text-based) with completion status",
    "abandon — abandon a project or individual task"
  ]
}
```

### `next_ready` for gallivanting integration

The `next_ready` operation is designed for integration with gallivanting blocks. Instead of unstructured exploration, the agent can:

```lua
-- Gallivanting with project option
local task = tools.project.next_ready()
if task then
    -- Pick up structured work
    tools.project.assign(task.id, chain_id)
else
    -- No tasks available, free exploration
    tools.diary.write("reflection", "No project tasks ready. Exploring freely.")
end
```

## Admin API

```
GET    /api/v1/agents/{id}/projects                    → list projects
GET    /api/v1/agents/{id}/projects/{pid}               → project detail + DAG
POST   /api/v1/agents/{id}/projects                     → create project
GET    /api/v1/agents/{id}/projects/{pid}/tasks          → list tasks
POST   /api/v1/agents/{id}/projects/{pid}/tasks          → add task
POST   /api/v1/agents/{id}/projects/{pid}/tasks/{tid}/confirm → human-confirm task
DELETE /api/v1/agents/{id}/projects/{pid}               → abandon project
```

## Acceptance Criteria

- [ ] `ProjectManager` class owns project/task/dependency lifecycle
- [ ] Rooted DAG structure with acyclicity enforcement on dependency insertion
- [ ] Three milestone types: self_certified, verified, human_confirmed
- [ ] Verification condition evaluator (file_exists, test_passes, api_status, custom)
- [ ] Auto-advance: completing a task cascades readiness to dependents
- [ ] Project tool with all 12 operations
- [ ] Per-agent project scoping (multi-tenant)
- [ ] Admin API for project/task management and human confirmation
- [ ] `next_ready` integration point for gallivanting
- [ ] All existing tests continue to pass
- [ ] New project tests cover: DAG creation, cycle detection, auto-advance, verification conditions, milestone types

## Key Questions

- Should projects be shareable across agents? (v1: no — per-agent scope)
- How to handle failed tasks? Retry? Reassign? Block dependents permanently?
- Should verification conditions support OR logic (any dependency satisfied)?
- How large can a project DAG be before performance degrades?
- Should project state be persisted in SQLite or JSON files?
- Relationship to chain execution: does assigning a chain to a task block the chain from other work?

## Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Cycle detection is expensive for large DAGs | Low | Topological sort on insert; O(V+E) is fast for reasonable sizes |
| Verification conditions create side-channel access | Medium | Sandboxed evaluation; file_exists limited to agent paths; test_passes runs in restricted environment |
| Auto-advance creates runaway execution | Medium | Rate limit auto-starts; require agent confirmation for ready→in_progress transition |
| DAG becomes stale if agent never picks up ready tasks | Low | `next_ready` is a pull model; gallivanting provides push; admin can see stalled tasks |
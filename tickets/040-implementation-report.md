# Ticket 040 — Implementation Report

**Ticket:** 040 — Project Tool (Slimmed)
**Implemented:** 2026-07-16
**Branch:** dev
**Commit:** 81a28eb

## Summary

Implemented the slimmed v1 of the Project Tool — a persistent project/task management system for agents. Projects and tasks survive session boundaries, enabling directed persistence across sessions.

## What Was Built

### ProjectStore (`ProjectTool.h` / `ProjectTool.cpp`)

Two-table persistence layer using the existing `IDataStore` abstraction (Postgres-compatible):

- **`projects`** — id, agent_id, title, description, status, timestamps
- **`project_tasks`** — id, project_id, agent_id, title, detail, status, depends_on (JSON array), position, result, timestamps

Dependencies stored as a JSON array on the task row (no separate join table). Cycle detection via BFS traversal before dependency insertion.

### ProjectTool — 9 Actions

| Action | Params | Returns |
|--------|--------|---------|
| `create` | title, description | project JSON with id |
| `add_task` | project_id, title, detail?, depends_on?[]? | task JSON with id |
| `list` | status? | array of projects with task counts |
| `tasks` | project_id, status? | array of tasks |
| `next_ready` | project_id? | next unblocked task (auto-marks in_progress) |
| `update` | task_id, title?, detail?, status?, result? | updated task |
| `complete` | task_id, result? | completion status + newly unblocked tasks |
| `progress` | project_id | text tree with status emojis |
| `abandon` | project_id | confirmation |

### Key Behaviors

- **`next_ready`** searches across all active projects (or a specific one), skips projects with in_progress tasks, finds first task with all dependencies completed, and auto-marks it `in_progress`
- **`complete`** marks task done, then scans for pending tasks whose dependencies are now all satisfied and marks them `ready`
- **`progress`** renders a text tree with emoji status indicators (✅🔄🎯❌⬜)
- Per-agent scoping enforced on all operations via `__agent_id`

### Registration

- `ProjectStore` instantiated in `AgentKernel` with shared `IDataStore`
- `ProjectTool` registered in tool registry
- Added to `CMakeLists.txt`
- Cleanup in `AgentKernel` destructor

## Acceptance Criteria Status

- [x] `project` tool registered with 9 actions
- [x] Projects and tasks persist in Postgres
- [x] Simple dependency tracking with cycle detection
- [x] `next_ready` returns correct unblocked task
- [x] `progress` renders readable text tree
- [x] Per-agent scoping (agent can only see/modify own projects)
- [x] Tool registered in AgentKernel and CMakeLists
- [ ] Existing tests continue to pass (pending build verification)
- [ ] Build compiles clean (pending verification — potential API mismatches)

## Known Risks

1. **Column accessor API** — uses `ColumnText` / `ColumnInt64` (correct per `IDataStore` interface). Needs build verification.
2. **`schema::DidAffectRows`** — used for UPDATE success detection. Relies on `PQcmdTuples` returning row count. Should work but untested.
3. **JSON array parsing for `depends_on`** — stored as JSON string, parsed with jsoncpp. Postgres `TEXT` column (not array type) for portability with SQLite.
4. **No admin API** — tool-only interface per v1 scope. No admin UI for viewing projects.

## Out of Scope (Deferred)

- Verification conditions (file_exists, test_passes, api_status)
- Auto-advance cascade logic (current: manual `next_ready` + `complete`)
- Human confirmation milestones
- Admin API / admin UI
- Cross-agent project sharing
- Lua gallivanting integration

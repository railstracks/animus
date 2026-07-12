# Ticket 027 — Scheduler

**Priority:** P2 (enables consolidation, gallivanting, and autonomous behavior)
**Status:** CLOSED
**Dependencies:** Ticket 029 (Multi-tenant — agent scoping), Ticket 021 (Tool Calling)
**Blocks:** Ticket 038 (Gallivanting), Ticket 041 (Memory Consolidation)

## Summary

Implement a cron-like scheduler subsystem that fires scheduled events into the existing session routing pipeline. This is the engine underneath consolidation passes (041), gallivanting blocks (038), and any future periodic agent behavior. Agents manage their own schedules via the `schedule` tool.

## Motivation

The kernel needs a way to fire events on a schedule — not just react to external input. Cron is the traditional answer but falls short: Docker containers lack cron/systemd, cron's syntax is rigid, and there's no API for inspection or per-agent limits. A native scheduler that feeds into the existing event pipeline is simpler and more powerful.

## Architecture

### The scheduler as an event source

The scheduler doesn't create sessions — it fires `IncomingEvent` objects into the existing routing pipeline, exactly like IRC does today:

```
Scheduler → IncomingEvent(type="scheduled") → SessionRouter → ChainRunner
```

This reuses the entire existing path: session key derivation, agent assignment, throttle acquisition, chain execution. The scheduler is just one more event source, alongside `irc:` and `webchat:`.

### Session keys

Scheduled events use session keys prefixed with `"scheduled:"`:

```
scheduled:{tag}:{agent_id}
```

This groups related schedules in the same session (e.g., all `"healthcheck"` schedules for an agent) while keeping different tags isolated.

### Schedule Store (SQLite)

```sql
CREATE TABLE schedules (
    id         TEXT PRIMARY KEY,
    agent_id   TEXT NOT NULL,
    tag        TEXT NOT NULL DEFAULT '',
    schedule_type TEXT NOT NULL DEFAULT 'one_shot',  -- 'one_shot' | 'recurring'
    next_fire  TEXT NOT NULL,                          -- ISO-8601 timestamp of next fire
    cron_expr  TEXT,                                   -- NULL for one-shot; cron expression for recurring
    timezone   TEXT NOT NULL DEFAULT 'UTC',            -- IANA timezone for cron evaluation
    message    TEXT NOT NULL,                           -- context delivered when schedule fires
    enabled    INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL,
    last_fire  TEXT,
    fire_count INTEGER NOT NULL DEFAULT 0,
    max_fires  INTEGER,                                 -- NULL = unlimited (one-shots default to 1)
    FOREIGN KEY (agent_id) REFERENCES agents(agent_id)
);

CREATE INDEX idx_schedules_agent ON schedules(agent_id, enabled, next_fire);
CREATE INDEX idx_schedules_tag ON schedules(agent_id, tag);
```

### Scheduler Subsystem

```cpp
class Scheduler {
public:
    // Lifecycle
    void Start();   // begins polling loop
    void Stop();    // graceful shutdown

    // Schedule CRUD
    std::string Create(const ScheduleDescriptor& sched);
    std::optional<ScheduleDescriptor> Get(const std::string& id) const;
    std::vector<ScheduleDescriptor> List(
        const std::string& agentId,
        const std::string& tag = "") const;
    bool Update(const std::string& id, const SchedulePatch& patch);
    bool Cancel(const std::string& id);

    // Configuration
    void SetMaxSchedulesPerAgent(uint32_t limit);  // default 50
    void SetPollIntervalMs(uint32_t ms);           // default 30000 (30s)

    // Event callback — fired when a schedule is due
    using FireCallback = std::function<void(const IncomingEvent& event)>;
    void SetFireCallback(FireCallback cb);

private:
    void PollLoop();
    void FireSchedules(const std::vector<ScheduleDescriptor>& due);
    std::string EvaluateNextFire(const std::string& cronExpr,
                                  const std::string& timezone) const;
};
```

### Time Expression Support

| Format | Example | Use |
|--------|---------|-----|
| ISO timestamp | `2026-05-10T09:00:00` | One-shot absolute time |
| Relative time | `in 30 minutes`, `in 2 hours`, `tomorrow at 9am` | One-shot convenience |
| Cron expression | `0 9 * * 1` | Recurring schedules |

Relative times are parsed at creation time and stored as absolute `next_fire`. Cron expressions are stored verbatim; `next_fire` is pre-computed from the cron expression and timezone after each fire.

### Timezone handling

- Agent config specifies timezone (default: UTC)
- ISO timestamps with explicit offset (`+02:00`) override the agent default
- Cron expressions are evaluated in the agent's timezone
- DST transitions handled by UTC-based scheduling with timezone-aware display

### Firing semantics

When due schedules fire:

1. Polling loop identifies all schedules with `next_fire <= now` and `enabled = 1`
2. For each due schedule, an `IncomingEvent` is constructed:
   ```cpp
   IncomingEvent event;
   event.type = "scheduled";
   event.agent_id = schedule.agent_id;
   event.metadata = {
       {"schedule_id", schedule.id},
       {"tag", schedule.tag},
       {"message", schedule.message}
   };
   ```
3. The `FireCallback` dispatches the event into the existing session routing pipeline
4. For recurring schedules: `next_fire` recomputed from `cron_expr`; `fire_count` incremented
5. For one-shot schedules: marked disabled (`enabled = 0`) and `last_fire` recorded
6. If `max_fires` is set and reached: schedule disabled

### Polling interval

Default 30 seconds. Configurable per-kernel. The tradeoff is between responsiveness and CPU — 30 seconds is reasonable for hourly/daily schedules; sub-minute precision isn't needed for the current use cases (consolidation, gallivanting).

Future optimization: after processing due schedules, sleep until the nearest `next_fire` rather than polling at a fixed interval. Not required for v1.

## Tool: `schedule`

```json
{
    "name": "schedule",
    "description": "Schedule a future action. The system will wake you at the specified time with the given context. Use for reminders, periodic checks, routines, and autonomous workflows. Schedules survive session boundaries.",
    "parameters": {
        "action": {
            "type": "string",
            "enum": ["set", "list", "cancel"],
            "description": "Schedule operation"
        }
    }
}
```

### `set` — Create or update a scheduled action

```json
{
    "when": {
        "type": "string",
        "description": "When to trigger. ISO timestamp (2026-05-10T09:00:00), relative time (in 30 minutes, tomorrow at 9am), or cron expression (0 9 * * 1 for every Monday at 9am). Timezone from agent config."
    },
    "message": {
        "type": "string",
        "description": "Context delivered when the schedule fires. Be specific — this is what future-you will read."
    },
    "tag": {
        "type": "string",
        "description": "Optional tag for grouping (e.g. 'healthcheck', 'consolidation'). Schedules with the same tag for the same agent share a session."
    },
    "repeat": {
        "type": "boolean",
        "description": "Whether this is recurring (default: false). When true, 'when' is interpreted as a cron expression."
    },
    "id": {
        "type": "string",
        "description": "Optional. If provided, updates the existing schedule with this ID."
    }
}
```

### `list` — List scheduled actions

```json
{
    "tag": {
        "type": "string",
        "description": "Filter by tag. Omit for all schedules."
    }
}
```

Returns: `[{ id, tag, next_fire, schedule_type, enabled, fire_count, last_fire, message_preview }]`

### `cancel` — Cancel a scheduled action

```json
{
    "id": {
        "type": "string",
        "description": "Schedule ID to cancel."
    }
}
```

## Integration Points

### Consolidation (Ticket 041)
Intake schedules: `schedule set "0 */5 * * *" "Run memory intake for agent" --tag consolidation`. Layer consolidation schedules: one per layer, on the layer's `consolidation_interval`. All use `tag: "consolidation"`. The consolidation handler receives the event and runs the appropriate process.

### Gallivanting (Ticket 038)
Gallivanting blocks: `schedule set "0 10,14,18 * * *" "Gallivanting block" --tag gallivanting --repeat`. The gallivanting handler receives the event, checks per-agent config (enabled, duration, boundaries), and dispatches a self-directed chain.

### User reminders
Direct agent use: `schedule set "tomorrow at 9am" "Check if the API key rotation happened"`. Simple reminders that fire a session with context.

## Acceptance Criteria

- [ ] SQLite `schedules` table created with indexes
- [ ] `Scheduler` class with Start/Stop/Create/List/Cancel
- [ ] Polling loop identifies due schedules and fires `IncomingEvent` objects
- [ ] Fired events routed through existing session pipeline (SessionRouter → ChainRunner)
- [ ] Session key format: `scheduled:{tag}:{agent_id}`
- [ ] `schedule set` creates one-shot schedules with ISO timestamps or relative times
- [ ] `schedule set` creates recurring schedules with cron expressions
- [ ] `schedule list` shows active schedules, filterable by tag
- [ ] `schedule cancel` removes a schedule
- [ ] Per-agent schedule limit enforced (default 50)
- [ ] Recurring schedules recompute `next_fire` after firing
- [ ] One-shot schedules auto-disable after firing
- [ ] `max_fires` enforcement for capped recurring schedules
- [ ] Timezone support from agent config; ISO timestamps respect explicit offsets
- [ ] Relative time parsing (`"in 30 minutes"`, `"tomorrow at 9am"` → absolute timestamp)
- [ ] `schedule` tool registered in ToolRegistry
- [ ] Schedule store persists across kernel restarts
- [ ] Tests: one-shot fire, recurring fire, relative parsing, cron evaluation, limit enforcement, cancel

## Notes

- The scheduler is the engine for all periodic kernel behavior. Without it, agents can only react; with it, they can plan, maintain routines, and drive background processes like consolidation.
- Building it as a generic event source (not session-aware) keeps the scheduler simple and the routing centralized. Adding a new event source (`scheduled:`) is trivial when the pipeline already handles `irc:` and `webchat:`.
- Cron expression parsing: use a lightweight library (ccronexpr or similar) rather than reimplementing. ~200 lines of C.
- Future: `observe` (resource watching) can be built on top of the scheduler — watchers create periodic check schedules that evaluate conditions before firing.
- Future: after processing due schedules, sleep until the nearest `next_fire` rather than polling at a fixed interval.

# Ticket 029 — Multi-Tenant Architecture

**Priority:** P1 (foundation for per-agent web_search, channel routing, memory scoping, tool availability)
**Status:** Complete (except session-agent routing, which depends on Ticket 025)
**Dependencies:** Ticket 003 (Session Management), Ticket 004 (Memory Layer API), Ticket 021 (Tool Calling)
**Updated:** 2026-05-03

## Summary

Introduce a multi-agent data model where each agent is a first-class entity with its own configuration, sessions, memory, tool access, and channel bindings. This replaces the current singleton agent model and enables multiple agents to coexist on a single Animus substrate.

## Motivation

Currently Animus runs a single global agent. Every session, memory observation, and tool registration is shared across one identity. This creates several problems:

- **`web_search`** needs per-agent provider configuration — the agent shouldn't pick a search provider, it should just call `web_search`
- **Channels** route inbound messages to specific agents — a Slack message arrives for Agent A, not "the agent"
- **Memory** observations belong to an agent's experience, not a global pool — especially after demotion to the archive
- **Tool access** varies per agent — not every agent needs `shell_exec`, and you don't want a customer-facing agent with `file` write access
- **Session isolation** — agents should not see each other's conversations

Multi-tenancy is the architectural prerequisite that makes all of these work correctly.

## Agent Data Model

### Agent Entity

```cpp
struct Agent {
    std::string id;              // UUID
    std::string name;            // Display name (e.g. "Kestrel", "Sable")
    std::string description;     // What this agent does
    std::string system_prompt;  // Agent's system prompt
    std::string avatar;          // URL or identifier for avatar

    // Model defaults
    std::string default_provider;  // e.g. "openai", "ollama"
    std::string default_model;    // e.g. "gpt-4.1-mini", "gemma4:31b-cloud"
    double temperature{0.7};

    // Reasoning
    bool reasoning_enabled{false};
    std::string reasoning_effort{"high"};

    // Budgets
    AgentBudgetConfig budget;      // maxChainSteps, maxToolCallsPerChain, etc.

    // Tool access
    std::vector<std::string> enabled_tools;  // Whitelist of tool names this agent can use.
                                              // Empty = all registered tools.

    // Channel bindings
    std::vector<std::string> bound_channels;  // Channel IDs this agent handles inbound from.

    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
};
```

### Persistence

Agents are stored in SQLite alongside sessions and memory:

```sql
CREATE TABLE agents (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    system_prompt TEXT NOT NULL DEFAULT 'You are Animus.',
    avatar TEXT NOT NULL DEFAULT '',
    default_provider TEXT NOT NULL DEFAULT 'openai',
    default_model TEXT NOT NULL DEFAULT 'gpt-4.1-mini',
    temperature REAL NOT NULL DEFAULT 0.7,
    reasoning_enabled INTEGER NOT NULL DEFAULT 0,
    reasoning_effort TEXT NOT NULL DEFAULT 'high',
    max_chain_steps INTEGER NOT NULL DEFAULT 20,
    max_tool_calls_per_chain INTEGER NOT NULL DEFAULT 10,
    timeout_seconds INTEGER NOT NULL DEFAULT 1800,
    token_budget_per_prompt INTEGER NOT NULL DEFAULT 200000,
    enabled_tools TEXT NOT NULL DEFAULT '[]',   -- JSON array of tool names
    bound_channels TEXT NOT NULL DEFAULT '[]',  -- JSON array of channel IDs
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
```

### Default Agent

On first startup (or migration from single-agent), a "default" agent is created with the current `AgentRuntimeConfig` values. Existing sessions and observations are assigned to this agent. This preserves backward compatibility.

## Session Parenting

Every session belongs to an agent. The `agent_id` is a foreign key:

```sql
ALTER TABLE sessions ADD COLUMN agent_id TEXT NOT NULL DEFAULT 'default';
```

### Session Resolution Changes

`SessionManager::Resolve` currently routes based on conversation/event metadata. It needs to:

1. Determine which agent an incoming event targets (from channel binding or explicit routing)
2. Filter session lookup by `agent_id`
3. Create new sessions with the correct `agent_id`

### Inbound Routing

When a message arrives on a channel:
1. Look up which agents are bound to that channel
2. If exactly one agent, route to it
3. If multiple, route based on routing rules (future: per-user, per-channel-pattern)
4. If none, route to the default agent

This is the inbound side of the channel architecture (Ticket 025).

## Memory Scoping

Memory observations belong to an agent, not to a layer or to the global system.

### Observation Agent ID

```sql
ALTER TABLE observations ADD COLUMN agent_id TEXT NOT NULL DEFAULT 'default';
```

The `agent_id` is a **first-class property on the observation itself**, not inferred from the layer. This is critical because:

- Observations can be demoted from active layers into the memory archive
- Once demoted, the observation no longer belongs to any active layer
- Without `agent_id` on the observation, a demoted observation loses its agent scope
- The archive is a flat pool — agent_id is the only way to filter it correctly

### MemoryStore Changes

All `MemoryStore` operations accept an `agent_id` filter:

- `Store(agent_id, observation)` — create with agent ownership
- `Recall(agent_id, query, ...)` — search only this agent's observations
- `Search(agent_id, query, ...)` — search only this agent's observations
- `Inspect(agent_id, layer)` — view layer state for this agent
- `Pin(agent_id, observation_id)` — pin for this agent's intake
- `Consolidate(agent_id)` — intake/demotion scoped to this agent

Layers remain global (same structure, same retention policies) but the observations *within* layers are partitioned by agent.

### Archive Behavior

When observations are demoted to the archive:
- The `agent_id` is preserved on the observation record
- The archive can be queried per-agent: "show me all archived observations for Agent X"
- An agent's `recall` never returns another agent's archived observations

## Tool Availability

Each agent has an `enabled_tools` list. When the `ChainRunner` builds tool definitions for an LLM request, it filters by the current agent's whitelist:

```cpp
std::vector<ToolDefinition> ChainRunner::GetToolDefinitionsForAgent(const Agent& agent) const {
    auto allDefs = m_tools.GetAllDefinitions();
    if (agent.enabled_tools.empty()) {
        return allDefs;  // Empty whitelist = all tools
    }
    // Filter: only include tools in the whitelist
    std::vector<ToolDefinition> filtered;
    for (const auto& def : allDefs) {
        if (std::find(agent.enabled_tools.begin(), agent.enabled_tools.end(), def.name)
            != agent.enabled_tools.end()) {
            filtered.push_back(def);
        }
    }
    return filtered;
}
```

This means a customer-facing agent can be restricted to `web_search` + `web_fetch` + `reply`, while a dev agent gets the full suite including `shell_exec` and `file`.

## Admin API

### CRUD Endpoints

```
GET    /api/v1/agents                  — List all agents
POST   /api/v1/agents                  — Create a new agent
GET    /api/v1/agents/{id}             — Get agent details
PATCH  /api/v1/agents/{id}             — Update agent config
DELETE /api/v1/agents/{id}             — Delete an agent (with safeguards)
```

### Agent Detail Response

```json
{
    "id": "a1b2c3d4",
    "name": "Kestrel",
    "description": "Development assistant and collaborator",
    "system_prompt": "You are Kestrel...",
    "avatar": "",
    "default_provider": "zai",
    "default_model": "glm-5.1",
    "temperature": 0.7,
    "reasoning_enabled": true,
    "reasoning_effort": "high",
    "budget": {
        "max_chain_steps": 20,
        "max_tool_calls_per_chain": 10,
        "timeout_seconds": 1800,
        "token_budget_per_prompt": 200000
    },
    "enabled_tools": [],
    "bound_channels": ["slack:D0AECLN2HL5"],
    "created_at": "2026-05-03T18:00:00Z",
    "updated_at": "2026-05-03T18:00:00Z"
}
```

### Safeguards

- The default agent cannot be deleted (it's the fallback)
- Deleting an agent requires confirming session/memory reassignment or deletion
- Agent IDs are immutable once created

## Admin UI

### Agent List Page

- Table view: name, provider/model, status, channel count, session count
- "Create Agent" button
- Click agent row → edit form

### Agent Edit Form

- **Identity section:** name, description, system prompt, avatar
- **Model section:** provider dropdown (from registered providers), model combobox (fetched from provider), temperature slider, reasoning toggle + effort
- **Budget section:** max chain steps, max tool calls, timeout, token budget
- **Tools section:** checkbox grid of registered tools — enable/disable per agent
- **Channels section:** list of bound channels with add/remove
- **Save & Continue** (persists without closing) + **Save & Close**

### Layout

Accessible from a new "Agents" nav item in the admin sidebar. This is a peer to the existing "Chat" and "Providers" views.

## Architecture Changes

### AgentKernel

`AgentKernel` currently holds a single `AgentRuntimeConfig`. It needs to:

- Own an `AgentRegistry` (or similar) that stores `Agent` entities
- Load agents from SQLite on startup
- Provide agent lookup for the `ChainRunner`, `SessionManager`, and `MemoryStore`
- The current `KernelConfig::AgentRuntimeConfig` becomes the config for the default agent

### ChainRunner

`ChainRunner` currently receives provider/model/budget config at construction time. It needs to:

- Accept an `agent_id` on each `Execute`/`ExecuteStreaming` call
- Look up the agent's provider, model, reasoning, budget, and tool whitelist at chain start
- Use `GetToolDefinitionsForAgent()` instead of `GetToolDefinitionsForRequest()`

### Migration

1. Create `agents` table in SQLite
2. Insert default agent from current `AgentRuntimeConfig`
3. Add `agent_id` column to `sessions` table, default 'default'
4. Add `agent_id` column to `observations` table, default 'default'
5. Update `AdminServer` with agent CRUD endpoints
6. Update `ChainRunner` with per-agent tool filtering
7. Update `SessionManager` with agent-scoped session resolution
8. Update `MemoryStore` with agent-scoped operations
9. Add Admin UI agents page

## Acceptance Criteria

- [ ] `agents` table created in SQLite with schema above
- [ ] Default agent created on first startup from current KernelConfig
- [ ] Agent CRUD endpoints: GET list, POST create, GET by id, PATCH update, DELETE
- [ ] Agent cannot be deleted if it has active sessions (or: reassignment required)
- [ ] Default agent cannot be deleted
- [ ] `sessions` table has `agent_id` column, existing sessions assigned to default agent
- [ ] Session resolution routes inbound events to the correct agent
- [ ] `observations` table has `agent_id` column, existing observations assigned to default agent
- [ ] Demoted/archived observations retain their `agent_id`
- [ ] MemoryStore recall/search/inspect operations are scoped by `agent_id`
- [ ] ChainRunner uses per-agent provider/model/reasoning/budget config
- [ ] ChainRunner filters tool definitions by agent's `enabled_tools` whitelist
- [ ] Empty `enabled_tools` list means all registered tools (backwards compat)
- [x] Admin UI: Agent list page with create/edit/delete
- [x] Admin UI: Agent edit form with identity, model, budget, tools, channels sections
- [x] Admin UI: Tool checkbox grid for per-agent tool enablement
- [x] All existing tests pass after migration

## Notes

- This is the architectural prerequisite for: `web_search` (per-agent search provider), channel routing (Ticket 025), email (per-agent sender identity), scheduler (per-agent schedules).
- Memory scoping is the most subtle piece. Observations carry `agent_id` as a first-class property so that demotion to the archive doesn't lose agent scope. Layers are shared structure; observations are partitioned data.
- The current `KernelConfig::AgentRuntimeConfig` is not deleted — it becomes the initial config for the default agent. New agents are created via the API, not via config file.
- Tool whitelisting is opt-in per agent. An empty whitelist means "all tools available" for backwards compatibility. Restricting tools is the intentional act.
- Cross-agent communication (agents talking to each other) and agent spawning/replication are explicitly out of scope — those are Five Points #2 and #3.
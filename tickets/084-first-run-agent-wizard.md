# Ticket 084: First-Run Agent Wizard + Remove "default" Sentinel

**Priority:** High  
**Status:** Draft  
**Depends on:** 083 (per-agent memory layers)  
**Blocks:** Multi-tenant correctness for all subsequent tickets

## Problem

The string `"default"` is used simultaneously as:
1. A config fallback (`GetString(cfg, "agent_id", "default")`)
2. A database sentinel (column defaults, migration backfills)
3. A layer template owner (7 seed layers created with `agent_id = 'default'`)
4. A session tag (all sessions without explicit agent config)

This causes agent data to bleed across boundaries. Adam's sessions consolidated into "default"-owned layers because nothing explicitly tied them to `adam`. The "default agent" row in `agents` is an auto-created ghost — not a real agent anyone configured.

## Solution

Remove the "default" concept entirely. At first run, no agents exist. A wizard guides creation. The 7-layer template is a code function, not database rows belonging to a phantom agent.

## Architecture

### Current flow (broken)
```
Boot → EnsureDefaultAgent() → creates ghost agent
Boot → SeedDefaultLayers() → 7 layers owned by "default"
Channel → agent_id = config["agent_id"] ?? "default"
Sessions → stored with agent_id = "default"
Consolidation → queries "default" → picks up everyone's data
```

### New flow (correct)
```
Boot → empty agents table → API signals wizard_required
Wizard → POST /api/v1/agents → creates real agent + 7 layers
Channel → agent_id = required from config → error if missing
Sessions → stored with real agent_id
Consolidation → scoped to actual agent → no data leakage
```

## Changes

### Backend (C++)

**AgentStore (`src/kernel/agent/AgentStore.cpp`)**
- Remove `EnsureDefaultAgent()` — no auto-created agent
- Remove `GetDefault()` — `Get("default")` no longer aliases anything
- Remove `is_default` column from schema and all queries
- Remove `is_default` field from `Agent` struct
- Remove migration that sets `is_default = 1 WHERE agent_id = 'default'`

**MemoryStore (`src/kernel/memory/MemoryStore.cpp`)**
- Rename `SeedDefaultLayers()` → `CreateDefaultLayersForAgent(const std::string& agent_id)`
- Remove boot-time call. Called only from `AgentManager::CreateAgent()`
- `CreateLayer()` no longer defaults `agent_id` to `"default"` — requires explicit value

**AgentManager (`src/kernel/admin/AgentManager.cpp`)**
- `CreateAgent()`: use hardcoded `LayerDef[]` template (same as current `SeedDefaultLayers`) instead of copying from a "default" agent's layers
- Copy-on-create logic now copies from the hardcoded template, not from DB rows

**AgentKernel (`src/kernel/AgentKernel.cpp`)**
- Remove boot-time `EnsureDefaultAgent()` call
- Remove boot-time `SeedDefaultLayers()` call
- Add `HasAnyAgent()` check exposed via admin server

**ChannelManager (`src/kernel/ChannelManager.cpp`)**
- Remove `GetString(cfg, "agent_id", "default")` fallback
- `agent_id` must come from channel config. If missing, log error and skip channel startup
- Remove `agentId == "default"` normalization in dispatch (line 1144)

**SocialEventPoller (`src/kernel/social/SocialEventPoller.cpp`)**
- Remove all hardcoded `"default"` agent IDs
- Agent ID must come from channel/adapter config

**All tools (Lua, Sessions, Diary, Memory, Consolidation, Schedule, Gallivanting, Channels)**
- Remove `"default"` fallback from `GetStringField(args, "__agent_id", "default")`
- Agent ID comes from session context (`__agent_id`). If empty, that's a runtime error.

**ConsolidationPipeline**
- Remove `toCheck = {"default"}` in `GetKnownAgents()`
- Only known agents from `AgentStore` + agents seen in data

**AdminServer routes**
- New endpoint: `GET /api/v1/system/first-run` → `{ wizard_required: bool }`
- Returns `true` if agents table is empty

**Database schema**
- Remove `is_default` from `agents` table schema
- Change column defaults from `'default'` to `''` (empty string) for `agent_id` columns
- Existing data: require migration path (see below)

### Frontend (Vue)

**New: `WizardView.vue`**
- 3-step form:
  1. **Identity** — Name, description, identity/personality (textarea, with guidance text about what to include)
  2. **Model** — Provider selection, model selection, temperature slider
  3. **Review** — Summary, create button
- On creation success: redirect to main dashboard

**App.vue or router**
- Route guard: on app mount, check `GET /api/v1/system/first-run`
- If `wizard_required`, redirect all routes to `/wizard`
- After wizard completion, redirect to `/` (agents view or dashboard)

**AgentsView.vue**
- "New Agent" button reuses same wizard component (or opens it as a dialog)

### Database Reset

For current development instance:
- Delete `state/animus.db` and `state/memory.db`
- Restart daemon → empty tables, wizard_required = true
- Walk through wizard → agent created with 7 layers

This is the cleanest path for testing. Production migration would need:
- Backfill all `"default"` agent_id values to the first registered agent's ID
- Drop `is_default` column
- But for now, development reset is sufficient

## Files Changed

### C++
- `include/animus_kernel/Agent.h` — remove `is_default` field
- `include/animus_kernel/AgentStore.h` — remove `EnsureDefaultAgent()`, `GetDefault()`, `is_default`
- `include/animus_kernel/MemoryStore.h` — rename `SeedDefaultLayers()` → `CreateDefaultLayersForAgent(agent_id)`
- `src/kernel/agent/AgentStore.cpp` — remove default agent logic
- `src/kernel/memory/MemoryStore.cpp` — refactor seeding, remove "default" fallback
- `src/kernel/admin/AgentManager.cpp` — template-based layer creation
- `src/kernel/AgentKernel.cpp` — remove boot-time seeding/default agent
- `src/kernel/ChannelManager.cpp` — require agent_id from config
- `src/kernel/social/SocialEventPoller.cpp` — remove hardcoded "default"
- `src/kernel/tools/*.cpp` — remove "default" fallbacks (~8 files)
- `src/kernel/consolidation/ConsolidationPipeline.cpp` — remove "default" from GetKnownAgents
- `src/kernel/admin/AdminServer.cpp` — add first-run endpoint

### Vue
- New: `src/views/WizardView.vue`
- Modified: `src/App.vue` or router — wizard guard
- Modified: `src/views/AgentsView.vue` — optional: reuse wizard for new agent

## Testing

1. Fresh database → daemon starts, no agents, no layers
2. Admin UI → redirects to wizard
3. Create agent "Test" → 7 layers created with `agent_id = "test"`
4. Configure channel with `agent_id: "test"` → sessions stored correctly
5. Consolidation runs → observations owned by "test", no data leakage
6. Create second agent → gets its own 7 layers, no cross-contamination
7. Delete agent → layers cascade-deleted

## Open Questions

- **Channel config without agent_id:** Error silently? Log and disable? Show in admin UI?
- **Wizard expansion:** Future steps for CHARTER.md assembly, memory preferences, channel setup?
- **Template customization:** Allow users to edit the 7-layer template before creation?

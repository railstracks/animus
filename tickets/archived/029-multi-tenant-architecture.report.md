# Ticket 029 — Progress Report: Multi-Tenant Architecture

**Date:** 2026-05-05  
**Status:** Partially Complete

---

## Summary

Ticket 029 has substantial foundational implementation in place:

1. Agent entity persistence and CRUD APIs exist.
2. Sessions carry `agent_id` and chat/session creation can bind to a selected agent.
3. ChainRunner resolves per-agent runtime settings and tool whitelists.
4. Observation schema includes `agent_id` with agent-scoped memory query methods.
5. Admin UI includes an Agents management page and editor.

Remaining work is concentrated in inbound channel-based routing and full runtime memory scoping integration.

---

## Implemented Scope

### 1. Agent data model + persistence

- SQLite-backed `AgentStore` implemented.
- Default agent bootstrap (`default`) is created from runtime config when missing.
- Agent model includes:
  - identity fields
  - provider/model defaults
  - reasoning/budget settings
  - `enabled_tools`
  - `bound_channels`
  - persisted timestamps

**Files:**
- `include/animus_kernel/AgentStore.h`
- `src/kernel/agent/AgentStore.cpp`
- `src/kernel/AgentKernel.cpp`

### 2. Agent CRUD API

- Endpoints implemented:
  - `GET /api/v1/agents`
  - `POST /api/v1/agents`
  - `GET /api/v1/agents/{id}`
  - `PATCH /api/v1/agents/{id}`
  - `DELETE /api/v1/agents/{id}`
- Safeguards implemented:
  - default agent cannot be deleted
  - active-session guard via `HasActiveSessions(...)`

**File:**
- `src/kernel/admin/AdminServer.cpp`

### 3. Session ownership

- Sessions have `agent_id` support in persistence/migration.
- New session creation through chat WebSocket can set `agent_id`.

**Files:**
- `src/kernel/session/SqliteSessionStore.cpp`
- `src/kernel/admin/AdminServer.cpp`
- `src/kernel/session/Session.cpp`

### 4. ChainRunner per-agent behavior

- Per-agent runtime settings are loaded from `AgentStore` at execution:
  - provider/config/model
  - system prompt
  - reasoning mode/effort
  - chain/tool budgets
- Tool definitions are filtered by agent `enabled_tools` whitelist.
- Empty whitelist preserves backwards-compatible “all tools”.

**Files:**
- `include/animus_kernel/ChainRunner.h`
- `src/kernel/chain/ChainRunner.cpp`

### 5. Memory schema + scoped methods

- `observations` include `agent_id` in memory schema.
- Agent-scoped methods exist:
  - list by agent/layer
  - create for agent
  - search for agent

**Files:**
- `include/animus_kernel/MemoryStore.h`
- `src/kernel/memory/MemoryStore.cpp`

### 6. Admin UI

- Agents nav/view exists.
- Agent list/create/edit/delete UI implemented.
- Model/reasoning/budget/tools/channels sections implemented.

**Files:**
- `admin-ui/src/views/AgentsView.vue`
- `admin-ui/src/router/index.ts`
- `admin-ui/src/components/AppSidebar.vue`
- `admin-ui/src/i18n/locales/en.ts`

---

## Outstanding / Partial Areas

### 1. Inbound routing by `bound_channels`

- Ticket 029 expects inbound event-to-agent selection via channel bindings.
- Current default session router derives conversation keys from metadata but does not resolve agent via `bound_channels`.
- No full channel-to-agent resolution engine is wired for connector ingress yet.

### 2. Session resolution filtering by `agent_id` for inbound routing rules

- Session ownership exists, but `SessionManager::Resolve` itself does not yet enforce agent-based routing selection from channel bindings/rules described in the ticket.

### 3. Full runtime memory scoping integration

- MemoryStore supports agent-scoped methods and schema.
- Broader memory pipeline/recall/consolidation flows described in ticket are not fully integrated end-to-end under agent scope in current runtime paths.

### 4. Acceptance criteria mismatch on ID format

- Ticket spec calls for UUID-style IDs.
- Implementation now uses server-generated MD5 fingerprint IDs (by design update from later ticket work).

---

## Acceptance Criteria Mapping (Condensed)

- `agents` table + default agent bootstrap: **Implemented**
- Agent CRUD endpoints: **Implemented**
- Delete safeguards (default + active sessions): **Implemented**
- `sessions.agent_id` migration/persistence: **Implemented**
- Session resolution inbound routing by bound channel: **Not Fully Implemented**
- `observations.agent_id` migration/persistence: **Implemented**
- MemoryStore agent-scoped operations: **Implemented (API level)**
- ChainRunner per-agent config + tool whitelist filtering: **Implemented**
- Empty whitelist => all tools: **Implemented**
- Admin UI agents list/edit/tool/channel controls: **Implemented**
- “All existing tests pass” (as global statement): **Not revalidated as part of this report**

---

## Notes

Ticket 029 established the critical multi-tenant substrate and enabled downstream tickets (agent form refinements, per-agent File tool config, chat agent selection).  
The main remaining architectural gap is connector-side inbound routing semantics using `bound_channels`, which aligns naturally with ongoing channel/interface work.


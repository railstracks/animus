# Ticket 033 — Completion Report: File Tool Agent Config Contract

**Date:** 2026-05-05  
**Status:** Complete

---

## Summary

Ticket 033 has been implemented end-to-end.

Delivered:

1. Tool config contract metadata support in tool definitions.
2. File tool config contract exposure via Tools API.
3. Per-agent persisted `tool_configs` storage and migration.
4. Agent CRUD/API support for `tool_configs`.
5. Runtime application of agent File policy in chain tool calls.
6. Agent form UI controls for File tool policy.

---

## Implemented Changes

### 1. Tool definition contract extensions

- `ToolDefinition` now supports `config_parameters`.
- `ToolParameter` now supports optional `default_value_json`.

**File:**
- `include/animus_kernel/tools/ToolTypes.h`

### 2. File tool contract + runtime policy merge

- File tool now advertises config parameters:
  - `restrict_to_workspace`
  - `workspace_root`
  - `path_allowlist`
  - `path_denylist`
- File tool execution supports injected per-agent policy via internal `__policy`.
- Merge behavior enforces conservative policy:
  - denylists are additive
  - allowlists require match when present
  - workspace override outside global boundary is rejected when global boundary is active
- Updated default root behavior:
  - empty workspace root means no implicit cwd boundary.

**Files:**
- `include/animus_kernel/tools/FileTool.h`
- `src/kernel/tools/FileTool.cpp`

### 3. Agent persistence (`tool_configs`)

- Added `tool_configs_json` to `Agent`.
- Added `tool_configs` column migration in `agents` table (default `{}`).
- Wired `List/Get/Create/Update` SQL and mapping.

**Files:**
- `include/animus_kernel/AgentStore.h`
- `src/kernel/agent/AgentStore.cpp`

### 4. Agent CRUD/API support

- Agent JSON responses now include `tool_configs`.
- Agent PATCH/POST accepts `tool_configs`.
- Added validation for `tool_configs.file` shape and field types.

**File:**
- `src/kernel/admin/AdminServer.cpp`

### 5. Tools API contract exposure

- `/api/v1/tools` now serializes both:
  - `parameters`
  - `config_parameters`
- Supports serialization of defaults/enums/nested properties where present.

**File:**
- `src/kernel/admin/AdminServer.cpp`

### 6. Runtime agent policy injection in ChainRunner

- Before executing `file` tool calls, `ChainRunner` injects the agent’s `tool_configs.file` into call args.
- Keeps File policy enforcement server-side and agent-specific.

**File:**
- `src/kernel/chain/ChainRunner.cpp`

### 7. Agent UI controls

- Agent form now carries `tool_configs`.
- When `file` tool is enabled, UI shows:
  - restrict toggle
  - workspace root
  - allowlist patterns
  - denylist patterns
- English locale strings added for File policy form labels/hints.

**Files:**
- `admin-ui/src/views/AgentsView.vue`
- `admin-ui/src/i18n/locales/en.ts`

---

## Validation Evidence

- Backend build: `cmake --build build` ✅
- Admin UI build: `cd admin-ui && npm run build` ✅

---

## Acceptance Criteria Review

- Tools API includes File config contract/defaults: ✅
- Agent payload supports persisted `tool_configs`: ✅
- Agent create/update accepts and validates `tool_configs.file`: ✅
- File policy applies per agent at runtime: ✅
- Agent form exposes File tool config controls: ✅
- Build passes for backend and admin UI: ✅

---

## Follow-up Recommendation

Add server-side save-time validation against current global root boundary so invalid `workspace_root` overrides are rejected at write time (not only at tool-call time), reducing operator confusion.


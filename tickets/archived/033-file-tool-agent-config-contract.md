# Ticket 033 — File Tool Per-Agent Config Contract and UI

**Priority:** P1 (security + operability)  
**Status:** Open  
**Dependencies:** Ticket 022 (System and File Tools), Ticket 031 (Agent Form Refinements)  
**Updated:** 2026-05-05

## Summary

Add per-agent configuration for the `file` tool via a tool-defined config contract, surfaced in the Agent form.

Today, File tool boundaries are globally configured and enforced at runtime, but not configurable per agent. This ticket adds:

1. A tool config contract model (starting with File tool).
2. Persistent per-agent tool config storage (`tool_configs` JSON).
3. Admin API support for reading/updating per-agent tool config.
4. Agent UI rendering for tool-specific config fields.

This ticket is intentionally scoped to `file` first. `shell_exec` hardening is deferred.

## Motivation

We now enforce workspace boundary behavior in File tool runtime, but the agent/operator UX cannot configure it per agent.

Practical needs:

1. Different agents should have different file scopes.
2. Operators need explicit control and visibility of boundaries.
3. Policy should be enforced server-side, not prompt-side.
4. Tool config should be extensible to other tools over time.

## Scope

### In Scope

1. Extend tool definition metadata to support a config contract for admin/runtime policy.
2. Add per-agent `tool_configs` storage and API exposure.
3. Implement File tool contract + runtime application.
4. Render File tool config controls in Agent form for enabled tools.
5. Enforce safe precedence rules between global and per-agent File policy.

### Out of Scope

1. `shell_exec` sandboxing or user impersonation/execution identity changes.
2. Cross-platform privilege separation work (Linux/macOS/Windows user model).
3. Full generic form renderer for every possible JSON schema edge case.
4. Provider/tool protocol changes unrelated to File policy.

## Current State (Observed)

1. File tool already supports:
   - `pathAllowlist`
   - `pathDenylist`
   - `restrictToWorkspace`
2. These are configured globally (`KernelConfig::FileConfig`) and enforced in `FileTool::IsPathAllowed`.
3. Agents currently have only `enabled_tools` (boolean inclusion), with no per-tool parameter persistence.

## Design Plan

### Phase 1: Tool Config Contract (Backend Model)

1. Extend `ToolDefinition` to optionally include policy contract fields, e.g.:
   - `config_schema` (JSON object)
   - `config_defaults` (JSON object)
2. Expose these fields via `GET /api/v1/tools`.
3. Start with File tool contract only:
   - `restrict_to_workspace` (boolean)
   - `workspace_root` (string, optional override)
   - `path_allowlist` (string[])
   - `path_denylist` (string[])

### Phase 2: Agent Storage and API

1. Extend `AgentDefinition` with:
   - `tool_configs` (JSON object keyed by tool name)
2. DB migration:
   - add `tool_configs` TEXT column (default `{}`) to `agents`.
3. Update CRUD serialization/parsing in `AgentStore` and Admin API.
4. Validation:
   - if a tool has config supplied but is not enabled, keep data but ignore at runtime.
   - reject malformed config types for known tool contracts.

### Phase 3: Runtime Wiring

1. Before tool execution, apply effective File policy for session agent.
2. Precedence (safe by default):
   - Global config defines hard upper bound.
   - Agent config may only tighten access, not widen beyond global bound.
3. Example policy merge rules:
   - `restrict_to_workspace`: effective = global OR agent (if either true, restrict)
   - workspace root: agent root must be within global root if global restriction is on
   - allowlist/denylist: union denylist; intersect/compose allowlist conservatively

### Phase 4: Agent UI

1. In `AgentsView.vue`, add a per-enabled-tool config section.
2. For File tool, render:
   - Restrict to workspace toggle
   - Workspace root input
   - Allowlist patterns editor
   - Denylist patterns editor
3. Persist via `tool_configs.file`.

## Concrete File Targets

- `include/animus_kernel/tools/ToolTypes.h`
- `src/kernel/tools/FileTool.cpp`
- `include/animus_kernel/AgentStore.h`
- `src/kernel/agent/AgentStore.cpp`
- `src/kernel/admin/AdminServer.cpp`
- `admin-ui/src/views/AgentsView.vue`
- `admin-ui/src/i18n/locales/en.ts`

## Acceptance Criteria

- [ ] `GET /api/v1/tools` includes File tool config contract/defaults.
- [ ] Agent payload supports persisted `tool_configs`.
- [ ] Agent create/update accepts `tool_configs.file` and validates types.
- [ ] File tool effective policy applies per agent at runtime.
- [ ] Agent config cannot broaden access beyond global file policy.
- [ ] Agent form exposes File tool config controls for enabled File tool.
- [ ] Build passes (`cmake --build build` and `admin-ui npm run build`).

## Risks and Mitigations

1. **Risk:** Mis-merged policies accidentally widen file access.
   - **Mitigation:** “tighten-only” merge semantics and explicit validation errors.
2. **Risk:** Contract shape drifts between backend and frontend.
   - **Mitigation:** API-driven contract payload and minimal hardcoded frontend assumptions.
3. **Risk:** Backward compatibility for existing agents.
   - **Mitigation:** default `tool_configs = {}` and non-breaking migration.

## Rollout Notes

1. Land backend storage + API first (no UI dependency).
2. Land runtime policy merge/enforcement next.
3. Land Agent UI controls last.
4. Follow with report file:
   - `tickets/033-file-tool-agent-config-contract.report.md`


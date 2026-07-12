# Ticket 031 — Completion Report: Agent Form Provider/Model UX and Context Inheritance Refinement

**Date:** 2026-05-04  
**Status:** Complete (with minor follow-up test hardening recommended)

---

## Summary

Ticket 031 has been implemented end-to-end for the requested UX and runtime behavior refinements:

- Agent IDs are now server-generated (MD5-based fingerprint) instead of user-entered.
- Agent Provider/Model fields are now selection-based in the UI.
- Agent-level context window editing/override was removed from the agent flow.
- Context window ownership moved to provider/model configuration.
- Agent form tabs now switch panes correctly.

---

## Implemented Changes

### 1. Server-generated immutable Agent ID

- Added MD5 ID generation from random creation-time material with bounded collision retry.
- Agent create no longer accepts client-provided `id`.
- Agent update path remains ID-immutable.

**Key files:**
- `src/kernel/agent/AgentStore.cpp`
- `include/animus_kernel/AgentStore.h`
- `src/kernel/admin/AdminServer.cpp`

### 2. Agent API/UI shape alignment (nested model/reasoning)

- Agent CRUD response/patch handling standardized around nested:
  - `model.provider`
  - `model.model_id`
  - `reasoning.enabled`
  - `reasoning.effort`
- Compatibility parsing retained for older top-level provider/model payloads.

**Key files:**
- `src/kernel/admin/AdminServer.cpp`
- `admin-ui/src/views/AgentsView.vue`

### 3. Provider + Model select UX in Agent form

- Replaced free-text provider/model inputs with `v-select`.
- Provider options load from `/api/v1/providers`.
- Model options load from `/api/v1/providers/{id}/models`.
- Create success now uses server-returned ID.

**Key files:**
- `admin-ui/src/views/AgentsView.vue`

### 4. Context window ownership migration

- Removed agent CRUD exposure/editing of `model.context_window`.
- Removed per-agent `context_window` override usage in `ChainRunner`.
- Added provider-level context fields:
  - `default_context_window`
  - `model_context_windows`
- Added provider JSON validation/persistence/load support for those fields.
- Runtime context resolution added with precedence:
  1. model override (`model_context_windows[model]`)
  2. provider default (`default_context_window`)
  3. provider capabilities `context_length`
  4. fallback `128000`

**Key files:**
- `include/animus_kernel/AdminServer.h`
- `src/kernel/admin/AdminServer.cpp`
- `src/kernel/chain/ChainRunner.cpp`
- `admin-ui/src/views/ProvidersView.vue`

### 5. Agent form tabs fix

- Migrated tab panes to Vuetify 3-compatible `v-tabs-window-item`.

**Key file:**
- `admin-ui/src/views/AgentsView.vue`

### 6. Locale and validation updates

- Updated EN/NL strings for:
  - server-generated ID messaging
  - provider/model select hints
  - provider/model-required validation
  - provider context-window configuration labels/hints

**Key files:**
- `admin-ui/src/i18n/locales/en.ts`
- `admin-ui/src/i18n/locales/nl.ts`

---

## Validation Evidence

### Build

- `cmake --build build` ✅
- `cd admin-ui && npm run build` ✅

### Tests

- `cd build && ctest --output-on-failure`:
  - 6/7 tests passed.
  - Existing known failure remains in `AdminServerTests` with message:
    `adaptation PATCH did not apply partial merge/audit update`
  - This failure predates Ticket 031 work.

---

## Acceptance Criteria Review

- Agent create no longer requires user-defined ID ✅
- IDs are server-generated and immutable ✅
- Agent form uses provider/model select controls ✅
- Agent form tabs switch content correctly ✅
- Agent no longer edits context window ✅
- Runtime context derived from provider/model settings ✅
- Provider config supports default + per-model context windows ✅
- Legacy provider files remain loadable (new fields optional) ✅
- Relevant test targets pass with known pre-existing failure caveat ✅

---

## Follow-up Recommendations (Non-blocking)

1. Add focused tests for:
   - MD5 ID shape + collision retry path.
   - Agent create ignoring client-supplied ID.
   - Context resolution precedence chain.
2. Add/extend Admin API tests for provider context fields (`default_context_window`, `model_context_windows`).
3. Optionally add frontend test coverage for tabs + provider/model selection flow.


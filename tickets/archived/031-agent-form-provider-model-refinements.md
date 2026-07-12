# Ticket 031 — Agent Form Provider/Model UX and Context Inheritance Refinement

**Priority:** P1 (high UX impact + config correctness)
**Status:** Open
**Dependencies:** Ticket 029 (Multi-Tenant Architecture), Ticket 014 (Provider Config Admin UI), Ticket 030 (OpenAI-Codex Responses Provider)
**Updated:** 2026-05-04

## Summary

Refine the agent creation/edit experience and related runtime wiring so agent configuration is safer and less error-prone.

This ticket removes user-entered agent IDs, replaces free-text provider/model fields with guided selects, moves context window ownership from agent to provider/model config, and fixes non-functional tab behavior in the agent form.

## Motivation

Current behavior introduces avoidable errors and inconsistent state:

- Agent IDs are currently user-provided in UI, but should be generated server-side.
- Agent form provider/model inputs are free-text and easy to misconfigure.
- Agent-level `context_window` conflicts with provider/model capability-driven context behavior.
- Agent form tab switching is visually present but not functionally switching pane content.
- Agent UI payload shape is currently inconsistent with the backend's nested agent API contract (`model` + `reasoning` objects).

## Scope

### In Scope

1. Server-generated immutable agent IDs (MD5 hash of random creation-time input).
2. Agent form provider/model select controls backed by provider/model APIs.
3. Removal of agent-level context window editing and runtime override.
4. Provider-level default context window support with model-level override support.
5. Agent form tab rendering fix.
6. Agent UI/API payload alignment to nested backend contract.
7. Test coverage for ID generation, context resolution precedence, and API behavior compatibility.

### Out of Scope

1. Full redesign of Provider configuration UX beyond fields needed for context inheritance.
2. Generic capability normalization across all providers.
3. Migration of unrelated runtime config endpoints (`/api/v1/agent/*`) unless needed for parity.

## Design Plan

### Phase 1: Agent API and Data Model Alignment

1. Align `admin-ui/src/views/AgentsView.vue` to the nested API shape:
   - `model.provider`
   - `model.model_id`
   - `reasoning.enabled`
   - `reasoning.effort`
2. Preserve backward-compatibility tolerance in backend patch parsing where reasonable, but standardize UI writes to nested shape.
3. Keep `id` immutable on update.

### Phase 2: Server-Generated Agent ID

1. Remove client requirement to provide `id` on create.
2. Update create path so server always generates ID for non-default agents:
   - MD5(random_bytes + timestamp + monotonic counter) encoded as 32-char lowercase hex.
3. Add collision handling with bounded retry loop before create fails.
4. Keep explicit `default` ID behavior unchanged for `EnsureDefaultAgent`.
5. Update UI create success handling to use returned ID from create response.

### Phase 3: Agent Form Provider/Model Select UX

1. Replace free-text provider/model fields with `v-select` controls.
2. Populate provider options from `GET /api/v1/providers`.
3. Populate model options from `GET /api/v1/providers/{id}/models` based on selected provider.
4. Handle model-list fallback gracefully:
   - If listing unavailable/empty, allow provider default model prefill and a clear warning state.
5. Preserve existing behavior for editing legacy agents where provider/model may no longer exist.

### Phase 4: Context Window Ownership Migration

1. Remove `context_window` from agent form and agent-facing multi-tenant JSON model payload.
2. Remove per-agent context override usage from `ChainRunner` resolution.
3. Add provider config fields:
   - `default_context_window` (provider-level fallback)
   - `model_context_windows` map (model-level override)
4. Persist/load these fields in provider storage (`config/providers.json`).
5. Runtime resolution order:
   1. `model_context_windows[selected_model]`
   2. `default_context_window`
   3. provider capabilities `context_length` (if available)
   4. kernel fallback default (`128000`)
6. Expose the new context fields in provider CRUD API responses and patch/put handlers.

### Phase 5: Agent Form Tabs Fix

1. Replace legacy tab content components with Vuetify 3-compatible tab window items.
2. Verify each tab displays and updates only its panel content.
3. Add a minimal UI test/regression check where feasible.

### Phase 6: Internationalization and Validation

1. Update locale strings for:
   - auto-generated Agent ID messaging
   - provider/model select hints
   - context ownership moved to provider/model
2. Ensure frontend validation blocks save when provider/model selection is invalid.
3. Ensure backend validation surfaces clear 400 errors for invalid provider/model references.

### Phase 7: Tests

Add/update tests for:

1. **Agent ID behavior**
   - Create ignores supplied non-default ID and returns server-generated MD5 ID.
   - Generated ID format: 32 lowercase hex chars.
   - PATCH cannot mutate ID.
2. **Agent API shape**
   - Agent responses include nested `model` and `reasoning` objects expected by UI.
3. **Context resolution**
   - Runtime uses provider/model-derived context without agent override.
   - Precedence order above is validated.
4. **Provider persistence migration**
   - New provider context fields read/write correctly, older files remain loadable.
5. **UI sanity**
   - Tab switching changes panel content.
   - Provider/model selects load and persist as expected.

## Concrete File Targets

- `admin-ui/src/views/AgentsView.vue`
- `admin-ui/src/i18n/locales/en.ts`
- `admin-ui/src/i18n/locales/nl.ts`
- `include/animus_kernel/AgentStore.h`
- `src/kernel/agent/AgentStore.cpp`
- `include/animus_kernel/AdminServer.h`
- `src/kernel/admin/AdminServer.cpp`
- `src/kernel/chain/ChainRunner.cpp`
- `tests/AdminServerTests.cpp`
- `tests/AgentConfigReloadTests.cpp` (if runtime/config coupling is touched)
- Additional focused tests (new file if needed): `tests/AgentStoreTests.cpp` or equivalent suite extension

## Acceptance Criteria

- [ ] Agent create no longer requires/accepts user-defined non-default ID from UI.
- [ ] New agent IDs are server-generated MD5 hex and immutable.
- [ ] Agent form uses provider/model select controls, not free-text.
- [ ] Agent form tabs switch content correctly.
- [ ] Agent no longer stores or edits a context window value.
- [ ] Runtime context window is derived from provider/model settings with defined precedence.
- [ ] Provider config supports persisted default and per-model context windows.
- [ ] Existing provider config files without new fields remain compatible.
- [ ] Relevant test targets pass under `ctest --output-on-failure`.

## Risks and Mitigations

1. **Risk:** Removing agent-level context window changes behavior for existing agents.
   - **Mitigation:** Introduce deterministic fallback chain and migration notes; default to previous effective behavior where possible.
2. **Risk:** Some providers may not expose model lists or context lengths.
   - **Mitigation:** Keep provider default context field mandatory in UI when model metadata is absent.
3. **Risk:** API contract transition may break current UI state mapping.
   - **Mitigation:** Implement UI mapping changes first and add compatibility parser guards backend-side.

## Rollout Notes

1. Land backend compatibility and data model migration first.
2. Land UI changes (tabs/selects/field removal) second.
3. Run full test suite and manually verify agent create/edit flows.
4. Add `tickets/031-agent-form-provider-model-refinements.report.md` after completion with test evidence and follow-up items.

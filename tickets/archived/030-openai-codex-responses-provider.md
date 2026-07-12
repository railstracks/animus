# Ticket 030 — OpenAI-Codex Responses Provider Migration

**Priority:** P0 (protocol correctness; prevents reliable Codex usage)
**Status:** Open
**Dependencies:** Ticket 013 (LLM Provider Abstraction), Ticket 015 (Concrete LLM Providers), Ticket 021 (Tool Calling and Reasoning)
**Updated:** 2026-05-04

## Summary

Migrate `openai-codex` from inherited OpenAI Chat Completions behavior to native Codex OAuth + Responses transport behavior.

Today, Animus refreshes Codex OAuth tokens correctly, but still sends inference to `/chat/completions` and parses Chat Completions-shaped responses. Codex should instead call `https://chatgpt.com/backend-api/codex/responses` with Responses-style payloads and SSE event handling.

## Motivation

Current behavior is protocol-mismatched:

- `OpenAICodexProvider` extends `OpenAIProvider` and delegates `Complete/StreamComplete`.
- Base endpoint defaults to `.../chat/completions`.
- Parser expects `choices[].message.content` and chat delta SSE.

Investigation and reference implementation indicate Codex requires:

- OAuth token lifecycle on `https://auth.openai.com/oauth/token`
- Responses inference on `https://chatgpt.com/backend-api/codex/responses`
- Responses streaming event model (`response.*` events)

Without migration, Codex behavior is undefined and may fail silently or degrade capability (tools/reasoning/streaming compatibility).

## Scope

### In Scope

1. Replace Codex inference transport from Chat Completions to Responses endpoint.
2. Add Codex-specific request builder for Responses payload.
3. Add Codex-specific response parser (non-streaming + streaming event handling).
4. Preserve existing OAuth refresh lifecycle and token persistence.
5. Add/expand tests for request shape, streaming parse, and token refresh integration.
6. Update provider defaults/config expectations for Codex base URL and model behavior.

### Out of Scope

1. Reworking generic OpenAI provider to Responses API.
2. Full multi-provider shared Responses abstraction (can be follow-up ticket).

## Design Plan

### Phase 1: Provider Structure

1. Refactor `OpenAICodexProvider` to own inference protocol behavior instead of delegating fully to `OpenAIProvider`.
2. Keep OAuth token refresh logic in `OpenAICodexProvider` (`LoadTokens`, `EnsureTokenValid`, `RefreshToken`, `PersistTokens`).
3. Override endpoint behavior for Codex:
   - Canonical base URL: `https://chatgpt.com/backend-api/codex`
   - Request path: `/responses`

### Phase 2: Request Mapping (Responses)

Implement Codex request construction compatible with Animus `LLMRequest`:

1. Map `LLMRequest.messages` into Responses `input[]` entries with typed `content[]`.
2. Map system prompt into `instructions` when appropriate.
3. Include `stream` flag.
4. Map tools (if present) into Responses tool schema.
5. Add conservative compatibility filtering for known Codex-native backend constraints (feature-gated or clearly isolated logic).

### Phase 3: Response Parsing

1. Non-streaming parser:
   - Parse Responses output items and synthesize assistant text.
   - Parse tool calls into Animus `LLMToolCall` structures.
2. Streaming parser:
   - Handle Responses event types (`response.created`, `response.output_item.added`, `response.output_text.delta`, `response.function_call_arguments.delta`, `response.output_item.done`, `response.completed`, etc.).
   - Emit text tokens and tool call completion in a way that remains compatible with current ChainRunner expectations.
3. Ensure final assembled assistant message and finish reason behavior remain deterministic.

### Phase 4: Config & Runtime Integration

1. Ensure Codex provider defaults no longer imply OpenAI `/v1/chat/completions`.
2. Validate required config fields:
   - `auth_file` path
   - Codex base URL override support (defaulting to canonical backend URL)
3. Keep 5-minute pre-expiry refresh threshold and refresh-token rotation persistence.

### Phase 5: Tests

Add or update tests for:

1. Endpoint routing:
   - Codex requests target `/backend-api/codex/responses`.
2. Request shape:
   - Proper `model`, `input`, `stream`, optional `instructions`, optional tools.
3. Streaming parse:
   - Responses delta events produce incremental assistant text.
   - Tool argument deltas accumulate into valid tool calls.
4. OAuth refresh:
   - Refresh before request when near expiry.
   - Persist rotated refresh token and new expiry.
5. Failure behavior:
   - Auth refresh failure surfaces clear error.
   - Malformed/unsupported response events fail predictably.

### Phase 6: OAuth Login UX (Now In Scope)

Implement first-class token acquisition flow for `openai-codex` so users can authenticate without manually editing `auth.json`.

1. Add provider-specific auth initiation path in admin API/runtime:
   - Start login session
   - Return verification/auth URL + state needed by UI
2. Implement UI-based OAuth path (browser-mediated):
   - Launch/open authorize URL
   - Receive callback code/state (localhost callback or equivalent safe handoff)
   - Exchange code for token pair via `https://auth.openai.com/oauth/token`
3. Implement device-code fallback path in UX:
   - Request `device_auth_id` + `user_code`
   - Show verification URL + code in UI
   - Poll until authorized, then exchange and persist tokens
4. Persist credentials into configured `auth_file` in the existing Animus format:
   - `access`, `refresh`, `expires`, `type: oauth`
5. Add explicit error-state UX/messages for:
   - user cancellation
   - timeout
   - token exchange failure
   - missing/invalid callback state

This phase should integrate with the same token lifecycle used by `OpenAICodexProvider` (refresh + rotation).

## Concrete File Targets

- `include/animus_kernel/llm/OpenAICodexProvider.h`
- `src/kernel/llm/OpenAICodexProvider.cpp`
- `src/kernel/llm/LLMProviderBase.cpp` (only if minimal template hooks are needed)
- `src/kernel/llm/OpenAIProvider.cpp` (only if shared helpers are extracted)
- `tests/LLMProviderTests.cpp` and/or new `tests/OpenAICodexProviderTests.cpp`
- `src/kernel/admin/*` (auth start/callback endpoints as needed)
- `admin-ui/*` (provider auth controls and login flow UI)
- auth flow tests (admin/API and UI where present)
- `docs/openai-codex/api-reference.md` (already revised; update only if implementation diverges)

## Acceptance Criteria

- [ ] `openai-codex` no longer uses `/chat/completions` for inference.
- [ ] Codex inference requests go to `/backend-api/codex/responses`.
- [ ] Codex request body is Responses-style and validated by unit tests.
- [ ] Streaming parser handles Responses `response.*` event family for text + tool calls.
- [ ] OAuth refresh lifecycle remains functional and covered by tests.
- [ ] UI-based OAuth login path can acquire and persist Codex credentials end-to-end.
- [ ] Device-code fallback can acquire and persist Codex credentials end-to-end.
- [ ] Existing non-Codex providers remain behaviorally unchanged.
- [ ] `LLMProviderTests` (and any new Codex tests) pass in local `ctest`.

## Risks and Mitigations

1. **Risk:** Responses parsing complexity breaks existing tool-calling flow.
   - **Mitigation:** Add focused fixtures for tool-call delta assembly and finalization.
2. **Risk:** Codex backend rejects certain optional fields.
   - **Mitigation:** Isolate optional params behind Codex-specific compatibility filtering.
3. **Risk:** Shared base-class assumptions are too chat-completions-specific.
   - **Mitigation:** Introduce narrow extension hooks rather than broad refactors.

## Rollout Notes

1. Land provider migration + tests first.
2. Run full test suite and validate no regressions in other providers.
3. After completion, add `tickets/030-openai-codex-responses-provider.report.md` summarizing:
   - Implemented changes
   - Test evidence
   - Any deferred follow-ups

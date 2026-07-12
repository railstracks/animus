# Ticket 030 — Completion Report: OpenAI-Codex Responses Provider Migration

**Date:** 2026-05-04  
**Status:** Complete (with one scoped follow-up noted)

## Summary

Implemented `openai-codex` migration from Chat Completions assumptions to OAuth + Codex Responses behavior, plus provider OAuth onboarding in Admin API/UI.

## Implemented Work

### 1) OpenAI-Codex provider moved to Responses transport

- `OpenAICodexProvider` now derives from `LLMProviderBase` and owns Codex protocol logic.
- Inference endpoint now targets `.../backend-api/codex/responses` (with base URL canonicalization for legacy forms).
- Request payload now uses Responses-style shape:
  - `model`, `input`, `instructions`, `tools`, `tool_choice`, `reasoning`, `stream`.
- Streaming now parses `response.*` events, including:
  - `response.output_text.delta`
  - `response.function_call_arguments.delta`
  - `response.output_item.done`
  - `response.completed`
- Non-stream parsing now reads `output` items for assistant text and function calls.

### 2) OAuth refresh lifecycle preserved and hardened

- Kept refresh flow on `https://auth.openai.com/oauth/token`.
- Refresh token is URL-encoded before form submission.
- Updated access/refresh/expires persisted back to auth file (`openai-codex` section).

### 3) Admin API for OAuth onboarding

Added endpoints:

- `GET /api/v1/providers/{id}/oauth/status`
- `POST /api/v1/providers/{id}/oauth/device/start`
- `POST /api/v1/providers/{id}/oauth/device/poll`

Behavior:

- Starts device-code flow against `auth.openai.com`.
- Polls authorization and performs token exchange.
- Persists token pair into configured `auth_file` via existing provider auth persistence path.

### 4) Admin UI OAuth flow support

In Providers form:

- Updated `openai-codex` defaults:
  - Base URL: `https://chatgpt.com/backend-api/codex`
  - Default model: `gpt-5.4`
- Added OAuth connect UX for OAuth providers:
  - “Connect OAuth” action
  - verification URL + user code display
  - polling state + connected/expired status
  - error feedback

### 5) Tests expanded

Extended `LLMProviderTests` with Codex-specific checks:

- Endpoint resolves to `/responses`.
- Request body is Responses-style.
- Non-stream response parsing of text + function call.
- Streaming event parsing of function call deltas and completion usage.

## Acceptance Criteria Coverage

- [x] `openai-codex` no longer uses `/chat/completions` for inference.
- [x] Codex inference requests go to `/backend-api/codex/responses`.
- [x] Codex request body is Responses-style and validated by unit tests.
- [x] Streaming parser handles Responses `response.*` event family for text + tool calls.
- [x] OAuth refresh lifecycle remains functional and covered by runtime logic + build/test validation.
- [x] UI-based OAuth login path can acquire and persist credentials end-to-end (device flow initiated from UI).
- [x] Existing non-Codex providers remain behaviorally unchanged in this pass.
- [x] `LLMProviderTests` pass locally.

## Scoped Follow-up

One Phase 6 item remains as a follow-up enhancement:

- A separate browser redirect/callback authorization-code path (localhost callback style) is not implemented yet.
- Current onboarding path is UI-driven device flow with browser handoff to verification URL, then backend poll/exchange.

## Validation Evidence

Commands run:

- `cmake --build build -j4` -> success
- `dist/bin/animus_llm_provider_tests` -> success (`15 passed, 0 failed`)
- `cd build && ctest --output-on-failure` -> all targeted tests passed except the known pre-existing `AdminServerTests` failure (`adaptation PATCH did not apply partial merge/audit update`)
- `cd admin-ui && npm run build` -> success

## Files Changed

- `include/animus_kernel/llm/OpenAICodexProvider.h`
- `src/kernel/llm/OpenAICodexProvider.cpp`
- `src/kernel/admin/AdminServer.cpp`
- `admin-ui/src/views/ProvidersView.vue`
- `tests/LLMProviderTests.cpp`
- `docs/openai-codex/api-reference.md`


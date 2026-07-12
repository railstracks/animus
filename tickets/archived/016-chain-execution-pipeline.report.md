# Ticket 016 Report — Chain Execution Pipeline

**Status:** Complete (foundation)
**Date:** 2026-04-29
**Implemented by:** Kestrel

## What was done

### New components

**PromptAssembler** (`include/animus_kernel/PromptAssembler.h`, `src/kernel/chain/PromptAssembler.cpp`):
- Assembles `LLMRequest` from session history + system prompt + user message
- Assembly order: system prompt → compaction summary → history turns → user message
- `TokenEstimator` for rough token budget checks (~4 chars/token, conservative)
- Signals `needs_compaction` when estimated tokens exceed configurable threshold (default 80% of context window)

**ChainRunner** (`include/animus_kernel/ChainRunner.h`, `src/kernel/chain/ChainRunner.cpp`):
- Full inference chain: resolve session → assemble prompt → call LLM → store turns
- `Execute()` — synchronous non-streaming path
- `ExecuteStreaming()` — streaming path with token callback
- `ExecuteOnSession()` / `ExecuteStreamingOnSession()` — lower-level variants for pre-resolved sessions
- Creates provider instances from `LLMProviderRegistry` per request
- Stores both user and assistant turns with timestamps
- `ChainResult` includes response, success flag, error, token usage, elapsed time

### Session changes

**Session.h / Session.cpp:**
- Added `provider_id` field with getter/setter on both `Session` and `SessionAccess`
- Provider stored per-session (not per-request), enabling session-level provider routing
- Default provider set from kernel config for new sessions; overridable per-session

### Kernel integration

**AgentKernel.h / AgentKernel.cpp:**
- Added `LLMProviderRegistry` member — factory for creating provider instances
- Added `ChainRunner` member — the execution pipeline
- All six providers registered at kernel startup: OpenAI, OpenAI-Codex, ZAI, Ollama, Cohere, Mistral
- `ProviderRegistry()` and `Chains()` accessors exposed
- Proper lifecycle: constructed after job system, destroyed before module unload

**CMakeLists.txt:**
- Added chain pipeline sources and all LLM provider implementations to `animus_kernel`
- Added `libcurl` dependency (required by LLM providers for HTTP/SSE)

### Build & test verification
- ✅ Full build passes (animus_kernel + animusd + all tests)
- ✅ 5/6 tests pass (AdminServerTests pre-existing failure unchanged)
- ✅ JobsTests now passes (was previously failing — possibly fixed by incidental changes)
- ✅ libcurl linked correctly

## Design decisions

1. **Provider per-session, not per-request** — The session stores which provider it uses. New sessions get the default from config. This enables future routing: different sessions → different models, configurable per cron job, per agent, per connector.

2. **Provider factory, not singleton** — Each chain execution creates a fresh provider instance from the registry. This avoids shared mutable state between concurrent requests and lets each request have its own config.

3. **PromptAssembler is stateless** — It's a utility that builds a request from session state. No caching, no side effects. Makes it easy to test and compose.

4. **Token estimation is rough** — ~4 chars/token is intentionally conservative. Will be replaced with a real tokenizer when needed. The compaction signal is advisory, not blocking.

## Deferred items

- **Admin chat wiring** — The WebSocket chat endpoint doesn't yet use ChainRunner. It still stores turns manually. Wiring the chat endpoint to use `Chains().ExecuteStreaming()` with token callbacks sent to the WebSocket is the natural next step.
- **Compaction trigger** — `needs_compaction` flag is computed but not acted upon. When set, the system should trigger `ConversationCompactor` before re-assembling. Requires async job dispatch.
- **Provider config → LLMProviderConfig** — ChainRunner currently creates providers with minimal config (just provider_id). It needs to read API keys, base URLs, etc. from the AdminServer's `m_providersByName` state. This bridge is the critical missing piece for end-to-end functionality.
- **Tool calling loop** — Future: LLM requests tool → execute → feed result back → continue. Not in scope for this ticket.
- **Unit tests with mock provider** — Deferred. Need a `MockLLMProvider` that implements `ILLMProvider` for testing without network.

## Acceptance criteria status

- [x] End-to-end pipeline code: user message → session resolution → prompt assembly → LLM call → response stored as turn
- [x] System prompt injected as first message
- [x] Compaction summary included when present
- [x] Token budget check signals compaction need
- [x] Both user and assistant turns stored with timestamps
- [x] Streaming path calls token callback, then stores complete response
- [x] ChainResult includes token usage stats and elapsed time
- [ ] Unit test with mock provider (deferred)
- [x] Error handling: provider unavailable, empty response — all produce ChainResult with error

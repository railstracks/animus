# Ticket 116 — Implementation Report

**Date:** 2026-07-15
**Status:** Implemented, build pending (workstation unavailable)

## Summary

Implemented prompt logging for Animus — capturing LLM token usage data that was previously discarded, and adding a configurable logging system with three levels.

## Changes

### Phase 1: Token Usage Capture (bug fix)

**Problem:** `LLMToken.prompt_tokens` and `LLMToken.completion_tokens` were parsed from SSE streams by all providers (OpenAI, Cohere, Ollama, Codex) but never captured by `LLMProviderBase`. The wrapped streaming callback only saved `finish_reason` and `tool_calls`. The non-streaming `Complete()` path didn't extract usage either. Result: `ChainResult.prompt_tokens` and `ChainResult.completion_tokens` were always 0.

**Fix:**
- Added `m_lastPromptTokens` and `m_lastCompletionTokens` to `LLMProviderBase`
- Streaming: wrapped callback now captures `token.prompt_tokens` / `token.completion_tokens`
- Non-streaming: extracts `usage.prompt_tokens` / `usage.completion_tokens` from response JSON via `ExtractJsonNumber`
- Exposed via `GetLastPromptTokens()` / `GetLastCompletionTokens()`
- `ChainRunner::CallLLM` now populates `LLMResponse.prompt_tokens` / `completion_tokens` from the provider

### Phase 2: PromptLogStore

New `PromptLogStore` class (`include/animus_kernel/PromptLogStore.h`, `src/kernel/PromptLogStore.cpp`):
- `EnsureSchema()`: creates `prompt_logs` table + indexes
- `Log()`: inserts a row at the configured level
- `QueryByAgent()`: returns entries for an agent in a time range
- `GetUsageSummary()`: aggregate token counts + average latency

### Phase 3: Log Levels

```
enum class PromptLogLevel { None, Default, Full };
```

- **None**: no logging, no DB writes
- **Default**: metadata only (tokens, model, provider, latency, chain_step)
- **Full**: metadata + full prompt/response content + tool calls JSON (EU AI Act Article 12)

### Phase 4: ChainRunner Integration

- `ChainRunner::LogPromptCall()` — private helper called after each `CallLLM`
- Per-call latency measured with `std::chrono::steady_clock` around `CallLLM`
- Both streaming and non-streaming paths log
- At Full level: assembles prompt content from request messages, serializes tool calls to JSON

### Phase 5: Config

- `KernelConfig::promptLogLevel` (string, default "default")
- Daemon flag: `--prompt-log-level=none|default|full`
- Wired in `AgentKernel` constructor: creates `PromptLogStore`, sets on `ChainRunner`

## Files Changed (12 files, +602/-7)

| File | Change |
|------|--------|
| `include/animus_kernel/llm/LLMProviderBase.h` | + GetLastPromptTokens/CompletionTokens, m_last* members |
| `src/kernel/llm/LLMProviderBase.cpp` | Token capture in Complete/StreamComplete callbacks |
| `include/animus_kernel/ChainRunner.h` | + SetPromptLogStore, SetPromptLogLevel, LogPromptCall |
| `src/kernel/chain/ChainRunner.cpp` | LogPromptCall impl, timing around CallLLM, response population |
| `include/animus_kernel/PromptLogStore.h` | New: PromptLogLevel enum, PromptLogEntry, PromptLogStore |
| `src/kernel/PromptLogStore.cpp` | New: EnsureSchema, Log, QueryByAgent, GetUsageSummary |
| `include/animus_kernel/KernelConfig.h` | + promptLogLevel config field |
| `include/animus_kernel/AgentKernel.h` | + m_promptLogStore member |
| `src/kernel/AgentKernel.cpp` | PromptLogStore creation + wiring |
| `src/main.cpp` | --prompt-log-level flag parsing |
| `CMakeLists.txt` | + PromptLogStore.cpp |
| `tickets/116-prompt-logging.md` | Ticket |

## Build Status

**Pending.** Workstation reverse tunnel was unresponsive at 01:00 CEST (likely sleeping). Build will be verified when workstation is available.

## API Endpoint (deferred)

The `/api/agents/:id/prompt-logs` endpoint is scoped but not yet implemented. The `PromptLogStore::QueryByAgent()` and `GetUsageSummary()` methods are ready for the admin server to expose. This can be a follow-up commit.

## Schema

```sql
CREATE TABLE IF NOT EXISTS prompt_logs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    agent_id        TEXT NOT NULL DEFAULT '',
    session_id      INTEGER,
    provider        TEXT NOT NULL DEFAULT '',
    model           TEXT NOT NULL DEFAULT '',
    prompt_tokens   INTEGER NOT NULL DEFAULT 0,
    completion_tokens INTEGER NOT NULL DEFAULT 0,
    total_tokens    INTEGER NOT NULL DEFAULT 0,
    latency_ms      INTEGER NOT NULL DEFAULT 0,
    chain_step      INTEGER NOT NULL DEFAULT 0,
    created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    prompt_content  TEXT,
    response_content TEXT,
    tool_calls      TEXT,
    tool_results    TEXT
);
```

With indexes on `(agent_id, created_at)` and `session_id`.

# Ticket 116 — Prompt Logging

**Date:** 2026-07-15
**Priority:** High (benchmarking + EU AI Act compliance)
**Status:** Open

## Problem

Every LLM API response contains token usage data (`prompt_tokens`, `completion_tokens`) that Animus currently discards. The `LLMToken` struct parses these from SSE streams, but `LLMProviderBase` never captures them — the wrapped streaming callback only saves `finish_reason` and `tool_calls`. The non-streaming `Complete()` path doesn't extract usage either.

This means:
1. No token usage data for benchmarking or cost analysis
2. No audit trail of LLM interactions (EU AI Act Article 12 compliance)
3. `ChainResult.prompt_tokens` and `ChainResult.completion_tokens` are always 0

## Proposal

### Phase 1: Token Usage Capture (prerequisite)

Fix the token usage pipeline that's already partially built:

1. Add `m_lastPromptTokens` and `m_lastCompletionTokens` to `LLMProviderBase`
2. In `StreamComplete`'s wrapped callback: capture `token.prompt_tokens` / `token.completion_tokens` from the final token
3. In `Complete`: parse `usage` object from the response body JSON
4. Expose via `GetLastPromptTokens()` / `GetLastCompletionTokens()`
5. Populate `LLMResponse.prompt_tokens` / `completion_tokens` in `ChainRunner::CallLLM`

### Phase 2: Prompt Log Store

New `PromptLogStore` class:

```
CREATE TABLE IF NOT EXISTS prompt_logs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    agent_id        TEXT NOT NULL DEFAULT '',
    session_id      INTEGER,
    provider        TEXT NOT NULL,
    model           TEXT NOT NULL,
    prompt_tokens   INTEGER NOT NULL DEFAULT 0,
    completion_tokens INTEGER NOT NULL DEFAULT 0,
    total_tokens    INTEGER NOT NULL DEFAULT 0,
    latency_ms      INTEGER NOT NULL DEFAULT 0,
    chain_step      INTEGER NOT NULL DEFAULT 0,
    created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),

    -- Only populated at 'full' log level
    prompt_content    TEXT,
    response_content  TEXT,
    tool_calls        TEXT,
    tool_results      TEXT
);
```

### Phase 3: Log Levels

```cpp
enum class PromptLogLevel {
    None,     // No logging
    Default,  // Metadata only: tokens, model, provider, latency, chain_step
    Full      // Metadata + full prompt/response content + tool calls/results
};
```

Config:
- `KernelConfig::promptLogLevel` (string: "none", "default", "full")
- Daemon flag: `--prompt-log-level=default`
- Default: `default` (metadata is cheap, benchmarking needs it)

### Phase 4: ChainRunner Integration

`ChainRunner::CallLLM` logs after each LLM call:
- At Default: agent_id, session_id, provider, model, tokens, latency, chain_step
- At Full: also stores assembled prompt text, response text, tool call array, tool results

The log call happens in `CallLLM` (not in `ExecuteOnSession`/`ExecuteStreamingOnSession`) so it captures every LLM interaction including multi-step chains.

### Phase 5: API Endpoint

`GET /api/agents/:id/prompt-logs?from=&to=&limit=` — returns token usage data for dashboard consumption.

## Acceptance Criteria

- [ ] `LLMProviderBase` captures and exposes token usage (streaming + non-streaming)
- [ ] `ChainResult.prompt_tokens` / `completion_tokens` populated correctly
- [ ] `prompt_logs` table created by `EnsureSchema()`
- [ ] `PromptLogStore::Log()` writes rows at the configured level
- [ ] `--prompt-log-level` daemon flag parsed
- [ ] Default level: metadata only (no prompt/response content)
- [ ] Full level: stores prompt, response, tool calls, tool results
- [ ] None level: no DB writes, no overhead
- [ ] API endpoint returns paginated token usage data
- [ ] Build clean on workstation

## Complexity

Medium. Token capture fix (~40 lines in LLMProviderBase), PromptLogStore (~120 lines), ChainRunner integration (~30 lines), config + daemon flag (~20 lines), API endpoint (~40 lines).

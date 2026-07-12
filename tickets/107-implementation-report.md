# Ticket 107 — Implementation Report

**Date:** 2026-07-09
**Commit:** `f083491`
**Status:** Implemented, clean build verified

## What Was Built

Alibaba Cloud (DashScope/Qwen) provider as the seventh LLM provider in Animus.

### Files Created
- `include/animus_kernel/llm/AlibabaProvider.h` — header (45 lines)
- `src/kernel/llm/AlibabaProvider.cpp` — implementation (162 lines)

### Files Modified
- `src/kernel/AgentKernel.cpp` — `#include` + `reg.Register("alibaba", ...)`
- `CMakeLists.txt` — added `src/kernel/llm/AlibabaProvider.cpp` to sources
- `admin-ui/src/views/ProvidersView.vue` — added to provider list
- `admin-ui/src/views/WizardView.vue` — added to wizard provider types

## Architecture

`AlibabaProvider` extends `LLMProviderBase` directly (same pattern as `ZaiProvider`). It uses `openai_compat::BuildOpenAIRequestBody()` for request construction and `openai_compat::ParseToolCalls()` / `ExtractFinishReason()` for tool call parsing.

### Request Body
Standard OpenAI format via `BuildOpenAIRequestBody()`. When reasoning is enabled and the model supports thinking, `enable_thinking: true` is injected into the JSON body.

### SSE Streaming
`ParseSSELine()` checks for `reasoning_content` first (thinking tokens, flagged with `is_thinking=true`), then standard `content` and `finish_reason`. Same pattern as Z.ai.

### Tool Calls
Delegates to `openai_compat::ParseToolCalls()` and `ExtractFinishReason()` for both streaming and non-streaming. Standard OpenAI function-calling format.

### Capability Detection

| Capability | Detection Logic |
|-----------|----------------|
| Vision | Model name contains `vl`, or starts with `qwen3.6-plus`, `qwen3.6-flash`, `qwen3.7-plus` |
| Reasoning (thinking) | Model name starts with `qwen3`, or is a stable alias (`qwen-plus`, `qwen-max`, `qwen-turbo`) |
| Tools | Default: true (OpenAI-compatible tool calling) |
| Streaming | Default: true |

## Defaults

- **Base URL:** `https://dashscope-intl.aliyuncs.com/compatible-mode/v1` (international endpoint)
- **Default model:** `qwen-plus`
- **Auth:** Bearer token (API key from Alibaba Cloud Model Studio console)

Users in China should override `base_url` to `https://dashscope.aliyuncs.com/compatible-mode/v1`.

## What's NOT Included (v2)

- Thinking mode is enabled but the `enable_thinking` parameter is not separately configurable per-agent (it's mapped from `reasoning_effort`). A future enhancement could expose it as an explicit agent config parameter.
- Embeddings endpoint (`/v1/embeddings`) — deferred to ticket 070 integration.
- Image generation (Wan2.6) — deferred to ticket 068 v2 (native async API, not OpenAI-compatible).
- STT (Paraformer) / TTS (CosyVoice) — deferred to ticket 108 v2 (native DashScope APIs).
- Model listing via `/v1/models` endpoint — not wired into admin UI model selector (user types model name manually).

## Build Verification

```
[100%] Built target animus_kernel
```

Clean build, no warnings or errors.

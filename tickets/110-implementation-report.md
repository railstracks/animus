# Ticket 110 — Implementation Report

**Date:** 2026-07-09
**Commit:** `47ed4cc`
**Status:** Implemented, clean build verified

## What Was Built

DeepSeek as the ninth dedicated LLM provider in Animus. OpenAI-compatible API with thinking mode support via the `thinking` parameter.

### Files Created
- `include/animus_kernel/llm/DeepSeekProvider.h` — header (54 lines)
- `src/kernel/llm/DeepSeekProvider.cpp` — implementation (123 lines)

### Files Modified
- `src/kernel/AgentKernel.cpp` — `#include` + `reg.Register("deepseek", ...)`
- `CMakeLists.txt` — added `src/kernel/llm/DeepSeekProvider.cpp`
- `admin-ui/src/views/ProvidersView.vue` — added to provider list
- `admin-ui/src/views/WizardView.vue` — added to wizard provider types

## Architecture

`DeepSeekProvider` extends `LLMProviderBase` directly (same pattern as `AlibabaProvider` and `ZaiProvider`). Uses `openai_compat::BuildOpenAIRequestBody()` for request construction and `openai_compat::ParseToolCalls()` / `ExtractFinishReason()` for tool call parsing.

### Request Body
Standard OpenAI format via `BuildOpenAIRequestBody()`. When reasoning is enabled, `thinking: {"type": "enabled"}` is injected into the JSON body. This is the same parameter format as Z.ai.

### SSE Streaming
`ParseSSELine()` checks for `reasoning_content` first (thinking tokens, flagged with `is_thinking=true`), then standard `content` and `finish_reason`. Identical pattern to ZaiProvider and AlibabaProvider — this is the third provider using this SSE pattern.

### Tool Calls
Delegates to `openai_compat::ParseToolCalls()` and `ExtractFinishReason()` for both streaming and non-streaming. Standard OpenAI function-calling format.

### Capability Detection

Both DeepSeek V4 models have identical capabilities:

| Capability | Value | Notes |
|-----------|-------|-------|
| Tools | ✅ | Standard OpenAI function-calling |
| Streaming | ✅ | SSE with reasoning_content |
| Reasoning | ✅ | thinking: {"type": "enabled"} — billed at standard output rate |
| Vision | ❌ | Text-only as of July 2026 |

### Key Difference from Alibaba

DeepSeek's thinking mode is billed at the **standard output rate** — no premium multiplier. Alibaba's `enable_thinking` bills thinking tokens at 3-10x the output rate. This means thinking mode is safe to enable by default for DeepSeek in a way it isn't for Alibaba.

### Key Difference from OpenAI

DeepSeek uses `thinking: {"type": "enabled"}` parameter format (same as Z.ai), not OpenAI's `reasoning_effort` parameter alone. The `reasoning_effort` value is mapped to the `thinking` parameter by the provider.

## Defaults

- **Base URL:** `https://api.deepseek.com/v1`
- **Default model:** `deepseek-v4-flash`
- **Auth:** Bearer token (API key from platform.deepseek.com)

## Pricing Context

| Model | Input (cache miss) | Input (cache hit) | Output |
|-------|-------------------|-------------------|--------|
| `deepseek-v4-flash` | $0.14/1M | $0.0028/1M | $0.28/1M |
| `deepseek-v4-pro` | $0.435/1M | $0.003625/1M | $0.87/1M |

Cache hit input is ~50x cheaper than cache miss. DeepSeek caches automatically — repeated system prompts and tool definitions are nearly free on subsequent calls.

## What's NOT Included (v2)

- Model listing via `/v1/models` endpoint
- Context cache awareness (log cache hit/miss stats)
- FIM completion support (for code editing workflows)
- JSON output mode (`response_format` parameter)

## Build Verification

```
[ 74%] Building CXX object CMakeFiles/animus_kernel.dir/src/kernel/llm/DeepSeekProvider.cpp.o
[ 75%] Linking CXX shared library /home/melvin/projects/animus/dist/libanimus_kernel.so
[100%] Built target animus_kernel
```

Clean build. Both `AgentKernel.cpp` and `DeepSeekProvider.cpp` recompiled. No warnings or errors.

## Note on SSE Pattern Duplication

This is the third provider (after Zai and Alibaba) using the `reasoning_content` SSE extraction pattern. The `ParseSSELine` logic is now triplicated. A future refactor should extract a shared `OpenAICompat::ExtractReasoningContent()` helper to reduce duplication. Not blocking — the pattern is stable and the code is small — but worth tracking.

# Ticket 110 — DeepSeek Provider

**Created:** 2026-07-09
**Status:** Draft
**Priority:** P1
**Depends on:** Provider system (existing), OpenAICompat layer (existing)

## Summary

Add DeepSeek as a dedicated LLM provider. DeepSeek exposes an OpenAI-compatible API at `https://api.deepseek.com` with two models: `deepseek-v4-flash` (cheapest serious model on the market) and `deepseek-v4-pro` (flagship). Both support tool calling, JSON output, thinking mode, and 1M token context.

## Motivation

- **Chinese market capture.** DeepSeek is the second-most-popular Chinese LLM after Qwen. With companion app regulation taking effect July 15, 2026, Chinese users will be looking for alternatives that maintain AI continuity without being companion apps. DeepSeek users are a natural conversion target.
- **Brand recognition.** Explicitly naming DeepSeek in the provider list signals "we support your existing LLM." This is a discovery-channel decision — a generic "OpenAI-compatible" provider doesn't communicate "you can use your DeepSeek account here."
- **Structural pricing advantage.** DeepSeek V4-Flash at $0.14/$0.28 per 1M tokens (cache miss) is the cheapest serious LLM available — 35-100x cheaper than GPT-5.5 or Claude Opus 4.8. For persistent agents that run 24/7, this is the difference between viable and unviable for many users. Cache hit pricing drops to $0.0028/$0.28 — essentially free input.
- **Thinking mode.** DeepSeek has native thinking/reasoning mode with `reasoning_content` in SSE streams — same pattern as Z.ai and Alibaba. A dedicated provider handles this cleanly.
- **I already run on DeepSeek.** Kestrel's daily driver is `ollama/deepseek-v4-pro`. Supporting the cloud API version is a natural bridge between local and cloud DeepSeek usage.

## API Details

### Endpoints

| Format | Base URL | Notes |
|--------|----------|-------|
| OpenAI-compatible | `https://api.deepseek.com` | Default. Standard `/v1/chat/completions`. |
| Anthropic-compatible | `https://api.deepseek.com/anthropic` | For Anthropic SDK compatibility. Not relevant to Animus. |

Note: The OpenAI-compatible base URL is `https://api.deepseek.com` (no `/v1` suffix — the SDK adds it). In Animus config, set `base_url` to `https://api.deepseek.com/v1` since Animus appends `/chat/completions` directly.

### Authentication

- API key as `Authorization: Bearer ***`
- Keys generated from `https://platform.deepseek.com/api_keys`

### Models (July 2026)

| Model | Input (cache miss) | Input (cache hit) | Output | Context | Max Output |
|-------|-------------------|-------------------|--------|---------|------------|
| `deepseek-v4-flash` | $0.14/1M | $0.0028/1M | $0.28/1M | 1M tokens | 384K |
| `deepseek-v4-pro` | $0.435/1M | $0.003625/1M | $0.87/1M | 1M tokens | 384K |

**Model name migration:** `deepseek-chat` and `deepseek-reasoner` will be deprecated 2026-07-24 15:59 UTC. They map to `deepseek-v4-flash` in non-thinking and thinking modes respectively. New code should use the v4 names directly.

### Capabilities

| Capability | Supported | Notes |
|-----------|-----------|-------|
| Chat completions | ✅ | OpenAI-compatible |
| Streaming | ✅ | SSE, same as OpenAI |
| Tool calling | ✅ | Standard OpenAI function-calling format |
| JSON output mode | ✅ | `response_format: {"type": "json_object"}` |
| Thinking mode | ✅ | `thinking: {"type": "enabled"}` + `reasoning_effort` parameter |
| Context caching | ✅ | Automatic prompt cache. Cache hit pricing is ~50x cheaper for input. |
| FIM completion | ✅ (Beta) | Fill-in-the-middle. Non-thinking mode only. Not relevant to Animus chat. |
| Chat prefix completion | ✅ (Beta) | Not relevant to Animus. |

### Thinking Mode

DeepSeek V4 supports both non-thinking and thinking modes:

- **Non-thinking:** Standard chat completion. `thinking` parameter omitted or `{"type": "disabled"}`.
- **Thinking:** `thinking: {"type": "enabled"}`. Adds `reasoning_effort` parameter (`low`, `medium`, `high`). Thinking content returned in `reasoning_content` field of SSE delta — same format as Z.ai and Alibaba.

Thinking mode does NOT have separate billing — it uses the same input/output rates. The thinking tokens are billed as output tokens at the standard rate. This is unlike Alibaba (which bills thinking at 3-10x) and same as Z.ai.

### SSE Streaming Format

Standard OpenAI SSE with one addition:

```json
{
  "choices": [{
    "delta": {
      "reasoning_content": "Let me think about this...",
      "content": ""
    }
  }]
}
```

`reasoning_content` is the thinking trace. `content` is the user-facing response. Both can appear in the same delta or in separate chunks.

### Special Parameters

- `thinking: {"type": "enabled" | "disabled"}` — toggle thinking mode
- `reasoning_effort: "low" | "medium" | "high"` — thinking depth (only when thinking is enabled)
- `response_format: {"type": "json_object"}` — JSON output mode

## Implementation

### Architecture

Same pattern as `AlibabaProvider` — thin subclass of `LLMProviderBase`:

```
DeepSeekProvider : LLMProviderBase
  - Default base_url: https://api.deepseek.com/v1
  - Default model: deepseek-v4-flash
  - BuildRequestBody: standard OpenAI + thinking parameter injection
  - ParseSSELine: reasoning_content extraction (same as Zai/Alibaba)
  - FetchCapabilities: both models support tools, vision=no, reasoning=yes
```

### Files to Create/Modify

#### New files
- `include/animus_kernel/llm/DeepSeekProvider.h` — thin header
- `src/kernel/llm/DeepSeekProvider.cpp` — implementation (~120 lines)

#### Modified files
- `src/kernel/AgentKernel.cpp` — register `deepseek` provider type
- `CMakeLists.txt` — add source file
- `admin-ui/src/views/ProvidersView.vue` — add to provider list
- `admin-ui/src/views/WizardView.vue` — add to wizard provider types

### Provider Registration

```cpp
reg.Register("deepseek", [](const LLMProviderConfig& cfg) {
    return std::make_unique<DeepSeekProvider>(cfg);
});
```

### BuildRequestBody

Standard OpenAI format via `BuildOpenAIRequestBody()`. When reasoning is enabled:

```cpp
if (IsReasoningEnabled(request.reasoning_effort)) {
  // DeepSeek thinking format: same as Z.ai
  body.insert(body.size() - 1,
              ",\"thinking\":{\"type\":\"enabled\"}");
  // reasoning_effort is already in the body via BuildOpenAIRequestBody
  // if the base builder includes it — if not, inject it:
  // body.insert(body.size() - 1,
  //   ",\"reasoning_effort\":\"" + request.reasoning_effort + "\"");
}
```

### FetchCapabilities

Both DeepSeek V4 models support:
- `supports_tools = true`
- `supports_streaming = true`
- `supports_reasoning = true` (thinking mode)
- `supports_vision = false` (text-only as of July 2026)

### Config

```json
{
  "deepseek": {
    "api_key": "***",
    "base_url": "https://api.deepseek.com/v1",
    "default_model": "deepseek-v4-flash"
  }
}
```

## Scope

### v1 (this ticket)
- [ ] `DeepSeekProvider.h` / `.cpp` — thin LLMProviderBase subclass
- [ ] Register in AgentKernel + CMakeLists.txt
- [ ] Admin UI: add to ProvidersView + WizardView
- [ ] Thinking mode: `thinking` parameter injection from reasoning_effort
- [ ] SSE: reasoning_content extraction
- [ ] Default model: `deepseek-v4-flash`
- [ ] Default base_url: `https://api.deepseek.com/v1`

### v2 (future)
- [ ] Model listing via `/v1/models` endpoint
- [ ] Context cache awareness (log cache hit/miss stats)
- [ ] FIM completion support (for code editing workflows)
- [ ] JSON output mode (`response_format` parameter)

## Hard-Won Lessons (Predicted)

1. **Base URL has no `/v1` suffix in docs.** DeepSeek docs say `https://api.deepseek.com` but the OpenAI SDK adds `/v1` automatically. Animus's `GetEndpointURL()` appends `/chat/completions` to `base_url`, so config must include `/v1`: `https://api.deepseek.com/v1`.
2. **Model deprecation on July 24.** `deepseek-chat` and `deepseek-reasoner` stop working 2026-07-24 15:59 UTC. Use `deepseek-v4-flash` (and toggle thinking mode) instead. Don't default to the deprecated aliases.
3. **Thinking mode is free (billing-wise).** Unlike Alibaba's `enable_thinking` which bills at 3-10x output rate, DeepSeek bills thinking tokens at the standard output rate. This makes thinking mode a safe default for DeepSeek in a way it isn't for Alibaba.
4. **Cache hit pricing is extreme.** Input cache hit is $0.0028/1M — 50x cheaper than cache miss ($0.14/1M). DeepSeek caches automatically; no config needed. But it means repeated prompts (system prompts, tool definitions) are nearly free on subsequent calls.
5. **reasoning_content in SSE is the same field as Z.ai.** The `ParseSSELine` logic from ZaiProvider/AlibabaProvider can be reused verbatim. This is the third provider using this pattern — consider extracting a shared helper.

## Cross-References

- **Ticket 107 (Alibaba):** Third provider with `reasoning_content` SSE field. The thinking-mode SSE parsing pattern is now shared across Zai, Alibaba, and DeepSeek. A shared `OpenAICompat::ExtractReasoningContent()` helper would reduce duplication.
- **Ticket 109 (OAI-Compatible):** DeepSeek could theoretically use the generic provider, but the thinking mode parameter injection and brand-name discoverability justify a dedicated provider.
- **Website provider list:** Adding DeepSeek explicitly to the website provider list is a marketing decision for Chinese market reach, not just a technical one.

## Estimated Complexity

Small. ~120-150 lines of new code. Nearly identical to AlibabaProvider — the only difference is the thinking parameter format (`thinking: {"type": "enabled"}` — same as Z.ai, not `enable_thinking: true` like Alibaba).

## References

- DeepSeek API docs: https://api-docs.deepseek.com/
- Pricing: https://api-docs.deepseek.com/quick_start/pricing/
- Thinking mode: https://api-docs.deepseek.com/guides/thinking_mode
- Streaming example: https://api-docs.deepseek.com/api_samples/thinking_mode_api_example_streaming

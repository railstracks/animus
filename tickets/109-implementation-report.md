# Ticket 109 — Implementation Report

**Date:** 2026-07-09
**Commit:** `a7ac6b9`
**Status:** Implemented, clean build verified

## What Was Built

Generic OpenAI-compatible provider as the eleventh provider type in Animus. Covers any LLM endpoint that exposes a standard `/v1/chat/completions` API without needing a dedicated provider implementation.

### Files Created
- `include/animus_kernel/llm/OpenAICompatProvider.h` — header-only, 69 lines

### Files Modified
- `src/kernel/AgentKernel.cpp` — `#include` + `reg.Register("openai-compatible", ...)`
- `admin-ui/src/views/ProvidersView.vue` — added to provider list (no pre-filled defaults)
- `admin-ui/src/views/WizardView.vue` — added to wizard provider types

## Architecture

`OpenAICompatProvider` inherits `OpenAIProvider` directly. Header-only — no `.cpp` file, no CMakeLists.txt change. The base class handles all request construction, SSE parsing, and tool call extraction. The only override is `FetchCapabilities` with heuristic model-name detection.

### Capability Detection

Conservative pattern matching against the model name string:

| Pattern | Capability | Example matches |
|---------|-----------|-----------------|
| `vision`, `vl`, `multimodal` | supports_vision | `llama-3.2-vision`, `qwen2-vl` |
| `4o` | supports_vision | `gpt-4o` (via proxy) |
| `gemini` | supports_vision | `gemini-1.5-pro` (via proxy) |
| `claude` | supports_vision | `claude-3.5-sonnet` (via proxy) |
| `pixtral` | supports_vision | `pixtral-12b` |
| `reasoner`, `thinking` | supports_reasoning | `deepseek-r1-distill` |
| `-r1` | supports_reasoning | `llama-3.1-r1` |
| `o1`, `o3`, `o4` (prefix) | supports_reasoning | `o1-mini`, `o3-pro` |
| Default | tools=yes, streaming=yes, vision=no, reasoning=no | Safe fallback |

### Design Decision: Header-Only

No `.cpp` file was needed because:
1. `OpenAIProvider` already implements all request/response logic
2. The only override (`FetchCapabilities`) is simple string matching
3. No custom request body modifications — standard OpenAI format
4. No custom SSE parsing — standard OpenAI streaming

This makes the provider maintenance-free: if a provider changes their API, the user updates their `base_url` or `model` config. No Animus code changes needed.

### Config

No pre-filled defaults. The user supplies everything:

```json
{
  "openai-compatible": {
    "api_key": "***",
    "base_url": "https://api.groq.com/openai/v1",
    "default_model": "llama-3.3-70b-versatile"
  }
}
```

Multiple instances supported via different instance IDs (e.g., `groq`, `together`, `fireworks` — all with `provider_type: "openai-compatible"`).

## What's NOT Included (v2)

- Model listing via `/v1/models` endpoint (many OAI-compatible providers support it)
- Provider-specific parameter passthrough (extra config fields forwarded to request body)
- Automatic capability detection via `/v1/models` response metadata

## Build Verification

```
[100%] Built target animus_kernel
```

Clean build. Only `AgentKernel.cpp` recompiled (include + registration). No new `.cpp` compiled.

## Maintenance Notes

This provider has **zero ongoing maintenance cost**. It makes no assumptions about any specific provider's API. If a provider changes their endpoint format, model naming, or parameter support, the user adjusts their config — no Animus code changes, no ticket, no release needed.

This is the deliberate trade-off vs. dedicated providers: dedicated providers get brand discoverability and custom features, but incur tracking and maintenance liability. The generic provider accepts conservative defaults in exchange for being universally applicable and maintenance-free.

# Ticket 109 — Generic OpenAI-Compatible Provider

**Created:** 2026-07-09
**Status:** Draft
**Priority:** P2
**Depends on:** Provider system (existing), OpenAICompat layer (existing)

## Summary

Add a generic `"openai-compatible"` provider type that accepts any base URL, API key, and model name. This lets users connect to any OpenAI-compatible LLM endpoint without needing a dedicated provider implementation — covering the long tail of providers that expose `/v1/chat/completions` but don't warrant dedicated treatment.

## Motivation

- **The long tail is real.** Groq, Together AI, Fireworks, Anyscale, Novita, Lepton, Replicate, AI21, Friendli, Infermatic, Hyperbolic — all expose OpenAI-compatible endpoints. We can't and shouldn't make dedicated providers for all of them.
- **Pairs with Ollama.** Ollama is the "generic local LLM API" standard. The OAI-compatible provider is the "generic cloud LLM API" standard. Together they cover the two most common integration patterns for self-hosted users.
- **Reduces friction.** Users currently need to wait for a dedicated provider or hack the OpenAI provider config. A named `"openai-compatible"` type makes it explicit: this is a generic endpoint, no special handling, no assumptions.
- **Future-proof.** New providers launch weekly. Most are OpenAI-compatible from day one. This lets users connect immediately without waiting for Animus updates.

## Design

### What It Is

A provider type `"openai-compatible"` (aliased as `"oai"`) that maps to the existing `OpenAIProvider` class. The difference is purely semantic — it signals to the admin UI and wizard that this is a user-configured generic endpoint, not a known provider with curated defaults.

### What It Is Not

- Not a new provider class. `OpenAIProvider` already IS a generic OAI-spec provider. This ticket just makes that explicit and registerable as a separate type.
- No custom request body modifications. Standard OpenAI format only.
- No custom SSE parsing. Standard OpenAI streaming format only.
- No model listing (user types model name manually). Could be added later via the `/v1/models` endpoint — many OAI-compatible providers support it.

### Capability Detection

Conservative defaults with heuristic model-name detection:

| Pattern | Capability | Examples |
|---------|-----------|----------|
| Contains `vision`, `vl`, `multimodal` | `supports_vision` | `llama-3.2-vision`, `qwen2-vl` |
| Contains `4o`, `gpt-4.1` | `supports_vision` | OpenAI-compatible proxies |
| Contains `reasoner`, `thinking`, `r1` | `supports_reasoning` | `deepseek-r1-distill` |
| Default | tools=yes, streaming=yes, vision=no, reasoning=no | Safe defaults |

### Implementation

**No new files.** Reuse `OpenAIProvider` directly:

```cpp
// AgentKernel.cpp
reg.Register("openai-compatible", [](const LLMProviderConfig& cfg) {
    return std::make_unique<OpenAIProvider>(cfg);
});
```

The only new code: a `FetchCapabilities` override with heuristic model-name detection. Two options:

**Option A (preferred):** Add a static helper to `OpenAIProvider` that does heuristic detection, and register a small lambda that creates an `OpenAIProvider` with a flag to use heuristic mode. Minimal code change.

**Option B:** Create a tiny `OpenAICompatProvider.h` (header-only, ~30 lines) that inherits `OpenAIProvider` and overrides `FetchCapabilities` with heuristics. Cleanest separation.

Either way: ~30-50 lines of new code.

### Admin UI

- Add `{ id: 'openai-compatible', label: 'OpenAI-Compatible (Generic)', baseUrl: '', defaultModel: '', authType: 'api_key' }` to ProvidersView
- Add `{ title: 'OpenAI-Compatible (Generic)', value: 'openai-compatible' }` to WizardView provider types
- No pre-filled base URL or default model — the user supplies everything
- Model listing: attempt `/v1/models` endpoint if API key is set (v2 feature)

### Config

```json
{
  "openai-compatible": {
    "api_key": "***",
    "base_url": "https://api.groq.com/openai/v1",
    "default_model": "llama-3.3-70b-versatile"
  }
}
```

The `provider_id` the user chooses becomes the instance identifier. Multiple instances are supported (e.g., `groq`, `together`, `fireworks` — all using `provider_type: "openai-compatible"` with different base URLs).

## Scope

### v1 (this ticket)
- [ ] Register `"openai-compatible"` provider type in AgentKernel
- [ ] Heuristic capability detection (model name patterns)
- [ ] Admin UI: add to ProvidersView + WizardView
- [ ] No pre-filled defaults (user supplies base_url, model, key)

### v2 (future)
- [ ] Model listing via `/v1/models` endpoint (if the provider supports it)
- [ ] Automatic capability detection via `/v1/models` response metadata
- [ ] Provider-specific parameter passthrough (extra config fields forwarded to request body)

## Estimated Complexity

Trivial. ~30-50 lines of new code. No new provider class needed (Option B) or minimal lambda (Option A).

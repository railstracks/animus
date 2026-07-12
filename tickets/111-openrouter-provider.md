# Ticket 111 — OpenRouter Provider

**Created:** 2026-07-09
**Status:** Draft
**Priority:** P2
**Depends on:** Provider system (existing), OpenAICompat layer (existing)

## Summary

Add OpenRouter as a dedicated LLM provider. OpenRouter is a multi-model aggregator that exposes a single OpenAI-compatible API endpoint (`https://openrouter.ai/api/v1`) providing access to 315+ models from OpenAI, Anthropic, Google, Meta, DeepSeek, Qwen, Mistral, and many others. A dedicated provider captures OpenRouter's unique features: model fallback routing, provider preferences, and per-request model selection.

## Motivation

- **Power-user demographics.** OpenRouter is the most popular LLM aggregator among developers who experiment with multiple models. The exact demographic that would adopt Animus for persistent AI agents — technically sophisticated, multi-model, cost-conscious.
- **Single API key, 315+ models.** Users who want flexibility without managing multiple provider accounts get access to every major model family through one OpenRouter key. This is particularly valuable for Animus agents that might want to switch models per-task.
- **Model fallback routing.** OpenRouter's `models` array + `route: "fallback"` parameter lets a single request try multiple models in sequence — if the first is unavailable, it falls back to the next. This is unique to OpenRouter and not available through the generic OAI-compatible provider.
- **Provider preferences.** Users can prefer providers by latency, throughput, or price. `provider: { preferred_max_latency: 5000 }` routes to the fastest available provider.
- **BYOK (Bring Your Own Key).** OpenRouter supports attaching your own provider API keys, so you pay provider-direct pricing without leaving OpenRouter's routing layer. Dedicated provider can expose this.

## API Details

### Endpoints

| Endpoint | URL | Notes |
|----------|-----|-------|
| Chat completions | `https://openrouter.ai/api/v1/chat/completions` | OpenAI-compatible |
| Models list | `https://openrouter.ai/api/v1/models` | Returns all available models with pricing |
| Credits | `https://openrouter.ai/api/v1/credits` | Balance and usage |

### Authentication

- API key as `Authorization: Bearer ***`
- Keys generated from `https://openrouter.ai/keys`
- Optional: `HTTP-Referer` header for app identification (OpenRouter uses this for ranking)
- Optional: `X-Title` header for app name (shown in OpenRouter dashboard)

### API Format

**OpenAI-compatible with extensions.** The `/api/v1/chat/completions` endpoint accepts standard OpenAI request format plus OpenRouter-specific parameters.

Standard OpenAI parameters work as-is: `model`, `messages`, `temperature`, `max_tokens`, `stream`, `tools`, `tool_choice`, `response_format`.

### OpenRouter-Specific Parameters

#### Model Fallback Routing

```json
{
  "models": ["anthropic/claude-opus-4.8", "openai/gpt-5.5", "deepseek/deepseek-v4-pro"],
  "route": "fallback"
}
```

If the first model is unavailable (rate limit, outage, etc.), OpenRouter automatically tries the next. This is transparent to the client — the response looks like a normal completion.

#### Provider Preferences

```json
{
  "provider": {
    "preferred_max_latency": 5000,
    "preferred_min_throughput": 100,
    "allow_fallbacks": true,
    "require_parameters": true,
    "ignore": ["some_provider_id"],
    "only": ["anthropic", "openai"]
  }
}
```

#### User Identification

```json
{
  "user": "stable_user_identifier"
}
```

Used for abuse detection. Optional but recommended for production deployments.

#### Plugins (Beta)

```json
{
  "plugins": [
    {"id": "pdf-parser", "config": {"strategy": "fast"}},
    {"id": "response-healer", "config": {"mode": "aggressive"}}
  ]
}
```

PDF parsing and response healing (fixing truncated JSON, etc.). Beta feature.

### Response Format

Standard OpenAI response with OpenRouter-specific additions:

```json
{
  "id": "gen-...",
  "choices": [{
    "message": {"role": "assistant", "content": "..."},
    "finish_reason": "stop"
  }],
  "model": "anthropic/claude-opus-4.8",
  "usage": {
    "prompt_tokens": 100,
    "completion_tokens": 50,
    "total_tokens": 150,
    "cost": 0.00345
  }
}
```

- `model` field reflects which model actually served the request (important for fallback routing)
- `usage.cost` field shows the actual cost in USD (OpenRouter-specific)
- SSE streaming is standard OpenAI format

### Pricing

OpenRouter charges per-token at provider-direct rates. No markup on pay-as-you-go. Credits purchased and consumed across models. BYOK (Bring Your Own Key) allows using your own provider keys with OpenRouter routing at no additional cost.

Free tier: limited free models available (e.g., `meta-llama/llama-3.3-70b-instruct:free`) with rate limits.

## Implementation

### Architecture

Thin subclass of `LLMProviderBase` with OpenRouter-specific extensions:

```
OpenRouterProvider : LLMProviderBase
  - Default base_url: https://openrouter.ai/api/v1
  - Default model: (none — user must specify)
  - BuildRequestBody: standard OpenAI + optional routing params
  - ParseSSELine: standard OpenAI (no reasoning_content)
  - FetchCapabilities: conservative defaults + model-name heuristics
  - GetHeaders: adds HTTP-Referer and X-Title for app identification
```

### Files to Create/Modify

#### New files
- `include/animus_kernel/llm/OpenRouterProvider.h` — thin header
- `src/kernel/llm/OpenRouterProvider.cpp` — implementation (~150 lines)

#### Modified files
- `src/kernel/AgentKernel.cpp` — register `openrouter` provider type
- `CMakeLists.txt` — add source file
- `admin-ui/src/views/ProvidersView.vue` — add to provider list
- `admin-ui/src/views/WizardView.vue` — add to wizard provider types

### BuildRequestBody

Standard OpenAI format via `BuildOpenAIRequestBody()`. No custom parameter injection needed for v1 — OpenRouter accepts standard OpenAI requests and routes to the specified model.

For v2: support `models[]` fallback array and `provider` preferences via the `extra` config map.

### GetHeaders

OpenRouter-specific headers for app identification:

```cpp
std::vector<std::pair<std::string, std::string>>
OpenRouterProvider::GetHeaders() const {
  auto headers = LLMProviderBase::GetHeaders();
  headers.emplace_back("HTTP-Referer", "https://animus.steadyfort.com");
  headers.emplace_back("X-Title", "Animus");
  return headers;
}
```

### FetchCapabilities

OpenRouter serves 315+ models with varying capabilities. Conservative approach:

- `supports_tools = true` (most models support tool calling via OpenRouter's normalization)
- `supports_streaming = true`
- `supports_vision = false` (default; could be detected from model name)
- `supports_reasoning = false` (default; could be detected from model name)

Model-name heuristics for vision: contains `vision`, `vl`, `4o`, `multimodal`, `gemini`, `claude` (most Claude models support vision).

### Config

```json
{
  "openrouter": {
    "api_key": "***",
    "base_url": "https://openrouter.ai/api/v1",
    "default_model": "deepseek/deepseek-v4-flash"
  }
}
```

Model names on OpenRouter use `provider/model` format (e.g., `anthropic/claude-opus-4.8`, `openai/gpt-5.5`, `deepseek/deepseek-v4-pro`).

## Scope

### v1 (this ticket)
- [ ] `OpenRouterProvider.h` / `.cpp` — thin LLMProviderBase subclass
- [ ] Register in AgentKernel + CMakeLists.txt
- [ ] Admin UI: add to ProvidersView + WizardView
- [ ] Custom headers: `HTTP-Referer` + `X-Title` for app identification
- [ ] Standard OpenAI request/response — no routing params yet
- [ ] Conservative capability defaults
- [ ] Model-name heuristic vision detection

### v2 (future)
- [ ] Model fallback routing: expose `models[]` array + `route: "fallback"` via agent config
- [ ] Provider preferences: `provider` object via `extra` config map
- [ ] Model listing via `/v1/models` endpoint (OpenRouter returns full catalog with pricing)
- [ ] Cost tracking: parse `usage.cost` from response and log per-agent spend
- [ ] BYOK support: allow attaching provider API keys via OpenRouter's key management
- [ ] Plugin support: PDF parser, response healer

## Hard-Won Lessons (Predicted)

1. **Model names use `provider/model` format.** `anthropic/claude-opus-4.8`, not `claude-opus-4.8`. OpenRouter requires the provider prefix. Don't strip it.
2. **`model` in response may differ from request.** With fallback routing, the response `model` field reflects which model actually served the request. Always log the response model, not just the requested model.
3. **Rate limits are per-account, not per-model.** OpenRouter enforces account-level rate limits. Multiple agents using the same key share a rate limit pool.
4. **Free models exist but have rate limits.** `:free` suffix models (e.g., `meta-llama/llama-3.3-70b-instruct:free`) are rate-limited and may be unavailable during peak. Don't default to free models for production agents.
5. **`usage.cost` is OpenRouter-specific.** The standard OpenAI response doesn't include cost. OpenRouter adds it. Useful for budget tracking but don't assume it exists on other providers.
6. **Headers matter for ranking.** `HTTP-Referer` and `X-Title` are optional but OpenRouter uses them to rank apps in their dashboard. Set them for visibility.

## Cross-References

- **Ticket 109 (OAI-Compatible):** OpenRouter could technically use the generic provider, but the custom headers, model fallback routing, and brand-name discoverability justify a dedicated provider.
- **Ticket 110 (DeepSeek):** DeepSeek models are available on OpenRouter as `deepseek/deepseek-v4-flash` and `deepseek/deepseek-v4-pro`. Users who want DeepSeek without a separate API key can use OpenRouter.
- **Website provider list:** Adding OpenRouter explicitly signals "we support multi-model routing" — important for the power-user demographic.

## Estimated Complexity

Small. ~120-150 lines of new code. Similar to AlibabaProvider/DeepSeekProvider. The v2 features (fallback routing, cost tracking) are more complex but not needed for v1.

## References

- OpenRouter API docs: https://openrouter.ai/docs/api/reference/overview
- Parameters: https://openrouter.ai/docs/api/reference/parameters
- Provider routing: https://openrouter.ai/docs/guides/routing/provider-selection
- Model fallbacks: https://openrouter.ai/blog/insights/reliability-failover
- Pricing: https://openrouter.ai/pricing
- OpenAPI spec: https://openrouter.ai/openapi.yaml

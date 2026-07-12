# Ticket 107 — Alibaba Cloud (DashScope / Qwen) Provider

**Created:** 2026-07-09
**Status:** Draft
**Priority:** P2
**Depends on:** Provider system (existing), OpenAICompat layer (existing)

## Summary

Add Alibaba Cloud Model Studio (DashScope) as a seventh LLM provider in Animus. Qwen models are accessed via an OpenAI-compatible API endpoint, making this a thin provider implementation on top of the existing `OpenAICompat` layer — same pattern as the Z.ai provider.

## Motivation

- **Chinese market access.** Qwen is the dominant LLM family in China by volume. Adding native DashScope support makes Animus directly usable by Chinese developers without routing through international proxies.
- **Competitive pricing.** Qwen-Turbo at $0.05/$0.20 per 1M tokens is among the cheapest capable models available. Qwen-Plus at $0.40/$1.20 is a strong mid-tier. MoE architecture makes this pricing structurally sustainable, not subsidized.
- **Open-weight option.** Qwen3 models are Apache 2.0 licensed. Users can run the same models locally via Ollama, or via DashScope API — same model, different substrate. This aligns with Animus's "the model is replaceable" architecture.
- **Multimodal capability.** Qwen3-VL-Plus and Qwen3.7-Plus have native vision support, relevant for the image tool (ticket 068).
- **Scale.** Qwen3.6-Plus was the first model on OpenRouter to process 1T+ tokens in a single day. The developer adoption is real.

## API Details

### Endpoints

DashScope offers OpenAI-compatible mode at two regional endpoints:

| Region | Base URL | Notes |
|--------|----------|-------|
| International (Singapore) | `https://dashscope-intl.aliyuncs.com/compatible-mode/v1` | Default for non-China users |
| China (Beijing) | `https://dashscope.aliyuncs.com/compatible-mode/v1` | For China-based deployments |
| Workspace-specific (SG) | `https://{WorkspaceId}.ap-southeast-1.maas.aliyuncs.com/compatible-mode/v1` | Per-workspace isolation |
| Workspace-specific (BJ) | `https://{WorkspaceId}.cn-beijing.maas.aliyuncs.com/compatible-mode/v1` | Per-workspace isolation |

The international endpoint is the default. China endpoint should be configurable via `base_url` override.

### Authentication

- API key passed as `Authorization: Bearer sk-xxx`
- Keys generated from Alibaba Cloud Model Studio console
- No OAuth option (the free OAuth tier was discontinued April 15, 2026)

### API Format

**Fully OpenAI-compatible.** The `/compatible-mode/v1/chat/completions` endpoint accepts standard OpenAI request format:

```json
{
  "model": "qwen-plus",
  "messages": [
    {"role": "system", "content": "..."},
    {"role": "user", "content": "..."}
  ],
  "temperature": 0.7,
  "max_tokens": 2048,
  "stream": true,
  "tools": [...],
  "tool_choice": "auto"
}
```

Response format is identical to OpenAI (`choices[0].message.content`, `tool_calls`, streaming `delta` chunks).

### Model Catalog (July 2026)

#### Text Generation

| Model | Input $/1M | Output $/1M | Context | Notes |
|-------|-----------|-----------|---------|-------|
| `qwen3.7-max` | $1.25 | $3.75 | 1M | Flagship. 50% promo. Text-only. |
| `qwen3.7-plus` | $0.32–$0.96 | $1.28–$3.84 | 1M | Native multimodal. Tiered by context length. |
| `qwen3-max` | $1.20 | $6.00 | 262K | Agent-optimized. Cache read $0.12/1M. |
| `qwen3.6-plus` | $0.50–$2.00 | $3.00–$6.00 | 1M | Native multimodal. Agentic coding. |
| `qwen3.6-flash` | $0.25–$1.00 | $1.50–$4.00 | 1M | Cost-optimized vision-language. |
| `qwen3-235b-a22b` | $0.70 | $2.80 / $8.40* | 131K | MoE flagship open-weight. *thinking mode. |
| `qwen3-30b-a3b` | $0.20 | $0.80 / $2.40* | 131K | Balanced MoE. *thinking mode. |
| `qwen3-8b` | $0.18 | $0.70 / $2.10* | 131K | Dense small. *thinking mode. |
| `qwen-max` | $1.60 | $6.40 | 32K | Stable production alias. |
| `qwen-plus` | $0.40 | $1.20 / $4.00* | 1M | Stable alias. *thinking mode. |
| `qwen-turbo` | $0.05 | $0.20 / $0.50* | 131K | Cheapest tier. 5M TPM. *thinking mode. |

*Thinking mode (`enable_thinking: true`) bills thinking tokens at a higher output rate.

#### Vision-Language

| Model | Input $/1M | Output $/1M | Context |
|-------|-----------|-----------|---------|
| `qwen3-vl-plus` | $0.20 | $1.60 | 262K |

#### Embeddings

| Model | Price |
|-------|-------|
| `text-embedding-v3` / `v4` | $0.07/1M tokens ($0.035/1M batch) |

### Capabilities

| Capability | Supported | Notes |
|-----------|-----------|-------|
| Chat completions | ✅ | OpenAI-compatible |
| Streaming | ✅ | SSE, same as OpenAI |
| Tool calling | ✅ | Standard OpenAI function-calling format |
| Vision (multimodal) | ✅ | Qwen3-VL-Plus, Qwen3.6-Plus, Qwen3.7-Plus. OpenAI content-array format. |
| Reasoning/thinking mode | ✅ | `enable_thinking: true` parameter. Billed at higher rate. |
| Embeddings | ✅ | Separate endpoint `/v1/embeddings` |
| Context caching | ✅ | Qwen3-Max supports prompt cache reads at $0.12/1M |

### Special Parameters

- `enable_thinking` (bool, default false): Triggers chain-of-thought reasoning. Thinking tokens billed at higher output rate. Not needed for most production tasks.
- `result_format` (optional): DashScope native mode supports `text` and `message` formats. In compatible mode, standard OpenAI format is used — this parameter is not needed.

## Implementation

### Architecture

Same pattern as Z.ai provider — thin subclass of `OpenAICompatProvider`:

```
AlibabaProvider : OpenAICompatProvider
  - Default base_url: https://dashscope-intl.aliyuncs.com/compatible-mode/v1
  - Default model: qwen-plus
  - FetchCapabilities: detect vision from model name patterns
  - No special request body modifications needed
```

### Files to Create/Modify

#### New files
- `include/animus_kernel/llm/AlibabaProvider.h` — thin header, same pattern as `ZaiProvider.h`

#### Modified files
- `src/kernel/AgentKernel.cpp` — register `alibaba` provider type
- `src/kernel/ProviderManager.cpp` — add `alibaba` to provider factory
- `admin-ui/src/views/ChannelsView.vue` — not needed (provider config, not channel)
- Admin UI provider config — add Alibaba to provider dropdown
- First-run wizard — add Alibaba as provider option

### Provider Registration

```cpp
// AgentKernel.cpp
{"alibaba", [](const ProviderConfig& cfg) -> std::unique_ptr<ILLMProvider> {
    auto p = std::make_unique<AlibabaProvider>();
    p->SetApiKey(cfg.api_key);
    p->SetBaseUrl(cfg.base_url.empty() 
        ? "https://dashscope-intl.aliyuncs.com/compatible-mode/v1" 
        : cfg.base_url);
    p->SetDefaultModel(cfg.default_model.empty() ? "qwen-plus" : cfg.default_model);
    return p;
}},
```

### Vision Capability Detection

Models with vision support (for `FetchCapabilities`):
- `qwen3-vl-*` — vision-language models
- `qwen3.6-plus`, `qwen3.6-flash` — native multimodal
- `qwen3.7-plus` — native multimodal

Pattern matching: if model name contains `vl`, or matches `qwen3.6-plus`, `qwen3.6-flash`, `qwen3.7-plus` → `supports_vision = true`.

### Config

```json
{
  "alibaba": {
    "api_key": "sk-xxx",
    "base_url": "https://dashscope-intl.aliyuncs.com/compatible-mode/v1",
    "default_model": "qwen-plus",
    "region": "international"
  }
}
```

`region` is informational — the actual endpoint is determined by `base_url`. Users in China should set:
```json
{
  "alibaba": {
    "api_key": "sk-xxx",
    "base_url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
    "default_model": "qwen-plus"
  }
}
```

## Scope

### v1 (this ticket)
- [ ] `AlibabaProvider.h` — thin OpenAICompat subclass
- [ ] Register in AgentKernel + ProviderManager
- [ ] Admin UI: add to provider dropdown in agent config
- [ ] First-run wizard: add as provider option
- [ ] Vision capability detection for Qwen VL models
- [ ] Default model: `qwen-plus`
- [ ] Default base_url: international endpoint

### v2 (future)
- [ ] Thinking mode support (`enable_thinking` parameter exposed to agent config)
- [ ] Embeddings endpoint integration (for semantic search, ticket 070)
- [ ] Prompt caching (Qwen3-Max)
- [ ] Image generation via DashScope Wan2.6 models (ticket 068 v2 — DashScope-native async API, NOT OpenAI-compatible)
- [ ] TTS via DashScope CosyVoice models (ticket 108 v2 — optimized for Mandarin/English bilingual)
- [ ] STT via DashScope Paraformer models (ticket 108 v2 — async create/poll model)
- [ ] Workspace-specific URL auto-configuration

## Cross-references

- **Ticket 068 (Image Tool):** DashScope image gen (Wan2.6) uses a native async task API, not OpenAI-compatible. Added as v2 provider candidate. The Qwen VL vision models ARE OpenAI-compatible and work with ticket 068's analyze action in v1.
- **Ticket 108 (Audio Tool):** DashScope STT (Paraformer) and TTS (CosyVoice) use native DashScope endpoints with async polling. Added as v2 provider candidates. CosyVoice is particularly interesting for Mandarin/English bilingual TTS.
- **Ticket 070 (Semantic Search):** DashScope embeddings (`text-embedding-v3/v4`) are OpenAI-compatible at `/v1/embeddings`. Straightforward integration candidate.

## Hard-Won Lessons (Predicted)

1. **Two regional endpoints, same API.** The international (`dashscope-intl`) and China (`dashscope`) endpoints are functionally identical but physically separate. Users in China get better latency with the China endpoint. Don't hardcode one — make `base_url` configurable.
2. **Thinking mode is not free.** `enable_thinking: true` produces hidden reasoning tokens billed at 3-10x the standard output rate. Don't enable it by default. Expose it as an opt-in agent config parameter.
3. **Model aliases are stable.** `qwen-plus` and `qwen-max` are stable aliases that always point to the current best model in their tier. Prefer aliases over versioned names (`qwen3.6-plus`) for production configs.
4. **MoE pricing is structural, not promotional.** Qwen's MoE architecture activates only 22B of 235B parameters per token. The low pricing is architecturally earned, not a loss-leader.
5. **Content-array vision format works.** Qwen VL models accept the same OpenAI `[{type: "text"}, {type: "image_url"}]` format that the image tool (068) already produces. No format conversion needed.
6. **Embeddings are a separate capability.** DashScope's `/v1/embeddings` endpoint uses a different request shape than chat. Don't try to route through OpenAICompat. Handle as a separate provider method in v2.

## Estimated Complexity

Small. ~50-80 lines of new code (header + registration). The OpenAICompat layer handles all the actual request/response work. This is the same effort as the Z.ai provider implementation.

## References

- Alibaba Cloud Model Studio: https://www.alibabacloud.com/help/en/model-studio/
- OpenAI compatibility docs: https://www.alibabacloud.com/help/en/model-studio/compatibility-of-openai-with-dashscope
- Model catalog: https://www.alibabacloud.com/help/en/model-studio/models
- Pricing analysis: https://www.eesel.ai/blog/qwen-pricing
- LiteLLM DashScope docs: https://docs.litellm.ai/docs/providers/dashscope
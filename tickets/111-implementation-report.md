# Ticket 111 — Implementation Report

**Date:** 2026-07-10  
**Commit:** `2cffeed`  
**Status:** Implemented (v1)

## What Was Built

### New Files
- `include/animus_kernel/llm/OpenRouterProvider.h` — Header (53 lines)
- `src/kernel/llm/OpenRouterProvider.cpp` — Implementation (153 lines)

### Modified Files
- `src/kernel/AgentKernel.cpp` — Registered `openrouter` provider (#11)
- `CMakeLists.txt` — Added `OpenRouterProvider.cpp` to sources
- `admin-ui/src/views/ProvidersView.vue` — Added to provider type list
- `admin-ui/src/views/WizardView.vue` — Added to wizard provider options

## Implementation Details

### Architecture
Thin `LLMProviderBase` subclass using shared `openai_compat` helpers. No protocol deviations from standard OpenAI — OpenRouter's `/v1/chat/completions` endpoint is fully compatible.

### Custom Headers
- `HTTP-Referer: https://animus.steadyfort.com` — for OpenRouter dashboard ranking
- `X-Title: Animus` — app identification

### Capability Detection
Model-name heuristics check the `provider/model` format names:
- **Vision indicators:** `vision`, `-vl`, `4o`, `gemini`, `claude`, `llava`, `pixtral`, `moondream`, `qwen-vl`, `internvl`, `minicpm-v`, `gemma3`, `gemma4`, `llama3.2-vision`
- **Reasoning indicators:** `reasoning`, `thinking`, `r1`, `o1`, `o3`, `o4`, `deepseek-r`, `deepseek-v4-pro`

### GetVisionModelIds
Filters `raw_features` (all models from capabilities fetch) against the same vision indicators. Works with the `/api/v1/vision-models` endpoint for agent configuration.

## Default Configuration

| Field | Value |
|-------|-------|
| Provider ID | `openrouter` |
| Base URL | `https://openrouter.ai/api/v1` |
| Default Model | `deepseek/deepseek-v4-flash` |
| Auth | Bearer token (API key from openrouter.ai/keys) |

## Build Verification

Clean build on workstation. `100% Built target animus_kernel`. No warnings or errors.

## v2 Features (Not Implemented)

Deferred per ticket scope:
- Model fallback routing (`models[]` + `route: "fallback"`)
- Provider preferences (latency, throughput, price)
- Cost tracking (`usage.cost` parsing)
- Model listing via `/v1/models` endpoint
- BYOK support
- Plugin support (PDF parser, response healer)

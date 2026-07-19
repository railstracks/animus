# Ticket 122: Diffusion Provider System

**Status:** Scoped
**Date:** 2026-07-19
**Author:** Melvin + Kestrel

## Problem

The existing `image` tool supports generation via OAI-compatible vision models (Ollama, Z.AI, OpenAI), but these are multimodal LLMs repurposed for image generation — not diffusion models. GetImg offers purpose-built diffusion with distinct capabilities: model selection for different artistic styles, reference image guidance, video generation, and a different API contract (sync for images, async polling for videos).

Overloading the `image` tool with diffusion generation would conflate two different API contracts (vision-model `analyze` + `generate` vs diffusion-only `generate` with different parameters). A separate tool keeps each tool's contract clean.

## Design

### New tool: `diffusion`

Separate from the existing `image` tool. One action: `generate`. Supports `type: "image" | "video"`.

**Tool definition (agent-facing):**
```
name: "diffusion"
description: "Generate images or video using diffusion models (e.g. GetImg)."
parameters:
  action: "generate" (required)
  type: "image" | "video" (default: "image")
  prompt: string (required) — text description
  model: string (required) — diffusion model ID. Agent selects from available
          models across all configured providers. Different models produce
          different visual styles (e.g. SynthWave, Anime, Cinematic, Photorealistic).
          The model list is surfaced to the agent via the provider's model listing.
  provider: string — which configured diffusion provider to use (e.g. "getimg")
  aspect_ratio: string — e.g. "1:1", "16:9", "9:16", "2:3", "3:2"
  resolution: string — e.g. "1K", "2K", "4K" (images) or "720p", "1080p" (video)
  output_format: string — "png" | "jpeg" | "webp" (images only)
  duration: integer — seconds (video only)
  sound: boolean — generate audio (video only, where supported)
  reference_images: array — [{ url: string, role: string }] for guided generation
```

**Model selection is agent-driven, not preconfigured.** The agent chooses the
model per-generation based on the desired style. Providers expose their model
lists via `GET /v2/models` (or equivalent), and these are surfaced to the agent
at runtime — either through the tool's model list or by the admin UI surfacing
them in the agent's system prompt. The `default_model` in provider config is
only a fallback when the agent doesn't specify one.

**Image generation flow (sync):**
1. Agent calls `diffusion` with `type: "image"`
2. Tool sends `POST /v2/images/generations` to configured provider
3. Response contains signed download URL(s)
4. Tool downloads image, saves to `media/generated/`
5. Returns saved filepath to agent

**Video generation flow (async with polling):**
1. Agent calls `diffusion` with `type: "video"`
2. Tool sends `POST /v2/videos/generations` → gets generation ID + `status: "pending"`
3. Tool polls `GET /v2/videos/generations/{id}` every 5s (configurable)
4. On `status: "completed"`, downloads video from signed URL
5. Saves to `media/generated/`, returns filepath
6. Timeout: 300s default (configurable)

### Provider Registration

New config section in agent config, separate from LLM providers:

```json
{
  "diffusion_providers": [
    {
      "id": "getimg",
      "type": "getimg",
      "base_url": "https://api.getimg.ai",
      "api_key": "***",
      "default_model": "seedream-5-lite",
      "default_aspect_ratio": "1:1"
    }
  ]
}
```

Provider types initially supported: `getimg`. Architecture should allow adding `comfyui`, `automatic1111`, etc. Each provider must implement a `ListModels()` method so the agent can discover available models and their capabilities (image vs video, supported aspect ratios, etc.).

### Admin UI

New page: **Diffusion** (sidebar nav, admin-only)

- Provider list (add/edit/delete)
  - ID, type, base URL, API key, default model
  - Test connection button (calls `GET /v2/models`)
- Model browser (fetches available models from provider)
  - Shows model ID, type (image/video), capabilities
  - Refresh button to re-fetch

### DB Schema

New table: `diffusion_providers`
```sql
CREATE TABLE IF NOT EXISTS diffusion_providers (
  id TEXT PRIMARY KEY,
  type TEXT NOT NULL,          -- "getimg", "comfyui", etc.
  base_url TEXT NOT NULL,
  api_key TEXT NOT NULL,
  default_model TEXT,
  default_aspect_ratio TEXT,
  config TEXT,                -- JSON blob for provider-specific options
  created_at BIGINT NOT NULL,
  updated_at BIGINT NOT NULL
);
```

### API Endpoints

- `GET /api/v1/diffusion/providers` — list providers (admin)
- `POST /api/v1/diffusion/providers` — create provider (admin)
- `PUT /api/v1/diffusion/providers/:id` — update provider (admin)
- `DELETE /api/v1/diffusion/providers/:id` — delete provider (admin)
- `GET /api/v1/diffusion/providers/:id/models` — list models from provider (admin)
- `POST /api/v1/diffusion/generate` — proxy generation (for admin UI testing, admin)

### Files to Create/Modify

**New:**
- `include/animus_kernel/DiffusionStore.h`
- `src/kernel/diffusion/DiffusionStore.cpp`
- `include/animus_kernel/tools/DiffusionTool.h`
- `src/kernel/tools/DiffusionTool.cpp`
- `src/kernel/diffusion/GetImgProvider.cpp` (first provider impl)
- `src/kernel/admin/internal/AdminServerRoutesDiffusion.inc`
- `admin-ui/src/views/DiffusionView.vue`
- `admin-ui/src/i18n/locales/en/diffusion.ts` (+ 22 locales)

**Modified:**
- `CMakeLists.txt` (new sources)
- `include/animus_kernel/AdminServer.h` (new members, RegisterRoutesDiffusion)
- `src/kernel/admin/AdminServer.cpp` (RegisterRoutesDiffusion call)
- `src/kernel/admin/AdminServerRoutes.cpp` (RegisterRoutesDiffusion)
- `src/kernel/AgentKernel.cpp` (wire DiffusionStore, register DiffusionTool)
- `admin-ui/src/router/index.ts` (diffusion route)
- `admin-ui/src/components/AppSidebar.vue` (diffusion nav, admin-only)
- `admin-ui/src/i18n/locales/en/sidebar.ts` (+ locales)
- `include/animus_kernel/tools/ToolRegistry.h` (register diffusion tool)

### Phases

**Phase 1 (MVP):**
- GetImg provider implementation (image generation only)
- DiffusionTool with `generate` action
- DB table + store
- Admin UI: provider CRUD + model listing
- Tool registered for agents

**Phase 2:**
- Video generation (async polling)
- Reference image support
- Additional providers (ComfyUI, Automatic1111)

### Reference Docs

GetImg documentation in `docs/getimg/`:
- `1.getting-started/` — intro, quickstart, auth, models
- `2.generation/` — image gen (sync), video gen (async + polling)
- `3.concepts/` — media responses, errors, rate limits, billing

Key API details:
- Base URL: `https://api.getimg.ai`
- Auth: `Authorization: Bearer $API_KEY`
- Image gen: `POST /v2/images/generations` (sync, returns completed image)
- Video gen: `POST /v2/videos/generations` (async, 202 + ID) → poll `GET /v2/videos/generations/{id}`
- Models: `GET /v2/models?type=image|video`
- Download URLs expire (signed, `deletes_at` timestamp)
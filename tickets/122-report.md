# Ticket 122 Report: Diffusion Provider System + Stability AI

**Ticket:** 122 — Diffusion Provider System
**Supplemented by:** 124 Phase 1 — Stability AI Provider
**Status:** Complete
**Date:** 2026-07-19 → 2026-07-20
**Author:** Melvin + Kestrel

## What was built

A pluggable diffusion provider system with an agent-facing `diffusion` tool for generating images (and eventually video, audio, 3D) via purpose-built diffusion model APIs. Two providers implemented: GetImg.ai and Stability AI.

## Implementation summary

### Core architecture
- **`IDiffusionProvider`** interface — `Generate`, `PollStatus`, `ListModels`, `DownloadMedia`, `TestConnection`, `GetCapabilities`
- **`DiffusionStore`** — DB persistence (`diffusion_providers` table), CRUD operations
- **`DiffusionTool`** — agent-facing tool with `generate` and `list_models` actions
- **`DiffusionCapabilities`** — bitmask flags for per-provider feature tracking (ImageGeneration, VideoGeneration, AudioGeneration, Model3DGeneration, Upscale, ImageEdit, ImageControl, NegativePrompt, StylePreset, SeedControl, ImageToImage, AudioToAudio)
- **`MultipartFormData`** — helper for `multipart/form-data` request construction (used by Stability provider)

### Providers

**GetImg.ai** (Ticket 122)
- JSON API (`POST /v2/images/generations`, `POST /v2/videos/generations`)
- Sync image generation, async video generation with polling
- Model listing via `GET /v2/models`
- Capabilities: ImageGeneration | VideoGeneration | ImageToImage

**Stability AI** (Ticket 124 Phase 1)
- Two API styles: v2beta (multipart) and v1 (JSON)
- v2beta models: Stable Image Ultra, Stable Image Core, SD 3.5 (Large, Large Turbo, Medium, Flash)
  - `POST /v2beta/stable-image/generate/{ultra|core|sd3}` — multipart/form-data
  - Response: `{"image": "base64...", "finish_reason": "SUCCESS"}`
- v1 model: SDXL 1.0
  - `POST /v1/generation/stable-diffusion-xl-1024-v1-0/text-to-image` — application/json
  - `text_prompts` array with weight, `height`/`width` instead of `aspect_ratio`
  - Response: `{"artifacts": [{"base64": "...", "finishReason": "SUCCESS", "seed": N}]}`
- No models endpoint — hardcoded model list with per-model capabilities
- Capabilities: ImageGeneration | ImageToImage | NegativePrompt | StylePreset | SeedControl
- Negative prompt, style presets, seed control, image-to-image with strength

### Tool definition (agent-facing)
22 total registered tools in Animus (up from 19). The `diffusion` tool accepts:
- `action`: generate | list_models
- `type`: image | video | audio | 3d (default: image)
- `prompt`, `model`, `provider`, `aspect_ratio`, `resolution`, `output_format`
- `duration`, `sound` (video/audio)
- `negative_prompt`, `style_preset`, `seed`, `strength`, `input_image` (Stability)
- `output_filepath` — absolute path for saved media (agent-specified)

Provider-aware parameter filtering: parameters only sent if the provider's capabilities support them (e.g. `negative_prompt` only sent to Stability, not GetImg).

`list_models` returns model ID, name, type, provider, and capability array per model across all configured providers.

### Admin UI
- **Diffusion page** in sidebar (after Providers, admin-only)
- Provider CRUD: ID, type, base URL, API key, default model, default aspect ratio
- Type select with auto-fill: `getimg` → `https://api.getimg.ai`, `stability` → `https://api.stability.ai`
- Auto-generated provider IDs (`getimg-1`, `stability-1`, etc.)
- Model browser (fetches available models from each provider)

### Key files
- `include/animus_kernel/DiffusionProvider.h` — interface + request/response types
- `include/animus_kernel/DiffusionCapabilities.h` — capability bitmask flags
- `include/animus_kernel/DiffusionStore.h` — DB persistence
- `include/animus_kernel/tools/DiffusionTool.h` — tool header
- `include/animus_kernel/tools/MultipartFormData.h` — multipart helper
- `src/kernel/diffusion/DiffusionStore.cpp` — DB CRUD
- `src/kernel/diffusion/GetImgProvider.cpp` — GetImg implementation
- `src/kernel/diffusion/StabilityProvider.cpp` — Stability implementation (v2beta + v1)
- `src/kernel/tools/DiffusionTool.cpp` — tool implementation
- `src/kernel/tools/MultipartFormData.cpp` — multipart file loading
- `src/kernel/admin/internal/AdminServerRoutesDiffusion.inc` — admin API
- `admin-ui/src/views/DiffusionView.vue` — admin UI

## Issues encountered and resolved

1. **`Base64.h` not found** — phantom include in GetImgProvider.cpp and DiffusionTool.cpp. Removed (not needed — we download from URLs or parse base64 from JSON).

2. **`DiffusionStore` not declared** — missing forward declaration in AgentKernel.h. Added.

3. **`Config config = {}` default argument** — compiler rejected brace-init as default. Removed default; callers pass Config explicitly.

4. **`ToolParameter` brace-init** — `push_back({...})` doesn't work with the struct (too many fields). Rewrote to use named field assignment. Same lesson from MEMORY.md.

5. **`ToolCall::GetParam()` doesn't exist** — `ToolCall` has `.arguments` (JSON string). Added `ParseArgs()` + `GetStringField()` / `GetIntField()` / `GetBoolField()` helpers (same pattern as ImageTool).

6. **`ToolResult::content` doesn't exist** — field is `.output`. Also needs `.call_id` from `call.id`.

7. **Init order bug** — `DiffusionStore` was created AFTER `RegisterBuiltinTools`, so `if (m_diffusionStore)` was null and the tool was never registered. Moved store init before registration. Same class of bug as the empty `session_type` bypass.

8. **SD 3.5 endpoint** — was `/generate/sd3.5`, should be `/generate/sd3` (model field selects variant). Caused 404s.

9. **Multipart boundary generation** — `dis(gen)` called twice per character, producing non-hex boundary chars. Fixed to single draw.

10. **1MB response body truncation** — shared `HttpClient` has 1MB max (for web scraping). Base64 image responses are 1-3MB, silently truncated → JSON parse fails → empty `file_data`. Fixed by giving DiffusionTool its own `HttpClient` with 50MB limit.

11. **Missing `<fstream>` include** — `std::ofstream` used in DiffusionTool.cpp without the include.

12. **Missing comma after array relocation** — moving diffusion nav item from end of array to middle required adding trailing comma. Fourth instance of this pattern.

## Lines changed

~3,400 lines across ~45 files (Ticket 122 + Ticket 124 Phase 1 combined).

## What's next

- **Ticket 123** — Chat attachments (scoped, next)
- **Ticket 124 Phase 2** — Audio (Stable Audio 3.0) and 3D (Fast 3D / SPAR3D) media types
- **Ticket 124 Phase 3** — Edit, control, and upscale actions
- Additional diffusion providers (Replicate, ComfyUI, Automatic1111) when needed
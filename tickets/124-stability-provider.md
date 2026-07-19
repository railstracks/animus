# Ticket 124: Stability AI Diffusion Provider + Media Type Expansion

**Status:** Draft
**Date:** 2026-07-19
**Author:** Melvin + Kestrel

## Problem

Ticket 122 shipped the `diffusion` tool with GetImg as the first provider. The tool supports `image` and `video` media types. Stability AI offers diffusion generation across three media types not covered by GetImg:

- **Audio** — Stable Audio 3.0 (text-to-audio, audio-to-audio, up to 6min, 44.1kHz stereo)
- **3D** — Stable Fast 3D + SPAR3D (single 2D image → GLB 3D model)
- **Upscale** — Fast/Conservative/Creative upscalers (4x to 40x)

Stability also offers image generation (Stable Image Ultra/Core, SD 3.5 variants) with features GetImg lacks: negative prompts, style presets, seed control, image-to-image with strength control, and a full edit suite (inpaint, outpaint, erase, search-and-replace, search-and-recolor, remove-background, replace-background-and-relight).

The current `IDiffusionProvider` interface and `DiffusionTool` were designed to be provider-agnostic, but need extension to support the broader media type coverage and Stability's `multipart/form-data` protocol.

## Design

### Provider capability tracking

Not all providers support all media types or actions. The provider interface needs a capability system so the tool can filter and route correctly.

**New: `DiffusionCapability` flags**

```cpp
enum class DiffusionCapability : uint32_t {
    ImageGeneration   = 1 << 0,
    VideoGeneration   = 1 << 1,
    AudioGeneration   = 1 << 2,
    Model3DGeneration = 1 << 3,
    Upscale           = 1 << 4,
    ImageEdit         = 1 << 5,  // inpaint, outpaint, erase, etc.
    ImageControl      = 1 << 6,  // sketch, structure, style transfer
    NegativePrompt    = 1 << 7,
    StylePreset       = 1 << 8,
    SeedControl       = 1 << 9,
    ImageToImage      = 1 << 10,
    AudioToAudio      = 1 << 11,
};

using DiffusionCapabilities = uint32_t;
```

**Extend `IDiffusionProvider`:**
```cpp
/// Returns the capabilities supported by this provider
virtual DiffusionCapabilities GetCapabilities() const = 0;
```

**Extend `DiffusionModel`:**
```cpp
struct DiffusionModel {
    std::string id;
    std::string type;         // "image" | "video" | "audio" | "3d"
    std::string name;
    DiffusionCapabilities capabilities{0};  // what this model can do
};
```

**Capability maps by provider:**

| Capability | GetImg | Stability |
|------------|--------|-----------|
| ImageGeneration | ✅ | ✅ |
| VideoGeneration | ✅ | ❌ |
| AudioGeneration | ❌ | ✅ |
| Model3DGeneration | ❌ | ✅ |
| Upscale | ❌ | ✅ |
| ImageEdit | ❌ | ✅ |
| ImageControl | ❌ | ✅ |
| NegativePrompt | ❌ | ✅ |
| StylePreset | ❌ | ✅ |
| SeedControl | ❌ | ✅ |
| ImageToImage | ✅ (via `image` param) | ✅ (via `image` + `strength` + `mode`) |
| AudioToAudio | ❌ | ✅ |

### Extended `DiffusionGenRequest`

New optional fields for Stability-specific features:

```cpp
struct DiffusionGenRequest {
    // Existing
    std::string prompt;
    std::string model;
    std::string type;          // "image" | "video" | "audio" | "3d"
    std::string aspect_ratio;
    std::string resolution;
    std::string output_format;  // "png" | "jpeg" | "webp" (images), "mp3" | "wav" (audio), "glb" (3D)
    int duration{0};            // seconds (video/audio)
    bool sound{false};

    // New — optional, provider-specific
    std::string negative_prompt;
    std::string style_preset;   // "3d-model", "anime", "cinematic", etc.
    int64_t seed{0};            // 0 = random
    double strength{0.0};        // image-to-image: 0=identical, 1=ignore input
    double cfg_scale{0.0};       // prompt adherence (0 = provider default)
    std::string input_image_path;  // for image-to-image, audio-to-audio, 3D from image
    std::string action;         // "generate" | "upscale" | "edit" | "control"
    std::string edit_mode;      // "inpaint" | "outpaint" | "erase" | "search_and_replace" | "search_and_recolor" | "remove_background" | "replace_background"
    std::string control_mode;   // "sketch" | "structure" | "style"
    std::string mask_image_path; // for inpaint/edit operations
    std::string upscale_mode;   // "fast" | "conservative" | "creative"
};
```

### Extended `DiffusionGenResult`

```cpp
struct DiffusionGenResult {
    // Existing
    bool success{false};
    bool async{false};
    std::string generation_id;
    std::string media_url;
    std::string saved_path;
    std::string content_type;
    int width{0};
    int height{0};
    int duration_seconds{0};
    std::string error;

    // New
    std::string file_data;      // raw bytes for direct-binary responses (Stability)
    std::string finish_reason;  // "SUCCESS" | "CONTENT_FILTERED"
};
```

### Tool definition updates

The `diffusion` tool's `type` parameter enum expands:

```
type: "image" | "video" | "audio" | "3d" (default: "image")
```

New parameters added to the tool definition:

```
negative_prompt: string — keywords of what to exclude from output (Stability)
style_preset: string — style guide ("3d-model", "anime", "cinematic", etc.)
seed: integer — reproducibility control (0 = random)
strength: number — image-to-image influence (0-1, Stability)
action: "generate" | "upscale" | "edit" | "control" (default: "generate")
edit_mode: string — inpaint/outpaint/erase/search_and_replace/search_and_recolor/remove_background
control_mode: string — sketch/structure/style
upscale_mode: string — fast/conservative/creative
input_image: string — path to input image (for img2img, audio2audio, 3D, upscale, edit, control)
mask_image: string — path to mask image (for inpaint/edit)
cfg_scale: number — prompt adherence (Stability)
```

**Provider-aware parameter filtering:** The tool should only surface parameters relevant to the selected provider's capabilities. For example, `style_preset` should only be accepted when the provider supports `StylePreset`. Parameters sent to a provider that doesn't support them should be silently ignored (not error).

### Stability provider implementation

**`StabilityProvider` class** (~350-400 lines):

- Base URL: `https://api.stability.ai`
- Auth: `Authorization: Bearer <key>` (same as GetImg)
- Request format: `multipart/form-data` (key difference from GetImg's JSON)
- Response: `accept: application/json` to get base64-encoded media (simplifies DownloadMedia — no binary response handling needed for images)
- Async pattern: 202 + `{id}` → poll `GET /v2beta/stable-image/results/{id}` or `GET /v2beta/audio/results/{id}`

**Endpoint mapping:**

| Media type | Action | Endpoint |
|------------|--------|----------|
| image | generate (ultra) | `POST /v2beta/stable-image/generate/ultra` |
| image | generate (core) | `POST /v2beta/stable-image/generate/core` |
| image | generate (sd3.5) | `POST /v2beta/stable-image/generate/sd3.5` |
| image | upscale (fast) | `POST /v2beta/stable-image/upscale/fast` |
| image | upscale (conservative) | `POST /v2beta/stable-image/upscale/conservative` |
| image | upscale (creative) | `POST /v2beta/stable-image/upscale/creative` (async) |
| image | edit (inpaint) | `POST /v2beta/stable-image/edit/inpaint` |
| image | edit (outpaint) | `POST /v2beta/stable-image/edit/outpaint` |
| image | edit (erase) | `POST /v2beta/stable-image/edit/erase` |
| image | edit (search_and_replace) | `POST /v2beta/stable-image/edit/search-and-replace` |
| image | edit (search_and_recolor) | `POST /v2beta/stable-image/edit/search-and-recolor` |
| image | edit (remove_background) | `POST /v2beta/stable-image/edit/remove-background` |
| image | edit (replace_background) | `POST /v2beta/stable-image/edit/replace-background-and-relight` (async) |
| image | control (sketch) | `POST /v2beta/stable-image/control/sketch` |
| image | control (structure) | `POST /v2beta/stable-image/control/structure` |
| image | control (style) | `POST /v2beta/stable-image/control/style` |
| audio | generate (text-to-audio) | `POST /v2beta/audio/stable-audio/text-to-audio` (async) |
| audio | generate (audio-to-audio) | `POST /v2beta/audio/stable-audio/audio-to-audio` (async) |
| audio | fetch result | `GET /v2beta/audio/results/{id}` |
| 3d | generate (fast) | `POST /v2beta/3d/stable-fast-3d` |
| 3d | generate (spar3d) | `POST /v2beta/3d/stable-point-aware-3d` |

**Model list (hardcoded — no list endpoint):**

```cpp
static std::vector<DiffusionModel> s_stabilityModels = {
    {"ultra",            "image", "Stable Image Ultra",        ImageGeneration|NegativePrompt|StylePreset|SeedControl|ImageToImage},
    {"core",             "image", "Stable Image Core",        ImageGeneration|NegativePrompt|StylePreset|SeedControl},
    {"sd3.5-large",      "image", "SD 3.5 Large",             ImageGeneration|NegativePrompt|SeedControl|ImageToImage},
    {"sd3.5-large-turbo","image", "SD 3.5 Large Turbo",       ImageGeneration|NegativePrompt|SeedControl|ImageToImage},
    {"sd3.5-medium",     "image", "SD 3.5 Medium",           ImageGeneration|NegativePrompt|SeedControl|ImageToImage},
    {"sd3.5-flash",      "image", "SD 3.5 Flash",             ImageGeneration|NegativePrompt|SeedControl|ImageToImage},
    {"stable-audio-3",   "audio", "Stable Audio 3.0",         AudioGeneration|AudioToAudio|SeedControl},
    {"stable-fast-3d",   "3d",    "Stable Fast 3D",           Model3DGeneration},
    {"spar3d",           "3d",    "Stable Point Aware 3D",    Model3DGeneration},
};
```

### Multipart form-data support

Stability uses `multipart/form-data` for all requests. GetImg uses JSON. Two options:

**Option A: Add multipart to HttpClient (preferred)**
- Add a `MultipartFormData` helper to `HttpClient` that constructs the boundary and body
- Reusable for future providers (ComfyUI, Automatic1111)
- ~50 lines

**Option B: Build multipart body manually in StabilityProvider**
- Self-contained, no shared changes
- Duplicated if other providers need multipart later

Recommend Option A — multipart is a common pattern for file-upload APIs.

### Admin UI updates

- Add `"stability"` to provider type select in `DiffusionView.vue`
- `onTypeChange()` handler: when `stability` selected, auto-fill:
  - Base URL: `https://api.stability.ai`
  - Default aspect ratio: `1:1`
  - Provider ID: `stability-#` (incrementing)
- Model browser: already works via `ListModels()` — Stability returns hardcoded list

### Files to Create/Modify

**New:**
- `src/kernel/diffusion/StabilityProvider.cpp` (~350-400 lines)
- `include/animus_kernel/DiffusionCapabilities.h` (capability flags)
- Possibly: `include/animus_kernel/tools/HttpClientMultipart.h` (multipart helper)

**Modified:**
- `include/animus_kernel/DiffusionProvider.h` — add capabilities, new request/response fields
- `include/animus_kernel/DiffusionStore.h` — no changes (DB schema unchanged)
- `src/kernel/tools/DiffusionTool.cpp` — extended tool definition (new params, new type enum values), provider-aware parameter filtering, new action routing (upscale/edit/control)
- `src/kernel/diffusion/GetImgProvider.cpp` — add `GetCapabilities()`, update `ListModels()` to populate capabilities
- `src/kernel/AgentKernel.cpp` — factory dispatch for `stability` type
- `admin-ui/src/views/DiffusionView.vue` — add stability to type select, auto-fill defaults
- `admin-ui/src/i18n/locales/en/sidebar.ts` — no changes (diffusion nav already exists)

### Phases

**Phase 1 — Stability image generation:**
- `DiffusionCapabilities` system
- `StabilityProvider` with image generation (ultra, core, sd3.5 variants)
- Multipart form-data support in HttpClient
- Extended `DiffusionGenRequest` with new optional fields
- Extended tool definition with new parameters
- Admin UI: stability provider type with auto-fill
- GetImg provider updated with `GetCapabilities()`

**Phase 2 — Audio and 3D media types:**
- Add `"audio"` and `"3d"` to tool `type` enum
- Stable Audio 3.0: text-to-audio (async), audio-to-audio (async with file upload)
- Stable Fast 3D + SPAR3D: image-to-3D (sync, GLB output)
- Output format extensions: `mp3`/`wav` (audio), `glb` (3D)
- File extension routing in `GenerateFilename()`

**Phase 3 — Edit, control, and upscale actions:**
- Upscale (fast/conservative/creative)
- Edit suite (inpaint, outpaint, erase, search-and-replace, search-and-recolor, remove-background, replace-background)
- Control (sketch, structure, style transfer)
- These require image upload (multipart with file binary) — more complex
- `action` parameter dispatch in tool definition

### Reference Docs

Stability API documentation in `docs/stability/`:
- `1.stable-image/` — generate (ultra/core/sd3.5), upscale, edit, control, results
- `2.audio/` — Stable Audio 2.5 + 3.0 (text-to-audio, audio-to-audio, inpaint, fetch result)
- `3.3d/` — Stable Fast 3D, Stable Point Aware 3D
- `4.version-1/` — legacy SDXL 1.0 + engines (deprecated, not implementing)

Key API details:
- Base URL: `https://api.stability.ai`
- Auth: `Authorization: Bearer $STABILITY_API_KEY`
- Request format: `multipart/form-data` (NOT JSON like GetImg)
- Image response: `accept: image/*` for raw bytes, `accept: application/json` for base64
- Async: 202 + `{id}` → poll `GET /v2beta/{stable-image|audio}/results/{id}`
- Results stored 24h on Stability's side
- Rate limit: 150 requests / 10 seconds
- Max request size: 10MiB (images), 100MB (audio-to-audio)

### Credit costs (for reference)

| Service | Credits |
|---------|---------|
| Stable Image Ultra | 8 |
| Stable Image Core | 3 |
| SD 3.5 (all variants) | 6.5 |
| Fast Upscale | 2 |
| Conservative Upscale | 5 |
| Creative Upscale | 15 |
| Edit (all types) | 3-5 |
| Control (all types) | 4 |
| Stable Audio 3.0 | 26 |
| Stable Fast 3D | 10 |
| SPAR3D | 4 |
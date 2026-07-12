# Ticket 068 — Image Tool (Analysis + Generation)

**Created:** 2026-06-05
**Updated:** 2026-07-07
**Status:** Refined — ready for implementation
**Priority:** P2
**Depends on:** Provider system (existing), LLM multi-modal message support (new)

## Summary

Add a built-in Animus tool for image analysis (understanding) and image generation (creation). Presented to the agent as a single unified `image` tool with an `action` parameter (`analyze` | `generate`). Internally, these are two completely separate code paths sharing only the tool interface.

## Provider Capability Matrix (July 7)

### All Animus Providers — Image Support Assessment

| Provider | Analyze (Vision) | Generate | Generation API | Notes |
|---|---|---|---|---|
| **Ollama** (local + Cloud) | ✅ | ✅ | `POST /api/generate` → `"image"` (base64) | Priority 1 for gen. Default `https://ollama.com/v1` |
| **Z.ai** (pay-as-you-go) | ✅ | ✅ | `POST /paas/v4/images/generations` (URL response) | Priority 2. Coding Plan doesn't cover gen. |
| **Z.ai** (Coding Plan) | ✅ | ❌ | — | Split provider for chat only |
| **OpenAI** (API key) | ✅ | ✅ | `POST /v1/images/generations` | Priority 3 |
| **OpenAI Codex** (OAuth) | ✅ (Responses API) | ❌ | — | Codex sub doesn't cover image gen |
| **Cohere** | ✅ (Aya Vision) | ❌ | — | No image gen API |
| **Mistral** (API) | ✅ (Pixtral) | ⚠️ Agent tool | Conversations API → file_id → download | Multi-step flow, not direct REST |
| **Mistral** (Vibe) | ✅ | ❌ | — | Subscription for CLI/IDE, not image gen |

### v1 Generation Providers (3)

Ollama, Z.ai (pay-as-you-go), and OpenAI (API). All others are either incapable or use non-standard flows unsuitable for v1.

---

### Image Analysis (Vision) — Detail

All chat-oriented providers except OpenAI Codex use the OpenAI content-array format. Codex uses the Responses API which has its own multi-modal format.

| Provider | Models with Vision | API Format |
|----------|-------------------|------------|
| Ollama | `llava`, `gemma4`, `minicpm-v`, etc. | OpenAI content-array on `/v1/chat/completions`, or native `images: [base64]` on `/api/chat` |
| Z.ai | GLM-4.6V, GLM-4.5V, GLM-5V-Turbo | OpenAI content-array on `/paas/v4/chat/completions` (both general + coding endpoints) |
| OpenAI | GPT-4o, GPT-5.x | OpenAI content-array on `/v1/chat/completions` |
| OpenAI Codex | GPT-5.x via Responses API | Responses API multi-modal format (different from content-array) |
| Cohere | Aya Vision 8B/32B | Cohere Chat API with `image_url` in content. Our `FetchCapabilities` already detects `vision` feature. |
| Mistral | Pixtral 12B, Mistral Large 3, Medium 3.1, Small 3.2 | OpenAI content-array on Chat Completions API. Max 8 images, 10MB/image. |

**Key insight:** The OpenAI content-array format (`[{type: "text"}, {type: "image_url"}]`) covers Ollama, Z.ai, OpenAI, and Mistral — four of our six vision providers. We implement this one format for v1. Cohere's format is slightly different but Cohere is analysis-only (no generation), so lower priority. Codex's Responses API format is deferred.

**Vision capability detection:**
- Cohere: Already detects `vision` feature from `/v1/models` endpoint. ✅ Done.
- Ollama: `FetchCapabilities` doesn't currently detect vision. Ollama doesn't expose modality in `/v1/models`. Options: query `/api/show` for model details, or maintain a known vision-models list. Needs implementation.
- OpenAI: All GPT-4o+ models support vision. Hardcode `supports_vision = true` for known model families.
- Z.ai: GLM-4V+ models. Query models endpoint or match model name patterns.
- Mistral: Pixtral, Large 3, Medium 3.1+ all support vision. Match model name patterns.

### Image Generation — Detail

Each generation provider has a completely different API:

#### Ollama (Priority 1)
- Endpoint: `POST {base_url}/api/generate` (NOT `/v1/chat/completions`)
- Request: `{model, prompt, stream: false, width, height, steps}`
- Response: NDJSON stream (even with `stream: false`, returns one final JSON object). Final object contains `"image": "base64..."` (singular field, NOT `images` array).
- Save base64 to file.
- Works with both local Ollama and Ollama Cloud (same API, different base_url).
- Default endpoint: `https://ollama.com/v1` (Ollama Cloud).
- Models: `x/z-image-turbo` (Apache 2.0, 6B, photorealistic + bilingual text), `x/flux2-klein` (4B Apache 2.0 / 9B non-commercial).

#### Z.ai / Zhipu (Priority 2)
- Endpoint: `POST {base_url}/paas/v4/images/generations`
- Auth: Bearer token (same API key as chat).
- Request: `{model: "glm-image", prompt, size: "1280x1280", quality: "hd"|"standard"}`
- Response: JSON with image URL. Must download image separately via HTTP GET.
- Models: `glm-image` (flagship, hybrid autoregressive + diffusion, $0.015/image), `cogview-4-250304` ($0.01/image).
- Size constraints: 512–2048px per side, multiples of 32.
- **Billing: pay-as-you-go only.** The GLM Coding Plan subscription (`/api/coding/paas/v4/`) covers chat completions only — image generation is NOT included. Users with a Coding Plan still need pay-as-you-go balance. The endpoint is always `/paas/v4/images/generations` (not `/coding/paas/v4/`).

#### OpenAI (Priority 3)
- Endpoint: `POST {base_url}/v1/images/generations`
- Auth: Bearer token (same API key as chat).
- Request: `{model: "gpt-image-2", prompt, size, quality}`
- Response: JSON with image URL or base64 (configurable with `response_format`).
- Models: `gpt-image-2`, `gpt-image-1.5`, `dall-e-3`.
- **NOT accessible through OpenAI Codex OAuth.** Codex subscription covers text/code only. Image gen requires separate API key with pay-as-you-go billing.

**Key insight:** Image generation has NO common API shape. Each provider needs its own implementation behind the `IImageGenProvider` interface.

### Provider Splits

Two providers should be split to reflect their distinct access tiers:

**Z.ai → `zai` + `zai-coding`**
- `zai`: General/pay-as-you-go. Base URL `https://api.z.ai/api/paas/v4`. Supports both chat + image generation.
- `zai-coding`: Coding Plan subscription. Base URL `https://api.z.ai/api/coding/paas/v4`. Chat only, no image gen.
- Mirrors the existing OpenAI vs OpenAI Codex pattern.

**Mistral** stays as-is for now. The Vibe plan uses a separate API key from the console, not a different endpoint. If we add a `mistral-vibe` provider later, it would follow the same split pattern. Mistral's image generation (agent tool flow) is deferred to v2.

**OpenAI Codex** already exists as a separate provider (`openai-codex`). Image generation simply isn't available through it — no code changes needed, just documentation.

## Architecture

### Unified Tool Interface (Agent-Facing)

```
image(action: "analyze" | "generate", ...)
```

Single tool registered as `image` in the tool registry. The `action` parameter dispatches to two internal handlers:

- `ImageAnalyzeHandler` — vision LLM path
- `ImageGenerateHandler` — generation API path

### Internal: ImageAnalyzeHandler

Reuses the existing LLM provider system. Flow:

1. Agent calls `image:analyze` with image path(s) + prompt
2. Handler loads image(s) from filesystem or URL
3. Handler resizes images to fit constraints (stb_image, header-only)
4. Handler base64-encodes images with MIME type
5. Handler constructs multi-modal LLM request with content-array messages
6. Handler sends through the existing provider system (LLMProviderBase)
7. Handler returns the model's text description

**Multi-modal message support (new):** `LLMMessage` needs to carry multi-modal content. Currently `content` is a plain `std::string`. Add:

```cpp
struct ContentPart {
    std::string type;         // "text" | "image_url"
    std::string text;         // for type="text"
    std::string image_url;    // for type="image_url" — data URI or HTTP URL
    std::string detail;       // optional: "auto" | "low" | "high" (OpenAI)
};

struct LLMMessage {
    std::string role;
    std::string content;                    // existing: plain text (when no parts)
    std::vector<ContentPart> content_parts;  // new: when non-empty, emit as array
    // ... existing fields ...
};
```

When `content_parts` is non-empty, `BuildOpenAIRequestBody` emits the array format instead of the string format for that message. Providers that don't support vision check `supports_vision` and reject with a clear error.

**Vision model resolution:** Check `ProviderCapabilities::supports_vision` on the active provider. If the default model doesn't support vision, the agent can specify a model override. The tool returns a clear error if no vision model is available.

### Internal: ImageGenerateHandler

Separate provider abstraction for image generation:

```cpp
struct ImageGenRequest {
    std::string prompt;
    std::string model;          // provider-specific model ID
    std::string size;           // "1024x1024", "1280x1280", etc.
    std::string quality;        // "standard" | "hd" | "auto"
    int width{0};               // Ollama-specific (0 = model default)
    int height{0};              // Ollama-specific (0 = model default)
    int steps{0};               // Ollama-specific (0 = model default)
    std::string output_format;  // "png" | "jpeg" | "webp"
    std::string filename;       // output filename (optional)
};

struct ImageGenResult {
    std::string image_data;     // base64-encoded image data OR local file path
    std::string image_url;      // provider URL (if applicable, must be downloaded)
    std::string content_type;   // "image/png", etc.
    std::string saved_path;     // where the image was saved locally
};

class IImageGenProvider {
public:
    virtual ~IImageGenProvider() = default;
    virtual ImageGenResult Generate(const ImageGenRequest& req,
                                     std::string* error) = 0;
    virtual std::string Name() const = 0;
    virtual std::vector<std::string> ListModels() const = 0;
};
```

**Three providers, three implementations:**

#### OllamaImageGenProvider
- Endpoint: `POST {base_url}/api/generate` (NOT `/v1/chat/completions`)
- Request: `{model, prompt, stream: false, width, height, steps}`
- Response: NDJSON; final object has `"image": "base64..."` (singular field, NOT `images` array).
- Save base64 to file.
- Works with both local Ollama and Ollama Cloud (same API).
- Models: `x/z-image-turbo` (Apache 2.0, 6B, photorealistic + bilingual text), `x/flux2-klein` (4B Apache 2.0 / 9B non-commercial).
- **Priority: 1** (Melvin's preferred default).

#### ZaiImageGenProvider
- Endpoint: `POST {base_url}/paas/v4/images/generations`
- Auth: Bearer token (same API key as chat).
- Request: `{model: "glm-image", prompt, size: "1280x1280", quality: "hd"|"standard"}`
- Response: JSON with image URL. Must download image separately via HTTP GET.
- Models: `glm-image` (flagship, $0.015/image), `cogview-4-250304` ($0.01/image).
- Size constraints: 512–2048px per side, multiples of 32.
- **Billing: pay-as-you-go only.** The GLM Coding Plan subscription (`/api/coding/paas/v4/`) covers chat completions only — image generation is NOT included in the Coding Plan. Users with a Coding Plan still need pay-as-you-go balance for image generation. The endpoint is always `/paas/v4/images/generations` (not `/coding/paas/v4/`).
- **Priority: 2**.

#### OpenAIImageGenProvider
- Endpoint: `POST {base_url}/v1/images/generations`
- Auth: Bearer token (same API key as chat).
- Request: `{model: "gpt-image-2", prompt, size, quality}`
- Response: JSON with image URL or base64 (configurable with `response_format`).
- Models: `gpt-image-2`, `gpt-image-1.5`, `dall-e-3`.
- **Priority: 3**.

### Image Resize/Sanitize Pipeline

Before sending images to any model (analyze only; generation doesn't need this):

1. **Load:** stb_image loads JPG/PNG/WebP/BMP/PSD/TGA.
2. **Max dimension:** Default 1200px longest side. Configurable.
3. **Max bytes:** Default 10MB raw. Resize iteratively until under limit.
4. **Format:** Re-encode to JPEG if original is exotic (BMP, TGA, PSD).
5. **Quality reduction:** If still over max bytes after dimension resize, reduce JPEG quality in steps (90 → 70 → 50 → 30).
6. **Output:** In-memory base64 string with correct MIME type.

Dependencies: `stb_image.h`, `stb_image_write.h`, `stb_image_resize2.h` — all header-only, no build dependencies. Already used in Lodestone.

## Tool Definition

```json
{
    "name": "image",
    "description": "Analyze images with a vision model or generate images from text prompts.",
    "parameters": [
        {
            "name": "action",
            "type": "string",
            "enum": ["analyze", "generate"],
            "required": true,
            "description": "analyze: describe/analyze one or more images. generate: create an image from a text prompt."
        },
        {
            "name": "images",
            "type": "array",
            "items": {"type": "string"},
            "description": "Image file paths or URLs to analyze (1-20 images). Required for analyze."
        },
        {
            "name": "prompt",
            "type": "string",
            "required": true,
            "description": "For analyze: what to look for or describe. For generate: the image generation prompt."
        },
        {
            "name": "model",
            "type": "string",
            "description": "Model override. For analyze: provider/model for vision LLM. For generate: provider-specific generation model."
        },
        {
            "name": "size",
            "type": "string",
            "description": "Generate only: image dimensions (e.g. '1024x1024', '1280x1280'). Default varies by provider."
        },
        {
            "name": "quality",
            "type": "string",
            "enum": ["auto", "standard", "hd"],
            "description": "Generate only: quality level. Default: auto."
        },
        {
            "name": "maxBytesMb",
            "type": "number",
            "description": "Analyze only: max image size in MB before resize (default 10)."
        },
        {
            "name": "maxImages",
            "type": "integer",
            "description": "Analyze only: max images per call (default 20)."
        },
        {
            "name": "filename",
            "type": "string",
            "description": "Generate only: output filename. Default: generated timestamp-based name."
        }
    ],
    "resultMode": "deliver_to_model"
}
```

## Configuration

```json
{
    "tools": {
        "image": {
            "analysis": {
                "default_model": null,
                "max_dimension_px": 1200,
                "max_bytes": 10485760,
                "max_images": 20
            },
            "generation": {
                "default_provider": "ollama",
                "providers": {
                    "ollama": {
                        "base_url": "https://ollama.com/v1",
                        "default_model": "x/z-image-turbo"
                    },
                    "zai": {
                        "default_model": "glm-image",
                        "default_size": "1280x1280"
                    },
                    "openai": {
                        "default_model": "gpt-image-2",
                        "default_size": "1024x1024"
                    }
                },
                "output_dir": "media/generated"
            }
        }
    }
}
```

Provider API keys for generation reuse the existing provider auth configuration. No separate key storage needed — the image generation handler looks up the provider's API key from the same `providers.json` used for chat.

The Z.ai generation provider's `base_url` must always resolve to the general endpoint (`https://api.z.ai/api/paas/v4`), even if the user has their chat provider configured as `zai-coding` with the coding endpoint (`https://api.z.ai/api/coding/paas/v4`). The generation config has its own `base_url` — it does not inherit from the chat provider config.

## Files to Create/Modify

### New files
- `include/animus_kernel/tools/ImageTool.h` — unified tool handler (dispatches to analyze/generate)
- `src/kernel/tools/ImageTool.cpp` — tool registration, argument parsing, dispatch
- `src/kernel/tools/ImageAnalyze.cpp` — analyze implementation (load, resize, encode, LLM call)
- `src/kernel/tools/ImageGenerate.cpp` — generate implementation (provider dispatch, save)
- `include/animus_kernel/tools/ImageGenProvider.h` — `IImageGenProvider` interface + `ImageGenRequest/Result`
- `src/kernel/tools/image/OllamaImageGenProvider.cpp` — Ollama `/api/generate` image gen
- `src/kernel/tools/image/ZaiImageGenProvider.cpp` — Z.ai `/paas/v4/images/generations`
- `src/kernel/tools/image/OpenAIImageGenProvider.cpp` — OpenAI `/v1/images/generations`
- `include/animus_kernel/tools/ImageResize.h` — stb_image wrapper for load/resize/encode
- `include/animus_kernel/llm/ZaiCodingProvider.h` — new `zai-coding` provider (thin subclass like MistralProvider)

### Modified files
- `include/animus_kernel/llm/LLMTypes.h` — add `ContentPart` struct + `content_parts` to `LLMMessage`
- `include/animus_kernel/llm/LLMProviderBase.h` — add vision capability check method
- `src/kernel/llm/OpenAICompat.cpp` — `BuildOpenAIRequestBody` emits content array when `content_parts` is non-empty
- `src/kernel/llm/OllamaProvider.cpp` — vision support in capability detection
- `src/kernel/llm/ZaiProvider.cpp` — vision support in capability detection
- `src/kernel/llm/OpenAIProvider.cpp` — vision support in capability detection
- `src/kernel/llm/MistralProvider.cpp` — vision support in capability detection (if it has FetchCapabilities; currently inherits from OpenAIProvider)
- `src/kernel/AgentKernel.cpp` — register ImageTool + ZaiCodingProvider
- `src/kernel/admin/ProviderManager.cpp` — register `zai-coding` provider type
- `CMakeLists.txt` — add new source files, stb_image include path

### Vendored headers
- `vendor/stb/stb_image.h`
- `vendor/stb/stb_image_write.h`
- `vendor/stb/stb_image_resize2.h`

## Implementation Order

1. **Multi-modal message support** — `LLMTypes.h` ContentPart, `BuildOpenAIRequestBody` array format. This unblocks the analyze path and is the most structural change.

2. **Vision capability detection** — populate `supports_vision` in Ollama, Z.ai, OpenAI, Mistral providers. Most infrastructure already exists (Cohere already does it).

3. **Z.ai provider split** — add `zai-coding` provider (thin subclass, different base URL). Register in ProviderManager.

4. **Image resize pipeline** — stb_image wrappers. Load, resize, encode to base64. Standalone, no dependencies on the rest.

5. **ImageAnalyzeHandler** — wire up: load images → resize → build multi-modal LLM request → call provider → return text.

6. **IImageGenProvider interface** + **OllamaImageGenProvider** — first generation provider. Ollama Cloud uses the same API as local Ollama.

7. **ImageGenerateHandler** — wire up: parse prompt → dispatch to provider → save result → return path.

8. **ZaiImageGenProvider** — second generation provider.

9. **OpenAIImageGenProvider** — third generation provider.

10. **ImageTool** — unified tool handler that dispatches to analyze/generate based on action parameter.

11. **Registration + config** — wire into AgentKernel, add config schema, admin UI support.

## Scope

### v1 (This ticket)
- [ ] Multi-modal message support in LLM types and request builder
- [ ] Vision capability detection for Ollama, Z.ai, OpenAI, Mistral providers
- [ ] Z.ai provider split (`zai` + `zai-coding`)
- [ ] Image resize/sanitize pipeline (stb_image, header-only)
- [ ] `image:analyze` action — multi-image vision analysis via LLM provider system
- [ ] `image:generate` action — three providers: Ollama (priority), Z.ai, OpenAI
- [ ] Image save to configured output directory
- [ ] Tool registration in AgentKernel
- [ ] Config schema for analysis + generation settings

### v2 (Future)
- [ ] Mistral image generation (agent tool flow via Conversations API)
- [ ] Admin UI for image tool configuration
- [ ] Cohere vision support (Aya Vision content format)
- [ ] OpenAI Codex vision support (Responses API multi-modal format)
- [ ] `mistral-vibe` provider (Vibe subscription, CLI/IDE auth)
- [ ] fal.ai generation provider
- [ ] Google Imagen provider
- [ ] DashScope Wan2.6 image generation (ticket 107 cross-ref — native async task API, NOT OpenAI-compatible)
- [ ] Image editing (mask-based inpainting, style transfer)
- [ ] Transparent background generation
- [ ] Streaming generation progress
- [ ] Image reference support (edit/style-transfer from existing image)
- [ ] Managed media directory with cleanup policy

## Hard-Won Lessons (Predicted)

1. **Vision tokens are expensive.** Always resize before sending. A 4000x3000 photo at base64 is ~15MB of context tokens. Resize to 1200px longest side first.
2. **Base64 encoding inflates size by ~33%.** Account for this in the max_bytes check — the raw image may be under limit but the base64 encoding may push it over.
3. **Model capability detection is essential.** Not all LLM models support images. The tool must check `supports_vision` before sending, and fall back gracefully with a clear error message.
4. **Ollama image generation response uses `"image"` (singular), not `"images"` (plural).** The `images` field in Ollama's API is for *input* images (vision). The `image` field in the response is the *output* image. Don't confuse them.
5. **Z.ai returns a URL, not base64.** Must download the image separately. The URL may be temporary.
6. **Ollama generation works with Ollama Cloud.** Same API endpoint, different base_url. No code differences needed.
7. **stb_image is reliable but not exotic-format-aware.** Handles JPG/PNG/WebP/BMP/TGA/PSD. Some formats (TIFF, HEIC) won't load. Return a clear error for unsupported formats rather than crashing.
8. **Content array format is the universal multi-modal format.** OpenAI defined it, Ollama's OpenAI-compat endpoint accepts it, Z.ai accepts it, Mistral accepts it. One format works for four of our six vision providers.
9. **Z.ai Coding Plan does NOT cover image generation.** The Coding Plan endpoint (`/api/coding/paas/v4/`) is for chat completions only. Image generation is always pay-as-you-go via `/paas/v4/images/generations`. Users with a Coding Plan subscription still need separate pay-as-you-go balance for image generation. Don't route image generation through the `/coding/` path — it will return 429.
10. **OpenAI Codex OAuth does NOT cover image generation.** GPT-Image-2 is accessible only through the standard OpenAI API (pay-as-you-go API key), not through the Codex/backend-api path. Codex subscription covers text/code generation only.
11. **Mistral image generation is NOT a direct REST endpoint.** It's a built-in agent tool accessed through the Conversations API (create agent → enable image_generation tool → converse → get file_id → download via files endpoint). Multi-step flow, deferred to v2.
12. **Cohere has no image generation API.** Aya Vision is for image *understanding* only. Embed v4 is for multimodal *embeddings* (input → vector), not generation. Cohere is an analyze-only provider.

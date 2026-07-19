#pragma once

#include <string>
#include <vector>
#include <functional>

#include "animus_kernel/DiffusionCapabilities.h"

namespace animus::kernel {

// ============================================================================
// Diffusion Provider Interface
// ============================================================================

struct DiffusionModel {
    std::string id;
    std::string type;         // "image" | "video" | "audio" | "3d"
    std::string name;         // display name
    DiffusionCapabilities capabilities{0};
};

struct DiffusionGenRequest {
    std::string prompt;
    std::string model;        // provider-specific model ID (required, agent-selected)
    std::string type;         // "image" | "video" | "audio" | "3d"
    std::string aspect_ratio; // "1:1", "16:9", etc.
    std::string resolution;   // "1K", "2K", "4K" or "720p", "1080p"
    std::string output_format; // "png" | "jpeg" | "webp" (images), "mp3" | "wav" (audio), "glb" (3D)
    int duration{0};          // seconds (video/audio)
    bool sound{false};        // generate audio (video only)

    // Extended (Ticket 124) — optional, provider-specific
    std::string negative_prompt;
    std::string style_preset;   // "3d-model", "anime", "cinematic", etc.
    int64_t seed{0};            // 0 = random
    double strength{0.0};        // image-to-image: 0=identical, 1=ignore input
    double cfg_scale{0.0};       // prompt adherence (0 = provider default)
    std::string input_image_path;  // for image-to-image, 3D, upscale, edit
    std::string action;         // "generate" | "upscale" | "edit" | "control"
    std::string mask_image_path; // for inpaint/edit operations
};

struct DiffusionGenResult {
    bool success{false};
    bool async{false};        // true if polling required
    std::string generation_id; // for async polling
    std::string media_url;    // signed download URL
    std::string saved_path;   // where the file was saved locally
    std::string content_type; // "image/png", "video/mp4", etc.
    int width{0};
    int height{0};
    int duration_seconds{0};
    std::string error;
    std::string file_data;      // raw bytes for direct-binary responses (base64)
    std::string finish_reason;  // "SUCCESS" | "CONTENT_FILTERED"
};

class IDiffusionProvider {
public:
    virtual ~IDiffusionProvider() = default;

    /// Generate media (image sync, video/audio async)
    virtual DiffusionGenResult Generate(const DiffusionGenRequest& req,
                                          std::string* error) = 0;

    /// Poll for async generation status (video/audio)
    /// Returns true if completed, false if still pending
    virtual bool PollStatus(const std::string& generationId,
                            DiffusionGenResult& outResult,
                            std::string* error) = 0;

    /// List available models from this provider
    virtual std::vector<DiffusionModel> ListModels() = 0;

    /// Download generated media from a signed URL or base64 data
    virtual bool DownloadMedia(const std::string& url,
                                const std::string& savePath,
                                std::string* error) = 0;

    /// Provider identifier
    virtual std::string Name() const = 0;

    /// Test connection (e.g. hit the models endpoint)
    virtual bool TestConnection(std::string* error) = 0;

    /// Returns the capabilities supported by this provider
    virtual DiffusionCapabilities GetCapabilities() const = 0;
};

// ============================================================================
// Diffusion Provider Configuration
// ============================================================================

struct DiffusionProviderConfig {
    std::string id;           // unique identifier (e.g. "getimg")
    std::string type;         // "getimg", "stability", etc.
    std::string base_url;     // API base URL
    std::string api_key;
    std::string default_model;
    std::string default_aspect_ratio;
    int64_t created_at{0};
    int64_t updated_at{0};
};

} // namespace animus::kernel
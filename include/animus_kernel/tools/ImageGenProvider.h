#pragma once

#include <string>
#include <vector>

namespace animus::kernel {

// ============================================================================
// Image Generation Provider Interface
// ============================================================================

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
    bool success{false};
    std::string image_data;     // base64-encoded image data (if returned directly)
    std::string image_url;      // provider URL (if applicable — must be downloaded)
    std::string content_type;   // "image/png", etc.
    std::string saved_path;     // where the image was saved locally
    std::string error;          // error message if !success
};

class IImageGenProvider {
public:
    virtual ~IImageGenProvider() = default;
    virtual ImageGenResult Generate(const ImageGenRequest& req,
                                     std::string* error) = 0;
    virtual std::string Name() const = 0;
    virtual std::vector<std::string> ListModels() const = 0;
};

// ============================================================================
// Image Generation Configuration
// ============================================================================

struct ImageGenProviderConfig {
    std::string base_url;
    std::string api_key;
    std::string default_model;
    std::string default_size;
};

} // namespace animus::kernel

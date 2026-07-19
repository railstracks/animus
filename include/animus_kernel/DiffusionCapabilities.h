#pragma once

#include <cstdint>

namespace animus::kernel {

// ============================================================================
// Diffusion Capabilities — bitmask flags for provider feature tracking
// ============================================================================

enum DiffusionCapability : uint32_t {
    ImageGeneration   = 1u << 0,
    VideoGeneration   = 1u << 1,
    AudioGeneration   = 1u << 2,
    Model3DGeneration = 1u << 3,
    Upscale           = 1u << 4,
    ImageEdit         = 1u << 5,
    ImageControl      = 1u << 6,
    NegativePrompt    = 1u << 7,
    StylePreset       = 1u << 8,
    SeedControl       = 1u << 9,
    ImageToImage      = 1u << 10,
    AudioToAudio      = 1u << 11,
};

using DiffusionCapabilities = uint32_t;

inline bool HasCapability(DiffusionCapabilities caps, DiffusionCapability cap) {
    return (caps & static_cast<uint32_t>(cap)) != 0;
}

} // namespace animus::kernel
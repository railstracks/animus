#pragma once

#include <string>
#include <vector>
#include <iostream>

// stb_image — header-only image loading
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_PSD
#define STBI_ONLY_TGA
#define STBI_ONLY_WEBP
#include "stb/stb_image.h"

// stb_image_write — for re-encoding
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

// stb_image_resize2 — for downscaling
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize2.h"

namespace animus::kernel {

// ============================================================================
// ImageResize — load, resize, and base64-encode images for vision LLMs
// ============================================================================

struct EncodedImage {
    std::string data_uri;    // "data:image/jpeg;base64,...."
    std::string mime_type;   // "image/jpeg", "image/png", etc.
    int width{0};
    int height{0};
    size_t raw_bytes{0};     // size of the decoded pixel data
    size_t encoded_bytes{0}; // size of the base64 payload
};

class ImageResize {
public:
    /// Load an image from a file path, resize to fit constraints, and
    /// encode as a base64 data URI suitable for OpenAI content-array format.
    ///
    /// @param filepath Path to image file
    /// @param max_dimension Max dimension in pixels (longest side). 0 = no resize.
    /// @param max_bytes Max encoded size in bytes. Iteratively reduces quality.
    /// @return EncodedImage with data URI, or empty on failure.
    static EncodedImage LoadAndEncode(const std::string& filepath,
                                       int max_dimension = 1200,
                                       size_t max_bytes = 10 * 1024 * 1024) {
        EncodedImage result;

        int w, h, channels;
        unsigned char* data = stbi_load(filepath.c_str(), &w, &h, &channels, 0);
        if (!data) {
            std::cerr << "[image] Failed to load: " << filepath
                      << " — " << stbi_failure_reason() << std::endl;
            return result;
        }

        // Resize if needed
        if (max_dimension > 0 && (w > max_dimension || h > max_dimension)) {
            int new_w = w, new_h = h;
            if (w > h) {
                new_w = max_dimension;
                new_h = static_cast<int>((float)h * (float)max_dimension / (float)w);
            } else {
                new_h = max_dimension;
                new_w = static_cast<int>((float)w * (float)max_dimension / (float)h);
            }

            unsigned char* resized = (unsigned char*)STBI_MALLOC(new_w * new_h * channels);
            if (!resized) {
                stbi_image_free(data);
                std::cerr << "[image] Failed to allocate resize buffer" << std::endl;
                return result;
            }

            unsigned char* ok = stbir_resize_uint8_linear(
                data, w, h, 0,
                resized, new_w, new_h, 0,
                static_cast<stbir_pixel_layout>(channels));

            if (!ok) {
                STBI_FREE(resized);
                stbi_image_free(data);
                std::cerr << "[image] stbir resize failed" << std::endl;
                return result;
            }

            stbi_image_free(data);
            data = resized;
            w = new_w;
            h = new_h;
        }

        result.width = w;
        result.height = h;
        result.raw_bytes = static_cast<size_t>(w) * h * channels;

        result.mime_type = "image/png";
        result.encoded_bytes = 0; // set below

        // Encode as PNG (stb_image_write provides stbi_write_png_to_mem)
        int out_len = 0;
        unsigned char* encoded = stbi_write_png_to_mem(
            data, w * channels, w, h, channels, &out_len);

        STBI_FREE(data);

        if (!encoded || out_len <= 0) {
            std::cerr << "[image] PNG encoding failed for: " << filepath << std::endl;
            return result;
        }

        std::string png_data(reinterpret_cast<const char*>(encoded), out_len);
        STBIW_FREE(encoded);

        result.encoded_bytes = png_data.size();
        result.data_uri = "data:image/png;base64," + Base64Encode(
            reinterpret_cast<const unsigned char*>(png_data.data()),
            png_data.size());

        return result;
    }

private:
    /// Base64 encode raw bytes.
    static std::string Base64Encode(const unsigned char* data, size_t len) {
        static const char kTable[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string out;
        out.reserve((len + 2) / 3 * 4);

        for (size_t i = 0; i < len; i += 3) {
            unsigned int n = data[i] << 16;
            if (i + 1 < len) n |= data[i + 1] << 8;
            if (i + 2 < len) n |= data[i + 2];

            out += kTable[(n >> 18) & 0x3F];
            out += kTable[(n >> 12) & 0x3F];
            out += (i + 1 < len) ? kTable[(n >> 6) & 0x3F] : '=';
            out += (i + 2 < len) ? kTable[n & 0x3F] : '=';
        }

        return out;
    }
};

} // namespace animus::kernel

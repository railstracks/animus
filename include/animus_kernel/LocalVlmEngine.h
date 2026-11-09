#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include "animus_kernel/DescribeService.h"

namespace animus::kernel {

// ============================================================================
// LocalVlmEngine — #160: local-first image description via llama.cpp mtmd
//
// Loads a multimodal (VLM) GGUF pair — text model + mmproj vision
// projector — and answers Describe prompts entirely in-process. Mirrors
// the EmbeddingService discipline: pimpl over llama/mtmd types, one mutex
// serializing all engine access (llama.cpp is not thread-safe), degraded
// mode when the model pair is missing or the build has no mtmd
// (IsAvailable() false; Describe returns loud, actionable errors).
//
// Greedy sampling: descriptions are deterministic, so the sha256+prompt
// cache in DescribeService stays honest (same image + prompt → same text).
//
// Image bytes stay kernel-side: Describe takes the vault object PATH and
// reads it through mtmd's stb-based decoder; bytes never appear in prompt
// text or context.
// ============================================================================

class LocalVlmEngine {
public:
    struct Params {
        std::filesystem::path model_path;   // text-side GGUF
        std::filesystem::path mmproj_path;  // vision projector GGUF
        int n_threads{4};
        int n_ctx{4096};
        int n_batch{512};
        int max_output_tokens{320};
        bool print_timings{false};
    };

    explicit LocalVlmEngine(const Params& params);
    ~LocalVlmEngine();

    LocalVlmEngine(const LocalVlmEngine&) = delete;
    LocalVlmEngine& operator=(const LocalVlmEngine&) = delete;

    // True when the model pair loaded and the mmproj supports vision.
    bool IsAvailable() const;

    // Provenance id (llama model description, e.g. "Gemma3 4B Q8_0"),
    // or empty when unavailable.
    const std::string& ModelId() const;

    // Why the engine is unavailable (missing file, load failure, or
    // build without mtmd). Empty when available.
    const std::string& LoadError() const;

    // Generate a description for one image file. Serialized; the KV cache
    // is cleared per call. Never throws; failures return ok=false + error
    // and never partial text.
    DescribeService::EngineResult Describe(const std::filesystem::path& image,
                                           const std::string& prompt);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::mutex m_mutex;
};

} // namespace animus::kernel

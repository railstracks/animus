#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "animus_kernel/DescribeService.h"
#include "animus_kernel/llm/LLMTypes.h"

namespace animus::kernel {

// ============================================================================
// CloudVlmEngine — #160: opt-in cloud image description
//
// Describes an image through a vision-capable LLM provider via the same
// multimodal request path as ImageTool.analyze (OpenAI content-array:
// text part + image_url data URI). The image is loaded kernel-side,
// resized to a dimension cap, and re-encoded to PNG before upload.
//
// Privacy contract (issue #160): cloud describe is OPT-IN. This engine
// requires an explicitly configured "provider/model_id" and deliberately
// has NO try-all-providers fallback — an image is only ever sent to the
// one destination the operator named. Empty vision_model → loud failure,
// no network, nothing uploaded.
//
// The provider call is an injected seam (CompleteFn): tests fake it;
// the kernel binding resolves the provider through the ChainRunner.
// ============================================================================

class CloudVlmEngine {
public:
    struct Params {
        // REQUIRED, "provider/model_id" — the single permitted
        // destination for image bytes. Empty = cloud engine disabled.
        std::string vision_model;
        int max_dimension{1024};  // resize cap before upload (cost control)
        int max_tokens{320};      // description length cap
    };

    // Performs the provider call for a fully built request.
    // Returns false + error on failure; fills `out` on success.
    using CompleteFn = std::function<bool(const llm::LLMRequest& req,
                                          const std::string& providerId,
                                          llm::LLMResponse& out,
                                          std::string& error)>;

    CloudVlmEngine(Params params, CompleteFn complete);

    // Engine name for DescribeService provenance ("cloud").
    static const char* kEngineName;

    // Generate a description. Never throws; failures return ok=false +
    // error and never partial text. Provenance model = the configured
    // "provider/model_id" string.
    DescribeService::EngineResult Describe(const std::filesystem::path& image,
                                           const std::string& prompt);

private:
    Params m_params;
    CompleteFn m_complete;
};

} // namespace animus::kernel

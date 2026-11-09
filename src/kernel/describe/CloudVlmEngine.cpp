// ============================================================================
// CloudVlmEngine — #160: opt-in cloud image description (implementation)
//
// See CloudVlmEngine.h for the contract. The image path through
// ImageResize::LoadAndEncode mirrors ImageTool.analyze: load, resize to
// the dimension cap, re-encode as PNG data URI, one text part + one
// image part, stream off.
// ============================================================================

#include "animus_kernel/CloudVlmEngine.h"

#include "animus_kernel/tools/ImageResize.h"

namespace animus::kernel {

const char* CloudVlmEngine::kEngineName = "cloud";

CloudVlmEngine::CloudVlmEngine(Params params, CompleteFn complete)
    : m_params(std::move(params)), m_complete(std::move(complete)) {}

DescribeService::EngineResult CloudVlmEngine::Describe(
    const std::filesystem::path& image, const std::string& prompt) {
    DescribeService::EngineResult out;

    // Gate 1: explicit destination required — no fallback, ever.
    if (m_params.vision_model.empty()) {
        out.error =
            "cloud describe model not configured (vision_model is empty)";
        return out;
    }
    const size_t slash = m_params.vision_model.find('/');
    if (slash == std::string::npos || slash == 0 ||
        slash + 1 == m_params.vision_model.size()) {
        out.error = "cloud describe model must be 'provider/model_id': " +
                    m_params.vision_model;
        return out;
    }
    const std::string providerId = m_params.vision_model.substr(0, slash);
    const std::string modelId = m_params.vision_model.substr(slash + 1);

    if (!m_complete) {
        out.error = "cloud describe engine has no provider binding";
        return out;
    }

    // Gate 2: decode + resize + re-encode kernel-side before any upload.
    const EncodedImage enc = ImageResize::LoadAndEncode(
        image.string(), m_params.max_dimension);
    if (enc.data_uri.empty()) {
        out.error = "failed to load image for cloud describe: " +
                image.string();
        return out;
    }

    // Build the multimodal request (ImageTool.analyze shape).
    llm::LLMRequest req;
    req.model = modelId;
    req.stream = false;
    req.max_tokens = m_params.max_tokens;

    llm::LLMMessage userMsg;
    userMsg.role = "user";
    llm::ContentPart textPart;
    textPart.type = "text";
    textPart.text = prompt;
    userMsg.content_parts.push_back(std::move(textPart));

    llm::ContentPart imgPart;
    imgPart.type = "image_url";
    imgPart.image_url = enc.data_uri;
    userMsg.content_parts.push_back(std::move(imgPart));

    req.messages.push_back(std::move(userMsg));

    llm::LLMResponse resp;
    std::string error;
    if (!m_complete(req, providerId, resp, error)) {
        out.error = error.empty() ? "cloud describe provider call failed"
                                  : error;
        return out;
    }
    if (resp.content.empty()) {
        out.error = "cloud describe returned empty content";
        return out;
    }

    out.ok = true;
    out.text = std::move(resp.content);
    out.model = m_params.vision_model;
    return out;
}

} // namespace animus::kernel

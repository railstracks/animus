// ============================================================================
// CloudVlmEngineTests — #160: cloud describe engine contract
//
// The provider call is a fake CompleteFn, so this suite pins the full
// contract without network: explicit-destination gate (never a broadcast),
// malformed config, bad image path, request shape on the happy path
// (prompt text part + data-URI image part + model id), provenance
// passthrough, and loud failure handling.
// ============================================================================

#include "animus_kernel/CloudVlmEngine.h"
#include "animus_kernel/tools/ImageResize.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace animus::kernel;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

// Generate a small valid PNG through the kernel's own stb write path —
// no hand-typed image constants (a from-memory 1x1 PNG proved corrupt;
// stb rejected it as "chunk not known").
std::filesystem::path WriteTempPng() {
    const std::string path = "/tmp/animus_cloud_vlm_test_" +
                             std::to_string(static_cast<long>(getpid())) +
                             ".png";
    constexpr int W = 4, H = 4, C = 3;
    std::vector<unsigned char> pixels(static_cast<size_t>(W) * H * C, 128);
    const int ok =
        stbi_write_png(path.c_str(), W, H, C, pixels.data(), W * C);
    Assert(ok != 0, "test PNG generated via stbi_write_png");
    return std::filesystem::path(path);
}

} // namespace

int main() {
    const auto img = WriteTempPng();

    // ── gate 1: no vision model configured → loud, nothing uploaded ──
    int calls = 0;
    auto fake = [&](const llm::LLMRequest&, const std::string&,
                    llm::LLMResponse& out, std::string&) {
        calls++;
        out.content = "should never happen";
        return true;
    };

    CloudVlmEngine::Params empty;
    CloudVlmEngine off(empty, fake);
    auto r1 = off.Describe(img, "describe");
    Assert(!r1.ok, "unconfigured engine fails");
    Assert(!r1.error.empty(), "unconfigured engine error is loud");
    Assert(r1.error.find("not configured") != std::string::npos,
           "unconfigured error names the cause: " + r1.error);
    Assert(calls == 0, "unconfigured engine never uploads (calls=0)");

    // ── gate 1b: malformed model id (no slash) ──
    CloudVlmEngine::Params bad;
    bad.vision_model = "just-a-model";
    CloudVlmEngine malformed(bad, fake);
    auto r2 = malformed.Describe(img, "describe");
    Assert(!r2.ok && calls == 0, "malformed model id rejected pre-upload");

    // ── gate 2: bad image path → loud, nothing uploaded ──
    CloudVlmEngine::Params ok;
    ok.vision_model = "openai/gpt-4o-mini";
    CloudVlmEngine engine(ok, fake);
    auto r3 = engine.Describe("/nonexistent/img.png", "describe");
    Assert(!r3.ok, "bad image path fails");
    Assert(r3.error.find("failed to load image") != std::string::npos,
           "bad path error names the cause: " + r3.error);
    Assert(calls == 0, "bad image path never uploads");

    // ── happy path: request shape + provenance ──
    std::string seenProvider, seenModel, seenPrompt, seenImage;
    auto capture = [&](const llm::LLMRequest& req, const std::string& pid,
                       llm::LLMResponse& out, std::string&) {
        calls++;
        seenProvider = pid;
        seenModel = req.model;
        if (!req.messages.empty() && req.messages[0].content_parts.size() == 2) {
            seenPrompt = req.messages[0].content_parts[0].text;
            seenImage = req.messages[0].content_parts[1].image_url;
        }
        out.content = "A single transparent pixel.";
        return true;
    };
    CloudVlmEngine good(ok, capture);
    auto r4 = good.Describe(img, "describe this image");
    Assert(r4.ok, "happy path ok: " + r4.error);
    Assert(r4.text == "A single transparent pixel.", "response text extracted");
    Assert(r4.model == "openai/gpt-4o-mini",
           "provenance carries configured provider/model: " + r4.model);
    Assert(seenProvider == "openai", "provider id split correctly");
    Assert(seenModel == "gpt-4o-mini", "model id split correctly");
    Assert(seenPrompt == "describe this image", "prompt in text part");
    Assert(seenImage.rfind("data:image/", 0) == 0,
           "image rides as data URI: " + seenImage.substr(0, 30) + "...");
    Assert(calls == 1, "exactly one provider call");

    // ── provider error → loud passthrough ──
    auto failing = [&](const llm::LLMRequest&, const std::string&,
                       llm::LLMResponse&, std::string& err) {
        err = "provider exploded";
        return false;
    };
    CloudVlmEngine failingEngine(ok, failing);
    auto r5 = failingEngine.Describe(img, "describe");
    Assert(!r5.ok && r5.error == "provider exploded",
           "provider error passes through loudly: " + r5.error);

    // ── empty response content → loud ──
    auto emptyResp = [&](const llm::LLMRequest&, const std::string&,
                         llm::LLMResponse&, std::string&) { return true; };
    CloudVlmEngine emptyEngine(ok, emptyResp);
    auto r6 = emptyEngine.Describe(img, "describe");
    Assert(!r6.ok && r6.error.find("empty content") != std::string::npos,
           "empty provider content fails loudly: " + r6.error);

    std::filesystem::remove(img);
    std::cout << (g_failures == 0 ? "CloudVlmEngineTests: ALL PASSED\n"
                                  : "CloudVlmEngineTests: FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}

// ============================================================================
// LocalVlmEngineTests — #160: degraded-mode behavior of the local VLM engine
//
// CI has no multimodal GGUF pair, so this suite pins the CONTRACT of
// unavailability: missing models → IsAvailable() false with a load error
// that names the missing file; Describe() fails loudly (never crashes,
// never partial text); ModelId() stays empty. The available-model happy
// path is proven live on a host with real GGUFs (same discipline as
// EmbeddingService).
//
// Robust across build flavors: with mtmd, the error names the missing
// file; without mtmd, it says the engine wasn't built. Both are loud —
// the asserts check shape, not the exact string.
// ============================================================================

#include "animus_kernel/LocalVlmEngine.h"

#include <filesystem>
#include <iostream>
#include <string>

using namespace animus::kernel;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

} // namespace

int main() {
    // ── missing model pair → degraded, error names a path ──
    LocalVlmEngine::Params p;
    p.model_path = "/nonexistent/vlm-model.gguf";
    p.mmproj_path = "/nonexistent/vlm-mmproj.gguf";
    LocalVlmEngine missing(p);

    Assert(!missing.IsAvailable(), "missing models -> unavailable");
    Assert(!missing.LoadError().empty(),
           "missing models -> load error present: '" + missing.LoadError() +
               "'");
    Assert(missing.ModelId().empty(), "unavailable -> no model id");

    // ── Describe while degraded → loud failure, no text ──
    const auto r1 = missing.Describe(std::filesystem::path("/any/img.png"),
                                     "describe this");
    Assert(!r1.ok, "degraded describe fails");
    Assert(r1.text.empty(), "degraded describe returns no text");
    Assert(!r1.error.empty(), "degraded describe error is loud");

    // ── Describe with a nonexistent image path while degraded → same loud
    //    failure shape (no crash) ──
    const auto r2 = missing.Describe(std::filesystem::path("/nope/never.png"),
                                     "");
    Assert(!r2.ok && !r2.error.empty(),
           "degraded describe on bad path fails loudly");

    // ── repeated calls stay degraded and stable ──
    const auto r3 = missing.Describe(std::filesystem::path("/any/img.png"),
                                     "again");
    Assert(!r3.ok && !r3.error.empty(), "repeated degraded call still loud");
    Assert(r3.error == r1.error, "degraded error is stable across calls");

    std::cout << (g_failures == 0 ? "LocalVlmEngineTests: ALL PASSED\n"
                                  : "LocalVlmEngineTests: FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}

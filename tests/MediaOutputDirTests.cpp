// ============================================================================
// MediaOutputDirTests — #229
//
// Regression: DiffusionTool's constructor calls EnsureOutputDir() at kernel
// boot (RegisterBuiltinTools). An uncreatable output directory used to throw
// an uncaught std::filesystem::filesystem_error -> std::terminate -> abort()
// -> exit-139 crash loop with zero diagnostics (observed live: volume
// ownership made media/generated EACCES; ggml's terminate handler masked the
// throw site in the backtrace).
//
// Contract after the fix: construction survives a bad output dir, the failure
// is logged, the healthy path still creates the directory, and the tool keeps
// dispatching (graceful per-call errors instead of a kernel kill).
// ============================================================================

#include "animus_kernel/tools/DiffusionTool.h"
#include "animus_kernel/tools/ImageTool.h"
#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/tools/ToolTypes.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace animus::kernel;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        ++g_failures;
    }
}

struct TempDir {
    std::filesystem::path path;

    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count() % 1000000;
        path = std::filesystem::temp_directory_path()
            / ("animus_media_outdir_tests_" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

} // namespace

int main() {
    std::cerr << "Running media output-dir boot-survival tests...\n";

    // ---- Healthy path: construction creates the configured directory ----
    {
        TempDir tmp;
        auto out = tmp.path / "generated";
        HttpClient client;
        DiffusionTool::Config cfg;
        cfg.output_dir = out.string();

        bool survived = true;
        try {
            DiffusionTool tool(client, nullptr, std::move(cfg));
        } catch (...) {
            survived = false;
        }
        Assert(survived, "DiffusionTool construction with healthy output_dir must not throw");
        Assert(std::filesystem::exists(out),
               "Healthy output_dir should be created at construction");

        // And the tool still dispatches (null store -> graceful per-call error)
        DiffusionTool::Config cfgDispatch;
        cfgDispatch.output_dir = out.string();
        DiffusionTool tool(client, nullptr, std::move(cfgDispatch));
        ToolCall call;
        call.id = "c1";
        call.name = "diffusion";
        call.arguments = R"({"action":"list_models"})";
        ToolResult r = tool.Execute(call);
        Assert(!r.success, "list_models with null store must fail gracefully");
        Assert(r.error.find("No diffusion store configured") != std::string::npos,
               "Graceful error expected, got: " + r.error);
    }

    // ---- Boot regression: uncreatable output dir must not kill the kernel ----
    {
        TempDir tmp;
        // A regular FILE where a directory component must be -> create_directories throws
        std::filesystem::path blocker = tmp.path / "blocker";
        { std::ofstream f(blocker); f << "x"; }
        auto bad = blocker / "media" / "generated";

        HttpClient client;
        DiffusionTool::Config cfg;
        cfg.output_dir = bad.string();

        bool survived = true;
        try {
            DiffusionTool tool(client, nullptr, std::move(cfg));
        } catch (const std::exception& e) {
            survived = false;
            std::cerr << "  unexpected exception: " << e.what() << "\n";
        } catch (...) {
            survived = false;
        }
        Assert(survived,
               "#229 regression: DiffusionTool construction with uncreatable "
               "output_dir must not throw (boot crash was exit-139)");
        Assert(!std::filesystem::exists(bad),
               "Uncreatable output_dir must not silently exist after failed creation");

        // The tool must still be dispatchable after the bad-dir boot
        DiffusionTool::Config cfg2;
        cfg2.output_dir = bad.string();
        DiffusionTool tool(client, nullptr, std::move(cfg2));
        ToolCall call;
        call.id = "c2";
        call.name = "diffusion";
        call.arguments = R"({"action":"list_models"})";
        ToolResult r = tool.Execute(call);
        Assert(!r.success, "Dispatch after bad-dir boot must fail gracefully, not crash");
    }

    // ---- ImageTool: same contract (bad generation_output_dir survives) ----
    {
        TempDir tmp;
        std::filesystem::path blocker = tmp.path / "blocker";
        { std::ofstream f(blocker); f << "x"; }
        auto bad = blocker / "media" / "generated";

        HttpClient client;
        ImageTool::Config cfg;
        cfg.generation_output_dir = bad.string();

        bool survived = true;
        try {
            ImageTool tool(client, std::move(cfg), nullptr, nullptr);
        } catch (...) {
            survived = false;
        }
        Assert(survived, "ImageTool construction with uncreatable output_dir must not throw");
    }

    if (g_failures > 0) {
        std::cerr << "\nFAILED: " << g_failures << " assertions failed\n";
        return 1;
    }

    std::cerr << "\nAll media output-dir tests passed!\n";
    return 0;
}
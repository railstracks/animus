// ============================================================================
// DescribeServiceTests — #160: cache-first describe over the media vault
//
// Covers the service core through the injected EngineFn seam (no model,
// no network): miss → engine once + store, hit → engine idle, prompt
// normalization (empty ≡ default, custom prompts are distinct rows),
// loud engine failure (nothing cached, retry re-runs), ref/kind gates,
// provenance fields carried end-to-end, and the Label format.
// ============================================================================

#include "animus_kernel/DescribeService.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/CryptoUtils.h"
#include "animus_kernel/tools/HttpClient.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

using namespace animus::kernel;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_describe_test_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd >= 0) close(fd);
    return std::string(tmp) + ".db";
}

std::filesystem::path MakeTempRoot() {
    char tmp[] = "/tmp/animus_describe_dir_XXXXXX";
    char* dir = mkdtemp(tmp);
    return std::filesystem::path(dir ? tmp : "/tmp/animus_describe_fallback");
}

MediaVault::FetchFn OkFetch() {
    return [](const HttpClient::Request& req,
              const std::vector<std::string>&) {
        HttpClient::Response r;
        r.status_code = 200;
        // Distinct bodies per URL: the vault is content-addressed, so
        // identical bytes would collapse both fixtures into one row
        // (last writer wins on kind) — exactly what tripped this suite
        // before. Distinct bytes → distinct sha256 → two real objects.
        r.body = req.url.find("a.png") != std::string::npos
                     ? std::string("\x89PNG\r\n\x1a\ntest-image-bytes")
                     : std::string("binary-file-bytes");
        r.content_type = "image/png";
        return r;
    };
}

} // namespace

int main() {
    const std::string dbPath = MakeTempDbPath();
    const std::filesystem::path root = MakeTempRoot();

    SqliteDataStore dataStore(dbPath);
    MediaVault vault(&dataStore, OkFetch(), root);
    vault.EnsureSchema();

    // One image + one non-image object in the vault.
    MediaVault::Request imgReq;
    imgReq.url = "https://cdn.example.com/a.png";
    imgReq.kind = "image";
    imgReq.filename = "a.png";
    imgReq.mime = "image/png";
    imgReq.scope_hosts = {"cdn.example.com"};
    const MediaVault::Result img = vault.Acquire(imgReq);
    Assert(img.ok, "vault image acquire ok: " + img.error);

    MediaVault::Request fileReq;
    fileReq.url = "https://cdn.example.com/b.bin";
    fileReq.kind = "file";
    fileReq.filename = "b.bin";
    fileReq.mime = "application/octet-stream";
    fileReq.scope_hosts = {"cdn.example.com"};
    const MediaVault::Result fileObj = vault.Acquire(fileReq);
    Assert(fileObj.ok, "vault file acquire ok: " + fileObj.error);
    Assert(fileObj.sha256 != img.sha256,
           "image and file fixtures are distinct content (content-addressed rows)");

    // Counting engine with request capture.
    int calls = 0;
    std::string failWith;
    std::string lastPrompt, lastSha, lastMime, lastPath;
    auto engine = [&](const DescribeService::EngineRequest& r)
        -> DescribeService::EngineResult {
        calls++;
        lastPrompt = r.prompt;
        lastSha = r.sha256;
        lastMime = r.mime;
        lastPath = r.image_path.string();
        if (!failWith.empty()) {
            return {false, "", "test-model", failWith};
        }
        return {true, "A red square with the word TEST.", "test-model", ""};
    };

    DescribeService svc(&dataStore, &vault, engine, "test-engine");
    svc.EnsureSchema();

    // ── miss → engine runs once, result carries provenance ──
    auto r1 = svc.Describe(img.ref);
    Assert(r1.ok, "first describe ok: " + r1.error);
    Assert(!r1.cached, "first describe is a miss");
    Assert(r1.text == "A red square with the word TEST.", "text passthrough");
    Assert(r1.model == "test-model", "model passthrough");
    Assert(r1.engine == "test-engine", "engine name passthrough");
    Assert(calls == 1, "engine called once (calls=" + std::to_string(calls) + ")");

    // Engine received the resolved request.
    Assert(!lastPrompt.empty(), "engine prompt resolved (never empty)");
    Assert(lastPrompt == DescribeService::kDefaultPrompt,
           "empty prompt normalizes to default");
    Assert(lastSha == img.sha256, "sha passthrough");
    Assert(lastMime == "image/png", "mime passthrough");
    Assert(std::filesystem::exists(lastPath), "engine path exists on disk");

    // ── hit → engine idle ──
    auto r2 = svc.Describe(img.ref);
    Assert(r2.ok && r2.cached, "second describe served from cache");
    Assert(r2.text == r1.text, "cached text identical");
    Assert(calls == 1, "engine NOT called on cache hit");

    // ── explicit default prompt ≡ empty prompt (same row) ──
    auto r3 = svc.Describe(img.ref, DescribeService::kDefaultPrompt);
    Assert(r3.ok && r3.cached, "explicit default prompt hits same row");
    Assert(calls == 1, "no engine call for explicit default");

    // ── custom prompt → distinct row ──
    auto r4 = svc.Describe(img.ref, "Read the text in this chart.");
    Assert(r4.ok && !r4.cached, "custom prompt is a miss");
    Assert(calls == 2, "engine ran for custom prompt");
    Assert(lastPrompt == "Read the text in this chart.", "custom prompt passthrough");
    auto r5 = svc.Describe(img.ref, "Read the text in this chart.");
    Assert(r5.ok && r5.cached, "custom prompt repeat hits cache");
    Assert(calls == 2, "no engine call on custom repeat");

    // ── engine failure: loud, uncached ──
    failWith = "engine exploded";
    auto r6 = svc.Describe("media://" + std::string("deadbeef") +
                           std::string(56, '0'));
    Assert(!r6.ok, "unknown ref fails");
    Assert(r6.error.find("unknown media ref") != std::string::npos,
           "unknown ref error names the problem: " + r6.error);
    Assert(calls == 2, "unknown ref never reaches the engine");

    auto r7 = svc.Describe(fileObj.ref);
    Assert(!r7.ok, "non-image ref rejected");
    Assert(r7.error.find("not an image") != std::string::npos,
           "kind gate error: " + r7.error);
    Assert(calls == 2, "kind gate never reaches the engine");

    // A real engine failure on a fresh prompt: nothing cached, retry works.
    auto r8 = svc.Describe(img.ref, "Different question entirely.");
    Assert(!r8.ok, "engine failure surfaces");
    Assert(r8.error == "engine exploded", "engine error passthrough: " + r8.error);
    failWith.clear();
    auto r9 = svc.Describe(img.ref, "Different question entirely.");
    Assert(r9.ok && !r9.cached, "retry after failure re-runs engine");
    Assert(calls == 4, "engine ran for failure + retry");

    // ── provenance label ──
    const std::string label = DescribeService::Label(r2);
    Assert(label.find("[machine description | engine=test-engine "
                      "model=test-model]") != std::string::npos,
           "label format: " + label);
    Assert(label.find("(cached)") != std::string::npos,
           "cached flag in label: " + label);
    Assert(label.find(r2.text) != std::string::npos, "label carries text");

    // ── two services, one table (kernel pattern: local + cloud) ──
    // The first writer wins the row; the other service cache-hits and
    // reports the ROW's provenance, not its own engine name — cache
    // honesty across engines sharing one media_descriptions table.
    {
        DescribeService::EngineFn engA =
            [](const DescribeService::EngineRequest&) {
                return DescribeService::EngineResult{
                    true, "answer from engine A", "model-a", ""};
            };
        DescribeService::EngineFn engB =
            [](const DescribeService::EngineRequest&) {
                return DescribeService::EngineResult{
                    true, "answer from engine B", "model-b", ""};
            };
        DescribeService svcA(&dataStore, &vault, engA, "engine-a");
        DescribeService svcB(&dataStore, &vault, engB, "engine-b");
        svcA.EnsureSchema();
        svcB.EnsureSchema();
        const std::string q = "Two-service shared cache question.";
        auto ra = svcA.Describe(img.ref, q);
        Assert(ra.ok && !ra.cached && ra.engine == "engine-a",
               "svcA writes the shared row");
        auto rb = svcB.Describe(img.ref, q);
        Assert(rb.ok && rb.cached, "svcB cache-hits svcA's row");
        Assert(rb.engine == "engine-a" && rb.model == "model-a",
               "provenance comes from the row, not the bound engine");
        Assert(rb.text == "answer from engine A", "row text served");
    }

    // ── prompt hash determinism ──
    Assert(DescribeService::PromptHash("x") == DescribeService::PromptHash("x"),
           "prompt hash deterministic");
    Assert(DescribeService::PromptHash("x") != DescribeService::PromptHash("y"),
           "prompt hash distinguishes prompts");

    std::cout << (g_failures == 0 ? "DescribeServiceTests: ALL PASSED\n"
                                  : "DescribeServiceTests: FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}

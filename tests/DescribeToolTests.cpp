// ============================================================================
// DescribeToolTests — #160 active layer: describe_image(ref, prompt?)
//
// Tool surface over DescribeService: ref/prompt parsing, ALWAYS-labeled
// provenance output, engine routing (local default; cloud honored only
// when configured — loud otherwise), loud errors for missing/unknown/
// non-image refs, cache marker on repeat questions. No model, no network:
// the engine seam is faked exactly like DescribeServiceTests.
// ============================================================================

#include "animus_kernel/tools/DescribeTool.h"
#include "animus_kernel/DescribeService.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/tools/HttpClient.h"

#include <filesystem>
#include <iostream>
#include <memory>
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
    char tmp[] = "/tmp/animus_describe_tool_test_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd >= 0) close(fd);
    return std::string(tmp) + ".db";
}

std::filesystem::path MakeTempRoot() {
    char tmp[] = "/tmp/animus_describe_tool_dir_XXXXXX";
    char* dir = mkdtemp(tmp);
    return std::filesystem::path(dir ? tmp : "/tmp/animus_dtfallback");
}

MediaVault::FetchFn OkFetch() {
    return [](const HttpClient::Request& req,
              const std::vector<std::string>&) {
        HttpClient::Response r;
        r.status_code = 200;
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

    // Prompt capture shared with both fake engines.
    auto lastPrompt = std::make_shared<std::string>();
    auto promptSink = lastPrompt;

    DescribeService localSvc(
        &dataStore, &vault,
        [promptSink](const DescribeService::EngineRequest& r) {
            *promptSink = r.prompt;
            return DescribeService::EngineResult{
                true, "A red square with the word TEST.", "local-model", ""};
        },
        "local");
    DescribeService cloudSvc(
        &dataStore, &vault,
        [](const DescribeService::EngineRequest&) {
            return DescribeService::EngineResult{
                true, "cloud vision answer", "cloud-model", ""};
        },
        "cloud-prov");
    localSvc.EnsureSchema();
    cloudSvc.EnsureSchema();

    DescribeTool tool(&localSvc);                    // cloud NOT configured
    DescribeTool dualTool(&localSvc, &cloudSvc);     // cloud configured

    ToolCall call;
    call.id = "t1";
    call.name = "describe_image";

    // ── default prompt → labeled output ──
    call.arguments = "{\"ref\":\"" + img.ref + "\"}";
    auto r1 = tool.Execute(call);
    Assert(r1.success, "default describe ok: " + r1.error);
    Assert(r1.output.find("[machine description") != std::string::npos &&
               r1.output.find("engine=local") != std::string::npos &&
               r1.output.find("model=local-model") != std::string::npos,
           "provenance label in output: " + r1.output);
    Assert(r1.output.find("A red square with the word TEST.") !=
               std::string::npos,
           "description text in output");
    Assert(*lastPrompt == DescribeService::kDefaultPrompt,
           "empty prompt normalized to default");

    // ── custom prompt: passthrough + distinct cache row ──
    call.arguments = "{\"ref\":\"" + img.ref + "\","
                     "\"prompt\":\"Read the text in this chart.\"}";
    auto r2 = tool.Execute(call);
    Assert(r2.success && *lastPrompt == "Read the text in this chart.",
           "custom prompt passthrough");
    Assert(r2.output.find("(cached)") == std::string::npos,
           "fresh prompt is a miss (no cached marker)");

    // ── repeat → cached marker ──
    auto r3 = tool.Execute(call);
    Assert(r3.success && r3.output.find("(cached)") != std::string::npos,
           "repeat question served from cache: " + r3.output);

    // ── missing ref ──
    call.arguments = "{\"prompt\":\"x\"}";
    auto r4 = tool.Execute(call);
    Assert(!r4.success && r4.error.find("ref") != std::string::npos,
           "missing ref fails loud: " + r4.error);

    // ── unparseable arguments ──
    call.arguments = "not json";
    auto r5 = tool.Execute(call);
    Assert(!r5.success, "unparseable args fail loud");

    // ── unknown ref ──
    call.arguments = "{\"ref\":\"media://" + std::string(64, '0') + "\"}";
    auto r6 = tool.Execute(call);
    Assert(!r6.success && r6.error.find("unknown media ref") !=
                              std::string::npos,
           "unknown ref error passthrough: " + r6.error);

    // ── non-image ref ──
    call.arguments = "{\"ref\":\"" + fileObj.ref + "\"}";
    auto r7 = tool.Execute(call);
    Assert(!r7.success && r7.error.find("not an image") != std::string::npos,
           "non-image ref rejected: " + r7.error);

    // ── engine=cloud without configuration → loud, no fallback run ──
    call.arguments = "{\"ref\":\"" + img.ref + "\",\"engine\":\"cloud\"}";
    auto r8 = tool.Execute(call);
    Assert(!r8.success && r8.error.find("not configured") !=
                              std::string::npos,
           "unconfigured cloud fails loud: " + r8.error);

    // ── engine=cloud with configuration → routes to the cloud service ──
    // (fresh prompt: the default row was already written by local —
    // first writer wins the cache row, so a fresh question is what
    // actually exercises the cloud engine.)
    call.arguments = "{\"ref\":\"" + img.ref + "\","
                     "\"engine\":\"cloud\",\"prompt\":\"cloud only\"}";
    auto r9 = dualTool.Execute(call);
    Assert(r9.success && r9.output.find("engine=cloud-prov") !=
                              std::string::npos,
           "cloud engine routing: " + r9.output);
    Assert(r9.output.find("cloud vision answer") != std::string::npos,
           "cloud answer served");

    // ── invalid engine ──
    call.arguments = "{\"ref\":\"" + img.ref + "\",\"engine\":\"quantum\"}";
    auto r10 = tool.Execute(call);
    Assert(!r10.success && r10.error.find("Invalid engine") !=
                                std::string::npos,
           "invalid engine fails loud: " + r10.error);

    // ── no service bound at all ──
    DescribeTool deadTool(nullptr);
    call.arguments = "{\"ref\":\"" + img.ref + "\"}";
    auto r11 = deadTool.Execute(call);
    Assert(!r11.success && r11.error.find("unavailable") !=
                                std::string::npos,
           "null service fails loud: " + r11.error);

    std::cout << (g_failures == 0 ? "DescribeToolTests: ALL PASSED\n"
                                  : "DescribeToolTests: FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}

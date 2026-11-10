// ============================================================================
// MediaVaultTests — #158: content-addressed inbound media vault
//
// Covers the Acquire pipeline through the injected FetchFn seam (no
// network): scope gate (deny = fetcher never runs), per-type caps (loud
// rejection, never truncation), dedupe by stable platform id (no second
// fetch), content addressing (same bytes → same object), Resolve/
// ByDedupeKey read paths, and KindFromMime normalization.
// ============================================================================

#include "animus_kernel/MediaVault.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/CryptoUtils.h"
#include "animus_kernel/tools/HttpClient.h"

#include <filesystem>
#include <fstream>
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
    char tmp[] = "/tmp/animus_mediavault_test_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd >= 0) close(fd);
    return std::string(tmp) + ".db";
}

std::filesystem::path MakeTempRoot() {
    char tmp[] = "/tmp/animus_mediavault_dir_XXXXXX";
    char* dir = mkdtemp(tmp);
    return std::filesystem::path(dir ? tmp : "/tmp/animus_mediavault_fallback");
}

MediaVault::FetchFn OkFetch(const std::string& body,
                            const std::string& contentType,
                            int status,
                            bool* called = nullptr) {
    return [body, contentType, status, called](
               const HttpClient::Request&,
               const std::vector<std::string>&) {
        if (called) *called = true;
        HttpClient::Response r;
        r.status_code = status;
        r.body = body;
        r.content_type = contentType;
        return r;
    };
}

MediaVault::Request MakeRequest(const std::string& url,
                                 const std::string& kind) {
    MediaVault::Request req;
    req.url = url;
    req.kind = kind;
    req.filename = "photo.png";
    req.mime = "image/png";
    req.scope_hosts = {"cdn.example.com"};
    return req;
}

// ── Happy path: fetch → sha256 address → file on disk → Resolve ──────────
void TestAcquireHappyPath() {
    std::cerr << "  [MediaVault] acquire happy path...\n";
    const auto dbPath = MakeTempDbPath();
    const auto root = MakeTempRoot();
    {
        SqliteDataStore dataStore(dbPath);
        const std::string body = "hello world";
        MediaVault vault(&dataStore, OkFetch(body, "image/png", 200), root);
        vault.EnsureSchema();

        MediaVault::Result r = vault.Acquire(MakeRequest(
            "https://cdn.example.com/photo.png", "image"));
        Assert(r.ok, "acquire ok (got: " + r.error + ")");
        Assert(!r.deduped, "first acquire is not a dedupe hit");
        Assert(r.sha256 == crypto::Sha256Hex(body),
               "sha256 content addressing");
        Assert(r.ref == "media://" + r.sha256, "ref form media://<sha256>");
        Assert(r.size == body.size(), "size surfaced");
        Assert(r.mime == "image/png", "mime surfaced");
        Assert(std::filesystem::exists(vault.PathFor(r.sha256)),
               "object file exists at content address");
        std::ifstream f(vault.PathFor(r.sha256), std::ios::binary);
        std::string onDisk((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
        Assert(onDisk == body, "bytes on disk match fetched body");

        // Resolve round-trip
        auto rec = vault.Resolve(r.ref);
        Assert(rec.has_value(), "Resolve by ref");
        Assert(rec && rec->filename == "photo.png", "record filename");
        Assert(rec && rec->mime == "image/png", "record mime");
        Assert(rec && rec->source_host == "cdn.example.com",
               "record source host");
        Assert(rec && rec->size == body.size(), "record size");
        unlink(dbPath.c_str());
        std::filesystem::remove_all(root);
    }
}

// ── Scope gate: unlisted host denied WITHOUT running the fetcher ──────────
void TestScopeDeny() {
    std::cerr << "  [MediaVault] egress scope deny (no fetch)...\n";
    const auto dbPath = MakeTempDbPath();
    const auto root = MakeTempRoot();
    {
        SqliteDataStore dataStore(dbPath);
        bool called = false;
        MediaVault vault(&dataStore, OkFetch("x", "image/png", 200, &called),
                         root);
        vault.EnsureSchema();

        auto req = MakeRequest("https://evil.example.net/photo.png", "image");
        MediaVault::Result r = vault.Acquire(req);
        Assert(!r.ok, "out-of-scope acquire fails");
        Assert(r.error.find("egress denied") != std::string::npos,
               "deny names the gate: " + r.error);
        Assert(!called, "fetcher NEVER runs on scope deny");
        unlink(dbPath.c_str());
        std::filesystem::remove_all(root);
    }
}

// ── Cap: oversized body rejected loudly, nothing stored ───────────────────
void TestCapExceeded() {
    std::cerr << "  [MediaVault] size cap (loud reject)...\n";
    const auto dbPath = MakeTempDbPath();
    const auto root = MakeTempRoot();
    {
        SqliteDataStore dataStore(dbPath);
        MediaVault vault(&dataStore, OkFetch("12345", "image/png", 200), root);
        vault.EnsureSchema();
        MediaVault::Caps caps;
        caps.image = 4;  // body is 5 bytes
        vault.set_caps(caps);

        MediaVault::Result r = vault.Acquire(MakeRequest(
            "https://cdn.example.com/photo.png", "image"));
        Assert(!r.ok, "over-cap acquire fails");
        Assert(r.error.find("size cap exceeded") != std::string::npos,
               "error names the cap: " + r.error);
        Assert(r.error.find("5 bytes") != std::string::npos &&
                   r.error.find("> 4") != std::string::npos,
               "error carries byte counts both sides");
        Assert(r.sha256.empty(), "nothing stored on reject");
        Assert(vault.Resolve("media://" + crypto::Sha256Hex("12345"))
                   .has_value() == false,
               "rejected content unresolvable");
        unlink(dbPath.c_str());
        std::filesystem::remove_all(root);
    }
}

// ── Dedupe: stable platform id resolves from the vault, zero network ──────
void TestDedupeNoFetch() {
    std::cerr << "  [MediaVault] dedupe by platform id (no second fetch)...\n";
    const auto dbPath = MakeTempDbPath();
    const auto root = MakeTempRoot();
    {
        SqliteDataStore dataStore(dbPath);
        bool called = false;
        const std::string body = "dedupe-me";
        MediaVault vault(&dataStore, OkFetch(body, "image/png", 200, &called),
                         root);
        vault.EnsureSchema();

        auto req = MakeRequest("https://cdn.example.com/photo.png", "image");
        req.dedupe_key = "discord:987";
        MediaVault::Result first = vault.Acquire(req);
        Assert(first.ok && !first.deduped, "first acquire downloads");
        Assert(called, "first acquire ran the fetcher");
        called = false;

        // Re-across a NEW vault instance on the SAME store: the dedupe table
        // is durable state, not instance cache.
        MediaVault vault2(&dataStore,
                          OkFetch("SHOULD-NOT-RUN", "image/png", 200, &called),
                          root);
        vault2.EnsureSchema();
        MediaVault::Result second = vault2.Acquire(req);
        Assert(second.ok, "second acquire ok");
        Assert(second.deduped, "second acquire deduped");
        Assert(!called, "dedupe hit runs ZERO network");
        Assert(second.sha256 == first.sha256,
               "dedupe resolves to the same object");

        auto rec = vault2.ByDedupeKey("discord:987");
        Assert(rec.has_value(), "ByDedupeKey reads the stable id");
        Assert(rec && rec->sha256 == first.sha256,
               "ByDedupeKey -> same object");
        unlink(dbPath.c_str());
        std::filesystem::remove_all(root);
    }
}

// ── Failure modes: HTTP error status, empty body, fetch-layer error ────────
void TestFailureModes() {
    std::cerr << "  [MediaVault] failure modes...\n";
    const auto dbPath = MakeTempDbPath();
    const auto root = MakeTempRoot();
    {
        SqliteDataStore dataStore(dbPath);
        MediaVault vault(&dataStore,
                          OkFetch("", "image/png", 404), root);
        vault.EnsureSchema();
        MediaVault::Result r = vault.Acquire(MakeRequest(
            "https://cdn.example.com/photo.png", "image"));
        Assert(!r.ok, "404 fails");
        Assert(r.error.find("HTTP 404") != std::string::npos,
               "error names the status");

        MediaVault vaultEmpty(&dataStore,
                              OkFetch("", "image/png", 200), root);
        r = vaultEmpty.Acquire(MakeRequest(
            "https://cdn.example.com/photo.png", "image"));
        Assert(!r.ok, "empty body fails");
        Assert(r.error.find("empty body") != std::string::npos,
               "empty body named");

        MediaVault vaultErr(
            &dataStore,
            [](const HttpClient::Request&, const std::vector<std::string>&) {
                HttpClient::Response resp;
                resp.status_code = 0;
                resp.error = "connection refused";
                return resp;
            },
            root);
        vaultErr.EnsureSchema();
        r = vaultErr.Acquire(MakeRequest(
            "https://cdn.example.com/photo.png", "image"));
        Assert(!r.ok, "fetch-layer error fails");
        Assert(r.error.find("connection refused") != std::string::npos,
               "fetch error propagated");
        unlink(dbPath.c_str());
        std::filesystem::remove_all(root);
    }
}

// ── Content collision: same bytes, different platform ids → one object ────
void TestContentCollision() {
    std::cerr << "  [MediaVault] content collision collapses...\n";
    const auto dbPath = MakeTempDbPath();
    const auto root = MakeTempRoot();
    {
        SqliteDataStore dataStore(dbPath);
        const std::string body = "identical-bytes";
        MediaVault vault(&dataStore, OkFetch(body, "image/png", 200), root);
        vault.EnsureSchema();

        auto a = MakeRequest("https://cdn.example.com/a.png", "image");
        a.dedupe_key = "discord:1";
        auto b = MakeRequest("https://cdn.example.com/b.png", "image");
        b.dedupe_key = "discord:2";
        const MediaVault::Result ra = vault.Acquire(a);
        const MediaVault::Result rb = vault.Acquire(b);
        Assert(ra.ok && rb.ok, "both acquisitions ok");
        Assert(ra.sha256 == rb.sha256,
               "identical bytes -> identical object");
        Assert(ra.ref == rb.ref, "identical refs");
        auto recA = vault.ByDedupeKey("discord:1");
        auto recB = vault.ByDedupeKey("discord:2");
        Assert(recA && recB && recA->sha256 == recB->sha256,
               "both ids resolve to the one object");
        unlink(dbPath.c_str());
        std::filesystem::remove_all(root);
    }
}

// ── KindFromMime normalization ─────────────────────────────────────────────
void TestKindFromMime() {
    std::cerr << "  [MediaVault] KindFromMime...\n";
    Assert(MediaVault::KindFromMime("image/png") == "image", "image/png");
    Assert(MediaVault::KindFromMime("video/mp4") == "video", "video/mp4");
    Assert(MediaVault::KindFromMime("audio/ogg") == "audio", "audio/ogg");
    Assert(MediaVault::KindFromMime("application/pdf") == "file",
           "application/pdf -> file");
    Assert(MediaVault::KindFromMime("") == "file", "empty -> file");
    Assert(MediaVault::KindFromMime("garbage") == "file",
           "no-slash -> file");
}

} // namespace

int main() {
    std::cerr << "[MediaVaultTests]\n";
    TestAcquireHappyPath();
    TestScopeDeny();
    TestCapExceeded();
    TestDedupeNoFetch();
    TestFailureModes();
    TestContentCollision();
    TestKindFromMime();
    if (g_failures == 0) {
        std::cerr << "  ALL PASSED\n";
        return 0;
    }
    std::cerr << "  " << g_failures << " FAILURES\n";
    return 1;
}

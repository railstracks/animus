#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "animus_kernel/IDataStore.h"
#include "animus_kernel/tools/HttpClient.h"

namespace animus::kernel {

// ============================================================================
// MediaVault — #158: content-addressed inbound media vault
//
// Channel attachments arrive as URL + metadata; the vault downloads them
// through the #25 egress scope rules, stores the bytes sha256-addressed on
// disk (never in the DB, never in agent context), enforces per-type size
// caps with loud rejection (no silent truncation), and hands back ref
// descriptors (media://<sha256>) for the channel-context card.
//
// Dedupe: platform attachment ids are stable across refeeds (Discord
// attachment ids, Bluesky blob cids, Mastodon media ids, Slack file ids) —
// a dedupe-key hit resolves straight to the stored object with zero
// network traffic. Content collisions (same bytes, different ids) collapse
// to the same object by construction.
//
// Fetch seam: the kernel wires the fetcher to a DEDICATED HttpClient
// instance (the shared WebClient's body cap and allowlist belong to the
// web tools — media caps are per-request here and must not leak into
// them). Tests inject a lambda; no network, no mocking of libcurl.
// ============================================================================

class MediaVault {
public:
    // The fetcher owns redirect policy: the kernel binds
    // ApiRuntime::ExecuteScoped, which scope-checks EVERY hop before curl
    // sees it and strips credentials across hosts (#25 semantics). The
    // vault adds the media-scoped policy layer on top (clear errors, caps).
    using FetchFn = std::function<HttpClient::Response(
        const HttpClient::Request&, const std::vector<std::string>& scopeHosts)>;

    // Per-type byte caps. Defaults are deliberately modest; operators tune
    // via config at the wiring site, not here.
    struct Caps {
        std::uint64_t image{10ull * 1024 * 1024};
        std::uint64_t video{50ull * 1024 * 1024};
        std::uint64_t audio{20ull * 1024 * 1024};
        std::uint64_t file{25ull * 1024 * 1024};
    };

    // One attachment acquisition request. `kind` ∈ image|video|audio|file
    // (anything else is treated as file). `scope_hosts` is the channel's
    // egress allowlist (#25 semantics: deny unless listed).
    struct Request {
        std::string url;
        std::string kind{"file"};
        std::string filename;
        std::string mime;          // platform-declared content type
        std::string dedupe_key;    // stable platform attachment id ("" = none)
        std::string alt_text;      // platform alt text when present
        std::string dimensions;    // "WxH" when the platform provides it
        std::string duration;      // seconds (string) when provided
        std::vector<std::string> scope_hosts;
        // Download auth when the platform CDN requires it (e.g. Slack's
        // url_private needs the bot token). Resolved kernel-side from
        // channel config at vault time — NEVER travels through dispatch
        // metadata, the arrivals table, or the context card.
        std::map<std::string, std::string> headers;
    };

    struct Result {
        bool ok{false};
        bool deduped{false};       // resolved from the vault — no download ran
        std::string error;         // loud failure reason when !ok
        std::string sha256;
        std::string ref;           // media://<sha256>
        std::string filename;
        std::string mime;
        std::uint64_t size{0};
    };

    // A stored object, for card rendering and tool-side fetch paths.
    struct Record {
        std::string sha256;
        std::string kind;
        std::string mime;
        std::string filename;
        std::string alt_text;
        std::string dimensions;
        std::string duration;
        std::string source_host;
        std::filesystem::path path;
        std::uint64_t size{0};
    };

    MediaVault(IDataStore* store, FetchFn fetch, const std::filesystem::path& root);

    void EnsureSchema();

    // The whole pipeline: scope gate → dedupe lookup → fetch → cap check →
    // sha256 → atomic file write → index upsert. Returns a ref descriptor
    // or a loud error; NEVER bytes.
    Result Acquire(const Request& req);

    // ref ("media://<sha256>" or bare hash) → record, if present and intact.
    std::optional<Record> Resolve(const std::string& ref);

    // Stable platform attachment id → record (refeed path, no download).
    std::optional<Record> ByDedupeKey(const std::string& dedupeKey);

    std::uint64_t CapFor(const std::string& kind) const;
    // "image/png" → "image" (the cap kinds); anything unrecognised →
    // "file". Shared by every inbound emitter so kind derives from the
    // platform-declared content type, never filename guesses.
    static std::string KindFromMime(const std::string& mime);
    const Caps& caps() const { return m_caps; }
    void set_caps(const Caps& c) { m_caps = c; }

    // Absolute path a given sha256 would live at (tests + tool fetch).
    std::filesystem::path PathFor(const std::string& sha256) const;

private:
    std::optional<Record> LoadBySha(const std::string& sha256);
    bool ObjectFileIntact(const Record& rec) const;

    IDataStore* m_store;
    FetchFn m_fetch;
    std::filesystem::path m_root;
    Caps m_caps;
};

} // namespace animus::kernel

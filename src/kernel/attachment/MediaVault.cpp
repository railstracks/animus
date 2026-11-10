#include "animus_kernel/MediaVault.h"

#include "animus_kernel/Log.h"
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/CryptoUtils.h"
#include "animus_kernel/api/ApiRuntime.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <set>

namespace animus::kernel {

namespace {

std::string NormalizeKind(const std::string& kind) {
    std::string k = kind;
    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return std::tolower(c); });
    static const std::set<std::string> known = {"image", "video", "audio", "file"};
    return known.count(k) ? k : "file";
}

// Strip media-type parameters ("image/png; charset=..." -> "image/png") and
// surrounding whitespace.
std::string CleanMime(const std::string& mime) {
    std::string m = mime;
    auto semi = m.find(';');
    if (semi != std::string::npos) m = m.substr(0, semi);
    // trim
    while (!m.empty() && (m.front() == ' ' || m.front() == '\t')) m.erase(0, 1);
    while (!m.empty() && (m.back() == ' ' || m.back() == '\t')) m.pop_back();
    std::transform(m.begin(), m.end(), m.begin(), [](unsigned char c) { return std::tolower(c); });
    return m;
}

// Best-effort filename from a URL's last path segment; empty when nothing
// sensible is there (caller falls back to the hash).
std::string FilenameFromUrl(const std::string& url) {
    auto q = url.find('?');
    std::string path = (q == std::string::npos) ? url : url.substr(0, q);
    auto slash = path.find_last_of('/');
    std::string seg = (slash == std::string::npos) ? path : path.substr(slash + 1);
    if (seg.empty() || seg == "." || seg == "..") return {};
    return seg;
}

bool IsHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) {
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!ok) return false;
    }
    return true;
}

std::int64_t NowUnix() {
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace

MediaVault::MediaVault(IDataStore* store, FetchFn fetch, const std::filesystem::path& root)
    : m_store(store), m_fetch(std::move(fetch)), m_root(root) {}

void MediaVault::EnsureSchema() {
    if (!m_store) return;

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS media_objects (
            sha256 TEXT PRIMARY KEY,
            kind TEXT NOT NULL DEFAULT 'file',
            mime_type TEXT NOT NULL DEFAULT '',
            filename TEXT NOT NULL DEFAULT '',
            size_bytes INTEGER NOT NULL DEFAULT 0,
            source_host TEXT NOT NULL DEFAULT '',
            alt_text TEXT NOT NULL DEFAULT '',
            dimensions TEXT NOT NULL DEFAULT '',
            duration TEXT NOT NULL DEFAULT '',
            created_at INTEGER NOT NULL DEFAULT 0
        );
    )");

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS media_dedupe (
            dedupe_key TEXT PRIMARY KEY,
            sha256 TEXT NOT NULL
        );
    )");

    m_store->Exec("CREATE INDEX IF NOT EXISTS idx_media_dedupe_sha ON media_dedupe(sha256)");
}

std::uint64_t MediaVault::CapFor(const std::string& kind) const {
    const std::string k = NormalizeKind(kind);
    if (k == "image") return m_caps.image;
    if (k == "video") return m_caps.video;
    if (k == "audio") return m_caps.audio;
    return m_caps.file;
}

std::filesystem::path MediaVault::PathFor(const std::string& sha256) const {
    if (sha256.size() < 2) return m_root / "media" / "_";
    return m_root / "media" / sha256.substr(0, 2) / sha256;
}

std::string MediaVault::KindFromMime(const std::string& mime) {
    const size_t slash = mime.find('/');
    if (slash == std::string::npos) return "file";
    const std::string top = mime.substr(0, slash);
    if (top == "image" || top == "video" || top == "audio") return top;
    return "file";
}

MediaVault::Result MediaVault::Acquire(const Request& req) {
    Result out;
    const std::string kind = NormalizeKind(req.kind);

    if (req.url.empty()) {
        out.error = "media url is empty";
        return out;
    }

    // ---- Scope gate (#25 semantics: deny unless listed) -------------------
    std::string host;
    if (!ApiRuntime::EgressAllowed(req.scope_hosts, req.url, host)) {
        ALOG_WARNING("mediavault", "[egress] DENIED inbound-media download -> "
                     << (host.empty() ? "<malformed url>" : host)
                     << " (host not in this channel's media egress scope)");
        out.error = "egress denied: host '" + (host.empty() ? "<malformed>" : host) +
                    "' is not in this channel's media egress scope";
        return out;
    }

    // ---- Dedupe by stable platform attachment id --------------------------
    if (!req.dedupe_key.empty()) {
        auto rec = ByDedupeKey(req.dedupe_key);
        if (rec) {
            out.ok = true;
            out.deduped = true;
            out.sha256 = rec->sha256;
            out.ref = "media://" + rec->sha256;
            out.filename = rec->filename;
            out.mime = rec->mime;
            out.size = rec->size;
            return out;
        }
    }

    // ---- Fetch -------------------------------------------------------------
    if (!m_fetch) {
        out.error = "mediavault has no fetcher wired";
        return out;
    }
    HttpClient::Request hreq;
    hreq.method = "GET";
    hreq.url = req.url;
    hreq.timeout_seconds = 60;   // media CDNs are allowed to be slow
    hreq.headers = req.headers;  // adapter auth (e.g. Slack Bearer) — never stored
    // Redirect policy is the fetcher's: the kernel binds
    // ApiRuntime::ExecuteScoped, which checks EVERY hop against the scope
    // (a redirect can never escape the allowlist) and drops credentials
    // when the host changes. Never let curl follow silently.
    HttpClient::Response r = m_fetch(hreq, req.scope_hosts);

    if (!r.error.empty()) {
        out.error = "download failed: " + r.error;
        return out;
    }
    if (r.status_code < 200 || r.status_code >= 300) {
        out.error = "download failed: HTTP " + std::to_string(r.status_code);
        return out;
    }

    // ---- Cap check: loud rejection, never truncation ------------------------
    const std::uint64_t cap = CapFor(kind);
    if (static_cast<std::uint64_t>(r.body.size()) > cap) {
        ALOG_WARNING("mediavault", "media over cap rejected: " << r.body.size()
                     << " bytes > " << cap << " cap for kind '" << kind
                     << "' from " << host);
        out.error = "size cap exceeded: " + std::to_string(r.body.size()) +
                    " bytes > " + std::to_string(cap) + " (kind '" + kind + "')";
        return out;
    }
    if (r.body.empty()) {
        out.error = "download returned an empty body";
        return out;
    }

    // ---- Content address + store --------------------------------------------
    const std::string sha = crypto::Sha256Hex(r.body);
    const std::string mime = !CleanMime(req.mime).empty() ? CleanMime(req.mime)
                                                          : CleanMime(r.content_type);
    const std::string filename = !req.filename.empty() ? req.filename
                                 : (!FilenameFromUrl(req.url).empty() ? FilenameFromUrl(req.url)
                                 : sha.substr(0, 12));

    const std::filesystem::path finalPath = PathFor(sha);
    const bool alreadyIndexed = static_cast<bool>(LoadBySha(sha));
    const bool filePresent = std::filesystem::exists(finalPath);

    if (!filePresent) {
        std::error_code ec;
        std::filesystem::create_directories(finalPath.parent_path(), ec);
        if (ec) {
            out.error = "vault dir create failed: " + ec.message();
            return out;
        }
        // Atomic publish: write temp then rename — a partial file must never
        // sit under its content-addressed name.
        const std::filesystem::path tmp =
            finalPath.string() + ".tmp-" + crypto::RandomHex(6);
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) {
                out.error = "vault temp open failed: " + tmp.string();
                return out;
            }
            f.write(r.body.data(), r.body.size());
            f.flush();
            if (!f.good()) {
                std::filesystem::remove(tmp);
                out.error = "vault write failed (stream error)";
                return out;
            }
        }
        std::filesystem::rename(tmp, finalPath, ec);
        if (ec) {
            std::filesystem::remove(tmp);
            out.error = "vault publish rename failed: " + ec.message();
            return out;
        }
    }

    if (m_store) {
        auto obj = m_store->Prepare(
            "INSERT OR REPLACE INTO media_objects "
            "(sha256, kind, mime_type, filename, size_bytes, source_host, alt_text, dimensions, duration, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        if (obj) {
            obj->BindText(1, sha);
            obj->BindText(2, kind);
            obj->BindText(3, mime);
            obj->BindText(4, filename);
            obj->BindInt64(5, static_cast<std::int64_t>(r.body.size()));
            obj->BindText(6, host);
            obj->BindText(7, req.alt_text);
            obj->BindText(8, req.dimensions);
            obj->BindText(9, req.duration);
            obj->BindInt64(10, NowUnix());
            obj->ExecDML();
        }
        if (!req.dedupe_key.empty()) {
            auto dk = m_store->Prepare(
                "INSERT OR REPLACE INTO media_dedupe (dedupe_key, sha256) VALUES (?, ?)");
            if (dk) {
                dk->BindText(1, req.dedupe_key);
                dk->BindText(2, sha);
                dk->ExecDML();
            }
        }
    }

    out.ok = true;
    out.deduped = alreadyIndexed && filePresent;  // same content seen before
    out.sha256 = sha;
    out.ref = "media://" + sha;
    out.filename = filename;
    out.mime = mime;
    out.size = static_cast<std::uint64_t>(r.body.size());
    return out;
}

std::optional<MediaVault::Record> MediaVault::Resolve(const std::string& ref) {
    std::string sha = ref;
    const std::string scheme = "media://";
    if (sha.rfind(scheme, 0) == 0) sha = sha.substr(scheme.size());
    if (!IsHex64(sha)) return std::nullopt;
    auto rec = LoadBySha(sha);
    if (!rec || !ObjectFileIntact(*rec)) return std::nullopt;
    return rec;
}

std::optional<MediaVault::Record> MediaVault::ByDedupeKey(const std::string& dedupeKey) {
    if (!m_store || dedupeKey.empty()) return std::nullopt;
    auto stmt = m_store->Prepare("SELECT sha256 FROM media_dedupe WHERE dedupe_key=?");
    if (!stmt) return std::nullopt;
    stmt->BindText(1, dedupeKey);
    if (!stmt->Step()) return std::nullopt;
    const std::string sha = stmt->ColumnText(0);
    auto rec = LoadBySha(sha);
    if (!rec || !ObjectFileIntact(*rec)) return std::nullopt;
    return rec;
}

std::optional<MediaVault::Record> MediaVault::LoadBySha(const std::string& sha256) {
    if (!m_store) return std::nullopt;
    auto stmt = m_store->Prepare(
        "SELECT sha256, kind, mime_type, filename, size_bytes, source_host, "
        "alt_text, dimensions, duration FROM media_objects WHERE sha256=?");
    if (!stmt) return std::nullopt;
    stmt->BindText(1, sha256);
    if (!stmt->Step()) return std::nullopt;

    Record rec;
    rec.sha256 = stmt->ColumnText(0);
    rec.kind = stmt->ColumnText(1);
    rec.mime = stmt->ColumnText(2);
    rec.filename = stmt->ColumnText(3);
    rec.size = static_cast<std::uint64_t>(stmt->ColumnInt64(4));
    rec.source_host = stmt->ColumnText(5);
    rec.alt_text = stmt->ColumnText(6);
    rec.dimensions = stmt->ColumnText(7);
    rec.duration = stmt->ColumnText(8);
    rec.path = PathFor(rec.sha256);
    return rec;
}

bool MediaVault::ObjectFileIntact(const Record& rec) const {
    std::error_code ec;
    if (!std::filesystem::exists(rec.path, ec) || ec) return false;
    auto size = std::filesystem::file_size(rec.path, ec);
    if (ec) return false;
    // Content-addressed name + size match is the practical integrity gate;
    // a full re-hash would re-read every media file on every arrival.
    return static_cast<std::uint64_t>(size) == rec.size;
}

} // namespace animus::kernel

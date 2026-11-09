// ============================================================================
// DescribeService — #160: two-layer image description engine (core)
//
// Cache-first describe over the #158 media vault. See DescribeService.h
// for the design contract (provenance, engine seam, bytes-stay-kernel).
// ============================================================================

#include "animus_kernel/DescribeService.h"

#include <ctime>

#include "animus_kernel/CryptoUtils.h"
#include "animus_kernel/SchemaHelpers.h"

namespace animus::kernel {

const char* DescribeService::kDefaultPrompt =
    "Describe this image factually and concisely: the main subjects, any "
    "visible text, and notable qualities.";

DescribeService::DescribeService(IDataStore* store, MediaVault* vault,
                                 EngineFn engine, std::string engineName)
    : m_store(store),
      m_vault(vault),
      m_engine(std::move(engine)),
      m_engineName(std::move(engineName)) {}

void DescribeService::EnsureSchema() {
    if (!m_store) return;

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS media_descriptions (
            sha256 TEXT NOT NULL,
            prompt_hash TEXT NOT NULL DEFAULT '',
            engine TEXT NOT NULL DEFAULT '',
            model TEXT NOT NULL DEFAULT '',
            description TEXT NOT NULL DEFAULT '',
            created_at INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (sha256, prompt_hash)
        );
    )");
}

std::string DescribeService::PromptHash(const std::string& prompt) {
    return crypto::Sha256Hex(prompt);
}

DescribeService::DescribeResult DescribeService::Describe(
    const std::string& ref, const std::string& prompt) {
    DescribeResult out;
    out.engine = m_engineName;

    if (!m_vault || !m_engine) {
        out.error = "describe engine not configured";
        return out;
    }

    // ref → vault record. Unknown/missing objects fail loudly; the engine
    // never runs for them.
    const auto rec = m_vault->Resolve(ref);
    if (!rec) {
        out.error = "unknown media ref (not vaulted or object missing): " + ref;
        return out;
    }
    if (rec->kind != "image") {
        out.error = "ref is not an image (kind=" + rec->kind + ")";
        return out;
    }

    // Prompt normalization: empty and explicit-default hit the SAME cache
    // row — the passive auto-describe and an unprompted active query are
    // one answer, cached once.
    const std::string p =
        prompt.empty() ? std::string(kDefaultPrompt) : prompt;
    const std::string promptHash = PromptHash(p);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Cached c;
        if (LoadCached(rec->sha256, promptHash, c)) {
            out.ok = true;
            out.cached = true;
            out.text = std::move(c.text);
            out.engine = std::move(c.engine);
            out.model = std::move(c.model);
            return out;
        }
    }

    EngineRequest req;
    req.sha256 = rec->sha256;
    req.image_path = rec->path;
    req.mime = rec->mime;
    req.prompt = p;

    const EngineResult er = m_engine(req);
    if (!er.ok || er.text.empty()) {
        // Loud failure, nothing cached — the next attempt re-runs the
        // engine (transient failures must not poison the cache).
        out.error = !er.error.empty()
                        ? er.error
                        : "describe engine returned no text";
        return out;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        StoreDescription(rec->sha256, promptHash, m_engineName, er.model,
                         er.text);
    }

    out.ok = true;
    out.cached = false;
    out.text = er.text;
    out.model = er.model;
    return out;
}

std::string DescribeService::Label(const DescribeResult& r) {
    std::string label = "[machine description | engine=" + r.engine;
    if (!r.model.empty()) label += " model=" + r.model;
    label += "]";
    if (r.cached) label += " (cached)";
    return label + " " + r.text;
}

bool DescribeService::LoadCached(const std::string& sha256,
                                 const std::string& promptHash,
                                 Cached& out) const {
    if (!m_store) return false;
    auto stmt = m_store->Prepare(
        "SELECT engine, model, description FROM media_descriptions "
        "WHERE sha256=? AND prompt_hash=?");
    if (!stmt) return false;
    if (!stmt->BindText(1, sha256)) return false;
    if (!stmt->BindText(2, promptHash)) return false;
    if (!stmt->Step()) return false;
    out.engine = stmt->ColumnText(0);
    out.model = stmt->ColumnText(1);
    out.text = stmt->ColumnText(2);
    return true;
}

void DescribeService::StoreDescription(const std::string& sha256,
                                       const std::string& promptHash,
                                       const std::string& engine,
                                       const std::string& model,
                                       const std::string& description) const {
    if (!m_store) return;
    auto stmt = m_store->Prepare(
        "INSERT OR REPLACE INTO media_descriptions "
        "(sha256, prompt_hash, engine, model, description, created_at) "
        "VALUES (?,?,?,?,?,?)");
    if (!stmt) return;
    stmt->BindText(1, sha256);
    stmt->BindText(2, promptHash);
    stmt->BindText(3, engine);
    stmt->BindText(4, model);
    stmt->BindText(5, description);
    // Numeric strings bind as INTEGER via column affinity.
    stmt->BindText(6, std::to_string(static_cast<std::int64_t>(
                          std::time(nullptr))));
    stmt->Step();
}

} // namespace animus::kernel

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

#include "animus_kernel/MediaVault.h"

namespace animus::kernel {

// ============================================================================
// DescribeService — #160: two-layer image description engine
//
// Cache-first: content-addressed (sha256(image) + prompt_hash) descriptions
// in the IDataStore; hits never touch an engine. Misses run the injected
// engine seam (same discipline as MediaVault::FetchFn — tests inject a
// lambda; the kernel binds the local mtmd VLM or the cloud chainRunner
// path per privacy policy, which lives at the CALL SITES, not here).
//
// Provenance: every result carries engine + model so callers can label
// machine descriptions in context — a description is evidence about an
// image, never ground truth.
//
// Image bytes stay kernel-side: the engine receives the vault object PATH
// (local engine reads it in-process; the cloud binding encodes it to a
// data URI exactly like ImageTool.analyze). Bytes never enter the service
// boundary as context and never enter prompt text.
// ============================================================================

class DescribeService {
public:
    // The default auto-describe question. Neutral and factual: a reusable
    // baseline answer, cached once per image. Explicit prompts (the active
    // tool: "read the text in this chart") hash to their own cache rows.
    static const char* kDefaultPrompt;
    // The #161 outbound alt-text question: concise, screen-reader-fit,
    // visibly-present-only. A distinct prompt → a distinct cache row per
    // image, so alt text never collides with default descriptions.
    static const char* kAltTextPrompt;

    struct EngineRequest {
        std::string sha256;               // content address of the image
        std::filesystem::path image_path; // vault object path (kernel-only)
        std::string mime;
        std::string prompt;               // resolved — never empty
    };

    struct EngineResult {
        bool ok{false};
        std::string text;                 // the description
        std::string model;                // engine-reported model id
        std::string error;                // loud failure reason when !ok
    };

    // Engine seam. The service serializes calls through one mutex — the
    // bound engine never sees concurrent Describe() traffic (mirrors the
    // EmbeddingService llama.cpp serialization discipline).
    using EngineFn = std::function<EngineResult(const EngineRequest&)>;

    struct DescribeResult {
        bool ok{false};
        bool cached{false};               // served from cache; engine idle
        std::string text;
        std::string engine;               // engine name bound at construction
        std::string model;
        std::string error;                // loud failure reason when !ok
    };

    DescribeService(IDataStore* store, MediaVault* vault, EngineFn engine,
                    std::string engineName);

    void EnsureSchema();

    // ref = "media://<sha256>" or bare 64-hex hash. prompt empty = default.
    // Cache-first; a miss runs the engine once and stores the result.
    DescribeResult Describe(const std::string& ref,
                            const std::string& prompt = "");

    // Vault-less variant for kernel-side outbound paths (#161 alt-fill):
    // content-addressed by the file's OWN bytes, so a path and a vault
    // ref to the same image share one cache row. Requires an engine but
    // no vault. prompt empty = default.
    DescribeResult DescribePath(const std::filesystem::path& image,
                                const std::string& mime,
                                const std::string& prompt = "");

    // Provenance-labeled line for context cards:
    //   "[machine description | engine=local model=X] text"
    static std::string Label(const DescribeResult& r);

    static std::string PromptHash(const std::string& prompt);

private:
    struct Cached {
        std::string text;
        std::string engine;
        std::string model;
    };

    bool LoadCached(const std::string& sha256, const std::string& promptHash,
                    Cached& out) const;
    void StoreDescription(const std::string& sha256,
                          const std::string& promptHash,
                          const std::string& engine, const std::string& model,
                          const std::string& description) const;

    IDataStore* m_store;
    MediaVault* m_vault;
    EngineFn m_engine;
    std::string m_engineName;
    mutable std::mutex m_mutex;
};

} // namespace animus::kernel

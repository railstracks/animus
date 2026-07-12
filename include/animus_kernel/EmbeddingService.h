#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace animus::kernel {

// ============================================================================
// EmbeddingService — in-process embedding generation via llama.cpp
//
// Loads a GGUF embedding model (e.g. embeddinggemma-300m-qat) at startup
// and provides text → vector embedding conversion. Used by the MemoryFile
// context injection pipeline for semantic similarity retrieval.
//
// When the model file is not found or loading fails, the service operates
// in "degraded mode" — Embed() returns std::nullopt and callers fall back
// to keyword-based matching.
// ============================================================================

class EmbeddingService {
public:
    // Dimension of the embedding vectors (768 for embeddinggemma-300m)
    static constexpr int DEFAULT_DIMENSION = 768;

    explicit EmbeddingService(const std::string& modelPath);
    ~EmbeddingService();

    EmbeddingService(const EmbeddingService&) = delete;
    EmbeddingService& operator=(const EmbeddingService&) = delete;

    // Returns true if the model is loaded and embeddings are available.
    bool IsAvailable() const;

    // Returns the embedding dimension (0 if not loaded).
    int Dimension() const;

    // Compute embedding for a text string.
    // Returns std::nullopt in degraded mode (model not loaded).
    std::optional<std::vector<float>> Embed(const std::string& text) const;

    // Cosine similarity between two embeddings.
    // Returns 0.0 if either vector is empty or dimensions don't match.
    static float CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    mutable std::mutex m_mutex; // Serialize access to llama.cpp (not thread-safe)
};

} // namespace animus::kernel

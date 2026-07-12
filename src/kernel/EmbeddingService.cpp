#include "animus_kernel/EmbeddingService.h"

#if defined(__has_include)
#  if __has_include(<llama.h>)
#    define ANIMUS_HAS_LLAMA 1
#  endif
#endif

#ifdef ANIMUS_HAS_LLAMA
#include "llama.h"
#include "ggml.h"
#endif

#include <cmath>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <mutex>

namespace animus::kernel {

#ifdef ANIMUS_HAS_LLAMA

struct EmbeddingService::Impl {
    llama_model* model{nullptr};
    llama_context* ctx{nullptr};
    int dimension{0};
    bool loaded{false};

    ~Impl() {
        if (ctx) llama_free(ctx);
        if (model) llama_model_free(model);
    }

    bool load(const std::string& modelPath) {
        if (!std::filesystem::exists(modelPath)) {
            std::cerr << "[embedding] Model file not found: " << modelPath << "\n";
            return false;
        }

        llama_model_params modelParams = llama_model_default_params();
        model = llama_model_load_from_file(modelPath.c_str(), modelParams);
        if (!model) {
            std::cerr << "[embedding] Failed to load model: " << modelPath << "\n";
            return false;
        }

        llama_context_params ctxParams = llama_context_default_params();
        // llama.cpp encoder requires n_ubatch >= n_tokens (all tokens in one micro-batch,
        // unlike decoding which can chunk). So n_batch must be >= our max tokenization cap.
        ctxParams.n_batch = 512;
        ctxParams.n_ubatch = 512;
        ctxParams.n_ctx = 2048;
        ctxParams.n_threads = 2;
        ctxParams.n_threads_batch = 2;
        ctxParams.embeddings = true;
        ctxParams.pooling_type = LLAMA_POOLING_TYPE_MEAN;

        ctx = llama_init_from_model(model, ctxParams);
        if (!ctx) {
            std::cerr << "[embedding] Failed to create context\n";
            llama_model_free(model);
            model = nullptr;
            return false;
        }

        dimension = llama_model_n_embd(model);
        loaded = true;

        std::cerr << "[embedding] Model loaded: " << modelPath
                  << " (dimension=" << dimension << ")\n";
        return true;
    }

    std::optional<std::vector<float>> embed(const std::string& text) const {
        if (!loaded || text.empty()) return std::nullopt;

        // Tokenize — cap at n_batch - 1 (leave room for BOS)
        const int n_max = 510;  // n_batch (512) minus headroom
        std::vector<llama_token> tokens(n_max);
        const llama_vocab* vocab = llama_model_get_vocab(model);
        int n_tokens = llama_tokenize(
            vocab,
            text.c_str(), text.size(),
            tokens.data(), n_max,
            true,  // add_bos
            true   // special
        );
        if (n_tokens <= 0) return std::nullopt;
        if (n_tokens > n_max) n_tokens = n_max;

        // Create batch with proper sequence ID and logits flag.
        // Match the official llama.cpp embedding example:
        // use llama_batch_init with seq_id and logits=true for each token.
        llama_batch batch = llama_batch_init(n_tokens, 0, 1);
        for (int i = 0; i < n_tokens; i++) {
           batch.token[i] = tokens[i];
           batch.pos[i] = i;
           batch.seq_id[i][0] = 0;
           batch.seq_id[i][1] = -1;
           batch.logits[i] = true;
            batch.n_seq_id[i] = 1;
        }
        batch.n_tokens = n_tokens;

        // Clear previous KV cache (irrelevant for embeddings but required by API)
        llama_memory_clear(llama_get_memory(ctx), true);

        // Decode — for embedding models, llama_decode is the correct call.
        // (llama_encode is for encoder-decoder models, not embedding-only models)
        int ret = llama_decode(ctx, batch);
        if (ret < 0) {
            // Some contexts only support encode
            ret = llama_encode(ctx, batch);
            if (ret != 0) {
                std::cerr << "[embedding] llama_decode and llama_encode both failed\n";
                llama_batch_free(batch);
                return std::nullopt;
            }
        }
        llama_batch_free(batch);

        // Get embeddings — for MEAN pooling, use llama_get_embeddings_seq
        // (per-sequence embeddings, not per-token)
        float* embd = llama_get_embeddings_seq(ctx, 0);
        if (!embd) {
            // Fallback for NONE pooling or older API
            embd = llama_get_embeddings_ith(ctx, 0);
        }
        if (!embd) {
            embd = llama_get_embeddings(ctx);
        }
        if (!embd) return std::nullopt;

        std::vector<float> result(embd, embd + dimension);

        // Normalize
        float norm = 0.0f;
        for (float v : result) norm += v * v;
        norm = std::sqrt(norm);
        if (norm == 0.0f) {
            std::cerr << "[embedding] WARNING: zero vector produced, rejecting\n";
            return std::nullopt;
        }
        for (float& v : result) v /= norm;

        return result;
    }
};

#else // !ANIMUS_HAS_LLAMA

struct EmbeddingService::Impl {
    bool loaded{false};
    int dimension{0};

    bool load(const std::string&) {
        return false;
    }

    std::optional<std::vector<float>> embed(const std::string&) const {
        return std::nullopt;
    }
};

#endif

EmbeddingService::EmbeddingService(const std::string& modelPath)
    : m_impl(std::make_unique<Impl>()) {
    if (!modelPath.empty()) {
        m_impl->load(modelPath);
    }
}
EmbeddingService::~EmbeddingService() = default;

bool EmbeddingService::IsAvailable() const {
    return m_impl && m_impl->loaded;
}

int EmbeddingService::Dimension() const {
    return m_impl ? m_impl->dimension : 0;
}

std::optional<std::vector<float>> EmbeddingService::Embed(const std::string& text) const {
    if (!m_impl) return std::nullopt;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_impl->embed(text);
}

float EmbeddingService::CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }
    if (normA == 0.0f || normB == 0.0f) return 0.0f;
    return dot / (std::sqrt(normA) * std::sqrt(normB));
}

} // namespace animus::kernel

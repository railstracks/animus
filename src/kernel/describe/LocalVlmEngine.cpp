// ============================================================================
// LocalVlmEngine — #160: local mtmd VLM engine implementation
//
// Real engine compiled when <mtmd.h> is on the include path (llama.cpp
// tools/mtmd built as animus_mtmd); degraded stub otherwise. See
// LocalVlmEngine.h for the contract.
// ============================================================================

#include "animus_kernel/LocalVlmEngine.h"

#include <iostream>

#if defined(__has_include)
#  if __has_include(<mtmd.h>)
#    define ANIMUS_HAS_MTMD 1
#  endif
#endif

#ifdef ANIMUS_HAS_MTMD
#include "llama.h"
#include "mtmd.h"
#include "mtmd-helper.h"
#endif

namespace animus::kernel {

#ifdef ANIMUS_HAS_MTMD

struct LocalVlmEngine::Impl {
    Params params;
    llama_model* model{nullptr};
    llama_context* lctx{nullptr};
    mtmd_context* mctx{nullptr};
    std::string modelId;
    std::string loadError;
    bool loaded{false};

    ~Impl() {
        if (mctx) mtmd_free(mctx);
        if (lctx) llama_free(lctx);
        if (model) llama_model_free(model);
    }

    bool load() {
        namespace fs = std::filesystem;
        if (!fs::exists(params.model_path)) {
            loadError =
                "VLM model file not found: " + params.model_path.string();
            return false;
        }
        if (!fs::exists(params.mmproj_path)) {
            loadError = "mmproj (vision projector) file not found: " +
                        params.mmproj_path.string();
            return false;
        }

        llama_model_params mp = llama_model_default_params();
        model = llama_model_load_from_file(params.model_path.c_str(), mp);
        if (!model) {
            loadError = "failed to load VLM model: " +
                        params.model_path.string();
            return false;
        }

        llama_context_params cp = llama_context_default_params();
        cp.n_ctx = params.n_ctx;
        cp.n_batch = params.n_batch;
        cp.n_ubatch = params.n_batch;
        cp.n_threads = params.n_threads;
        cp.n_threads_batch = params.n_threads;
        lctx = llama_init_from_model(model, cp);
        if (!lctx) {
            loadError = "failed to create VLM llama context";
            return false;
        }

        mtmd_context_params mcp = mtmd_context_params_default();
        mcp.use_gpu = true;
        mcp.print_timings = params.print_timings;
        mcp.n_threads = params.n_threads;
        mctx = mtmd_init_from_file(params.mmproj_path.c_str(), model, mcp);
        if (!mctx) {
            loadError =
                "failed to load mmproj: " + params.mmproj_path.string();
            return false;
        }
        if (!mtmd_support_vision(mctx)) {
            loadError =
                "mmproj has no vision support: " + params.mmproj_path.string();
            return false;
        }

        char desc[256];
        llama_model_desc(model, desc, sizeof(desc));
        modelId = desc;
        loaded = true;
        return true;
    }
};

#else // !ANIMUS_HAS_MTMD — degraded stub

struct LocalVlmEngine::Impl {
    Params params;
    std::string modelId;
    std::string loadError{
        "local VLM engine not built (llama.cpp mtmd unavailable)"};
};

#endif

LocalVlmEngine::LocalVlmEngine(const Params& params)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->params = params;
#ifdef ANIMUS_HAS_MTMD
    if (!m_impl->load()) {
        std::cerr << "[describe] local VLM degraded: " << m_impl->loadError
                  << "\n";
    }
#endif
}

LocalVlmEngine::~LocalVlmEngine() = default;

bool LocalVlmEngine::IsAvailable() const { return m_impl->loaded; }
const std::string& LocalVlmEngine::ModelId() const { return m_impl->modelId; }
const std::string& LocalVlmEngine::LoadError() const {
    return m_impl->loadError;
}

#ifdef ANIMUS_HAS_MTMD

DescribeService::EngineResult LocalVlmEngine::Describe(
    const std::filesystem::path& image, const std::string& prompt) {
    std::lock_guard<std::mutex> lock(m_mutex);
    DescribeService::EngineResult out;

    if (!m_impl->loaded) {
        out.error = m_impl->loadError.empty() ? "local VLM engine unavailable"
                                              : m_impl->loadError;
        return out;
    }

    // Fresh KV state per call — the engine is long-lived.
    llama_memory_clear(llama_get_memory(m_impl->lctx), true);

    // Decode the image through mtmd's stb-based loader (kernel-side bytes).
    mtmd_helper_bitmap_wrapper bw = mtmd_helper_bitmap_init_from_file(
        m_impl->mctx, image.string().c_str(), /*placeholder=*/false);
    if (!bw.bitmap) {
        out.error = "failed to decode image: " + image.string();
        return out;
    }

    // Image first, then the question — one marker for one bitmap.
    const std::string input =
        std::string(mtmd_get_marker(m_impl->mctx)) + "\n" + prompt;

    mtmd_input_text itext{};
    itext.text = input.c_str();
    itext.add_special = true;
    itext.parse_special = true;

    mtmd_input_chunks* chunks = mtmd_input_chunks_init();
    const mtmd_bitmap* bitmaps[] = {bw.bitmap};
    const int32_t trc =
        mtmd_tokenize(m_impl->mctx, chunks, &itext, bitmaps, 1);

    if (trc != 0) {
        out.error = trc == 1
                        ? "mtmd_tokenize: bitmap/marker mismatch"
                        : "mtmd_tokenize: image preprocessing failed";
        mtmd_input_chunks_free(chunks);
        mtmd_bitmap_free(bw.bitmap);
        return out;
    }

    // Encode + decode every chunk — the helper runs llama_decode on text
    // chunks and the vision encode + embed on image chunks (non-causal
    // attention and M-RoPE handling included), leaving logits on the last
    // position for sampling.
    llama_pos n_past = 0;
    const int32_t erc = mtmd_helper_eval_chunks(
        m_impl->mctx, m_impl->lctx, chunks, /*n_past=*/0, /*seq_id=*/0,
        m_impl->params.n_batch, /*logits_last=*/true, &n_past);

    mtmd_input_chunks_free(chunks);
    mtmd_bitmap_free(bw.bitmap);

    if (erc != 0) {
        out.error =
            "prompt encode failed (mtmd rc=" + std::to_string(erc) + ")";
        return out;
    }

    // Greedy generation loop.
    const llama_vocab* vocab = llama_model_get_vocab(m_impl->model);
    llama_sampler* smpl =
        llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_greedy());

    llama_batch batch = llama_batch_init(1, /*embd=*/0, /*n_seq_max=*/1);
    std::string text;
    text.reserve(256);

    for (int i = 0; i < m_impl->params.max_output_tokens; ++i) {
        const llama_token tok = llama_sampler_sample(smpl, m_impl->lctx, -1);
        if (llama_vocab_is_eog(vocab, tok)) break;

        char piece[64];
        const int n = llama_token_to_piece(vocab, tok, piece,
                                           static_cast<int32_t>(sizeof(piece)),
                                           /*lstrip=*/0, /*special=*/false);
        if (n > 0) text.append(piece, static_cast<size_t>(n));

        batch.n_tokens = 1;
        batch.token[0] = tok;
        batch.pos[0] = n_past;
        batch.n_seq_id[0] = 1;
        batch.seq_id[0][0] = 0;
        batch.logits[0] = 1;
        if (llama_decode(m_impl->lctx, batch) != 0) {
            llama_sampler_free(smpl);
            llama_batch_free(batch);
            out.error = "llama_decode failed during generation";
            return out;
        }
        ++n_past;
    }

    llama_sampler_free(smpl);
    llama_batch_free(batch);

    if (text.empty()) {
        out.error = "VLM produced no text";
        return out;
    }

    out.ok = true;
    out.text = std::move(text);
    out.model = m_impl->modelId;
    return out;
}

#else // !ANIMUS_HAS_MTMD

DescribeService::EngineResult LocalVlmEngine::Describe(
    const std::filesystem::path& /*image*/, const std::string& /*prompt*/) {
    std::lock_guard<std::mutex> lock(m_mutex);
    DescribeService::EngineResult out;
    out.error = m_impl->loadError;
    return out;
}

#endif

} // namespace animus::kernel

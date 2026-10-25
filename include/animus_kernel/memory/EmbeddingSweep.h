#pragma once

// #232 — the pending-embedding sweep, extracted from AgentKernel for
// testability (the kernel path needs a live embedding model; the sweep
// semantics need a regression suite).
//
// Bug being fixed: the sweep looped over agents and skipped any file whose
// agent_id didn't match the agent's numeric_id — global (operator-shared,
// agent_id=0) files matched no agent and were never chunked or embedded.
// A global file created with the embedding model enabled stayed
// chunk-less forever while the keyword fallback still surfaced it — the
// same mode-dependent silent failure as #225, one layer earlier.
// Dropping the agent loop also fixes the zero-agents case: with no
// agents registered, the old sweep did nothing at all.

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace animus::kernel {
namespace memory {

class MemoryFileStore;

struct EmbeddingSweepStats {
    int chunks_created{0};
    int embeddings_computed{0};
};

// Embed callback — same shape as EmbeddingService::Embed, injectable for tests.
using EmbedFn = std::function<std::optional<std::vector<float>>(const std::string&)>;

// Sweep all non-superseded memory files once: re-chunk files that have no
// chunks yet, then compute embeddings for chunks missing them. Includes
// global (agent_id=0) files — the #225 semantics one layer earlier.
// Idempotent: a second run over already-chunked, already-embedded files
// reports zero work.
EmbeddingSweepStats SweepPendingEmbeddings(MemoryFileStore* store, const EmbedFn& embed);

} // namespace memory
} // namespace animus::kernel
// #232 — pending-embedding sweep, extracted from AgentKernel.
//
// The extraction is the fix's testability half; the semantic half is the
// dropped agent loop. See EmbeddingSweep.h for the bug narrative.

#include "animus_kernel/memory/EmbeddingSweep.h"

#include "animus_kernel/MemoryFileStore.h"
#include "animus_kernel/Log.h"

namespace animus::kernel {
namespace memory {

EmbeddingSweepStats SweepPendingEmbeddings(MemoryFileStore* store, const EmbedFn& embed) {
    EmbeddingSweepStats stats;
    if (!store || !embed) return stats;

    auto files = store->ListFiles(std::nullopt, 1000, 0);

    for (const auto& file : files) {
        // #232: global (agent_id=0) files are INCLUDED. The old loop's
        // `file.agent_id != agent.numeric_id` skip meant globals matched no
        // agent and were never chunked or embedded — while the keyword
        // fallback still surfaced them, the #225 mode-dependent silent
        // failure one layer earlier. Superseded files stay excluded.
        if (file.superseded) continue;

        auto existingChunks = store->GetChunks(file.id);
        bool needsRechunk = existingChunks.empty();

        if (needsRechunk && !file.content.empty()) {
            std::vector<MemoryFileChunk> newChunks;
            const std::string& content = file.content;
            std::size_t pos = 0;
            std::size_t lastHeader = 0;
            bool foundAnyHeader = false;
            int lineNum = 0;
            int headerLine = 0;

            while (pos < content.size()) {
                std::size_t nl = content.find('\n', pos);
                lineNum++;
                std::size_t lineEnd = (nl == std::string::npos) ? content.size() : nl;
                if (lineEnd > pos && content[pos] == '#') {
                    if (foundAnyHeader) {
                        MemoryFileChunk chunk;
                        chunk.source_path = file.source_path;
                        chunk.content = content.substr(lastHeader, pos - lastHeader);
                        chunk.start_line = headerLine;
                        chunk.end_line = lineNum - 1;
                        newChunks.push_back(std::move(chunk));
                    }
                    lastHeader = pos;
                    headerLine = lineNum;
                    foundAnyHeader = true;
                }
                pos = (nl == std::string::npos) ? content.size() : nl + 1;
            }

            if (foundAnyHeader) {
                MemoryFileChunk chunk;
                chunk.source_path = file.source_path;
                chunk.content = content.substr(lastHeader);
                chunk.start_line = headerLine;
                chunk.end_line = lineNum;
                newChunks.push_back(std::move(chunk));
            } else {
                MemoryFileChunk chunk;
                chunk.source_path = file.source_path;
                chunk.content = content;
                chunk.start_line = 1;
                chunk.end_line = lineNum;
                newChunks.push_back(std::move(chunk));
            }

            store->ReplaceChunks(file.id, newChunks);
            existingChunks = store->GetChunks(file.id);
            stats.chunks_created += static_cast<int>(newChunks.size());
        }

        for (const auto& chunk : existingChunks) {
            if (chunk.embedding_dim > 0 && !chunk.embedding.empty()) continue;
            if (chunk.content.empty()) continue;

            auto emb = embed(chunk.content);
            if (emb.has_value()) {
                store->UpdateChunkEmbedding(chunk.id, *emb);
                stats.embeddings_computed++;
            }
        }
    }

    if (stats.chunks_created > 0 || stats.embeddings_computed > 0) {
        ALOG_INFO("embedding", "Swept " << files.size() << " files: "
                  << stats.chunks_created << " chunks, "
                  << stats.embeddings_computed << " embeddings"
                  << " (global files included — #232)");
    }
    return stats;
}

} // namespace memory
} // namespace animus::kernel
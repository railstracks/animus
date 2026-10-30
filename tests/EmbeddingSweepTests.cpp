// ============================================================================
// EmbeddingSweepTests — #232
//
// The pending-embedding sweep (extracted from AgentKernel into
// memory/EmbeddingSweep for exactly this testability) used to loop over
// registered agents and skip any file whose agent_id didn't match:
// global (operator-shared, agent_id=0) files matched no agent and were
// never chunked or embedded — while the keyword fallback still surfaced
// them. Mode-dependent silent failure, the #225 semantics one layer
// earlier. Dropping the agent loop also fixes the zero-agents case.
//
// Contracts under test (fake embed callback — no live model needed):
//   1. A GLOBAL (agent_id=0) file gets chunked AND embedded.
//   2. A per-agent file still gets chunked AND embedded.
//   3. Superseded files are skipped.
//   4. Idempotence: a second sweep reports zero new work.
//   5. Multi-header content chunks per header.
//   6. A failing embed callback leaves chunks in place (retry next sweep).
// ============================================================================

#include "animus_kernel/memory/EmbeddingSweep.h"
#include "animus_kernel/MemoryFileStore.h"
#include "animus_kernel/SqliteDataStore.h"

#include <filesystem>
#include <iostream>
#include <string>

using namespace animus::kernel;
using namespace animus::kernel::memory;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        ++g_failures;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_embedding_sweep_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

struct Fixture {
    std::string dbPath = MakeTempDbPath();
    SqliteDataStore dataStore{dbPath};
    MemoryFileStore store{&dataStore};

    ~Fixture() {
        std::error_code ec;
        std::filesystem::remove(dbPath, ec);
        (void)ec;
    }

    MemoryFile MakeFile(int64_t agentId, const std::string& content,
                        const std::string& path = "test.md") {
        MemoryFile f;
        f.source_path = path;
        f.file_type = MemoryFileType::ExternalDoc;
        f.content = content;
        f.agent_id = agentId;
        f.status = MemoryFileStatus::Processed;
        return store.CreateFile(f);
    }
};

EmbedFn FakeEmbed() {
    return [](const std::string&) -> std::optional<std::vector<float>> {
        return std::vector<float>{0.5f, 1.0f, -0.5f};
    };
}

} // namespace

int main() {
    std::cerr << "Running embedding sweep tests...\n";

    // ---- 1+2+3: global file embedded, agent file embedded, superseded skipped ----
    {
        Fixture fx;
        auto global = fx.MakeFile(0, "# Global Doc\nshared operator context");
        auto agentFile = fx.MakeFile(7, "# Agent Doc\nprivate notes");
        MemoryFile sup;
        sup.source_path = "superseded.md";
        sup.file_type = MemoryFileType::ExternalDoc;
        sup.content = "# Old\nsuperseded content";
        sup.agent_id = 0;
        sup.superseded = true;
        sup.status = MemoryFileStatus::Processed;
        auto superseded = fx.store.CreateFile(sup);
        (void)superseded;

        auto stats = SweepPendingEmbeddings(&fx.store, FakeEmbed());

        Assert(stats.chunks_created == 2,
               "2 chunks created (global + agent), got " +
                   std::to_string(stats.chunks_created));
        Assert(stats.embeddings_computed == 2,
               "2 embeddings computed, got " +
                   std::to_string(stats.embeddings_computed));

        // THE #232 regression: the global file must have chunks + embeddings.
        auto globalChunks = fx.store.GetChunks(global.id);
        Assert(!globalChunks.empty(), "GLOBAL file has chunks (the #232 bug)");
        Assert(globalChunks[0].embedding_dim == 3,
               "global chunk embedded (dim=3), got dim=" +
                   std::to_string(globalChunks[0].embedding_dim));

        auto agentChunks = fx.store.GetChunks(agentFile.id);
        Assert(!agentChunks.empty() && agentChunks[0].embedding_dim == 3,
               "agent file chunked + embedded");

        // Superseded file untouched.
        Assert(fx.store.GetChunks(superseded.id).empty(),
               "superseded file left chunk-less (skipped)");
    }

    // ---- 4: idempotence ----
    {
        Fixture fx;
        auto global = fx.MakeFile(0, "# G\ncontent");
        (void)global;

        auto first = SweepPendingEmbeddings(&fx.store, FakeEmbed());
        Assert(first.chunks_created == 1 && first.embeddings_computed == 1,
               "first sweep does the work");

        auto second = SweepPendingEmbeddings(&fx.store, FakeEmbed());
        Assert(second.chunks_created == 0 && second.embeddings_computed == 0,
               "second sweep is a no-op (idempotent)");
    }

    // ---- 5: multi-header content chunks per header ----
    {
        Fixture fx;
        auto multi = fx.MakeFile(0, "# One\nfirst\n\n# Two\nsecond");
        (void)multi;

        auto stats = SweepPendingEmbeddings(&fx.store, FakeEmbed());
        Assert(stats.chunks_created == 2,
               "multi-header file yields 2 chunks, got " +
                   std::to_string(stats.chunks_created));
        Assert(stats.embeddings_computed == 2, "both chunks embedded");
    }

    // ---- 6: failing embed leaves chunks for retry ----
    {
        Fixture fx;
        auto global = fx.MakeFile(0, "# G\ncontent");
        (void)global;

        EmbedFn failing = [](const std::string&) -> std::optional<std::vector<float>> {
            return std::nullopt;
        };
        auto stats = SweepPendingEmbeddings(&fx.store, failing);
        Assert(stats.chunks_created == 1, "chunking independent of embed success");
        Assert(stats.embeddings_computed == 0, "failed embed computes nothing");

        // Chunks exist without embeddings — retry with a working callback.
        auto retry = SweepPendingEmbeddings(&fx.store, FakeEmbed());
        Assert(retry.chunks_created == 0, "no re-chunk on retry");
        Assert(retry.embeddings_computed == 1, "retry embeds the pending chunk");
    }

    if (g_failures > 0) {
        std::cerr << "\nFAILED: " << g_failures << " assertions failed\n";
        return 1;
    }

    std::cerr << "\nAll embedding sweep tests passed!\n";
    return 0;
}
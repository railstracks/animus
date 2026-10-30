// ActiveMemoryProviderTests — global (agent_id=0) memory-file regression (issue #225)
//
// Enabling the embedding model must not change WHICH files exist in an agent's
// context. The cached vector path previously fetched chunks with a strict
// per-agent predicate, silently hiding global (operator-shared, agent_id=0)
// memory files, while the keyword fallback returned them.
//
// Leg A (always runs): keyword-fallback mode (no embedding service) —
//   global + agent-owned files surface, foreign files do not.
// Leg B (runs when ANIMUS_TEST_EMBEDDING_MODEL points at a loadable GGUF
//   embedding model, e.g. models/embeddinggemma-300m-qat-Q8_0.gguf): cached
//   vector path with pre-computed chunk embeddings — same assertions. Skipped
//   with a notice when the model file is absent (repo carries no model).

#include "animus_kernel/context/ActiveMemoryProvider.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/EmbeddingService.h"
#include "animus_kernel/MemoryFileStore.h"
#include "animus_kernel/Session.h"
#include "animus_kernel/SqliteDataStore.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace animus::kernel;
using namespace animus::kernel::memory;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_active_memory_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

struct Fixture {
    std::string dbPath;
    std::unique_ptr<SqliteDataStore> dataStore;
    std::unique_ptr<MemoryFileStore> files;

    Fixture() {
        dbPath = MakeTempDbPath();
        dataStore = std::make_unique<SqliteDataStore>(dbPath);
        files = std::make_unique<MemoryFileStore>(dataStore.get());
    }
};

MemoryFile MakeFile(const std::string& path, const std::string& content, int64_t agentId) {
    MemoryFile file;
    file.source_path = path;
    file.file_type = MemoryFileType::ExternalDoc;
    file.content = content;
    file.content_mutable = true;
    file.agent_id = agentId;
    return file;
}

Agent MakeAgent(int64_t numericId) {
    Agent agent;
    agent.id = "agent-" + std::to_string(numericId);
    agent.numeric_id = numericId;
    agent.name = "test-agent";
    return agent;
}

std::shared_ptr<SessionAccess> MakeSession(const std::string& utterance) {
    auto session = std::make_shared<Session>(
        static_cast<SessionId>(1),
        SessionKey{"webchat", "conv-active-memory-test", ""});
    SessionTurn turn;
    turn.role = "user";
    turn.content = utterance;
    turn.unix_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    session->AddTurn(turn);
    return std::make_shared<SessionAccess>(session, SessionAccessMode::ReadOnly);
}

// Global + agent-owned memory files, chunked at the single header, with
// embeddings pre-computed when a service is supplied (cached vector path).
void SeedFiles(Fixture& fx, EmbeddingService* embedding) {
    const std::string globalContent =
        "# Shared Operator Notes\n"
        "The quantumharmonics calibration must run before every deployment.\n";
    const std::string agentContent =
        "# Agent Private Notes\n"
        "The quantumharmonics calibration log for agent seven lives here.\n";
    const std::string foreignContent =
        "# Foreign Notes\n"
        "The quantumharmonics calibration log for agent three lives here.\n";

    auto globalFile = fx.files->CreateFile(MakeFile("/globals/shared-notes.md", globalContent, 0));
    auto agentFile = fx.files->CreateFile(MakeFile("/agents/7/private-notes.md", agentContent, 7));
    fx.files->CreateFile(MakeFile("/agents/3/foreign-notes.md", foreignContent, 3));

    for (const auto& f : {globalFile, agentFile}) {
        std::vector<MemoryFileChunk> chunks;
        MemoryFileChunk chunk;
        chunk.source_path = f.source_path;
        chunk.content = f.content;
        chunk.start_line = 1;
        chunk.end_line = 2;
        chunks.push_back(chunk);
        fx.files->ReplaceChunks(f.id, chunks);

        if (embedding) {
            auto emb = embedding->Embed(f.content);
            if (emb.has_value()) {
                auto stored = fx.files->GetChunks(f.id);
                if (!stored.empty()) {
                    fx.files->UpdateChunkEmbedding(stored.front().id, *emb);
                }
            }
        }
    }
}

std::optional<ContextBlock> RunProvider(const Fixture& fx, const Agent& agent,
                                        const std::shared_ptr<SessionAccess>& session,
                                        EmbeddingService* embedding) {
    ActiveMemoryProvider provider(nullptr, nullptr, nullptr, fx.files.get(),
                                  nullptr, nullptr, nullptr, embedding);
    return provider.Provide(agent, *session);
}

void AssertGlobalAndAgentFilesVisible(Fixture& fx, const Agent& agent,
                                      const std::shared_ptr<SessionAccess>& session,
                                      EmbeddingService* embedding,
                                      const std::string& modeLabel) {
    std::cerr << "  [ActiveMemoryProvider] " << modeLabel << "...\n";
    SeedFiles(fx, embedding);

    auto block = RunProvider(fx, agent, session, embedding);
    Assert(block.has_value(), modeLabel + ": provider returned a block");
    if (!block) return;

    Assert(block->content.find("## MEMORY FILES") != std::string::npos,
           modeLabel + ": MEMORY FILES section present");
    Assert(block->content.find("/globals/shared-notes.md") != std::string::npos,
           modeLabel + ": GLOBAL file's chunk surfaces in assembled context");
    Assert(block->content.find("quantumharmonics calibration must run") != std::string::npos,
           modeLabel + ": GLOBAL file's content surfaces");
    Assert(block->content.find("/agents/7/private-notes.md") != std::string::npos,
           modeLabel + ": agent-owned file still surfaces");
    Assert(block->content.find("/agents/3/foreign-notes.md") == std::string::npos,
           modeLabel + ": foreign agent's file stays hidden");
}

// Session utterance quotes both files' distinctive sentences so the cached
// vector path's tier selection is deterministic (each chunk's similarity to
// the session embedding is high, not threshold-dependent).
const char* kSessionUtterance =
    "The quantumharmonics calibration must run before every deployment. "
    "The quantumharmonics calibration log for agent seven lives here.";

int TestKeywordFallbackIncludesGlobalFiles() {
    Fixture fx;
    Agent agent = MakeAgent(7);
    auto session = MakeSession(kSessionUtterance);
    AssertGlobalAndAgentFilesVisible(fx, agent, session, nullptr, "keyword fallback mode");
    return 0;
}

int TestCachedVectorPathIncludesGlobalFiles() {
    const char* modelPath = std::getenv("ANIMUS_TEST_EMBEDDING_MODEL");
    if (!modelPath || !*modelPath) {
        std::cerr << "  [ActiveMemoryProvider] cached vector path SKIPPED — "
                  << "ANIMUS_TEST_EMBEDDING_MODEL not set (no GGUF model in repo)\n";
        return 0;
    }

    EmbeddingService embedding(modelPath);
    if (!embedding.IsAvailable()) {
        std::cerr << "  [ActiveMemoryProvider] cached vector path SKIPPED — "
                  << "embedding model at ANIMUS_TEST_EMBEDDING_MODEL failed to load\n";
        return 0;
    }

    Fixture fx;
    Agent agent = MakeAgent(7);
    auto session = MakeSession(kSessionUtterance);
    AssertGlobalAndAgentFilesVisible(fx, agent, session, &embedding, "cached vector path");
    return 0;
}

} // namespace

int main() {
    TestKeywordFallbackIncludesGlobalFiles();
    TestCachedVectorPathIncludesGlobalFiles();
    if (g_failures == 0) {
        std::cerr << "ActiveMemoryProviderTests: all assertions passed\n";
        return 0;
    }
    std::cerr << "ActiveMemoryProviderTests: " << g_failures << " assertion(s) failed\n";
    return 1;
}
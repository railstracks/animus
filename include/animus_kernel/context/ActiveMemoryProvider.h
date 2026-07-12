#pragma once

#include "animus_kernel/ContextProviderRegistry.h"

namespace animus::kernel {

namespace memory { class MemoryStore; class MemoryFileStore; }
namespace ontology { class OntologyStore; }

class DiaryStore;
class SessionTagsStore;
class SessionManager;
class Scheduler;
class EmbeddingService;

// ============================================================================
// ActiveMemoryProvider — composes temporal, episodic, diary, and ontology
// blocks into a single ContextBlock for the agent preamble.
//
// Priority: 30 (between identity at 0 and session notes at 50)
//
// Sub-blocks:
//   1. Temporal grounding (mandatory) — clock, last session, upcoming events
//   2. Episodic memory — top-weighted observations per layer
//   3. Diary presence — last 3 entry titles
//   4. Ontology — tag-matched + session-context entities (item/property limits)
// ============================================================================

class ActiveMemoryProvider : public IContextProvider {
public:
    struct Config {
        int max_ontology_items = 15;
        int max_properties_per_item = 5;
        int ontology_baseline_threshold = 10;  // Include all entities when count ≤ threshold
    };

    ActiveMemoryProvider(const memory::MemoryStore* memoryStore,
                         const DiaryStore* diaryStore,
                         const ontology::OntologyStore* ontologyStore,
                         const memory::MemoryFileStore* memoryFileStore,
                         const SessionTagsStore* tagsStore,
                         const SessionManager* sessionManager,
                         const Scheduler* scheduler,
                         const EmbeddingService* embeddingService = nullptr);

    std::string Name() const override { return "active_memory"; }
    int Priority() const override { return 30; }

    std::optional<ContextBlock> Provide(
        const Agent& agent,
        const SessionAccess& session) const override;

private:
    // Session context flags — determines which sub-blocks to include.
    struct SessionFlags {
        bool include_episodic = true;
        bool include_diary = true;
        bool include_ontology = true;
        bool include_memory_files = true;
    };

    SessionFlags DetermineFlags(const SessionAccess& session) const;

    // Session keyword extraction (shared across sub-blocks)
    struct SessionKeywords {
        std::vector<std::string> tags;  // from session tags
        std::vector<std::string> all;   // tags + turn keywords
    };
    SessionKeywords CollectSessionKeywords(const SessionAccess& session) const;

    // Sub-block builders — each appends to the output string.
    void AppendTemporal(std::string& out, const Agent& agent,
                        const SessionAccess& session) const;
    void AppendEpisodic(std::string& out, const Agent& agent,
                        const std::vector<std::string>& keywords) const;
    void AppendPerspectives(std::string& out, const Agent& agent) const;
    void AppendDiaryTitles(std::string& out, const Agent& agent) const;
    void AppendOntology(std::string& out, const Agent& agent,
                        const SessionAccess& session,
                        const std::vector<std::string>& tags,
                        const std::vector<std::string>& keywords) const;
    void AppendMemoryFiles(std::string& out, const Agent& agent,
                           const SessionAccess& session,
                           const std::vector<std::string>& keywords) const;

    // Token estimation helper (4 chars ≈ 1 token)
    static std::size_t EstimateTokens(const std::string& text) {
        return text.size() / 4 + 1;
    }

    const memory::MemoryStore* m_memoryStore;
    const DiaryStore* m_diaryStore;
    const ontology::OntologyStore* m_ontologyStore;
    const memory::MemoryFileStore* m_memoryFileStore;
    const EmbeddingService* m_embeddingService;
    const SessionTagsStore* m_tagsStore;
    const SessionManager* m_sessionManager;
    const Scheduler* m_scheduler;
    Config m_config;
};

} // namespace animus::kernel

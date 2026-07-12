#include "animus_kernel/context/ActiveMemoryProvider.h"

#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>

#include "animus_kernel/AgentStore.h"
#include "animus_kernel/Session.h"
#include "animus_kernel/SessionManager.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/MemoryFileStore.h"
#include "animus_kernel/EmbeddingService.h"

namespace {

// Format a Unix-ms timestamp as "YYYY-MM-DD HH:MM" (UTC)
std::string FormatTimestamp(int64_t unix_ms) {
    if (unix_ms == 0) return "";
    auto t = static_cast<time_t>(unix_ms / 1000);
    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_buf);
    return buf;
}

} // namespace
#include "animus_kernel/OntologyStore.h"
#include "animus_kernel/SessionTagsStore.h"
#include "animus_kernel/admin/DiaryManager.h"
#include "animus_kernel/scheduler/Scheduler.h"
namespace animus::kernel {

// ============================================================================
// Construction
// ============================================================================

ActiveMemoryProvider::ActiveMemoryProvider(
        const memory::MemoryStore* memoryStore,
        const DiaryStore* diaryStore,
        const ontology::OntologyStore* ontologyStore,
        const memory::MemoryFileStore* memoryFileStore,
        const SessionTagsStore* tagsStore,
        const SessionManager* sessionManager,
        const Scheduler* scheduler,
        const EmbeddingService* embeddingService)
    : m_memoryStore(memoryStore)
    , m_diaryStore(diaryStore)
    , m_ontologyStore(ontologyStore)
    , m_memoryFileStore(memoryFileStore)
    , m_embeddingService(embeddingService)
    , m_tagsStore(tagsStore)
    , m_sessionManager(sessionManager)
    , m_scheduler(scheduler)
    , m_config{} {}

// ============================================================================
// Session-type variant flags
// ============================================================================

ActiveMemoryProvider::SessionFlags
ActiveMemoryProvider::DetermineFlags(const SessionAccess& session) const {
    SessionFlags flags;

    // Default: all sub-blocks included (direct human chat).
    //
    // Session type is determined by the connector in the session key.
    // Known connectors: "slack", "webchat", "cli", "irc", "webhook".
    // Scheduler-driven events (gallivanting, consolidation) don't create
    // sessions — they run through separate pipelines. When session types
    // are formalized, this method extends with:
    //   if (sessionType == "gallivanting") { ... }
    //   if (sessionType == "consolidation") { ... }
    //   if (sessionType == "project") { ... }

    // Future: check session metadata for type hints.
    // For now, all sessions are "direct" type — full active memory.

    return flags;
}

// ============================================================================
// Provide — main entry point
// ============================================================================

std::optional<ContextBlock> ActiveMemoryProvider::Provide(
        const Agent& agent,
        const SessionAccess& session) const {
    std::cerr << "[active-memory] Provide() entered, calling DetermineFlags..." << std::endl;
    auto flags = DetermineFlags(session);

    std::cerr << "[active-memory] Provide() start" << std::endl;

    // Collect session-context keywords once, shared across sub-blocks
    std::cerr << "[active-memory] calling CollectSessionKeywords..." << std::endl;
    auto sessionKeywords = CollectSessionKeywords(session);
    std::cerr << "[active-memory] keywords collected: " << sessionKeywords.all.size() << std::endl;

    std::string content;

    AppendTemporal(content, agent, session);
    std::cerr << "[active-memory] temporal done" << std::endl;
    if (flags.include_episodic) {
        AppendEpisodic(content, agent, sessionKeywords.all);
        AppendPerspectives(content, agent);
    }
    std::cerr << "[active-memory] episodic+perspectives done" << std::endl;
    if (flags.include_diary) AppendDiaryTitles(content, agent);
    if (flags.include_ontology) AppendOntology(content, agent, session, sessionKeywords.tags, sessionKeywords.all);
    std::cerr << "[active-memory] ontology done" << std::endl;
    if (flags.include_memory_files) {
        std::cerr << "[active-memory] starting memory files..." << std::endl;
        AppendMemoryFiles(content, agent, session, sessionKeywords.all);
        std::cerr << "[active-memory] memory files done" << std::endl;
    }

    if (content.empty()) return std::nullopt;

    ContextBlock block;
    block.name = "Active Memory";
    block.content = content;
    block.priority = 30;
    return block;
}

// ============================================================================
// Session keyword extraction (shared across sub-blocks)
// ============================================================================

ActiveMemoryProvider::SessionKeywords
ActiveMemoryProvider::CollectSessionKeywords(
        const SessionAccess& session) const {
    SessionKeywords result;
    std::cerr << "[active-memory] CollectSessionKeywords: getting key..." << std::endl;
    std::string sessionKey = session.Key().ToString();
    std::cerr << "[active-memory] CollectSessionKeywords: key=" << sessionKey << std::endl;

    // Session tags — higher signal, shorter, more precise
    if (m_tagsStore) {
        std::cerr << "[active-memory] CollectSessionKeywords: querying tags..." << std::endl;
        auto tags = m_tagsStore->ListForSession(sessionKey);
        std::cerr << "[active-memory] CollectSessionKeywords: tags=" << tags.size() << std::endl;
        for (const auto& t : tags) {
            result.tags.push_back(t.tag);
            result.all.push_back(t.tag);
        }
    }

    // Recent turns — extract significant words
    std::cerr << "[active-memory] CollectSessionKeywords: getting turns..." << std::endl;
    auto turns = session.Turns();
    std::cerr << "[active-memory] CollectSessionKeywords: turns=" << turns.size() << std::endl;
    // Use last 10 turns maximum for keyword extraction
    std::size_t startIdx = turns.size() > 10 ? turns.size() - 10 : 0;
    std::set<std::string> seenWords;
    for (std::size_t i = startIdx; i < turns.size(); ++i) {
        const auto& turn = turns[i];
        std::istringstream iss(turn.content);
        std::string word;
        while (iss >> word) {
            std::string normalized;
            for (char c : word) {
                if (std::isalnum(static_cast<unsigned char>(c))) normalized += static_cast<char>(std::tolower(c));
            }
            if (normalized.size() > 3 && seenWords.insert(normalized).second) {
                result.all.push_back(normalized);
            }
        }
    }

    return result;
}

// ============================================================================
// Temporal Grounding (mandatory)
// ============================================================================

void ActiveMemoryProvider::AppendTemporal(std::string& out,
                                           const Agent& /*agent*/,
                                           const SessionAccess& session) const {
    const auto now = std::chrono::system_clock::now();
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    const auto nowT = std::chrono::system_clock::to_time_t(now);

    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M UTC", std::gmtime(&nowT));

    out += "# ACTIVE MEMORY\n";
    out += "Generated: ";
    out += timeBuf;
    out += "\n\n## TEMPORAL CONTEXT\n";
    out += "Current: ";
    out += timeBuf;
    out += "\n";

    // Session duration from earliest turn
    const auto& turns = session.Turns();
    if (!turns.empty()) {
        int64_t earliestMs = turns.front().unix_ms;
        if (earliestMs > 0 && static_cast<int64_t>(nowMs) > earliestMs) {
            int64_t diffSec = (static_cast<int64_t>(nowMs) - earliestMs) / 1000;
            if (diffSec < 60) {
                out += "Session duration: <1 min\n";
            } else if (diffSec < 3600) {
                out += "Session duration: ~" + std::to_string(diffSec / 60) + " min\n";
            } else {
                out += "Session duration: ~" + std::to_string(diffSec / 3600) + "h\n";
            }
        }
    }

    out += "\n";
}

// ============================================================================
// Episodic Memory — top-weighted observations per layer
// ============================================================================

void ActiveMemoryProvider::AppendEpisodic(std::string& out,
                                           const Agent& agent,
                                           const std::vector<std::string>& keywords) const {
    if (!m_memoryStore) return;

    auto* store = const_cast<memory::MemoryStore*>(m_memoryStore);
    auto layers = store->ListLayersForAgent(agent.id);
    if (layers.empty()) layers = store->ListLayers();
    if (layers.empty()) return;

    // Collect all candidate observations across all enabled layers
    struct Candidate {
        std::string ageTag;
        std::string text;
        double weight;
        int64_t createdMs;
        int64_t updatedMs;
        int relevance{0};  // keyword match count
    };

    std::vector<Candidate> candidates;

    for (const auto& layer : layers) {
        if (!layer.enabled) continue;
        auto observations = store->ListObservationsForLayer(layer.id);
        for (const auto& obs : observations) {
            // Skip retired observations
            if (obs.memory_state == memory::MemoryState::Deprecated) continue;
            // Skip superseded observations (only show current versions)
            if (obs.superseded_by != 0) continue;

            std::string ageTag = "[" + layer.name + "]";
            candidates.push_back({ageTag, obs.text, obs.weight, obs.created_at_unix_ms, obs.updated_at_unix_ms, 0});
        }
    }

    if (candidates.empty()) return;

    // Score relevance: count keyword matches in observation text
    if (!keywords.empty()) {
        for (auto& c : candidates) {
            std::string textLower = c.text;
            std::transform(textLower.begin(), textLower.end(),
                           textLower.begin(), ::tolower);
            for (const auto& kw : keywords) {
                std::string kwLower = kw;
                std::transform(kwLower.begin(), kwLower.end(),
                               kwLower.begin(), ::tolower);
                if (textLower.find(kwLower) != std::string::npos) c.relevance++;
            }
        }
    }

    // Sort: relevance-first, then weight descending, then recency
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) {
            if (a.relevance != b.relevance) return a.relevance > b.relevance;
            if (a.weight != b.weight) return a.weight > b.weight;
            return a.createdMs > b.createdMs;
        });

    // Token budget: relevance-first, pad with remaining
    const uint32_t tokenBudget = agent.budget.episodicTokenBudget;
    const uint32_t charBudget = tokenBudget * 4;
    uint32_t charsUsed = 0;

    std::string episodicContent = "## EPISODIC MEMORY\n";
    int emitted = 0;

    for (const auto& c : candidates) {
        // When pad_context is off, skip items with no relevance match
        if (!agent.pad_context && c.relevance == 0 && !keywords.empty()) continue;

        std::string ts = FormatTimestamp(c.createdMs);
        std::string updatedTs = (c.updatedMs > 0 && c.updatedMs != c.createdMs)
            ? std::string(", updated ") + FormatTimestamp(c.updatedMs)
            : "";

        std::string line = c.ageTag;
        if (!ts.empty()) line += "[" + ts + updatedTs + "]";
        line += " " + c.text +
            " (weight: " + std::to_string(static_cast<int>(c.weight * 100) / 100.0) + ")\n";

        if (charsUsed + line.size() > charBudget && emitted > 0) break;

        episodicContent += line;
        charsUsed += line.size();
        emitted++;
    }

    out += episodicContent + "\n";
}

// ============================================================================
// Perspectives — retrospective, current, future per layer
// ============================================================================

void ActiveMemoryProvider::AppendPerspectives(std::string& out,
                                               const Agent& agent) const {
    if (!m_memoryStore) return;

    auto* store = const_cast<memory::MemoryStore*>(m_memoryStore);
    auto layers = store->ListLayersForAgent(agent.id);
    if (layers.empty()) {
        layers = store->ListLayers();
    }

    const uint32_t tokenBudget = agent.budget.perspectivesTokenBudget;
    const uint32_t charBudget = tokenBudget * 4;
    uint32_t charsUsed = 0;
    bool hasAny = false;

    // Prioritize shorter-lived layers first (day > week > month > ...)
    // They contain the most immediately relevant perspectives.
    for (const auto& layer : layers) {
        if (!layer.enabled) continue;

        auto perspective = store->GetPerspective(layer.id);
        if (!perspective.has_value()) continue;

        std::string block;

        if (!perspective->retrospective.empty()) {
            block += "### " + layer.name + " Retrospective\n";
            block += perspective->retrospective + "\n";
        }
        if (!perspective->current_perspective.empty()) {
            block += "### " + layer.name + " Current Posture\n";
            block += perspective->current_perspective + "\n";
        }
        if (!perspective->future_perspective.empty()) {
            block += "### " + layer.name + " Future Outlook\n";
            block += perspective->future_perspective + "\n";
        }

        if (block.empty()) continue;

        if (charsUsed + block.size() > charBudget && hasAny) break;

        if (!hasAny) {
            out += "## TEMPORAL PERSPECTIVES\n";
            hasAny = true;
        }
        out += block + "\n";
        charsUsed += block.size();
    }

    if (hasAny) out += "\n";
}

// ============================================================================
// Diary Titles — last 3 entries
// ============================================================================

void ActiveMemoryProvider::AppendDiaryTitles(std::string& out,
                                               const Agent& agent) const {
    if (!m_diaryStore) return;

    auto* store = const_cast<DiaryStore*>(m_diaryStore);
    auto entries = store->ListByAgent(agent.id, 0, 0, "", 3, 0);
    if (entries.empty()) return;

    out += "## RECENT DIARY\n";
    for (const auto& entry : entries) {
        // Extract first line as title
        std::string title = entry.content;
        auto newlinePos = title.find('\n');
        if (newlinePos != std::string::npos) {
            title = title.substr(0, newlinePos);
        }
        // Truncate long titles
        if (title.size() > 80) {
            title = title.substr(0, 77) + "...";
        }

        // Date stamp
        char dateBuf[32];
        auto entryT = static_cast<time_t>(entry.timestamp_unix_ms / 1000);
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", std::gmtime(&entryT));

        out += "- " + title + " (" + dateBuf + ")\n";
    }
    out += "\n";
}

// ============================================================================
// Ontology — tag-matched + session-context entities
// ============================================================================

namespace {

std::string ToLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

} // anonymous namespace

void ActiveMemoryProvider::AppendOntology(std::string& out,
                                           const Agent& agent,
                                           const SessionAccess& session,
                                           const std::vector<std::string>& tags,
                                           const std::vector<std::string>& keywords) const {
    if (!m_ontologyStore) return;

    // Agent-scoped entity listing — only this agent's ontology
    auto* store = const_cast<ontology::OntologyStore*>(m_ontologyStore);
    auto allEntities = store->ListEntities(std::nullopt, agent.id);

    if (allEntities.empty()) return;

    // Determine baseline inclusion: when ontology is small, include everything
    bool baselineInclude = allEntities.size() <= static_cast<size_t>(m_config.ontology_baseline_threshold);

    // Score entities by tag match count and keyword match
    struct ScoredEntity {
        const ontology::OntologyEntity* entity;
        int tagScore;     // number of matching tags
        int kwScore;      // number of matching keywords (including property values)
        bool isBaseline;  // included because ontology is small, not because of match
    };
    std::vector<ScoredEntity> scored;

    for (const auto& entity : allEntities) {
        ScoredEntity se{&entity, 0, 0, false};
        std::string nameLower = ToLower(entity.name);
        std::string pathLower = ToLower(entity.full_path);

        // Tag matching — check if any tag matches entity name or path
        for (const auto& tag : tags) {
            std::string tagLower = ToLower(tag);
            if (nameLower.find(tagLower) != std::string::npos ||
                pathLower.find(tagLower) != std::string::npos) {
                se.tagScore++;
            }
        }

        // Keyword matching — check if any keyword appears in name or path
        for (const auto& kw : keywords) {
            if (nameLower.find(kw) != std::string::npos ||
                pathLower.find(kw) != std::string::npos) {
                se.kwScore++;
            }
        }

        // Property value matching — check if any keyword appears in property values
        if (se.tagScore == 0 && se.kwScore == 0) {
            auto props = store->ListProperties(entity.id);
            for (const auto& p : props) {
                if (p.memory_state == memory::MemoryState::Deprecated) continue;
                std::string valLower = ToLower(p.value);
                for (const auto& kw : keywords) {
                    if (valLower.find(kw) != std::string::npos) {
                        se.kwScore++;
                    }
                }
            }
        }

        // Include if matched, or if baseline inclusion applies
        se.isBaseline = (se.tagScore == 0 && se.kwScore == 0);
        if (se.tagScore > 0 || se.kwScore >= 1 || (baselineInclude && se.isBaseline && agent.pad_context)) {
            scored.push_back(se);
        }
    }

    if (scored.empty()) return;

    // Sort: tag matches first, then keyword matches, then baseline
    std::sort(scored.begin(), scored.end(),
        [](const ScoredEntity& a, const ScoredEntity& b) {
            // Baseline items go last
            if (a.isBaseline != b.isBaseline) return !a.isBaseline;
            if (a.tagScore != b.tagScore) return a.tagScore > b.tagScore;
            return a.kwScore > b.kwScore;
        });

    // Group by root category
    // Category order: persons, concepts, procedures, events, locations, organizations, projects
    static const std::vector<ontology::RootCategory> categoryOrder = {
        ontology::RootCategory::Persons,
        ontology::RootCategory::Concepts,
        ontology::RootCategory::Procedures,
        ontology::RootCategory::Events,
        ontology::RootCategory::Locations,
        ontology::RootCategory::Organizations,
        ontology::RootCategory::Projects
    };

    std::map<ontology::RootCategory, std::vector<const ScoredEntity*>> grouped;
    for (const auto& se : scored) {
        grouped[se.entity->root_category].push_back(&se);
    }

    out += "## SEMANTIC MEMORY\n";
    const uint32_t tokenBudget = agent.budget.semanticTokenBudget;
    const uint32_t charBudget = tokenBudget * 4;
    uint32_t charsUsed = 0;
    int itemsRendered = 0;

    for (auto cat : categoryOrder) {
        auto it = grouped.find(cat);
        if (it == grouped.end()) continue;

        // Category header
        std::string catHeader = "\n### " + ontology::OntologyStore::RootCategoryToString(cat) + "\n";
        if (charsUsed + catHeader.size() > charBudget && charsUsed > 0) break;
        out += catHeader;
        charsUsed += catHeader.size();

        for (const auto* sePtr : it->second) {
            if (itemsRendered >= m_config.max_ontology_items) break;

            auto props = store->ListProperties(sePtr->entity->id);

            // Include all property states: Current, New, and Deprecated.
            // Deprecated properties may still be relevant context for the agent.
            std::vector<std::string> propLines;
            for (const auto& p : props) {
                if (static_cast<int>(propLines.size()) >= m_config.max_properties_per_item) break;
                propLines.push_back(p.key + ": " + p.value);
            }

            std::string line = sePtr->entity->name;
            if (!propLines.empty()) {
                line += " — ";
                for (size_t i = 0; i < propLines.size(); ++i) {
                    if (i > 0) line += ", ";
                    line += propLines[i];
                }
            }
            line += "\n";

            if (charsUsed + line.size() > charBudget && charsUsed > 0) break;

            out += line;
            charsUsed += line.size();
            itemsRendered++;
        }

        if (itemsRendered >= m_config.max_ontology_items) break;
    }
    out += "\n";
}

// ============================================================================
// Memory File Context — relevance-first with padding
// ============================================================================

void ActiveMemoryProvider::AppendMemoryFiles(std::string& out,
                                              const Agent& agent,
                                              const SessionAccess& session,
                                              const std::vector<std::string>& keywords) const {
    if (!m_memoryFileStore) return;

    // Fetch all active (non-superseded) files for this agent
    auto* store = const_cast<memory::MemoryFileStore*>(m_memoryFileStore);
    auto allFiles = store->ListFiles(
        std::nullopt,  // all types
        1000,          // generous limit
        0);

    // --- Chunk files at section boundaries (markdown headers) ---
    struct Chunk {
        std::string source_path;
        std::string content;
        float similarity{0.0f};   // Embedding cosine similarity
        int relevance{0};          // Keyword match count (fallback)
        std::size_t tokenEstimate{0};
        int64_t imported_at_unix_ms{0};
        bool needsEmbedding{false}; // True when no cached embedding exists
    };

    auto chunkFile = [](const memory::MemoryFile& file) -> std::vector<Chunk> {
        std::vector<Chunk> chunks;
        const std::string& content = file.content;

        // Split at markdown headers (lines starting with #)
        std::vector<std::pair<std::size_t, std::size_t>> sections;
        std::size_t pos = 0;
        std::size_t lastHeader = 0;
        bool foundAnyHeader = false;

        while (pos < content.size()) {
            std::size_t nl = content.find('\n', pos);
            std::size_t lineEnd = (nl == std::string::npos) ? content.size() : nl;
            std::size_t lineStart = pos;
            if (lineEnd > lineStart && content[lineStart] == '#') {
                if (foundAnyHeader) sections.push_back({lastHeader, lineStart});
                lastHeader = lineStart;
                foundAnyHeader = true;
            }
            pos = (nl == std::string::npos) ? content.size() : nl + 1;
        }

        if (foundAnyHeader) {
            sections.push_back({lastHeader, content.size()});
        } else {
            sections.push_back({0, content.size()});
        }

        for (const auto& [start, end] : sections) {
            Chunk chunk;
            chunk.source_path = file.source_path;
            chunk.content = content.substr(start, end - start);
            chunk.imported_at_unix_ms = file.imported_at_unix_ms;
            chunks.push_back(std::move(chunk));
        }
        return chunks;
    };

    // --- Compute session embedding for semantic similarity ---
    std::vector<float> sessionEmbedding;
    bool useEmbeddings = false;
    if (m_embeddingService && m_embeddingService->IsAvailable()) {
        std::cerr << "[active-memory] embedding service available, computing session embedding...\n";
        // Build session text from recent turns for embedding
        std::string sessionText;
        auto turns = session.Turns();
        std::size_t startIdx = turns.size() > 6 ? turns.size() - 6 : 0;
        for (std::size_t i = startIdx; i < turns.size(); ++i) {
            if (turns[i].role == "user" || turns[i].role == "assistant") {
                sessionText += turns[i].content + " ";
            }
        }
        // Also include draft message context from keywords
        for (const auto& kw : keywords) sessionText += kw + " ";

        // Truncate session text to fit within the embedding model's token cap.
        // The embedding service caps at ~190 tokens. At ~4 chars/token,
        // 700 chars gives headroom for BOS and keywords.
        const std::size_t sessionTextCharBudget = 700;
        if (sessionText.size() > sessionTextCharBudget) {
            // Keep the most recent context (end of string)
            sessionText = "... " + sessionText.substr(sessionText.size() - sessionTextCharBudget);
        }

        if (!sessionText.empty()) {
            auto emb = m_embeddingService->Embed(sessionText);
            if (emb.has_value()) {
                sessionEmbedding = std::move(*emb);
                useEmbeddings = true;
                std::cerr << "[active-memory] session embedding computed (dim=" << sessionEmbedding.size() << ")\n";
            }
        }
    }

    // --- Build candidate chunks ---
    // Try cached chunks from DB first (pre-computed embeddings)
    bool usedCachedChunks = false;
    std::vector<Chunk> candidates;

    if (useEmbeddings) {
        std::cerr << "[active-memory] fetching cached chunks for agent.numeric_id=" << agent.numeric_id << std::endl;
        auto cachedChunks = store->GetChunksForAgent(agent.numeric_id);
        std::cerr << "[active-memory] cached chunks: " << cachedChunks.size() << "\n";
        if (!cachedChunks.empty()) {
            bool allHaveEmbeddings = true;
            for (const auto& cc : cachedChunks) {
                Chunk chunk;
                chunk.source_path = cc.source_path;
                chunk.content = cc.content;
                chunk.imported_at_unix_ms = 0;
                chunk.tokenEstimate = EstimateTokens(cc.content);

                if (!cc.embedding.empty()) {
                    chunk.similarity = EmbeddingService::CosineSimilarity(
                        sessionEmbedding, cc.embedding);
                } else {
                    allHaveEmbeddings = false;
                    chunk.needsEmbedding = true;
                }

                candidates.push_back(std::move(chunk));
            }
            usedCachedChunks = true;

            // Keyword matching for low-similarity cached chunks (supplementary signal)
            for (auto& c : candidates) {
                if (c.similarity < 0.3f) {
                    std::string contentLower = c.content;
                    std::transform(contentLower.begin(), contentLower.end(),
                                   contentLower.begin(), ::tolower);
                    for (const auto& kw : keywords) {
                        std::string kwLower = kw;
                        std::transform(kwLower.begin(), kwLower.end(),
                                       kwLower.begin(), ::tolower);
                        if (contentLower.find(kwLower) != std::string::npos) c.relevance++;
                    }
                }
            }

            // If some chunks lack embeddings, compute them on-the-fly
            if (!allHaveEmbeddings) {
                int embedded = 0;
                for (auto& c : candidates) {
                    // Only compute for chunks that actually lack embeddings.
                    // Check via a flag, not similarity==0 (orthogonal vectors also give 0).
                    if (c.needsEmbedding && !c.content.empty()) {
                        auto emb = m_embeddingService->Embed(c.content);
                        if (emb.has_value()) {
                            c.similarity = EmbeddingService::CosineSimilarity(
                                sessionEmbedding, *emb);
                            embedded++;
                        }
                    }
                }
                std::cerr << "[active-memory] computed " << embedded << " missing chunk embeddings on-the-fly\n";
            }
        }
    }

    // Fallback: on-the-fly chunking with keyword scoring (when no cached chunks)
    // NOTE: We do NOT compute embeddings on-the-fly here — that blocks prompt
    // assembly for seconds or minutes. If cached chunks aren't available,
    // keyword matching is used. The background ComputePendingEmbeddings job
    // populates the cache asynchronously.
    if (!usedCachedChunks) {
        useEmbeddings = false;  // Force keyword path
        std::cerr << "[active-memory] no cached chunks, using keyword fallback for " << allFiles.size() << " files\n";
        for (auto& file : allFiles) {
            if (file.agent_id != 0 && file.agent_id != agent.numeric_id) continue;
            if (file.superseded) continue;

            auto chunks = chunkFile(file);
            for (auto& chunk : chunks) {
                // Semantic similarity scoring (primary)
                if (useEmbeddings) {
                    auto chunkEmb = m_embeddingService->Embed(chunk.content);
                    if (chunkEmb.has_value()) {
                        chunk.similarity = EmbeddingService::CosineSimilarity(
                            sessionEmbedding, *chunkEmb);
                    }
                }

                if (!useEmbeddings || chunk.similarity < 0.3f) {
                    std::string contentLower = chunk.content;
                    std::transform(contentLower.begin(), contentLower.end(),
                                   contentLower.begin(), ::tolower);
                    for (const auto& kw : keywords) {
                        std::string kwLower = kw;
                        std::transform(kwLower.begin(), kwLower.end(),
                                       kwLower.begin(), ::tolower);
                        if (contentLower.find(kwLower) != std::string::npos) chunk.relevance++;
                    }
                }

                chunk.tokenEstimate = EstimateTokens(chunk.content);
                candidates.push_back(std::move(chunk));
            }
        }
    }

    if (candidates.empty()) return;

    // --- Sort: primary signal (similarity or keyword relevance), then recency ---
    if (useEmbeddings) {
        std::stable_sort(candidates.begin(), candidates.end(),
            [](const Chunk& a, const Chunk& b) {
                if (a.similarity != b.similarity) return a.similarity > b.similarity;
                return a.imported_at_unix_ms > b.imported_at_unix_ms;
            });
    } else {
        std::stable_sort(candidates.begin(), candidates.end(),
            [](const Chunk& a, const Chunk& b) {
                if (a.relevance != b.relevance) return a.relevance > b.relevance;
                return a.imported_at_unix_ms > b.imported_at_unix_ms;
            });
    }

    // Fill budget with chunks (store copies, not pointers)
    // Two-tier budget: Tier 1 (directly relevant, ≥0.7) + Tier 2 (ambient, 0.3–0.7)
    // Per-file cap: no single file may consume more than tier1Budget tokens
    // in Tier 1, preventing one large file from monopolizing the budget.
    const std::size_t tier1Budget = static_cast<std::size_t>(agent.budget.memoryFileTokenBudget);
    const std::size_t tier2Budget = static_cast<std::size_t>(agent.budget.ambientContextLimit);
    const std::size_t totalBudget = tier1Budget + tier2Budget;
    std::size_t spent = 0;
    std::unordered_map<std::string, std::size_t> spentPerFile;
    std::vector<Chunk> tier1Selected, tier2Selected;

    auto tryAddChunk = [&](const Chunk& c, std::vector<Chunk>& selected, std::size_t budget,
                           std::size_t perFileCap) {
        if (spent + c.tokenEstimate > totalBudget) return;
        if (selected.size() >= 100) return; // safety limit

        std::size_t& fileSpent = spentPerFile[c.source_path];
        if (fileSpent >= perFileCap) return;

        if (spent + c.tokenEstimate > budget || fileSpent + c.tokenEstimate > perFileCap) {
            // Truncate to fit remaining budget (whichever is tighter)
            std::size_t remainingBudget = (budget > spent) ? (budget - spent) : 0;
            std::size_t remainingFile = perFileCap - fileSpent;
            std::size_t remaining = std::min(remainingBudget, remainingFile);
            if (remaining == 0) return;
            Chunk truncated = c;
            std::size_t maxChars = remaining * 4;
            if (maxChars < truncated.content.size()) {
                std::size_t lastNl = truncated.content.rfind('\n', maxChars);
                if (lastNl != std::string::npos) maxChars = lastNl;
                truncated.content = truncated.content.substr(0, maxChars) + "\n[...]";
                truncated.tokenEstimate = EstimateTokens(truncated.content);
            }
            if (spent + truncated.tokenEstimate <= totalBudget) {
                spent += truncated.tokenEstimate;
                fileSpent += truncated.tokenEstimate;
                selected.push_back(std::move(truncated));
            }
        } else {
            spent += c.tokenEstimate;
            fileSpent += c.tokenEstimate;
            selected.push_back(c);
        }
    };

    // Per-file cap for Tier 1: tier1Budget tokens (same as the tier budget).
    // This means a single file can at most fill Tier 1 entirely, leaving
    // Tier 2 for other files. Per-file cap for Tier 2 is more generous
    // (tier1Budget + tier2Budget) since ambient context is meant to pad.
    const std::size_t tier1PerFile = tier1Budget;
    const std::size_t tier2PerFile = tier1Budget + tier2Budget;

    if (useEmbeddings) {
        // Tier 1: directly relevant (similarity ≥ 0.7)
        for (const auto& c : candidates) {
            if (c.similarity < 0.7f) break;
            tryAddChunk(c, tier1Selected, tier1Budget, tier1PerFile);
        }

        // Tier 2: ambient awareness (0.3 ≤ similarity < 0.7), if pad_context enabled
        if (agent.pad_context) {
            for (const auto& c : candidates) {
                if (c.similarity >= 0.7f) continue;  // already in Tier 1
                if (c.similarity < 0.3f) break;       // below ambient threshold
                tryAddChunk(c, tier2Selected, tier1Budget + tier2Budget, tier2PerFile);
            }
        }
    } else {
        // Keyword fallback: single tier, relevance > 0 is Tier 1
        for (const auto& c : candidates) {
            if (c.relevance > 0) {
                tryAddChunk(c, tier1Selected, tier1Budget, tier1PerFile);
            }
        }
        if (agent.pad_context) {
            for (const auto& c : candidates) {
                if (c.relevance > 0) continue;
                tryAddChunk(c, tier2Selected, tier1Budget + tier2Budget, tier2PerFile);
            }
        }
    }

    if (tier1Selected.empty() && tier2Selected.empty()) {
        std::cerr << "[active-memory] WARNING: no chunks selected. candidates=" << candidates.size()
                  << " useEmbeddings=" << useEmbeddings;
        if (!candidates.empty()) {
            std::cerr << " top_similarity=" << candidates[0].similarity
                      << " top_relevance=" << candidates[0].relevance
                      << " top_path=" << candidates[0].source_path;
        }
        std::cerr << " pad_context=" << agent.pad_context << std::endl;
        return;
    }

    // Render — Tier 1 directly relevant
    if (!tier1Selected.empty()) {
        out += "\n## MEMORY FILES\n";
        std::string lastPath;
        for (const auto& c : tier1Selected) {
            if (c.source_path != lastPath) {
                out += "\n### " + c.source_path;
                if (useEmbeddings && c.similarity > 0) {
                    out += " (similarity: " + std::to_string(static_cast<int>(c.similarity * 100)) + "%)";
                } else if (c.relevance > 0) {
                    out += " (" + std::to_string(c.relevance) + " keyword matches)";
                }
                out += "\n";
                lastPath = c.source_path;
            }
            out += c.content;
            if (!c.content.empty() && c.content.back() != '\n') out += '\n';
        }
    }

    // Render — Tier 2 ambient awareness
    if (!tier2Selected.empty()) {
        if (tier1Selected.empty()) out += "\n## MEMORY FILES\n";
        out += "\n### Related but not directly relevant\n";
        std::string lastPath;
        for (const auto& c : tier2Selected) {
            if (c.source_path != lastPath) {
                out += "\n**" + c.source_path + "**";
                if (useEmbeddings && c.similarity > 0) {
                    out += " (similarity: " + std::to_string(static_cast<int>(c.similarity * 100)) + "%)";
                }
                out += "\n";
                lastPath = c.source_path;
            }
            out += c.content;
            if (!c.content.empty() && c.content.back() != '\n') out += '\n';
        }
    }
}

} // namespace animus::kernel

#include "animus_kernel/context/SessionReportProvider.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/EmbeddingService.h"
#include "animus_kernel/Session.h"

#include <chrono>
#include <sstream>
#include <algorithm>
#include <set>

namespace animus::kernel {

SessionReportProvider::SessionReportProvider(
        const SessionReportStore* store,
        const EmbeddingService* embeddingService)
    : m_store(store)
    , m_embeddingService(embeddingService) {}

std::optional<ContextBlock> SessionReportProvider::Provide(
        const Agent& agent,
        const SessionAccess& session) const {

    if (!m_store) return std::nullopt;

    // Fetch reports with embeddings (up to 50 for scoring pool)
    auto reports = m_store->ListRecentWithEmbeddings(agent.id, 50);
    if (reports.empty()) return std::nullopt;

    // Skip if only report is for the current session
    int otherCount = 0;
    for (const auto& r : reports) {
        if (static_cast<SessionId>(r.session_id) != session.Id()) ++otherCount;
    }
    if (otherCount == 0) return std::nullopt;

    // --- Compute session embedding for relevance scoring ---
    std::vector<float> sessionEmbedding;
    bool useEmbeddings = false;
    if (m_embeddingService && m_embeddingService->IsAvailable()) {
        std::string sessionText = BuildSessionText(session);
        if (!sessionText.empty()) {
            auto emb = m_embeddingService->Embed(sessionText);
            if (emb.has_value()) {
                sessionEmbedding = std::move(*emb);
                useEmbeddings = true;
            }
        }
    }

    // --- Score and partition into pools ---
    struct ScoredReport {
        const SessionReportWithEmbedding* report;
        float similarity{0.0f};
        int64_t ageMs{0};
        bool recent{false};       // within recency window
        bool hasEmbedding{false};
    };

    const int64_t now = NowUnixMs();
    const int64_t RECENCY_WINDOW_MS = 12 * 3600 * 1000;  // 12 hours

    std::vector<ScoredReport> scored;
    for (const auto& r : reports) {
        if (static_cast<SessionId>(r.session_id) == session.Id()) continue;

        ScoredReport sr;
        sr.report = &r;
        sr.ageMs = now - r.updated_at_unix_ms;
        sr.recent = sr.ageMs < RECENCY_WINDOW_MS;
        sr.hasEmbedding = r.has_embedding;

        if (useEmbeddings && r.has_embedding) {
            sr.similarity = EmbeddingService::CosineSimilarity(
                sessionEmbedding, r.embedding);
        }

        scored.push_back(std::move(sr));
    }

    if (scored.empty()) return std::nullopt;

    // --- Two-pool selection ---
    // Pool 1 (Recency): Most recent reports within the window.
    //   Budget allocation: 40% of total session report budget.
    // Pool 2 (Relevance): Highest similarity to current session.
    //   Budget allocation: remaining 60%.
    //   Falls back to recency when embeddings are unavailable.
    //
    // A report can appear in both pools but is only rendered once
    // (deduplicated by session_id).

    const std::uint32_t totalCharBudget = agent.budget.sessionReportTokenBudget * 4;
    const std::uint32_t recencyCharBudget = (totalCharBudget * 40) / 100;
    const std::uint32_t relevanceCharBudget = totalCharBudget - recencyCharBudget;

    // Sort by recency (most recent first) — for Pool 1
    auto byRecency = scored;
    std::sort(byRecency.begin(), byRecency.end(),
        [](const ScoredReport& a, const ScoredReport& b) {
            return a.ageMs < b.ageMs;
        });

    // Sort by relevance (highest similarity first) — for Pool 2
    auto byRelevance = scored;
    std::sort(byRelevance.begin(), byRelevance.end(),
        [](const ScoredReport& a, const ScoredReport& b) {
            return a.similarity > b.similarity;
        });

    struct SelectedEntry {
        const SessionReportWithEmbedding* report;
        std::string formatted;
        std::size_t charLen;
        std::string poolLabel;  // "recent", "relevant", or "recent+relevant"
    };

    std::set<int64_t> selectedSessionIds;
    std::vector<SelectedEntry> selected;
    std::uint32_t charsUsed = 0;

    auto formatEntry = [&](const ScoredReport& sr, const char* poolTag) -> SelectedEntry {
        const auto* r = sr.report;
        std::ostringstream entry;

        entry << "[Session #" << r->session_id;
        if (sr.ageMs < 60000) {
            entry << " — just now";
        } else if (sr.ageMs < 3600000) {
            entry << " — " << (sr.ageMs / 60000) << "m ago";
        } else if (sr.ageMs < 86400000) {
            entry << " — " << (sr.ageMs / 3600000) << "h ago";
        } else {
            entry << " — " << (sr.ageMs / 86400000) << "d ago";
        }
        if (useEmbeddings && sr.similarity > 0) {
            entry << " · " << static_cast<int>(sr.similarity * 100) << "% match";
        }
        entry << "]\n";

        if (!r->summary.empty())          entry << "Summary: " << r->summary << "\n";
        if (!r->past_events.empty())      entry << "Past: " << r->past_events << "\n";
        if (!r->current_activity.empty()) entry << "Current: " << r->current_activity << "\n";
        if (!r->forward_look.empty())     entry << "Forward: " << r->forward_look << "\n";

        std::string text = entry.str();
        return {r, text, text.size(), poolTag};
    };

    // Pool 1: Recency — fill up to recencyCharBudget
    std::uint32_t recencyUsed = 0;
    for (const auto& sr : byRecency) {
        if (recencyUsed >= recencyCharBudget && !selected.empty()) break;

        // Estimate size before formatting
        std::size_t estLen = 200;  // rough estimate per entry
        if (recencyUsed + estLen > recencyCharBudget && !selected.empty()) break;

        auto entry = formatEntry(sr, "recent");
        if (recencyUsed + entry.charLen > recencyCharBudget && !selected.empty()) {
            // Would exceed budget — stop if we already have entries
            break;
        }

        if (selectedSessionIds.insert(sr.report->session_id).second) {
            selected.push_back(std::move(entry));
            recencyUsed += static_cast<std::uint32_t>(selected.back().charLen);
            charsUsed += static_cast<std::uint32_t>(selected.back().charLen);
        }
    }

    // Pool 2: Relevance — fill remaining budget
    std::uint32_t relevanceUsed = 0;
    for (const auto& sr : byRelevance) {
        if (relevanceUsed >= relevanceCharBudget) break;

        // Skip already-selected (from recency pool)
        if (selectedSessionIds.count(sr.report->session_id)) continue;

        // Skip very low relevance if we're not padding
        if (useEmbeddings && sr.similarity < 0.2f && !agent.pad_context) continue;

        auto entry = formatEntry(sr, "relevant");
        if (charsUsed + entry.charLen > totalCharBudget && !selected.empty()) break;

        if (selectedSessionIds.insert(sr.report->session_id).second) {
            selected.push_back(std::move(entry));
            relevanceUsed += static_cast<std::uint32_t>(selected.back().charLen);
            charsUsed += static_cast<std::uint32_t>(selected.back().charLen);
        }
    }

    if (selected.empty()) return std::nullopt;

    // --- Render ---
    std::ostringstream oss;
    oss << "## ACTIVE SESSIONS\n\n";

    for (const auto& entry : selected) {
        oss << entry.formatted << "\n";
    }

    ContextBlock block;
    block.name = "session_reports";
    block.content = oss.str();
    block.priority = 35;
    return block;
}

std::string SessionReportProvider::BuildSessionText(const SessionAccess& session) const {
    // Build text from recent turns for embedding computation
    std::string text;
    auto turns = session.Turns();
    std::size_t startIdx = turns.size() > 6 ? turns.size() - 6 : 0;
    for (std::size_t i = startIdx; i < turns.size(); ++i) {
        if (turns[i].role == "user" || turns[i].role == "assistant") {
            text += turns[i].content + " ";
        }
    }

    // Truncate to ~700 chars for embedding model
    const std::size_t charBudget = 700;
    if (text.size() > charBudget) {
        text = "... " + text.substr(text.size() - charBudget);
    }

    return text;
}

int64_t SessionReportProvider::NowUnixMs() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace animus::kernel

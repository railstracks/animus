#pragma once

#include "animus_kernel/ContextProviderRegistry.h"
#include "animus_kernel/SessionReportStore.h"

namespace animus::kernel {

// Forward declaration
class EmbeddingService;

// Injects session reports into active memory using a two-pool mix:
//
//   Pool 1 (Recency):  Most recently updated reports (within 12h window).
//                       Budget allocation: 40% of sessionReportTokenBudget.
//
//   Pool 2 (Relevance): Highest embedding similarity to current session.
//                       Budget allocation: remaining 60%.
//                       Falls back to recency order when embeddings
//                       are unavailable.
//
// Reports appearing in both pools are rendered only once.
// Priority 35 — after active memory (30), before channel context.

class SessionReportProvider : public IContextProvider {
public:
    SessionReportProvider(const SessionReportStore* store,
                          const EmbeddingService* embeddingService = nullptr);

    std::string Name() const override { return "session_reports"; }
    int Priority() const override { return 35; }

    std::optional<ContextBlock> Provide(
        const Agent& agent,
        const SessionAccess& session) const override;

private:
    const SessionReportStore* m_store;
    const EmbeddingService* m_embeddingService;

    std::string BuildSessionText(const SessionAccess& session) const;
    static int64_t NowUnixMs();
};

} // namespace animus::kernel

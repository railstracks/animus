#pragma once

#include <cstddef>

#include "animus_kernel/CompactionPolicy.h"
#include "animus_kernel/Session.h"

namespace animus::kernel {

class ConversationCompactor {
public:
    ConversationCompactor(Session& session, CompactionPolicy policy);

    bool ShouldCompact() const;
    bool Compact();

    std::size_t EstimateTokenCount() const;
    static std::size_t EstimateTokenCount(const SessionTurn& turn);

private:
    std::size_t DetermineTurnsToCompact() const;
    SessionTurn BuildSummaryTurn(const std::vector<SessionTurn>& compacted_turns) const;

    Session& m_session;
    CompactionPolicy m_policy;
};

} // namespace animus::kernel

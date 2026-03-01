#include "animus_kernel/ConversationCompactor.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace animus::kernel {

ConversationCompactor::ConversationCompactor(Session& session, CompactionPolicy policy)
    : m_session(session), m_policy(std::move(policy)) {
}

bool ConversationCompactor::ShouldCompact() const {
    if (m_policy.maxTurnsBeforeCompaction > 0
        && m_session.Turns().size() > m_policy.maxTurnsBeforeCompaction) {
        return true;
    }

    if (m_policy.maxTokenEstimateBeforeCompaction > 0
        && EstimateTokenCount() > m_policy.maxTokenEstimateBeforeCompaction) {
        return true;
    }

    return false;
}

bool ConversationCompactor::Compact() {
    if (!ShouldCompact()) {
        return false;
    }

    const std::size_t turnsToCompact = DetermineTurnsToCompact();
    if (turnsToCompact == 0) {
        return false;
    }

    std::vector<SessionTurn> compactedTurns(
        m_session.Turns().begin(),
        m_session.Turns().begin() + static_cast<std::ptrdiff_t>(turnsToCompact));

    if (m_policy.compactionStrategy == CompactionStrategy::Summarize) {
        m_session.SetCompactionSummary(BuildSummaryTurn(compactedTurns));
    }

    m_session.TrimOldestTurns(turnsToCompact);
    return true;
}

std::size_t ConversationCompactor::EstimateTokenCount() const {
    std::size_t total = 0;
    for (const auto& turn : m_session.Turns()) {
        total += EstimateTokenCount(turn);
    }
    if (const SessionTurn* summary = m_session.GetCompactionSummary()) {
        total += EstimateTokenCount(*summary);
    }
    return total;
}

std::size_t ConversationCompactor::EstimateTokenCount(const SessionTurn& turn) {
    const std::size_t textBytes = turn.role.size() + turn.content.size();
    return std::max<std::size_t>(1, (textBytes + 3) / 4);
}

std::size_t ConversationCompactor::DetermineTurnsToCompact() const {
    const auto& turns = m_session.Turns();
    if (turns.empty()) {
        return 0;
    }

    std::size_t keepCount = turns.size();
    if (m_policy.maxTurnsBeforeCompaction > 0 && keepCount > m_policy.maxTurnsBeforeCompaction) {
        keepCount = m_policy.maxTurnsBeforeCompaction;
    }

    if (m_policy.maxTokenEstimateBeforeCompaction > 0) {
        std::size_t tokenCount = EstimateTokenCount();
        std::size_t compactedCount = turns.size() - keepCount;
        while (keepCount > 1 && tokenCount > m_policy.maxTokenEstimateBeforeCompaction
               && compactedCount < turns.size()) {
            tokenCount -= EstimateTokenCount(turns[compactedCount]);
            ++compactedCount;
            --keepCount;
        }
    }

    if (keepCount >= turns.size()) {
        return 0;
    }

    return turns.size() - keepCount;
}

SessionTurn ConversationCompactor::BuildSummaryTurn(
    const std::vector<SessionTurn>& compacted_turns) const {
    SessionTurn summaryTurn;
    summaryTurn.role = "system";
    summaryTurn.unix_ms = compacted_turns.back().unix_ms;
    summaryTurn.is_summary = true;

    std::ostringstream content;
    content << "Compacted " << compacted_turns.size()
            << " conversation turns into a placeholder summary.";
    summaryTurn.content = content.str();

    summaryTurn.compacted_from.reserve(compacted_turns.size());
    for (const auto& turn : compacted_turns) {
        summaryTurn.compacted_from.push_back(turn.turn_id);
    }

    return summaryTurn;
}

} // namespace animus::kernel

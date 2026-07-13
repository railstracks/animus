#include "animus_kernel/PromptAssembler.h"
#include "animus_kernel/Session.h"
#include "animus_kernel/TokenEstimate.h"

#include <chrono>

namespace animus::kernel {

std::size_t TokenEstimator::Estimate(const std::string& text) const {
    return TokenEstimate::Estimate(text);
}

std::size_t TokenEstimator::EstimateTurns(const std::vector<SessionTurn>& turns) const {
    std::size_t total = 0;
    for (const auto& turn : turns) {
        // Use cached token count if available (set during compaction or previous estimation)
        if (turn.token_count > 0) {
            total += turn.token_count + 4;  // +4 for message overhead
        } else {
            total += Estimate(turn.content) + 4;
        }
    }
    return total;
}

PromptAssemblyResult PromptAssembler::BuildFromAccess(
    const SessionAccess& session,
    const std::string& userMessage,
    const std::string& systemPrompt,
    const std::string& modelId,
    std::size_t contextWindowTokens,
    float compactionThreshold) const {

    return Build(
        session.Turns(),
        session.GetCompactionSummary(),
        userMessage,
        systemPrompt,
        modelId,
        contextWindowTokens,
        compactionThreshold);
}

PromptAssemblyResult PromptAssembler::Build(
    const std::vector<SessionTurn>& turns,
    const SessionTurn* compactionSummary,
    const std::string& userMessage,
    const std::string& systemPrompt,
    const std::string& modelId,
    std::size_t contextWindowTokens,
    float compactionThreshold) const {

    PromptAssemblyResult result;
    result.request.model = modelId;
    result.request.temperature = 0.7f;
    result.request.stream = true;
    result.request.max_tokens = -1;

    // 1. System prompt
    if (!systemPrompt.empty()) {
        result.request.messages.push_back({"system", systemPrompt});
        result.system_prompt_tokens = m_estimator.Estimate(systemPrompt) + 4;
    }

    // 2. Compaction summary
    if (compactionSummary && !compactionSummary->content.empty()) {
        const std::string summaryText =
            "[Previous conversation summary]\n" + compactionSummary->content;
        result.request.messages.push_back({"system", summaryText});
        result.compaction_summary_tokens = m_estimator.Estimate(summaryText) + 4;
    }

    // 3. Session history
    for (const auto& turn : turns) {
        if (turn.is_summary) continue;
        if (turn.is_compacted) continue;

        // Skip thinking-only turns (turns with no content and no tool_calls).
        // thinking_content is a display-only property — never sent to the LLM.
        if (turn.content.empty() && turn.tool_calls.empty()) continue;

        llm::LLMMessage msg;
        msg.role = turn.role;
        msg.content = turn.content;

        // Carry tool result metadata for proper API formatting
        if (turn.role == "tool") {
            msg.tool_call_id = turn.tool_call_id;
            msg.name = turn.tool_name;
        }

        // Carry tool calls for assistant turns (needed for API compliance)
        if (turn.role == "assistant" && !turn.tool_calls.empty()) {
            for (const auto& tc : turn.tool_calls) {
                msg.tool_calls.push_back(llm::LLMToolCall{
                    tc.id, tc.name, tc.arguments
                });
            }
        }

        result.request.messages.push_back(std::move(msg));
    }

    // 4. User message (only if non-empty — avoids duplicate from session turns)
    if (!userMessage.empty()) {
        result.request.messages.push_back({"user", userMessage});
        result.draft_message_tokens = m_estimator.Estimate(userMessage) + 4;
    }

    // Compute session turn tokens (already assembled above)
    for (const auto& turn : turns) {
        if (turn.is_summary) continue;
        if (turn.is_compacted) continue;
        if (turn.content.empty() && turn.tool_calls.empty()) continue;
        if (turn.token_count > 0) {
            result.session_turns_tokens += turn.token_count + 4;
        } else {
            result.session_turns_tokens += m_estimator.Estimate(turn.content) + 4;
        }
    }

    // Token budget check
    result.total_tokens = result.system_prompt_tokens
                        + result.compaction_summary_tokens
                        + result.session_turns_tokens
                        + result.draft_message_tokens;

    if (contextWindowTokens > 0) {
        const std::size_t budget = static_cast<std::size_t>(
            static_cast<float>(contextWindowTokens) * compactionThreshold);

        if (result.total_tokens > budget && turns.size() > 4) {
            result.needs_compaction = true;
        }
    }

    return result;
}

} // namespace animus::kernel
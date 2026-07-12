#pragma once

#include <string>
#include <vector>

#include "animus_kernel/llm/LLMTypes.h"

namespace animus::kernel {

class Session;
class SessionAccess;

// Estimates token count for budget checks. Very rough: ~4 chars per token.
class TokenEstimator {
public:
    std::size_t Estimate(const std::string& text) const;
    std::size_t EstimateTurns(const std::vector<struct SessionTurn>& turns) const;
};

// Builds an LLMRequest from session state + user message.
struct PromptAssemblyResult {
    bool needs_compaction{false};
    llm::LLMRequest request;

    // Per-section estimated token counts (populated during Build()).
    std::size_t system_prompt_tokens{0};
    std::size_t compaction_summary_tokens{0};
    std::size_t session_turns_tokens{0};
    std::size_t draft_message_tokens{0};
    std::size_t total_tokens{0};
};

class PromptAssembler {
public:
    // Build from SessionAccess (primary interface).
    PromptAssemblyResult BuildFromAccess(
        const SessionAccess& session,
        const std::string& userMessage,
        const std::string& systemPrompt,
        const std::string& modelId,
        std::size_t contextWindowTokens,
        float compactionThreshold = 0.8f) const;

    // Build from raw turn list (for direct use without session).
    PromptAssemblyResult Build(
        const std::vector<SessionTurn>& turns,
        const SessionTurn* compactionSummary,
        const std::string& userMessage,
        const std::string& systemPrompt,
        const std::string& modelId,
        std::size_t contextWindowTokens,
        float compactionThreshold = 0.8f) const;

private:
    TokenEstimator m_estimator;
};

} // namespace animus::kernel

#pragma once

#include "animus_kernel/CompactionService.h"
#include "animus_kernel/ChainRunner.h"
#include "animus_kernel/Session.h"
#include "animus_kernel/llm/LLMProviderRegistry.h"
#include "animus_kernel/llm/LLMTypes.h"

#include <memory>
#include <string>

namespace animus::kernel {

// ============================================================================
// CompactionSummaryGenerator — generates compaction summaries using an LLM
//
// Creates a SummaryGenerator callback that uses the configured provider
// to produce high-quality summaries of session context for compaction.
// ============================================================================

class CompactionSummaryGenerator {
public:
    struct Config {
        std::string provider_id;       // Provider to use for summaries (registry key)
        std::string config_id;         // Config ID for provider lookup
        std::string model;             // Model to use (empty = provider default)
        float temperature{0.3f};       // Low temperature for factual summaries
        int max_tokens{2048};          // Max tokens for summary output
    };

    explicit CompactionSummaryGenerator(
        llm::LLMProviderRegistry& providers,
        ProviderConfigLookup configLookup,
        const Config& config);

    // Creates a SummaryGenerator callback suitable for CompactionService.
    SummaryGenerator CreateCallback();

    // Direct summary generation — for use outside the callback pattern.
    std::string Generate(
        const std::string& systemPrompt,
        const std::vector<SessionTurn>& turns,
        const std::string& model);

private:
    std::unique_ptr<llm::ILLMProvider> CreateProvider() const;

    llm::LLMProviderRegistry& m_providers;
    ProviderConfigLookup m_configLookup;
    Config m_config;
};

} // namespace animus::kernel
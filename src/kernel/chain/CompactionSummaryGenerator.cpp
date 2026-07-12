#include "animus_kernel/CompactionSummaryGenerator.h"

#include <sstream>

namespace animus::kernel {

CompactionSummaryGenerator::CompactionSummaryGenerator(
    llm::LLMProviderRegistry& providers,
    ProviderConfigLookup configLookup,
    const Config& config)
    : m_providers(providers)
    , m_configLookup(std::move(configLookup))
    , m_config(config)
{
}

SummaryGenerator CompactionSummaryGenerator::CreateCallback() {
    auto* providers = &m_providers;
    auto configLookup = m_configLookup;  // Copy (std::function is copyable)
    auto config = m_config;

    return [providers, configLookup, config](
        const std::string& systemPrompt,
        const std::vector<SessionTurn>& turns,
        const std::string& model) -> std::string {
        CompactionSummaryGenerator gen(*providers, configLookup, config);
        return gen.Generate(systemPrompt, turns, model);
    };
}

std::unique_ptr<llm::ILLMProvider> CompactionSummaryGenerator::CreateProvider() const {
    llm::LLMProviderConfig config;
    config.provider_id = m_config.provider_id;
    config.default_model = m_config.model;

    if (m_configLookup) {
        auto lookedUp = m_configLookup(m_config.config_id);
        if (lookedUp) {
            config = std::move(*lookedUp);
            // Override model if specified
            if (!m_config.model.empty()) {
                config.default_model = m_config.model;
            }
        }
    }

    return m_providers.Create(config);
}

std::string CompactionSummaryGenerator::Generate(
    const std::string& systemPrompt,
    const std::vector<SessionTurn>& turns,
    const std::string& model)
{
    // Build the compaction prompt
    std::ostringstream prompt;
    prompt << "You are summarizing a conversation session for context compression.\n"
              "Produce a structured summary preserving:\n\n"
              "1. **Decisions made** — what was decided and why\n"
              "2. **Actions taken** — files modified, commands run, their outcomes\n"
              "3. **Current state** — what exists now (files, config, running processes)\n"
              "4. **Open issues** — unresolved problems, blocked tasks, pending questions\n"
              "5. **Key context** — names, IDs, paths, credentials, versions referenced later\n"
              "6. **Emotional/narrative state** — ongoing interpersonal dynamics worth preserving\n\n"
              "Be specific. Include exact file paths, commit hashes, error messages.\n"
              "A future reader must be able to resume work from this summary without\n"
              "access to the original messages.\n\n"
              "Format as a concise, structured summary.\n";

    if (!systemPrompt.empty()) {
        prompt << "\nSystem context (agent identity): " << systemPrompt.substr(0, 500) << "\n";
    }

    prompt << "\n--- CONVERSATION TO SUMMARIZE ---\n\n";

    // Include up to the last N turns worth of content
    // For very long sessions, include the first few and last many
    const std::size_t maxTurns = turns.size();
    const std::size_t headCount = 5;
    const std::size_t tailCount = std::min(maxTurns, static_cast<std::size_t>(50));
    const std::size_t skipFrom = headCount;
    const std::size_t skipTo = maxTurns > (headCount + tailCount + 5) ? maxTurns - tailCount : skipFrom;

    for (std::size_t i = 0; i < turns.size(); ++i) {
        // Skip middle turns for very long sessions
        if (maxTurns > (headCount + tailCount + 5) && i >= skipFrom && i < skipTo) {
            if (i == skipFrom) {
                prompt << "... (" << (skipTo - skipFrom) << " turns omitted) ...\n\n";
            }
            continue;
        }

        const auto& turn = turns[i];
        std::string preview = turn.content;
        // Truncate very long messages
        if (preview.size() > 2000) {
            preview = preview.substr(0, 2000) + "... [truncated]";
        }
        prompt << "[" << turn.role << "] " << preview << "\n\n";
    }

    prompt << "--- END CONVERSATION ---\n\n"
              "Produce the summary now.";

    // Create the LLM provider and call
    const std::string useModel = model.empty() ? m_config.model : model;

    auto provider = CreateProvider();
    if (!provider) {
        return "[Compaction: LLM provider unavailable. Falling back to structural summary.]\n"
               "Turns compacted: " + std::to_string(turns.size());
    }

    llm::LLMRequest request;
    request.model = useModel;
    request.temperature = m_config.temperature;
    request.max_tokens = m_config.max_tokens;
    request.stream = false;

    request.messages.push_back({"system", prompt.str()});
    request.messages.push_back({"user", "Summarize the above conversation for context compression."});

    std::string error;
    try {
        auto response = provider->Complete(request, &error);
        if (!response.content.empty()) {
            return response.content;
        }

        // If content is empty but thinking_content exists, use that
        if (!response.thinking_content.empty()) {
            return response.thinking_content;
        }

        return "[Compaction: LLM returned empty response" +
               (error.empty() ? "." : ": " + error) +
               ". Turns compacted: " + std::to_string(turns.size()) + "]";
    } catch (const std::exception& e) {
        return "[Compaction: LLM error: " + std::string(e.what()) +
               ". Turns compacted: " + std::to_string(turns.size()) + "]";
    }
}

} // namespace animus::kernel
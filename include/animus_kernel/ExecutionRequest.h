#pragma once

#include <cstdint>
#include <string>

namespace animus::kernel {

/// Fully resolved execution parameters for a chain run.
///
/// All fields are resolved before ChainRunner is invoked — no further
/// agent/provider lookups are needed inside the chain loop. Callers
/// (ChatSessionService, AgentKernel, ConsolidationPipeline) fill this
/// struct via their own resolution path, then pass it to ChainRunner.
///
/// For backward compatibility, the old ExecuteOnSession / ExecuteStreamingOnSession
/// overloads that take individual string/size_t parameters still work — they
/// internally resolve into an ExecutionRequest via ResolveAgentOverrides().
struct ExecutionRequest {
    // --- Provider resolution ---
    std::string providerId;       // Registry key (e.g., "ollama", "openai")
    std::string configId;         // Config-lookup key (provider instance/config ID)
    std::string model;            // Resolved model name

    // --- Token budget ---
    std::size_t contextWindow = 128000;  // Resolved context window (tokens)

    // --- Prompt ---
    std::string systemPrompt;     // Base system prompt (before context provider blocks)

    // --- Reasoning ---
    bool reasoningEnabled = false;
    std::string reasoningEffort;

    // --- Chain budgets ---
    std::uint32_t maxChainSteps = 200;
    std::uint32_t maxToolCallsPerChain = 100;
};

} // namespace animus::kernel

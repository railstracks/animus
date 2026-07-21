#pragma once

#include <chrono>
#include <functional>
#include <string>

#include "animus_kernel/IncomingEvent.h"
#include "animus_kernel/llm/ILLMProvider.h"
#include "animus_kernel/llm/LLMProviderConfig.h"
#include "animus_kernel/llm/LLMProviderRegistry.h"
#include "animus_kernel/llm/LLMTypes.h"
#include "animus_kernel/PromptAssembler.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/tools/ToolTypes.h"
#include "animus_kernel/SessionNotesStore.h"
#include "animus_kernel/ContextProviderRegistry.h"
#include "animus_kernel/NodeManager.h"
#include "animus_kernel/MessageQueue.h"

#include "animus_kernel/llm/LLMProviderConfig.h"

#include "animus_kernel/PromptLogStore.h"

namespace animus::kernel {

class SessionManager;

struct ChainResult {
    std::string response;
    bool success{false};
    std::string error;
    int prompt_tokens{0};
    int completion_tokens{0};
    double elapsed_ms{0.0};
    bool triggered_compaction{false};

    // Tool execution trace
    int chain_steps{0};
    int tool_calls_executed{0};
};

// Executes the full inference chain:
//   incoming event → session resolution → prompt assembly → LLM call →
//   (optional) tool execution loop → response → turn storage

using ProviderConfigLookup = std::function<std::optional<llm::LLMProviderConfig>(const std::string&)>;

/// Callback for streaming user-visible text (from tools with stream_to_user result mode).
using ChainTextCallback = std::function<void(const std::string&)>;

/// Callback for streaming thinking text (native provider thinking).
using ChainThinkingCallback = std::function<void(const std::string&)>;

/// Callback for streaming tool execution events.
using ChainToolEventCallback = std::function<void(const ToolCall&, const ToolResult&)>;

/// Callback for tool call notifications (fires before execution).
using ChainToolCallCallback = std::function<void(const ToolCall&)>;

/// Callback fired when the chain produces a user-visible assistant message
/// at any step (not thinking, not tool calls). Used by channel dispatch to
/// send intermediate messages immediately rather than only the final response.
using ChainAssistantMessageCallback = std::function<void(const std::string&)>;

class ChainRunner {
public:
    ChainRunner(
        llm::LLMProviderRegistry& providers,
        SessionManager& sessions,
        ToolRegistry& tools,
        ProviderConfigLookup configLookup);

    // Execute a full chain synchronously (non-streaming).
    ChainResult Execute(
        const IncomingEvent& event,
        const std::string& systemPrompt,
        const std::string& providerId,
        const std::string& model,
        std::size_t contextWindowTokens);

    // Streaming variant — calls tokenCallback as tokens arrive.
    // textCallback receives user-visible text from stream_to_user tools.
    ChainResult ExecuteStreaming(
        const IncomingEvent& event,
        const std::string& systemPrompt,
        const std::string& providerId,
        const std::string& model,
        std::size_t contextWindowTokens,
        llm::LLMTokenCallback tokenCallback,
        ChainTextCallback textCallback = nullptr,
        ChainToolEventCallback toolEventCallback = nullptr);

    // Lower-level: execute on an already-resolved session.
    // Callbacks are per-invocation — they fire only for this call.
    ChainResult ExecuteOnSession(
        SessionAccess& session,
        const std::string& userMessage,
        const std::string& systemPrompt,
        const std::string& providerId,
        const std::string& configId,
        const std::string& model,
        std::size_t contextWindowTokens,
        ChainThinkingCallback thinkingCallback = nullptr,
        ChainToolCallCallback toolCallCallback = nullptr,
        ChainAssistantMessageCallback assistantMessageCallback = nullptr,
        const std::string& reasoningEffortOverride = "",
        bool reasoningEnabledOverride = false,
        bool hasReasoningEnabledOverride = false);

    ChainResult ExecuteStreamingOnSession(
        SessionAccess& session,
        const std::string& userMessage,
        const std::string& systemPrompt,
        const std::string& providerId,
        const std::string& configId,
        const std::string& model,
        std::size_t contextWindowTokens,
        llm::LLMTokenCallback tokenCallback,
        ChainTextCallback textCallback = nullptr,
        ChainToolEventCallback toolEventCallback = nullptr,
        ChainThinkingCallback thinkingCallback = nullptr,
        ChainToolCallCallback toolCallCallback = nullptr,
        ChainAssistantMessageCallback assistantMessageCallback = nullptr,
        const std::string& reasoningEffortOverride = "",
        bool reasoningEnabledOverride = false,
        bool hasReasoningEnabledOverride = false);

    // Set the agent store for per-agent tool filtering.
    void SetAgentStore(AgentStore* store) { m_agentStore = store; }

    // Set the session notes store for preamble injection.
    void SetSessionNotesStore(SessionNotesStore* store) { m_sessionNotesStore = store; }

    // Set the context provider registry for prompt assembly.
    void SetContextRegistry(ContextProviderRegistry* registry) { m_contextRegistry = registry; }
    void SetNodeManager(NodeManager* nm) { m_nodeManager = nm; }

    // Set the message queue for interjection (injecting messages into active chains).
    void SetMessageQueue(MessageQueue* mq) { m_messageQueue = mq; }

    // Configuration
    void SetReasoningEnabled(bool enabled);
    bool IsReasoningEnabled() const { return m_reasoningEnabled; }

    void SetReasoningEffort(const std::string& effort);
    std::string GetReasoningEffort() const { return m_reasoningEffort; }

    void SetReasoningInstruction(const std::string& instruction);
    std::string GetReasoningInstruction() const { return m_reasoningInstruction; }

    // Update reasoning mode — now sets native thinking on requests.
    void UpdateReasoningMode(bool enabled, const std::string& instruction);

    void SetMaxChainSteps(std::uint32_t steps) { m_maxChainSteps = steps; }
    void SetMaxToolCallsPerChain(std::uint32_t max) { m_maxToolCallsPerChain = max; }

    // Set the prompt log store for LLM call logging.
    void SetPromptLogStore(PromptLogStore* store) { m_promptLogStore = store; }
    void SetPromptLogLevel(PromptLogLevel level) { m_promptLogLevel = level; }

    // Access the provider registry (for compaction summary generation, etc.)
    llm::LLMProviderRegistry& GetProviders() { return m_providers; }
    const ProviderConfigLookup& GetConfigLookup() const { return m_configLookup; }

private:
    std::unique_ptr<llm::ILLMProvider> CreateProvider(
        const std::string& providerId,
        const std::string& configId,
        std::string* error) const;

    llm::LLMResponse CallLLM(
        llm::ILLMProvider& provider,
        llm::LLMRequest& request,
        llm::LLMTokenCallback tokenCallback,
        std::string* error);

    /// Log an LLM call to the prompt log store (if enabled).
    void LogPromptCall(
        const std::string& agent_id,
        int64_t session_id,
        const std::string& provider,
        const std::string& model,
        const llm::LLMResponse& response,
        const llm::LLMRequest& request,
        int chain_step,
        int latency_ms);

    // Process a parsed LLM response: store turns, execute tools.
    // thinkingContent: accumulated native thinking text (may be empty).
    // Returns true if the chain should continue (tool calls need another LLM round).
    bool ProcessResponse(
        SessionAccess& session,
        llm::LLMResponse& response,
        const std::string& content,
        const std::string& thinkingContent,
        std::vector<llm::LLMMessage>& toolResultMessages,
        std::string& userVisibleText,
        std::string& toolOutputText,
        int& toolCallsExecuted,
        ChainToolEventCallback toolEventCallback = nullptr,
        ChainToolCallCallback toolCallCallback = nullptr);

    std::vector<llm::LLMToolDef> GetToolDefinitionsForRequest() const;

    // Get tool definitions filtered by an agent's enabled_tools whitelist.
    // If agent has empty enabled_tools, returns all (backwards compat).
    std::vector<llm::LLMToolDef> GetToolDefinitionsForAgent(const std::string& agent_id) const;

    // Get tool definitions filtered by agent whitelist AND session type.
    // Session-type-specific tools (e.g. "consolidation" for consolidation sessions)
    // are only included when session_type matches.
    llm::LLMToolDef ConvertToolDef(const ToolDefinition& def) const;

    std::vector<llm::LLMToolDef> GetToolDefinitionsForSession(
        const std::string& agent_id,
        const std::string& session_type) const;

    llm::LLMProviderRegistry& m_providers;
    SessionManager& m_sessions;
    ToolRegistry& m_tools;
    PromptAssembler m_assembler;
    ProviderConfigLookup m_configLookup;

    // Agent store for per-agent tool filtering (may be null if not set)
    AgentStore* m_agentStore{nullptr};

    // Session notes store for preamble injection (may be null if not set)
    SessionNotesStore* m_sessionNotesStore{nullptr};

    // Context provider registry for prompt assembly (may be null if not set)
    ContextProviderRegistry* m_contextRegistry{nullptr};

    // Node manager for remote tool execution (may be null if not set)
    NodeManager* m_nodeManager{nullptr};

    // Message queue for interjection (may be null if not set)
    MessageQueue* m_messageQueue{nullptr};

    // Thinking mode
    bool m_reasoningEnabled{false};
    std::string m_reasoningEffort{"high"};
    std::string m_reasoningInstruction; // Kept for API compat, not injected into prompts

    // Chain budgets
    std::uint32_t m_maxChainSteps{200};
    std::uint32_t m_maxToolCallsPerChain{100};

    // Prompt logging
    PromptLogStore* m_promptLogStore{nullptr};
    PromptLogLevel m_promptLogLevel{PromptLogLevel::Default};
};

} // namespace animus::kernel

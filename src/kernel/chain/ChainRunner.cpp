#include "animus_kernel/ChainRunner.h"
#include "animus_kernel/Log.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <json/json.h>

#include "animus_kernel/SessionManager.h"
#include "animus_kernel/ContextProviderRegistry.h"
#include "animus_kernel/llm/LLMProviderBase.h"

namespace animus::kernel {

ChainRunner::ChainRunner(
    llm::LLMProviderRegistry& providers,
    SessionManager& sessions,
    ToolRegistry& tools,
    ProviderConfigLookup configLookup)
    : m_providers(providers)
    , m_sessions(sessions)
    , m_tools(tools)
    , m_configLookup(std::move(configLookup))
    , m_schemaService(std::make_unique<ToolSchemaService>(tools))
    , m_execService(std::make_unique<ToolExecutionService>(tools)) {
}

void ChainRunner::SetReasoningEnabled(bool enabled) {
    m_reasoningEnabled = enabled;
}

void ChainRunner::SetReasoningEffort(const std::string& effort) {
    m_reasoningEffort = effort;
}

void ChainRunner::SetReasoningInstruction(const std::string& instruction) {
    m_reasoningInstruction = instruction;
}

void ChainRunner::UpdateReasoningMode(bool enabled, const std::string& instruction) {
    m_reasoningEnabled = enabled;
    m_reasoningInstruction = instruction;

    // Native thinking mode: no ReplyTool registration needed.
    // The provider handles thinking/reasoning natively via reasoning_effort.
}

// ============================================================================
// Public entry points
// ============================================================================

ChainResult ChainRunner::Execute(
    const IncomingEvent& event,
    const std::string& systemPrompt,
    const std::string& providerId,
    const std::string& model,
    std::size_t contextWindowTokens) {

    const auto start = std::chrono::steady_clock::now();

    auto ctx = m_sessions.Resolve(event);
    if (!ctx.primary) {
        ChainResult result;
        result.error = "failed to resolve session for event";
        return result;
    }

    auto result = ExecuteOnSession(
        ctx.primary, event.text, systemPrompt,
        providerId, providerId, model, contextWindowTokens);

    if (result.success) {
        m_sessions.FlushSession(ctx.primary.Id());
    }

    const auto end = std::chrono::steady_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
    return result;
}

ChainResult ChainRunner::ExecuteStreaming(
    const IncomingEvent& event,
    const std::string& systemPrompt,
    const std::string& providerId,
    const std::string& model,
    std::size_t contextWindowTokens,
    llm::LLMTokenCallback tokenCallback,
    ChainTextCallback textCallback,
    ChainToolEventCallback toolEventCallback) {

    const auto start = std::chrono::steady_clock::now();

    auto ctx = m_sessions.Resolve(event);
    if (!ctx.primary) {
        ChainResult result;
        result.error = "failed to resolve session for event";
        return result;
    }

    auto result = ExecuteStreamingOnSession(
        ctx.primary, event.text, systemPrompt,
        providerId, providerId, model, contextWindowTokens,
        std::move(tokenCallback), std::move(textCallback),
        std::move(toolEventCallback));

    if (result.success) {
        m_sessions.FlushSession(ctx.primary.Id());
    }

    const auto end = std::chrono::steady_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
    return result;
}

namespace {

ToolCall FromLLM(const llm::LLMToolCall& lc) {
    ToolCall tc;
    tc.id = lc.id;
    tc.name = lc.name;
    tc.arguments = lc.arguments;
    return tc;
}

std::uint64_t NowUnixMs() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());
}

bool ParseJsonObject(const std::string& text, Json::Value* out) {
    if (!out) return false;
    Json::CharReaderBuilder builder;
    std::istringstream stream(text);
    std::string errors;
    return Json::parseFromStream(builder, stream, out, &errors) && out->isObject();
}

} // namespace

// ============================================================================
// Agent override resolution (shared by legacy and pre-resolved paths)
// ============================================================================

void ChainRunner::ResolveAgentOverrides(
    const SessionAccess& session,
    const std::string& systemPrompt,
    const std::string& providerId,
    const std::string& configId,
    const std::string& model,
    std::size_t contextWindowTokens,
    bool hasReasoningEnabledOverride,
    bool reasoningEnabledOverride,
    const std::string& reasoningEffortOverride,
    ExecutionRequest& out) const {

    out.providerId = providerId;
    out.configId = configId;
    out.model = model;
    out.systemPrompt = systemPrompt;
    out.contextWindow = contextWindowTokens;
    out.reasoningEnabled = hasReasoningEnabledOverride ? reasoningEnabledOverride : m_reasoningEnabled;
    out.reasoningEffort = !reasoningEffortOverride.empty() ? reasoningEffortOverride : m_reasoningEffort;
    out.maxChainSteps = m_maxChainSteps;
    out.maxToolCallsPerChain = m_maxToolCallsPerChain;

    if (m_agentStore && !session.AgentId().empty()) {
        auto agent = m_agentStore->GetById(session.AgentId());
        if (agent) {
            if (out.configId.empty() && !agent->default_provider.empty()) {
                out.configId = agent->default_provider;
                if (m_configLookup) {
                    auto cfg = m_configLookup(out.configId);
                    if (cfg && !cfg->provider_id.empty()) {
                        out.providerId = cfg->provider_id;
                    } else {
                        out.providerId = agent->default_provider;
                    }
                } else {
                    out.providerId = agent->default_provider;
                }
            }
            if (out.model.empty() && !agent->default_model.empty()) {
                out.model = agent->default_model;
            }
            // Identity is now provided by IdentityProvider via the context registry.
            // Do not assign agent->identity to systemPrompt here.
            if (!hasReasoningEnabledOverride) out.reasoningEnabled = agent->reasoning_enabled;
            if (reasoningEffortOverride.empty() && !agent->reasoning_effort.empty())
                out.reasoningEffort = agent->reasoning_effort;
            if (agent->budget.maxChainSteps > 0) out.maxChainSteps = agent->budget.maxChainSteps;
            if (agent->budget.maxToolCallsPerChain > 0) out.maxToolCallsPerChain = agent->budget.maxToolCallsPerChain;

            // Consolidation sessions need more tool calls (review, promote, merge, perspectives, summary)
            if (session.SessionType() == "consolidation" &&
                out.maxToolCallsPerChain < agent->budget.consolidationToolBudget) {
                out.maxToolCallsPerChain = agent->budget.consolidationToolBudget;
            }
        }
    }
}

// ============================================================================
// Non-streaming on session — pre-resolved overload (preferred)
// ============================================================================

ChainResult ChainRunner::ExecuteOnSession(
    SessionAccess& session,
    const std::string& userMessage,
    const ExecutionRequest& req,
    ChainThinkingCallback thinkingCallback,
    ChainToolCallCallback toolCallCallback,
    ChainAssistantMessageCallback assistantMessageCallback) {

    ChainResult result;
    const auto start = std::chrono::steady_clock::now();

    // Store user turn
    SessionTurn userTurn;
    userTurn.role = "user";
    userTurn.content = userMessage;
    userTurn.unix_ms = NowUnixMs();
    session.AddTurn(std::move(userTurn));

    // Assemble context from the provider registry.
    std::string resolvedSystemPrompt = req.systemPrompt;
    if (m_contextRegistry) {
        auto agentOpt = m_agentStore ? m_agentStore->GetById(session.AgentId()) : std::nullopt;
        const Agent& agentRef = agentOpt ? *agentOpt : Agent{};
        auto blocks = m_contextRegistry->Assemble(agentRef, session);
        for (const auto& block : blocks) {
            resolvedSystemPrompt += "\n\n## " + block.name + "\n\n" + block.content;
        }
    }

    // Create provider
    std::string providerErr;
    auto provider = CreateProvider(req.providerId, req.configId, &providerErr);
    if (!provider) {
        result.error = providerErr;
        result.elapsed_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        return result;
    }

    // Tool calling loop
    std::vector<llm::LLMMessage> toolResultMessages;
    int totalToolCalls = 0;

    for (std::uint32_t step = 0; step < req.maxChainSteps; ++step) {
        result.chain_steps = step + 1;

        // Assemble prompt
        auto assembly = m_assembler.BuildFromAccess(
            session, "", resolvedSystemPrompt,
            req.model, req.contextWindow);
        // Non-streaming execution must request a non-stream response body.
        assembly.request.stream = false;

        ALOG_DEBUG("chain", "ExecuteOnSession id=" << session.Id()
                  << " conv_id=" << session.Key().conversation_id
                  << " turns=" << session.Turns().size()
                  << " -> " << assembly.request.messages.size()
                  << " messages to LLM");

        result.triggered_compaction = assembly.needs_compaction;

        // Set reasoning effort on the request
        if (req.reasoningEnabled) {
            assembly.request.reasoning_effort = req.reasoningEffort;
        }

        // Add tool definitions
        auto toolDefs = GetToolDefinitionsForSession(session.AgentId(), session.SessionType());
        assembly.request.tools = toolDefs;
        if (!toolDefs.empty()) {
            assembly.request.tool_choice = "auto";
        }

        // Append any tool result messages from previous iteration
        for (auto& msg : toolResultMessages) {
            assembly.request.messages.push_back(std::move(msg));
        }
        toolResultMessages.clear();

        // Call LLM (non-streaming)
        auto llmCallStart = std::chrono::steady_clock::now();
        std::string llmErr;
        auto response = CallLLM(*provider, assembly.request, {}, &llmErr);
        auto llmCallEnd = std::chrono::steady_clock::now();
        int llmLatencyMs = static_cast<int>(std::chrono::duration<double, std::milli>(llmCallEnd - llmCallStart).count());
        if (!llmErr.empty()) {
            result.error = llmErr;
            result.elapsed_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            return result;
        }

        result.prompt_tokens += response.prompt_tokens;
        result.completion_tokens += response.completion_tokens;

        // Log LLM call if prompt logging is enabled
        if (m_promptLogStore && m_promptLogLevel != PromptLogLevel::None) {
            LogPromptCall(session.AgentId(), session.Id(),
                          req.providerId, req.model,
                          response, assembly.request, step, llmLatencyMs);
        }

        // Deliver thinking content to callback
        if (!response.thinking_content.empty() && thinkingCallback) {
            thinkingCallback(response.thinking_content);
        }

        // Process response: store turns, execute tools
        std::string userVisibleText;
        std::string toolOutputText;
        int toolCallsThisStep = 0;
        bool shouldContinue = ProcessResponse(
            session, response, response.content,
            response.thinking_content,
            toolResultMessages, userVisibleText, toolOutputText, toolCallsThisStep,
            nullptr, toolCallCallback);

        totalToolCalls += toolCallsThisStep;
        result.tool_calls_executed = totalToolCalls;

        // Check tool budget
        if (totalToolCalls >= static_cast<int>(req.maxToolCallsPerChain)) {
            ALOG_WARNING("chain", "tool call budget exhausted (" << totalToolCalls
                      << "/" << req.maxToolCallsPerChain << ")");
            shouldContinue = false;
        }

        if (!userVisibleText.empty()) {
            result.response = userVisibleText;
            if (assistantMessageCallback) {
                assistantMessageCallback(userVisibleText);
            }
        }

        // Interjection: check for messages that arrived during chain execution
        if (shouldContinue && m_messageQueue) {
            const auto& queueKey = session.Key().connector;
            if (m_messageQueue->HasPending(queueKey)) {
                std::string injected = m_messageQueue->DrainInterjection(queueKey);
                if (!injected.empty()) {
                    SessionTurn interjectionTurn;
                    interjectionTurn.role = "user";
                    interjectionTurn.content = injected;
                    interjectionTurn.unix_ms = NowUnixMs();
                    session.AddTurn(std::move(interjectionTurn));
                    ALOG_DEBUG("chain", "Injected interjection: " << injected.size() << " chars");
                }
            }
        }

        if (!shouldContinue) {
            if (result.response.empty() && !response.content.empty()) {
                result.response = response.content;
            }
            break;
        }
    }

    result.success = true;
    result.elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    return result;
}

// ============================================================================
// Non-streaming on session — legacy overload (delegates to pre-resolved)
// ============================================================================

ChainResult ChainRunner::ExecuteOnSession(
    SessionAccess& session,
    const std::string& userMessage,
    const std::string& systemPrompt,
    const std::string& providerId,
    const std::string& configId,
    const std::string& model,
    std::size_t contextWindowTokens,
    ChainThinkingCallback thinkingCallback,
    ChainToolCallCallback toolCallCallback,
    ChainAssistantMessageCallback assistantMessageCallback,
    const std::string& reasoningEffortOverride,
    bool reasoningEnabledOverride,
    bool hasReasoningEnabledOverride) {

    ExecutionRequest req;
    ResolveAgentOverrides(session, systemPrompt, providerId, configId, model,
                          contextWindowTokens, hasReasoningEnabledOverride,
                          reasoningEnabledOverride, reasoningEffortOverride, req);
    return ExecuteOnSession(session, userMessage, req,
                            thinkingCallback, toolCallCallback, assistantMessageCallback);
}

// ============================================================================
// Streaming on session — pre-resolved overload (preferred)
// ============================================================================

ChainResult ChainRunner::ExecuteStreamingOnSession(
    SessionAccess& session,
    const std::string& userMessage,
    const ExecutionRequest& req,
    llm::LLMTokenCallback tokenCallback,
    ChainTextCallback textCallback,
    ChainToolEventCallback toolEventCallback,
    ChainThinkingCallback thinkingCallback,
    ChainToolCallCallback toolCallCallback,
    ChainAssistantMessageCallback assistantMessageCallback) {

    ChainResult result;
    const auto start = std::chrono::steady_clock::now();

    // Store user turn
    SessionTurn userTurn;
    userTurn.role = "user";
    userTurn.content = userMessage;
    userTurn.unix_ms = NowUnixMs();
    session.AddTurn(std::move(userTurn));

    // Assemble context from the provider registry.
    std::string resolvedSystemPrompt = req.systemPrompt;
    if (m_contextRegistry) {
        auto agentOpt = m_agentStore ? m_agentStore->GetById(session.AgentId()) : std::nullopt;
        if (!agentOpt) {
            ALOG_WARNING("chain", "agent lookup failed for agent_id=" << session.AgentId()
                      << " store=" << (m_agentStore ? "set" : "null") << " — falling back to Agent{}");
        }
        const Agent& agentRef = agentOpt ? *agentOpt : Agent{};
        auto blocks = m_contextRegistry->Assemble(agentRef, session);
        for (const auto& block : blocks) {
            resolvedSystemPrompt += "\n\n## " + block.name + "\n\n" + block.content;
        }
    }

    // Create provider
    std::string providerErr;
    auto provider = CreateProvider(req.providerId, req.configId, &providerErr);
    if (!provider) {
        result.error = providerErr;
        result.elapsed_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        return result;
    }

    // Tool calling loop
    std::vector<llm::LLMMessage> toolResultMessages;
    int totalToolCalls = 0;

    for (std::uint32_t step = 0; step < req.maxChainSteps; ++step) {
        result.chain_steps = step + 1;

        // Assemble prompt
        auto assembly = m_assembler.BuildFromAccess(
            session, "", resolvedSystemPrompt,
            req.model, req.contextWindow);
        assembly.request.stream = true;

        ALOG_DEBUG("chain", "StreamingOnSession id=" << session.Id()
                  << " conv_id=" << session.Key().conversation_id
                  << " turns=" << session.Turns().size()
                  << " -> " << assembly.request.messages.size()
                  << " messages to LLM");

        result.triggered_compaction = assembly.needs_compaction;

        // Set reasoning effort on the request
        if (req.reasoningEnabled) {
            assembly.request.reasoning_effort = req.reasoningEffort;
        }

        // Add tool definitions
        auto toolDefs = GetToolDefinitionsForSession(session.AgentId(), session.SessionType());
        assembly.request.tools = toolDefs;
        if (!toolDefs.empty()) {
            assembly.request.tool_choice = "auto";
        }

        // Append any tool result messages from previous iteration
        for (auto& msg : toolResultMessages) {
            assembly.request.messages.push_back(std::move(msg));
        }
        toolResultMessages.clear();

        ALOG_DEBUG("chain/stream", "Step " << step
                  << ": toolResultMessages=" << toolResultMessages.size()
                  << " sessionTurns=" << session.Turns().size()
                  << " messages=" << assembly.request.messages.size());

        // Build the per-step token callback that routes thinking vs. content
        std::string accumulatedThinking;
        llm::LLMTokenCallback stepTokenCallback;
        if (thinkingCallback) {
            stepTokenCallback = [thinkingCallback, tokenCallback, &accumulatedThinking](const llm::LLMToken& token) {
                if (token.is_thinking) {
                    accumulatedThinking += token.content;
                    thinkingCallback(token.content);
                } else if (tokenCallback) {
                    tokenCallback(token);
                }
            };
        } else {
            stepTokenCallback = tokenCallback;
        }

        // Call LLM (streaming)
        auto llmCallStart = std::chrono::steady_clock::now();
        std::string llmErr;
        auto response = CallLLM(*provider, assembly.request, stepTokenCallback, &llmErr);
        auto llmCallEnd = std::chrono::steady_clock::now();
        int llmLatencyMs = static_cast<int>(std::chrono::duration<double, std::milli>(llmCallEnd - llmCallStart).count());
        if (!llmErr.empty()) {
            result.error = llmErr;
            result.elapsed_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            return result;
        }

        result.prompt_tokens += response.prompt_tokens;
        result.completion_tokens += response.completion_tokens;

        // Log LLM call if prompt logging is enabled
        if (m_promptLogStore && m_promptLogLevel != PromptLogLevel::None) {
            LogPromptCall(session.AgentId(), session.Id(),
                          req.providerId, req.model,
                          response, assembly.request, step, llmLatencyMs);
        }

        // Diagnostic: log what the LLM returned
        ALOG_DEBUG("chain", "LLM response: content_len=" << response.content.size()
                  << " thinking_len=" << accumulatedThinking.size()
                  << " tool_calls=" << response.tool_calls.size()
                  << " finish_reason=" << response.finish_reason);
        for (const auto& tc : response.tool_calls) {
            ALOG_TRACE("chain", "  tool_call: id=" << tc.id
                      << " name=" << tc.name
                      << " args_len=" << tc.arguments.size());
        }
        if (!response.content.empty()) {
            ALOG_TRACE("chain", "  content: "
                      << response.content.substr(0, 200)
                      << (response.content.size() > 200 ? "..." : ""));
        }

        // Process response: store turns, execute tools
        std::string userVisibleText;
        std::string toolOutputText;
        int toolCallsThisStep = 0;
        ALOG_DEBUG("chain/stream", "Calling ProcessResponse, tool_calls=" << response.tool_calls.size());
        bool shouldContinue = false;
        try {
          shouldContinue = ProcessResponse(
              session, response, response.content,
              accumulatedThinking,
              toolResultMessages, userVisibleText, toolOutputText, toolCallsThisStep,
              toolEventCallback, toolCallCallback);
        } catch (const std::exception& e) {
          ALOG_ERROR("chain/stream", "EXCEPTION in ProcessResponse: " << e.what());
          result.error = std::string("ProcessResponse exception: ") + e.what();
          break;
        }

        totalToolCalls += toolCallsThisStep;
        result.tool_calls_executed = totalToolCalls;

        ALOG_DEBUG("chain/stream", "ProcessResponse done: shouldContinue=" << shouldContinue
                  << " toolCallsThisStep=" << toolCallsThisStep
                  << " totalToolCalls=" << totalToolCalls
                  << " toolResultMessages=" << toolResultMessages.size());

        // Stream tool output text to caller (from stream_to_user tool results only).
        if (!toolOutputText.empty()) {
            result.response += toolOutputText;
            if (textCallback) {
                textCallback(toolOutputText);
            }
        }
        if (!userVisibleText.empty()) {
            result.response = userVisibleText;
            if (assistantMessageCallback) {
                assistantMessageCallback(userVisibleText);
            }
        }

        // Check tool budget
        if (totalToolCalls >= static_cast<int>(req.maxToolCallsPerChain)) {
            ALOG_WARNING("chain", "tool call budget exhausted (" << totalToolCalls
                      << "/" << req.maxToolCallsPerChain << ")");
            shouldContinue = false;
        }

        // Interjection: check for messages that arrived during chain execution
        if (shouldContinue && m_messageQueue) {
            const auto& queueKey = session.Key().connector;
            if (m_messageQueue->HasPending(queueKey)) {
                std::string injected = m_messageQueue->DrainInterjection(queueKey);
                if (!injected.empty()) {
                    SessionTurn interjectionTurn;
                    interjectionTurn.role = "user";
                    interjectionTurn.content = injected;
                    interjectionTurn.unix_ms = NowUnixMs();
                    session.AddTurn(std::move(interjectionTurn));
                    ALOG_DEBUG("chain/stream", "Injected interjection: "
                              << injected.size() << " chars");
                }
            }
        }

        if (!shouldContinue) {
            if (result.response.empty() && !response.content.empty()) {
                // Content was already streamed via tokenCallback
                result.response = response.content;
            }
            break;
        }
    }

    result.success = true;
    result.elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    return result;
}

// ============================================================================
// Streaming on session — legacy overload (delegates to pre-resolved)
// ============================================================================

ChainResult ChainRunner::ExecuteStreamingOnSession(
    SessionAccess& session,
    const std::string& userMessage,
    const std::string& systemPrompt,
    const std::string& providerId,
    const std::string& configId,
    const std::string& model,
    std::size_t contextWindowTokens,
    llm::LLMTokenCallback tokenCallback,
    ChainTextCallback textCallback,
    ChainToolEventCallback toolEventCallback,
    ChainThinkingCallback thinkingCallback,
    ChainToolCallCallback toolCallCallback,
    ChainAssistantMessageCallback assistantMessageCallback,
    const std::string& reasoningEffortOverride,
    bool reasoningEnabledOverride,
    bool hasReasoningEnabledOverride) {

    ExecutionRequest req;
    ResolveAgentOverrides(session, systemPrompt, providerId, configId, model,
                          contextWindowTokens, hasReasoningEnabledOverride,
                          reasoningEnabledOverride, reasoningEffortOverride, req);
    return ExecuteStreamingOnSession(session, userMessage, req,
                                     tokenCallback, textCallback, toolEventCallback,
                                     thinkingCallback, toolCallCallback,
                                     assistantMessageCallback);
}

// ============================================================================
// LLM call helper
// ============================================================================

llm::LLMResponse ChainRunner::CallLLM(
    llm::ILLMProvider& provider,
    llm::LLMRequest& request,
    llm::LLMTokenCallback tokenCallback,
    std::string* error) {

    llm::LLMResponse response;

    if (tokenCallback) {
        // Streaming
        std::string llmErr;
        auto msg = provider.StreamComplete(request, std::move(tokenCallback), &llmErr);
        if (!llmErr.empty()) {
            if (error) *error = llmErr;
            return response;
        }
        response.content = msg.content;
        response.thinking_content = msg.thinking_content;

        // Retrieve tool calls and token usage accumulated during streaming
        auto* baseProvider = dynamic_cast<llm::LLMProviderBase*>(&provider);
        if (baseProvider) {
            const auto& llmCalls = baseProvider->GetLastToolCalls();
            response.tool_calls = llmCalls;
            response.finish_reason = baseProvider->GetLastFinishReason();
            response.prompt_tokens = baseProvider->GetLastPromptTokens();
            response.completion_tokens = baseProvider->GetLastCompletionTokens();
        }
    } else {
        // Non-streaming
        std::string llmErr;
        auto msg = provider.Complete(request, &llmErr);
        if (!llmErr.empty()) {
            if (error) *error = llmErr;
            return response;
        }
        response.content = msg.content;
        response.thinking_content = msg.thinking_content;

        // Parse tool calls and token usage from the complete response body.
        auto* baseProvider = dynamic_cast<llm::LLMProviderBase*>(&provider);
        if (baseProvider) {
            const auto& lastCalls = baseProvider->GetLastToolCalls();
            const auto& lastReason = baseProvider->GetLastFinishReason();
            if (!lastCalls.empty()) {
                response.tool_calls = lastCalls;
            }
            if (!lastReason.empty()) {
                response.finish_reason = lastReason;
            }
            response.prompt_tokens = baseProvider->GetLastPromptTokens();
            response.completion_tokens = baseProvider->GetLastCompletionTokens();
        }
    }

    return response;
}

// ============================================================================
// Process response — store turns, execute tools, route results
// ============================================================================

bool ChainRunner::ProcessResponse(
    SessionAccess& session,
    llm::LLMResponse& response,
    const std::string& content,
    const std::string& thinkingContent,
    std::vector<llm::LLMMessage>& toolResultMessages,
    std::string& userVisibleText,
    std::string& toolOutputText,
    int& toolCallsExecuted,
    ChainToolEventCallback toolEventCallback,
    ChainToolCallCallback toolCallCallback) {

    ALOG_DEBUG("chain", "ProcessResponse: tool_calls=" << response.tool_calls.size()
              << " content_len=" << content.size()
              << " thinking_len=" << thinkingContent.size());

    // 1. Store assistant turn with thinking_content as a property.
    //    thinking_content is never sent to the LLM (excluded by PromptAssembler),
    //    but displayed in the UI as a collapsible block.
    if (!response.tool_calls.empty()) {
        // Assistant turn with tool calls
        SessionTurn assistantTurn;
        assistantTurn.role = "assistant";
        assistantTurn.content = content;
        assistantTurn.thinking_content = thinkingContent;
        assistantTurn.unix_ms = NowUnixMs();
        for (const auto& call : response.tool_calls) {
            assistantTurn.tool_calls.push_back(FromLLM(call));
        }
        session.AddTurn(std::move(assistantTurn));
    } else if (!content.empty() || !thinkingContent.empty()) {
        // Content-only or thinking-only response — store as single assistant turn
        userVisibleText = content;

        SessionTurn assistantTurn;
        assistantTurn.role = "assistant";
        assistantTurn.content = content;
        assistantTurn.thinking_content = thinkingContent;
        assistantTurn.unix_ms = NowUnixMs();
        session.AddTurn(std::move(assistantTurn));
    }

    // 3. Execute tool calls and store results
    toolCallsExecuted = 0;
    bool shouldContinueLoop = false;

    // Build the execution context for this session
    ToolExecutionContext execCtx;
    execCtx.sessionKey = session.Key().ToString();
    execCtx.agentId = session.AgentId();
    if (m_agentStore && !session.AgentId().empty()) {
        auto agent = m_agentStore->GetById(session.AgentId());
        if (agent) {
            execCtx.toolConfigs = agent->tool_configs_json;
        }
    }

    for (const auto& call : response.tool_calls) {
        // Notify before execution (for real-time UI feedback)
        ToolCall tcNotif = FromLLM(call);
        if (toolCallCallback) {
            toolCallCallback(tcNotif);
        }

        ALOG_INFO("chain", "Executing tool: name=" << call.name
                  << " id=" << call.id
                  << " args_len=" << call.arguments.size());

        // Delegate execution to ToolExecutionService
        ToolRouteResult routeResult = ToolRouteResult::deliver_to_model;
        ToolResult toolResult = m_execService->Execute(call, execCtx, &routeResult);

        toolCallsExecuted++;

        // Store tool result turn
        SessionTurn toolResultTurn;
        toolResultTurn.role = "tool";
        // When success=false but error is empty, fall back to output so the
        // LLM sees something informative rather than a blank tool result.
        if (toolResult.success) {
            toolResultTurn.content = toolResult.output;
        } else {
            toolResultTurn.content = toolResult.error.empty()
                ? toolResult.output : toolResult.error;
        }
        toolResultTurn.tool_call_id = call.id;
        toolResultTurn.tool_name = call.name;
        toolResultTurn.unix_ms = NowUnixMs();
        session.AddTurn(std::move(toolResultTurn));

        if (toolEventCallback) {
            toolEventCallback(tcNotif, toolResult);
        }

        // Route the result based on the tool's result mode
        // Tool result is already stored as a session turn,
        // so it will be included in BuildFromAccess on the next chain step.
        switch (routeResult) {
            case ToolRouteResult::stream_to_user:
                if (toolResult.success) {
                    toolOutputText += toolResult.output;
                }
                break;

            case ToolRouteResult::deliver_to_model:
                shouldContinueLoop = true;
                break;

            case ToolRouteResult::both:
                if (toolResult.success) {
                    userVisibleText += toolResult.output;
                }
                shouldContinueLoop = true;
                break;
        }
    }

    // 4. If no tool calls — chain is done
    if (response.tool_calls.empty()) {
        return false;
    }

    // 5. Continue the loop if any tool had deliver_to_model or both result mode
    return shouldContinueLoop;
}

// ============================================================================
// Tool definitions — thin wrapper delegating to ToolSchemaService
// ============================================================================

std::vector<llm::LLMToolDef> ChainRunner::GetToolDefinitionsForSession(
    const std::string& agent_id,
    const std::string& session_type) const {
    return m_schemaService->GetForSession(agent_id, session_type);
}
// Provider creation
// ============================================================================

std::unique_ptr<llm::ILLMProvider> ChainRunner::CreateProvider(
    const std::string& providerId,
    const std::string& configId,
    std::string* error) const {
    if (!m_providers.Has(providerId)) {
        if (error) {
            *error = "no provider registered for id: " + providerId;
        }
        return nullptr;
    }

    llm::LLMProviderConfig config;
    config.provider_id = providerId;

    if (m_configLookup) {
        auto lookedUp = m_configLookup(configId);
        if (lookedUp) {
            config = std::move(*lookedUp);
        }
    }

    auto provider = m_providers.Create(config);
    if (!provider) {
        if (error) {
            *error = "failed to create provider instance for: " + providerId;
        }
        return nullptr;
    }

    // Fetch model capabilities from the provider (e.g. Cohere /v1/models)
    auto* baseProvider = dynamic_cast<llm::LLMProviderBase*>(provider.get());
    if (baseProvider) {
        std::string modelId = config.default_model;
        if (!modelId.empty()) {
            baseProvider->FetchCapabilities(modelId);
        }
    }

    return provider;
}

// ============================================================================
// Prompt logging helper
// ============================================================================

void ChainRunner::LogPromptCall(
    const std::string& agent_id,
    int64_t session_id,
    const std::string& provider,
    const std::string& model,
    const llm::LLMResponse& response,
    const llm::LLMRequest& request,
    int chain_step,
    int latency_ms) {

    if (!m_promptLogStore || m_promptLogLevel == PromptLogLevel::None) return;

    // Build content strings only at Full level
    std::string promptContent;
    std::string responseContent;
    std::string toolCallsJson;
    std::string toolResultsJson;

    if (m_promptLogLevel == PromptLogLevel::Full) {
        // Assemble prompt content from request messages
        for (const auto& msg : request.messages) {
            promptContent += "{" + msg.role + "} ";
            promptContent += msg.content;
            if (!msg.content.empty() && msg.content.back() != '\n')
                promptContent += '\n';
        }

        responseContent = response.content;

        // Serialize tool calls to JSON array
        if (!response.tool_calls.empty()) {
            Json::Value calls(Json::arrayValue);
            for (const auto& tc : response.tool_calls) {
                Json::Value call(Json::objectValue);
                call["id"] = tc.id;
                call["name"] = tc.name;
                call["arguments"] = tc.arguments;
                calls.append(call);
            }
            Json::StreamWriterBuilder wb;
            wb["indentation"] = "";
            toolCallsJson = Json::writeString(wb, calls);
        }
    }

    m_promptLogStore->Log(
        m_promptLogLevel,
        agent_id,
        session_id,
        provider,
        model,
        response.prompt_tokens,
        response.completion_tokens,
        latency_ms,
        chain_step,
        promptContent,
        responseContent,
        toolCallsJson,
        toolResultsJson);
}

} // namespace animus::kernel

#include "animus_kernel/ChainRunner.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <json/json.h>

#include "animus_kernel/SessionManager.h"
#include <set>
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/ContextProviderRegistry.h"
#include "animus_kernel/llm/LLMProviderBase.h"
#include "animus_kernel/tools/ChannelsTool.h"

namespace animus::kernel {

ChainRunner::ChainRunner(
    llm::LLMProviderRegistry& providers,
    SessionManager& sessions,
    ToolRegistry& tools,
    ProviderConfigLookup configLookup)
    : m_providers(providers)
    , m_sessions(sessions)
    , m_tools(tools)
    , m_configLookup(std::move(configLookup)) {
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

std::string InjectFilePolicy(const std::string& argumentsJson, const std::string& toolConfigsJson) {
    Json::Value toolConfigs(Json::objectValue);
    if (!ParseJsonObject(toolConfigsJson, &toolConfigs)) return argumentsJson;
    if (!toolConfigs.isMember("file") || !toolConfigs["file"].isObject()) return argumentsJson;

    Json::Value args(Json::objectValue);
    if (!ParseJsonObject(argumentsJson, &args)) return argumentsJson;

    const Json::Value& filePolicy = toolConfigs["file"];
    Json::Value policy(Json::objectValue);
    if (filePolicy.isMember("restrict_to_workspace")) {
        policy["restrict_to_workspace"] = filePolicy["restrict_to_workspace"];
    }
    if (filePolicy.isMember("workspace_root")) {
        policy["workspace_root"] = filePolicy["workspace_root"];
    }
    if (filePolicy.isMember("path_allowlist")) {
        policy["path_allowlist"] = filePolicy["path_allowlist"];
    }
    if (filePolicy.isMember("path_denylist")) {
        policy["path_denylist"] = filePolicy["path_denylist"];
    }
    if (policy.empty()) return argumentsJson;
    args["__policy"] = policy;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, args);
}

} // namespace

// ============================================================================
// Non-streaming on session
// ============================================================================

ChainResult ChainRunner::ExecuteOnSession(
    SessionAccess& session,
    const std::string& userMessage,
    const std::string& systemPrompt,
    const std::string& providerId,
    const std::string& configId,
    const std::string& model,
    std::size_t contextWindowTokens) {

    ChainResult result;
    const auto start = std::chrono::steady_clock::now();

    // Resolve per-agent config overrides
    std::string resolvedProvider = providerId;
    std::string resolvedConfigId = configId;
    std::string resolvedModel = model;
    std::string resolvedSystemPrompt = systemPrompt;
    std::size_t resolvedContextWindow = contextWindowTokens;
    bool resolvedReasoningEnabled = m_reasoningEnabled;
    std::string resolvedReasoningEffort = m_reasoningEffort;
    std::uint32_t resolvedMaxChainSteps = m_maxChainSteps;
    std::uint32_t resolvedMaxToolCallsPerChain = m_maxToolCallsPerChain;
    if (m_agentStore && !session.AgentId().empty()) {
        auto agent = m_agentStore->GetById(session.AgentId());
        if (agent) {
            if (resolvedConfigId.empty() && !agent->default_provider.empty()) {
                resolvedConfigId = agent->default_provider;
                if (m_configLookup) {
                    auto cfg = m_configLookup(resolvedConfigId);
                    if (cfg && !cfg->provider_id.empty()) {
                        resolvedProvider = cfg->provider_id;
                    } else {
                        resolvedProvider = agent->default_provider;
                    }
                } else {
                    resolvedProvider = agent->default_provider;
                }
            }
            if (resolvedModel.empty() && !agent->default_model.empty()) {
                resolvedModel = agent->default_model;
            }
            // Identity is now provided by IdentityProvider via the context registry.
            // Do not assign agent->identity to resolvedSystemPrompt here.
            resolvedReasoningEnabled = agent->reasoning_enabled;
            if (!agent->reasoning_effort.empty()) resolvedReasoningEffort = agent->reasoning_effort;
            if (agent->budget.maxChainSteps > 0) resolvedMaxChainSteps = agent->budget.maxChainSteps;
            if (agent->budget.maxToolCallsPerChain > 0) resolvedMaxToolCallsPerChain = agent->budget.maxToolCallsPerChain;

            // Consolidation sessions need more tool calls (review, promote, merge, perspectives, summary)
            if (session.SessionType() == "consolidation" &&
                resolvedMaxToolCallsPerChain < agent->budget.consolidationToolBudget) {
                resolvedMaxToolCallsPerChain = agent->budget.consolidationToolBudget;
            }
        }
    }

    // Store user turn
    SessionTurn userTurn;
    userTurn.role = "user";
    userTurn.content = userMessage;
    userTurn.unix_ms = NowUnixMs();
    session.AddTurn(std::move(userTurn));

    // Assemble context from the provider registry.
    // If registry is available, it produces identity, session notes, etc.
    // as separate blocks. Fall back to the raw systemPrompt if no registry.
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
    auto provider = CreateProvider(resolvedProvider, resolvedConfigId, &providerErr);
    if (!provider) {
        result.error = providerErr;
        result.elapsed_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        return result;
    }

    // Tool calling loop
    std::vector<llm::LLMMessage> toolResultMessages;
    int totalToolCalls = 0;

    for (std::uint32_t step = 0; step < resolvedMaxChainSteps; ++step) {
        result.chain_steps = step + 1;

        // Assemble prompt
        auto assembly = m_assembler.BuildFromAccess(
            session, "", resolvedSystemPrompt,
            resolvedModel, resolvedContextWindow);
        // Non-streaming execution must request a non-stream response body.
        // Otherwise providers may return SSE payloads that ParseResponse cannot decode.
        assembly.request.stream = false;

        std::cerr << "[chain] ExecuteOnSession id=" << session.Id()
                  << " conv_id=" << session.Key().conversation_id
                  << " turns=" << session.Turns().size()
                  << " -> " << assembly.request.messages.size()
                  << " messages to LLM" << std::endl;

        result.triggered_compaction = assembly.needs_compaction;

        // Set reasoning effort on the request
        if (resolvedReasoningEnabled) {
            assembly.request.reasoning_effort = resolvedReasoningEffort;
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
                          resolvedProvider, resolvedModel,
                          response, assembly.request, step, llmLatencyMs);
        }

        // Deliver thinking content to callback
        if (!response.thinking_content.empty() && m_thinkingCallback) {
            m_thinkingCallback(response.thinking_content);
        }

        // Process response: store turns, execute tools
        std::string userVisibleText;
        std::string toolOutputText;
        int toolCallsThisStep = 0;
        bool shouldContinue = ProcessResponse(
            session, response, response.content,
            response.thinking_content,
            toolResultMessages, userVisibleText, toolOutputText, toolCallsThisStep);

        totalToolCalls += toolCallsThisStep;
        result.tool_calls_executed = totalToolCalls;

        // Check tool budget
        if (totalToolCalls >= static_cast<int>(resolvedMaxToolCallsPerChain)) {
            std::cerr << "[chain] tool call budget exhausted (" << totalToolCalls
                      << "/" << resolvedMaxToolCallsPerChain << ")" << std::endl;
            shouldContinue = false;
        }

        if (!userVisibleText.empty()) {
            result.response = userVisibleText;
            if (m_assistantMessageCallback) {
                m_assistantMessageCallback(userVisibleText);
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
                    std::cerr << "[chain] Injected interjection: " << injected.size() << " chars" << std::endl;
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
// Streaming on session
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
    ChainToolEventCallback toolEventCallback) {

    ChainResult result;
    const auto start = std::chrono::steady_clock::now();

    // Resolve per-agent config overrides
    std::string resolvedProvider = providerId;
    std::string resolvedConfigId = configId;
    std::string resolvedModel = model;
    std::string resolvedSystemPrompt = systemPrompt;
    std::size_t resolvedContextWindow = contextWindowTokens;
    bool resolvedReasoningEnabled = m_reasoningEnabled;
    std::string resolvedReasoningEffort = m_reasoningEffort;
    std::uint32_t resolvedMaxChainSteps = m_maxChainSteps;
    std::uint32_t resolvedMaxToolCallsPerChain = m_maxToolCallsPerChain;
    if (m_agentStore && !session.AgentId().empty()) {
        auto agent = m_agentStore->GetById(session.AgentId());
        if (agent) {
            if (resolvedConfigId.empty() && !agent->default_provider.empty()) {
                resolvedConfigId = agent->default_provider;
                if (m_configLookup) {
                    auto cfg = m_configLookup(resolvedConfigId);
                    if (cfg && !cfg->provider_id.empty()) {
                        resolvedProvider = cfg->provider_id;
                    } else {
                        resolvedProvider = agent->default_provider;
                    }
                } else {
                    resolvedProvider = agent->default_provider;
                }
            }
            if (resolvedModel.empty() && !agent->default_model.empty()) {
                resolvedModel = agent->default_model;
            }
            // Identity is now provided by IdentityProvider via the context registry.
            // Do not assign agent->identity to resolvedSystemPrompt here.
            resolvedReasoningEnabled = agent->reasoning_enabled;
            if (!agent->reasoning_effort.empty()) resolvedReasoningEffort = agent->reasoning_effort;
            if (agent->budget.maxChainSteps > 0) resolvedMaxChainSteps = agent->budget.maxChainSteps;
            if (agent->budget.maxToolCallsPerChain > 0) resolvedMaxToolCallsPerChain = agent->budget.maxToolCallsPerChain;

            // Consolidation sessions need more tool calls (review, promote, merge, perspectives, summary)
            if (session.SessionType() == "consolidation" &&
                resolvedMaxToolCallsPerChain < agent->budget.consolidationToolBudget) {
                resolvedMaxToolCallsPerChain = agent->budget.consolidationToolBudget;
            }
        }
    }

    // Store user turn
    SessionTurn userTurn;
    userTurn.role = "user";
    userTurn.content = userMessage;
    userTurn.unix_ms = NowUnixMs();
    session.AddTurn(std::move(userTurn));

    // Assemble context from the provider registry.
    // If registry is available, it produces identity, session notes, etc.
    // as separate blocks. Fall back to the raw systemPrompt if no registry.
    if (m_contextRegistry) {
        auto agentOpt = m_agentStore ? m_agentStore->GetById(session.AgentId()) : std::nullopt;
        if (!agentOpt) {
            std::cerr << "[chain/stream] WARNING: agent lookup failed for agent_id=" << session.AgentId()
                      << " store=" << (m_agentStore ? "set" : "null") << " — falling back to Agent{}" << std::endl;
        }
        const Agent& agentRef = agentOpt ? *agentOpt : Agent{};
        auto blocks = m_contextRegistry->Assemble(agentRef, session);
        for (const auto& block : blocks) {
            resolvedSystemPrompt += "\n\n## " + block.name + "\n\n" + block.content;
        }
    }

    // Create provider
    std::string providerErr;
    auto provider = CreateProvider(resolvedProvider, resolvedConfigId, &providerErr);
    if (!provider) {
        result.error = providerErr;
        result.elapsed_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        return result;
    }

    // Tool calling loop
    std::vector<llm::LLMMessage> toolResultMessages;
    int totalToolCalls = 0;

    for (std::uint32_t step = 0; step < resolvedMaxChainSteps; ++step) {
        result.chain_steps = step + 1;

        // Assemble prompt
        auto assembly = m_assembler.BuildFromAccess(
            session, "", resolvedSystemPrompt,
            resolvedModel, resolvedContextWindow);
        // Streaming execution expects SSE/tokenized provider responses.
        assembly.request.stream = true;

        std::cerr << "[chain] StreamingOnSession id=" << session.Id()
                  << " conv_id=" << session.Key().conversation_id
                  << " turns=" << session.Turns().size()
                  << " -> " << assembly.request.messages.size()
                  << " messages to LLM" << std::endl;

        result.triggered_compaction = assembly.needs_compaction;

        // Set reasoning effort on the request
        if (resolvedReasoningEnabled) {
            assembly.request.reasoning_effort = resolvedReasoningEffort;
        }

        // Add tool definitions
        auto toolDefs = GetToolDefinitionsForSession(session.AgentId(), session.SessionType());
        assembly.request.tools = toolDefs;
        if (!toolDefs.empty()) {
            assembly.request.tool_choice = "auto";
        }

        // Append any tool result messages from previous iteration
        // (Currently always empty since ProcessResponse stores in session turns)
        for (auto& msg : toolResultMessages) {
            assembly.request.messages.push_back(std::move(msg));
        }
        toolResultMessages.clear();

        std::cerr << "[chain/stream] Step " << step
                  << ": toolResultMessages=" << toolResultMessages.size()
                  << " sessionTurns=" << session.Turns().size()
                  << " messages=" << assembly.request.messages.size()
                  << std::endl;

        // Build the per-step token callback that routes thinking vs. content:
        //   - is_thinking tokens (native thinking, tool-plan-delta) → thinking callback
        //   - non-thinking tokens (content) → regular token callback (user-visible)
        std::string accumulatedThinking;
        llm::LLMTokenCallback stepTokenCallback;
        if (m_thinkingCallback) {
            stepTokenCallback = [this, tokenCallback, &accumulatedThinking](const llm::LLMToken& token) {
                if (token.is_thinking) {
                    accumulatedThinking += token.content;
                    m_thinkingCallback(token.content);
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
                          resolvedProvider, resolvedModel,
                          response, assembly.request, step, llmLatencyMs);
        }

        // Diagnostic: log what the LLM returned
        std::cerr << "[chain] LLM response: content_len=" << response.content.size()
                  << " thinking_len=" << accumulatedThinking.size()
                  << " tool_calls=" << response.tool_calls.size()
                  << " finish_reason=" << response.finish_reason
                  << std::endl;
        for (const auto& tc : response.tool_calls) {
            std::cerr << "[chain]   tool_call: id=" << tc.id
                      << " name=" << tc.name
                      << " args_len=" << tc.arguments.size()
                      << std::endl;
        }
        if (!response.content.empty()) {
            std::cerr << "[chain]   content: "
                      << response.content.substr(0, 200)
                      << (response.content.size() > 200 ? "..." : "")
                      << std::endl;
        }

        // Process response: store turns, execute tools
        std::string userVisibleText;
        std::string toolOutputText;
        int toolCallsThisStep = 0;
        std::cerr << "[chain/stream] Calling ProcessResponse, tool_calls=" << response.tool_calls.size() << std::endl;
        bool shouldContinue = false;
        try {
          shouldContinue = ProcessResponse(
              session, response, response.content,
              accumulatedThinking,
              toolResultMessages, userVisibleText, toolOutputText, toolCallsThisStep,
              toolEventCallback);
        } catch (const std::exception& e) {
          std::cerr << "[chain/stream] EXCEPTION in ProcessResponse: " << e.what() << std::endl;
          result.error = std::string("ProcessResponse exception: ") + e.what();
          break;
        }

        totalToolCalls += toolCallsThisStep;
        result.tool_calls_executed = totalToolCalls;

        std::cerr << "[chain/stream] ProcessResponse done: shouldContinue=" << shouldContinue
                  << " toolCallsThisStep=" << toolCallsThisStep
                  << " totalToolCalls=" << totalToolCalls
                  << " toolResultMessages=" << toolResultMessages.size()
                  << std::endl;

        // Stream tool output text to caller (from stream_to_user tool results only).
        if (!toolOutputText.empty()) {
            result.response += toolOutputText;
            if (textCallback) {
                textCallback(toolOutputText);
            }
        }
        if (!userVisibleText.empty()) {
            result.response = userVisibleText;
            if (m_assistantMessageCallback) {
                m_assistantMessageCallback(userVisibleText);
            }
        }

        // Check tool budget
        if (totalToolCalls >= static_cast<int>(resolvedMaxToolCallsPerChain)) {
            std::cerr << "[chain] tool call budget exhausted (" << totalToolCalls
                      << "/" << resolvedMaxToolCallsPerChain << ")" << std::endl;
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
                    std::cerr << "[chain/stream] Injected interjection: "
                              << injected.size() << " chars" << std::endl;
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
    ChainToolEventCallback toolEventCallback) {

    std::cerr << "[chain] ProcessResponse: tool_calls=" << response.tool_calls.size()
              << " content_len=" << content.size()
              << " thinking_len=" << thinkingContent.size()
              << std::endl;

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

    for (const auto& call : response.tool_calls) {
        ToolCall tc = FromLLM(call);
        if (tc.name == "file" && m_agentStore && !session.AgentId().empty()) {
            auto agent = m_agentStore->GetById(session.AgentId());
            if (agent.has_value()) {
                tc.arguments = InjectFilePolicy(tc.arguments, agent->tool_configs_json);
            }
        }
        // Inject session context for agent-scoped tools
        {
            Json::Value args;
            if (ParseJsonObject(tc.arguments, &args)) {
                args["__session_key"] = session.Key().ToString();
                args["__agent_id"] = session.AgentId();
                Json::StreamWriterBuilder wb;
                wb["indentation"] = "";
                tc.arguments = Json::writeString(wb, args);
            }
        }
        std::cerr << "[chain] Executing tool: name=" << call.name
                  << " id=" << call.id
                  << " args_len=" << tc.arguments.size()
                  << std::endl;

        // Check for __node parameter — if present, forward to remote node
        ToolResult toolResult;
        std::string nodeName;
        {
            Json::Value argsJson;
            Json::CharReaderBuilder rb;
            std::string parseErr;
            auto reader = std::unique_ptr<Json::CharReader>(rb.newCharReader());
            if (reader->parse(tc.arguments.c_str(), tc.arguments.c_str() + tc.arguments.size(), &argsJson, &parseErr)) {
                nodeName = argsJson.get("__node", "").asString();
            }
        }

        // Notify before execution (for real-time UI feedback)
        if (m_toolCallCallback) {
            m_toolCallCallback(tc);
        }

        if (!nodeName.empty() && m_nodeManager) {
            // Forward to remote node
            std::cerr << "[chain] Forwarding to node: " << nodeName << std::endl;
            toolResult = m_nodeManager->ExecuteOnNode(nodeName, tc);
            std::cerr << "[chain] Node result: success=" << toolResult.success
                      << " output_len=" << toolResult.output.size() << std::endl;
        } else {
            auto* handler = m_tools.Find(call.name);

            if (handler) {
                toolResult = handler->Execute(tc);
                std::cerr << "[chain] Tool result: success=" << toolResult.success
                          << " output_len=" << toolResult.output.size()
                          << " error=" << toolResult.error
                          << std::endl;
            } else {
                toolResult.call_id = call.id;
                toolResult.success = false;
                toolResult.error = "unknown tool: " + call.name;
            }
        }

        // Determine result mode from the handler (or default for unknown tools)
        ToolResultMode resultMode = ToolResultMode::deliver_to_model;
        // For remote node calls, default mode is used
        if (nodeName.empty()) {
            auto* handler = m_tools.Find(call.name);
            if (handler) {
                resultMode = handler->GetResultMode();
            }
        }

        toolCallsExecuted++;

        // Store tool result turn
        SessionTurn toolResultTurn;
        toolResultTurn.role = "tool";
        toolResultTurn.content = toolResult.success ? toolResult.output : toolResult.error;
        toolResultTurn.tool_call_id = call.id;
        toolResultTurn.tool_name = call.name;
        toolResultTurn.unix_ms = NowUnixMs();
        session.AddTurn(std::move(toolResultTurn));

        if (toolEventCallback) {
            toolEventCallback(tc, toolResult);
        }

        // Route the result based on the tool's result mode
        // Note: tool result is already stored as a session turn above (line 554),
        // so it will be included in BuildFromAccess on the next chain step.
        // We do NOT push it to toolResultMessages — that would duplicate it.
        switch (resultMode) {
            case ToolResultMode::stream_to_user:
                // Result goes directly to the user, not back into the LLM loop
                if (toolResult.success) {
                    toolOutputText += toolResult.output;
                }
                break;

            case ToolResultMode::deliver_to_model:
                // Result feeds back into the LLM loop for the next chain step
                // (already stored as session turn, no need to duplicate in toolResultMessages)
                shouldContinueLoop = true;
                break;

            case ToolResultMode::both:
                // Result streams to user AND feeds back to model
                if (toolResult.success) {
                    userVisibleText += toolResult.output;
                }
                // (already stored as session turn, no need to duplicate in toolResultMessages)
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
// Tool definitions
// ============================================================================

llm::LLMToolDef ChainRunner::ConvertToolDef(const ToolDefinition& def) const {
    llm::LLMToolDef ltd;
    ltd.type = "function";
    ltd.name = def.name;
    ltd.description = def.description;

    Json::Value schema(Json::objectValue);
    schema["type"] = "object";
    Json::Value properties(Json::objectValue);
    Json::Value required(Json::arrayValue);

    for (const auto& param : def.parameters) {
        Json::Value prop(Json::objectValue);
        prop["type"] = param.type;
        prop["description"] = param.description;
        if (!param.enum_values.empty()) {
            Json::Value en(Json::arrayValue);
            for (const auto& e : param.enum_values) {
                en.append(e);
            }
            prop["enum"] = en;
        }
        if (!param.properties.empty()) {
            Json::Value nestedProps(Json::objectValue);
            for (const auto& np : param.properties) {
                Json::Value npVal(Json::objectValue);
                npVal["type"] = np.type;
                npVal["description"] = np.description;
                nestedProps[np.name] = npVal;
            }
            prop["properties"] = nestedProps;
        }
        properties[param.name] = prop;
        if (param.required) {
            required.append(param.name);
        }
    }

    schema["properties"] = properties;
    if (required.size() > 0) {
        schema["required"] = required;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    ltd.parameters = Json::writeString(builder, schema);

    return ltd;
}

std::vector<llm::LLMToolDef> ChainRunner::GetToolDefinitionsForRequest() const {
    std::vector<llm::LLMToolDef> defs;

    for (const auto& def : m_tools.GetAllDefinitions()) {
        defs.push_back(ConvertToolDef(def));
    }

    return defs;
}


std::vector<llm::LLMToolDef> ChainRunner::GetToolDefinitionsForAgent(const std::string& agent_id) const {
    // If no agent store or empty agent_id, fall back to all tools
    if (!m_agentStore || agent_id.empty()) {
        return GetToolDefinitionsForRequest();
    }

    auto agent = m_agentStore->GetById(agent_id);
    if (!agent || agent->enabled_tools.empty()) {
        // Empty whitelist = all tools (backwards compat)
        return GetToolDefinitionsForRequest();
    }

    // Set agent context on ChannelsTool for per-agent schema filtering
    auto* channelsHandler = m_tools.Find("channels");
    if (channelsHandler) {
        auto* channelsTool = dynamic_cast<ChannelsTool*>(channelsHandler);
        if (channelsTool) {
            channelsTool->SetCurrentAgentId(agent_id);
        }
    }

    // Filter: only include tools in the whitelist
    std::vector<llm::LLMToolDef> defs;
    for (const auto& def : m_tools.GetAllDefinitions()) {
        if (std::find(agent->enabled_tools.begin(), agent->enabled_tools.end(), def.name)
            != agent->enabled_tools.end()) {
            llm::LLMToolDef ltd;
            ltd.type = "function";
            ltd.name = def.name;
            ltd.description = def.description;

            Json::Value schema(Json::objectValue);
            schema["type"] = "object";
            Json::Value properties(Json::objectValue);
            Json::Value required(Json::arrayValue);

            for (const auto& param : def.parameters) {
                Json::Value prop(Json::objectValue);
                prop["type"] = param.type;
                prop["description"] = param.description;
                if (!param.enum_values.empty()) {
                    Json::Value en(Json::arrayValue);
                    for (const auto& e : param.enum_values) en.append(e);
                    prop["enum"] = en;
                }
                if (!param.properties.empty()) {
                    Json::Value nestedProps(Json::objectValue);
                    for (const auto& np : param.properties) {
                        Json::Value npVal(Json::objectValue);
                        npVal["type"] = np.type;
                        npVal["description"] = np.description;
                        nestedProps[np.name] = npVal;
                    }
                    prop["properties"] = nestedProps;
                }
                properties[param.name] = prop;
                if (param.required) required.append(param.name);
            }

            schema["properties"] = properties;
            if (required.size() > 0) schema["required"] = required;

            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            ltd.parameters = Json::writeString(builder, schema);

            defs.push_back(ltd);
        }
    }

    // Clear agent context to avoid state leakage
    if (channelsHandler) {
        auto* channelsTool = dynamic_cast<ChannelsTool*>(channelsHandler);
        if (channelsTool) {
            channelsTool->SetCurrentAgentId("");
        }
    }

    return defs;
}

std::vector<llm::LLMToolDef> ChainRunner::GetToolDefinitionsForSession(
    const std::string& agent_id,
    const std::string& session_type) const {

    // For consolidation sessions: bypass agent whitelist, use dedicated
    // toolset — the consolidation tool is infrastructure, not user-facing.
    if (session_type == "consolidation") {
        // Tools available in consolidation sessions:
        // - Tools with session_types containing "consolidation" (explicit opt-in)
        // - Tools with empty session_types on a safe allowlist
        static const std::set<std::string> kConsolidationSafe = {
            "diary", "memory", "sessions"
        };

        std::vector<llm::LLMToolDef> defs;
        for (const auto& def : m_tools.GetAllDefinitions()) {
            auto* handler = m_tools.Find(def.name);
            if (!handler) continue;
            const auto& td = handler->GetDefinition();

            if (!td.session_types.empty()) {
                // Explicit session_types — include if "consolidation" is listed
                for (const auto& st : td.session_types) {
                    if (st == "consolidation") {
                        defs.push_back(ConvertToolDef(def));
                        break;
                    }
                }
            } else {
                // No session_types restriction — include only if on safe list
                if (kConsolidationSafe.count(def.name)) {
                    defs.push_back(ConvertToolDef(def));
                }
            }
        }
        return defs;
    }

    // Default path: agent whitelist + session type filter
    auto agentDefs = GetToolDefinitionsForAgent(agent_id);

    // Filter: include tools with no session_types restriction, plus tools
    // explicitly available for this session type.
    // This applies even when session_type is empty — tools that opt into
    // specific session types (e.g. consolidation) should never appear in
    // untyped sessions.
    std::vector<llm::LLMToolDef> defs;
    for (const auto& def : agentDefs) {
        // Look up the original ToolDefinition to check session_types
        auto* handler = m_tools.Find(def.name);
        if (!handler) continue;
        const auto& td = handler->GetDefinition();
        if (td.session_types.empty()) {
            // No restriction — always available
            defs.push_back(def);
        } else {
            // Only available in listed session types
            for (const auto& st : td.session_types) {
                if (st == session_type) {
                    defs.push_back(def);
                    break;
                }
            }
        }
    }
    return defs;
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

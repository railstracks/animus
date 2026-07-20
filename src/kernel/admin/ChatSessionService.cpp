#include "animus_kernel/admin/ChatSessionService.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>
#include <vector>

#include <json/json.h>

#include "animus_kernel/AgentStore.h"
#include "animus_kernel/ChainRunner.h"
#include "animus_kernel/CompactionService.h"
#include "animus_kernel/CompactionSummaryGenerator.h"
#include "animus_kernel/llm/LLMProviderRegistry.h"
#include "animus_kernel/ProviderThrottle.h"
#include "animus_kernel/SessionManager.h"
#include "animus_kernel/admin/ProviderManager.h"
#include "animus_kernel/tools/ToolTypes.h"
#include "animus/Jobs.h"
#include "kernel/admin/internal/AdminServerInternals.h"

namespace animus::kernel {
namespace {

std::string JsonToString(const Json::Value& value) {
    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    return Json::writeString(writerBuilder, value);
}

void QueueSendJson(const drogon::WebSocketConnectionPtr& conn, const Json::Value& payload) {
    if (!conn || !conn->connected()) {
        return;
    }
    conn->send(JsonToString(payload));
}

std::vector<std::string> ChunkText(const std::string& input, std::size_t maxChunkSize) {
    std::vector<std::string> chunks;
    if (input.empty()) {
        return chunks;
    }
    const std::size_t chunkSize = std::max<std::size_t>(1, maxChunkSize);
    for (std::size_t pos = 0; pos < input.size(); pos += chunkSize) {
        chunks.push_back(input.substr(pos, chunkSize));
    }
    return chunks;
}

void QueueError(
    const drogon::WebSocketConnectionPtr& conn,
    SessionId sessionId,
    const std::string& message) {
    Json::Value errMsg(Json::objectValue);
    errMsg["type"] = "error";
    if (sessionId > 0) {
        errMsg["session_id"] = std::to_string(sessionId);
    }
    errMsg["message"] = message;
    QueueSendJson(conn, errMsg);
}

} // namespace

void ChatSessionService::Configure(const Dependencies& deps) {
    m_deps = deps;
}

bool ChatSessionService::EnqueueStreamingResponse(const Request& request) const {
    if (!m_deps.jobs || !request.wsConnection || !request.session || request.userContent.empty()) {
        return false;
    }

    const auto wsConnPtr = request.wsConnection;
    const auto session = request.session;
    const SessionId sessionId = request.sessionId;
    const std::string userContent = request.userContent;
    const auto stopSignal = request.stopSignal;
    const std::string requestedProviderOverride = request.requestedProviderOverride;
    const std::string requestedModelOverride = request.requestedModelOverride;

    SessionManager* const sessions = m_deps.sessions;
    ChainRunner* const chainRunner = m_deps.chainRunner;
    CompactionService* const compactionService = m_deps.compactionService;
    llm::LLMProviderRegistry* const providerRegistry = m_deps.providerRegistry;
    ProviderManager* const providerManager = m_deps.providerManager;
    AgentStore* const agentStore = m_deps.agentStore;
    ProviderThrottle* const providerThrottle = m_deps.providerThrottle;
    AttachmentStore* const attachmentStore = m_deps.attachmentStore;
    AttachmentTokenManager* const attachmentTokenManager = m_deps.attachmentTokenManager;
    std::mutex* const chatMutex = m_deps.chatMutex;
    const KernelConfig::AgentRuntimeConfig* const agentConfig = m_deps.agentConfig;

    // Get config lookup from chain runner for compaction summary generation
    const auto configLookup = chainRunner ? chainRunner->GetConfigLookup() : ProviderConfigLookup{};

    m_deps.jobs->EnqueueInLane(
        ::animus::jobs::JobLane::Cognition,
        [wsConnPtr, session, sessionId, userContent, stopSignal,
         requestedProviderOverride, requestedModelOverride,
         sessions, chainRunner, compactionService, providerRegistry, providerManager, agentStore, providerThrottle, chatMutex, agentConfig, configLookup,
         attachmentStore, attachmentTokenManager]() {
            std::string providerId = requestedProviderOverride.empty()
                ? session->ProviderId()
                : requestedProviderOverride;
            if (providerId.empty() && agentStore && !session->AgentId().empty()) {
                auto agent = agentStore->GetById(session->AgentId());
                if (agent && !agent->default_provider.empty()) {
                    providerId = agent->default_provider;
                }
            }
            if (providerId.empty() && providerManager) {
                providerId = providerManager->GetDefaultProviderId();
            }
            if (!requestedProviderOverride.empty()) {
                session->SetProviderId(requestedProviderOverride);
            }

            std::string registryKey = providerId;
            if (providerManager) {
                if (const auto provider = providerManager->GetProvider(providerId)) {
                    registryKey = provider->providerType.empty()
                        ? provider->providerId : provider->providerType;
                }
            }

            if (providerId.empty()) {
                {
                    if (chatMutex) {
                        std::lock_guard<std::mutex> lock(*chatMutex);
                        SessionTurn userTurn;
                        userTurn.role = "user";
                        userTurn.content = userContent;
                        userTurn.unix_ms = adminserver_internal::NowUnixMs();
                        session->AddTurn(std::move(userTurn));
                    } else {
                        SessionTurn userTurn;
                        userTurn.role = "user";
                        userTurn.content = userContent;
                        userTurn.unix_ms = adminserver_internal::NowUnixMs();
                        session->AddTurn(std::move(userTurn));
                    }
                }

                const std::string assistantText = "Echo (no provider configured): " + userContent;
                const std::vector<std::string> chunks = ChunkText(assistantText, 16);
                std::string partialText;
                bool interrupted = false;
                for (const auto& chunk : chunks) {
                    if (stopSignal && stopSignal->load()) {
                        interrupted = true;
                        break;
                    }
                    Json::Value tokenMsg(Json::objectValue);
                    tokenMsg["type"] = "token";
                    tokenMsg["content"] = chunk;
                    tokenMsg["session_id"] = std::to_string(sessionId);
                    QueueSendJson(wsConnPtr, tokenMsg);
                    partialText += chunk;
                    std::this_thread::sleep_for(std::chrono::milliseconds(8));
                }

                std::uint64_t assistantMessageId = 0;
                {
                    if (chatMutex) {
                        std::lock_guard<std::mutex> lock(*chatMutex);
                        const std::string finalText = interrupted ? partialText : assistantText;
                        if (!finalText.empty()) {
                            SessionTurn turn;
                            turn.role = "assistant";
                            turn.content = finalText;
                            turn.unix_ms = adminserver_internal::NowUnixMs();
                            session->AddTurn(std::move(turn));
                            const auto& turns = session->Turns();
                            if (!turns.empty()) {
                                assistantMessageId = turns.back().turn_id;
                            }
                        }
                    } else {
                        const std::string finalText = interrupted ? partialText : assistantText;
                        if (!finalText.empty()) {
                            SessionTurn turn;
                            turn.role = "assistant";
                            turn.content = finalText;
                            turn.unix_ms = adminserver_internal::NowUnixMs();
                            session->AddTurn(std::move(turn));
                            const auto& turns = session->Turns();
                            if (!turns.empty()) {
                                assistantMessageId = turns.back().turn_id;
                            }
                        }
                    }
                }

                Json::Value doneMsg(Json::objectValue);
                doneMsg["type"] = "done";
                doneMsg["session_id"] = std::to_string(sessionId);
                doneMsg["message_id"] = std::to_string(assistantMessageId);
                doneMsg["interrupted"] = interrupted;
                QueueSendJson(wsConnPtr, doneMsg);
                return;
            }

            SlotGuard throttleGuard;
            if (providerThrottle) {
                throttleGuard = providerThrottle->Acquire(providerId);
            }

            if (!chainRunner) {
                QueueError(wsConnPtr, sessionId, "chain runner not available");
                return;
            }

            std::string model = requestedModelOverride;
            std::size_t contextWindow = 128000;
            ProviderState providerState;
            bool hasProviderState = false;
            if (providerManager) {
                if (const auto provider = providerManager->GetProvider(providerId)) {
                    providerState = *provider;
                    hasProviderState = true;
                    if (model.empty()) {
                        model = providerState.defaultModel;
                    }
                }
            }
            // Resolve agent context window for min-of-all clamp
            std::uint32_t agentCtxWindow = 0;
            if (agentStore && !session->AgentId().empty()) {
                auto agent = agentStore->GetById(session->AgentId());
                if (model.empty() && agent && !agent->default_model.empty()) {
                    model = agent->default_model;
                }
                if (agent && agent->context_window > 0) {
                    agentCtxWindow = agent->context_window;
                }
            }
            if (hasProviderState) {
                contextWindow = AdminServer::ResolveProviderContextWindow(
                    providerState,
                    model,
                    static_cast<std::uint32_t>(contextWindow),
                    agentCtxWindow);
            } else if (agentCtxWindow > 0) {
                contextWindow = std::min(contextWindow, static_cast<std::size_t>(agentCtxWindow));
            }
            if (model.empty()) {
                model = "gpt-4o";
            }

            const std::string identity = agentConfig ? agentConfig->identity : "";

            auto tokenCallback = [wsConnPtr, sessionId, stopSignal](const llm::LLMToken& token) {
                if (stopSignal && stopSignal->load()) {
                    return;
                }
                Json::Value tokenMsg(Json::objectValue);
                tokenMsg["type"] = "token";
                tokenMsg["content"] = token.content;
                tokenMsg["session_id"] = std::to_string(sessionId);
                QueueSendJson(wsConnPtr, tokenMsg);
            };

            auto thinkingCallback = [wsConnPtr, sessionId, stopSignal](const std::string& text) {
                if (stopSignal && stopSignal->load()) {
                    return;
                }
                Json::Value thinkMsg(Json::objectValue);
                thinkMsg["type"] = "thinking";
                thinkMsg["content"] = text;
                thinkMsg["session_id"] = std::to_string(sessionId);
                QueueSendJson(wsConnPtr, thinkMsg);
            };

            auto textCallback = [wsConnPtr, sessionId, stopSignal](const std::string& text) {
                if (stopSignal && stopSignal->load()) {
                    return;
                }
                Json::Value textMsg(Json::objectValue);
                textMsg["type"] = "text";
                textMsg["content"] = text;
                textMsg["session_id"] = std::to_string(sessionId);
                QueueSendJson(wsConnPtr, textMsg);
            };

            auto toolCallCallback = [wsConnPtr, sessionId, stopSignal](
                                        const ToolCall& call) {
                if (stopSignal && stopSignal->load()) {
                    return;
                }
                Json::Value msg(Json::objectValue);
                msg["type"] = "tool_call";
                msg["session_id"] = std::to_string(sessionId);
                msg["tool_call_id"] = call.id;
                msg["tool_name"] = call.name;
                msg["arguments"] = call.arguments;
                QueueSendJson(wsConnPtr, msg);
            };

            auto sessionKeyStr = session->Key().ToString();

            auto toolEventCallback = [wsConnPtr, sessionId, stopSignal,
                                      attachmentStore, attachmentTokenManager,
                                      sessionKeyStr](
                                         const ToolCall& call,
                                         const ToolResult& result) {
                if (stopSignal && stopSignal->load()) {
                    return;
                }
                Json::Value msg(Json::objectValue);
                msg["type"] = "tool_result";
                msg["session_id"] = std::to_string(sessionId);
                msg["tool_call_id"] = call.id;
                msg["tool_name"] = call.name;
                msg["success"] = result.success;
                msg["content"] = result.success ? result.output : result.error;
                QueueSendJson(wsConnPtr, msg);

                // If this was a successful attachment tool call, emit attachment event
                if (result.success && call.name == "add_chat_attachment" &&
                    attachmentStore && attachmentTokenManager) {
                    auto allAtts = attachmentStore->GetForSession(sessionKeyStr);
                    if (!allAtts.empty()) {
                        const auto& att = allAtts.back();
                        Json::Value attMsg(Json::objectValue);
                        attMsg["type"] = "attachment";
                        attMsg["session_id"] = std::to_string(sessionId);
                        Json::Value attData(Json::objectValue);
                        attData["id"] = att.id;
                        attData["filename"] = att.filename;
                        attData["mime_type"] = att.mime_type;
                        attData["size_bytes"] = static_cast<Json::Int64>(att.size_bytes);
                        attData["filepath"] = att.filepath;
                        attData["has_inline_data"] = !att.data_b64.empty();
                        attData["access_token"] = attachmentTokenManager->GenerateToken(att.id);
                        attMsg["attachment"] = attData;
                        QueueSendJson(wsConnPtr, attMsg);
                    }
                }
            };

            SessionAccess sessionAccess(session, SessionAccessMode::ReadWrite);

            // Load the latest compaction summary from DB and inject into session.
            // This ensures the PromptAssembler uses the DB-backed compaction
            // rather than a stale in-memory copy.
            if (compactionService) {
                auto latestCompaction = compactionService->GetLatestCompaction(session->Id());
                if (latestCompaction) {
                    SessionTurn summaryTurn;
                    summaryTurn.role = "system";
                    summaryTurn.content = latestCompaction->summary;
                    summaryTurn.is_summary = true;
                    summaryTurn.unix_ms = latestCompaction->created_at_unix_ms;
                    session->SetCompactionSummary(summaryTurn);
                }
            }

            chainRunner->SetThinkingCallback(thinkingCallback);
            chainRunner->SetToolCallCallback(toolCallCallback);

            auto result = chainRunner->ExecuteStreamingOnSession(
                sessionAccess,
                userContent,
                identity,
                registryKey,
                providerId,
                model,
                contextWindow,
                tokenCallback,
                textCallback,
                toolEventCallback);

            if (result.success && sessions) {
                sessions->FlushSession(session->Id());

                // Trigger compaction if needed — use LLM-based summary generation
                if (result.triggered_compaction && compactionService) {
                    // Use the already-resolved context window (clamped to agent limit)
                    std::size_t cw = contextWindow;

                    // Create LLM-based summary generator for this compaction
                    SummaryGenerator summaryGen;
                    if (providerRegistry && chainRunner) {
                        CompactionSummaryGenerator::Config genConfig;
                        genConfig.provider_id = registryKey;
                        genConfig.config_id = registryKey;
                        genConfig.model = model;
                        CompactionSummaryGenerator gen(
                            *providerRegistry,
                            configLookup,
                            genConfig);
                        summaryGen = gen.CreateCallback();
                    }

                    // Compaction runs synchronously for now.
                    // Future: offload to background job.
                    compactionService->CompactIfNeeded(
                        session->Id(),
                        identity,
                        registryKey,
                        model,
                        cw,
                        summaryGen);
                }
            }

            std::uint64_t assistantMessageId = 0;
            const auto& turns = session->Turns();
            if (!turns.empty()) {
                assistantMessageId = turns.back().turn_id;
            }

            if (!result.success) {
                QueueError(wsConnPtr, sessionId, result.error);
            }

            Json::Value doneMsg(Json::objectValue);
            doneMsg["type"] = "done";
            doneMsg["session_id"] = std::to_string(sessionId);
            doneMsg["message_id"] = std::to_string(assistantMessageId);
            doneMsg["interrupted"] = false;
            doneMsg["elapsed_ms"] = result.elapsed_ms;
            QueueSendJson(wsConnPtr, doneMsg);
        });

    return true;
}

} // namespace animus::kernel

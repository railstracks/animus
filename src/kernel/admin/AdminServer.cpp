#include "animus_kernel/AdminServer.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/ChainRunner.h"
#include "animus_kernel/CompactionService.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/ProviderThrottle.h"
#include "animus_kernel/llm/LLMProviderRegistry.h"
#include "animus_kernel/lua/ScriptStore.h"
#include "animus_kernel/lua/FilesystemScriptLoader.h"
#include "animus_kernel/tools/ToolTypes.h"

#include <curl/curl.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>
#include <drogon/drogon.h>
#include <drogon/WebSocketController.h>

#include "kernel/admin/AdminUiResources.h"
#include "kernel/admin/internal/AdminServerInternals.h"
#include "animus_kernel/SessionManager.h"
#include <optional>

namespace animus::kernel {
namespace adminserver_internal {

// Forward declarations (anonymous namespace helpers defined later)
std::string JsonToCompactString(const Json::Value& value);

bool ParseJsonPayload(const std::string& payload, Json::Value* out, std::string* error) {
    if (!out) return false;
    Json::CharReaderBuilder builder;
    std::istringstream stream(payload);
    std::string parseErrors;
    if (!Json::parseFromStream(builder, stream, out, &parseErrors)) {
        if (error) *error = parseErrors;
        return false;
    }
    return true;
}

std::once_flag g_handlersRegistered;
std::atomic<AdminServer*> g_activeAdminServer{nullptr};

std::string MaskSecret(const std::string& value) {
    if (value.empty()) {
        return "";
    }
    if (value.size() <= 4U) {
        return "****";
    }
    return value.substr(0, 4) + "...****";
}

bool ShouldServeAdminUiFromFilesystem() {
    if (const char* env = std::getenv("ANIMUS_ADMIN_UI_SERVE_FILESYSTEM")) {
        const std::string value(env);
        return value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "YES";
    }
    return false;
}

std::string DetectAdminUiMimeType(std::string_view relativePath) {
    const std::string path(relativePath);
    const std::filesystem::path parsed(path);
    std::string ext = parsed.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext == ".html") {
        return "text/html; charset=utf-8";
    }
    if (ext == ".js") {
        return "application/javascript; charset=utf-8";
    }
    if (ext == ".css") {
        return "text/css; charset=utf-8";
    }
    if (ext == ".json") {
        return "application/json; charset=utf-8";
    }
    if (ext == ".woff2") {
        return "font/woff2";
    }
    if (ext == ".woff") {
        return "font/woff";
    }
    if (ext == ".ttf") {
        return "font/ttf";
    }
    if (ext == ".eot") {
        return "application/vnd.ms-fontobject";
    }
    if (ext == ".svg") {
        return "image/svg+xml";
    }
    if (ext == ".png") {
        return "image/png";
    }
    if (ext == ".jpg" || ext == ".jpeg") {
        return "image/jpeg";
    }
    if (ext == ".webp") {
        return "image/webp";
    }
    if (ext == ".ico") {
        return "image/x-icon";
    }

    return "application/octet-stream";
}

drogon::HttpResponsePtr BuildEmbeddedAdminUiResponse(
    std::string_view relativePath,
    bool cacheable) {
    EmbeddedAdminUiAsset asset;
    if (!GetEmbeddedAdminUiAsset(relativePath, &asset) || !asset.data || asset.size == 0) {
        return nullptr;
    }

    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k200OK);
    response->setBody(std::string(reinterpret_cast<const char*>(asset.data), asset.size));
    response->setContentTypeString(DetectAdminUiMimeType(relativePath));
    if (cacheable) {
        response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
    } else {

    }
    return response;
}

Json::Value BuildObservationStreamEvent(const KernelConfig::MemoryObservation& observation) {
    Json::Value out(Json::objectValue);
    out["type"] = "observation";
    out["id"] = observation.id;
    out["layer"] = observation.layer;
    Json::Value tags(Json::arrayValue);
    for (const auto& tag : observation.tags) {
        tags.append(tag);
    }
    out["tags"] = tags;
    out["content"] = observation.text;
    out["timestamp"] = std::to_string(observation.timestampUnixMs);
    out["timestamp_unix_ms"] = static_cast<Json::UInt64>(observation.timestampUnixMs);
    return out;
}

bool ObservationMatchesFilter(
    const KernelConfig::MemoryObservation& observation,
    const ObservationStreamFilter& filter) {
    if (!filter.layers.empty()) {
        bool layerMatch = false;
        for (const auto& layer : filter.layers) {
            if (layer == observation.layer) {
                layerMatch = true;
                break;
            }
        }
        if (!layerMatch) {
            return false;
        }
    }

    if (!filter.tags.empty()) {
        bool tagMatch = false;
        for (const auto& filterTag : filter.tags) {
            for (const auto& observationTag : observation.tags) {
                if (filterTag == observationTag) {
                    tagMatch = true;
                    break;
                }
            }
            if (tagMatch) {
                break;
            }
        }
        if (!tagMatch) {
            return false;
        }
    }

    return true;
}

bool IsSensitiveFieldName(const std::string& key) {
    static const std::unordered_set<std::string> sensitiveKeys{
        "token", "bot_token", "api_key", "apikey", "secret", "client_secret", "password"};
    if (sensitiveKeys.find(key) != sensitiveKeys.end()) {
        return true;
    }
    std::string lower = key;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    return lower.find("secret") != std::string::npos
        || lower.find("password") != std::string::npos
        || lower.find("api_key") != std::string::npos;
}

Json::Value MaskInterfaceConfig(const Json::Value& input) {
    if (input.isObject()) {
        Json::Value out(Json::objectValue);
        const std::vector<std::string> names = input.getMemberNames();
        for (const auto& name : names) {
            if (IsSensitiveFieldName(name) && input[name].isString()) {
                out[name] = MaskSecret(input[name].asString());
            } else {
                out[name] = MaskInterfaceConfig(input[name]);
            }
        }
        return out;
    }
    if (input.isArray()) {
        Json::Value out(Json::arrayValue);
        for (Json::ArrayIndex i = 0; i < input.size(); ++i) {
            out.append(MaskInterfaceConfig(input[i]));
        }
        return out;
    }
    return input;
}

Json::Value BuildInterfaceJson(const InterfaceState& iface, bool includeMaskedConfig) {
    Json::Value out(Json::objectValue);
    out["name"] = iface.name;
    out["type"] = iface.type;
    out["module_id"] = iface.moduleId;
    out["enabled"] = iface.enabled;
    out["connected"] = iface.connected;
    out["last_event_unix_ms"] = static_cast<Json::UInt64>(iface.lastEventUnixMs);
    if (includeMaskedConfig) {
        out["config"] = MaskInterfaceConfig(iface.config);
    }
    return out;
}

bool ParsePrefixedUInt64(
    const std::string& text,
    const std::string& prefix,
    std::uint64_t* out) {
    if (!out || text.size() <= prefix.size()) {
        return false;
    }
    if (text.rfind(prefix, 0) != 0) {
        return false;
    }
    try {
        *out = static_cast<std::uint64_t>(std::stoull(text.substr(prefix.size())));
    } catch (...) {
        return false;
    }
    return true;
}

std::string JsonToCompactString(const Json::Value& value) {
    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    return Json::writeString(writerBuilder, value);
}

std::vector<std::filesystem::path> CandidateAdminUiDistDirectories() {
    std::vector<std::filesystem::path> candidates;

    if (const char* env = std::getenv("ANIMUS_ADMIN_UI_DIST_DIR")) {
        const std::string configured(env);
        if (!configured.empty()) {
            candidates.emplace_back(std::filesystem::path(configured));
        }
    }

    const std::filesystem::path cwd = std::filesystem::current_path();
    candidates.push_back(cwd / "admin-ui" / "dist");
    candidates.push_back(cwd / ".." / "admin-ui" / "dist");
    candidates.push_back(cwd / "dist" / "admin-ui");

    return candidates;
}

bool IsSafeAdminAssetPath(const std::string& relativePath) {
    if (relativePath.empty() || relativePath.front() == '/' || relativePath.find('\\') != std::string::npos) {
        return false;
    }
    if (relativePath.find("..") != std::string::npos) {
        return false;
    }
    return true;
}

bool ResolveAdminUiFilePath(
    const std::string& relativePath,
    std::filesystem::path* resolvedPath) {
    if (!resolvedPath || !IsSafeAdminAssetPath(relativePath)) {
        return false;
    }

    const auto candidates = CandidateAdminUiDistDirectories();
    for (const auto& base : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(base, ec) || ec) {
            continue;
        }
        const std::filesystem::path candidate = base / relativePath;
        if (std::filesystem::exists(candidate, ec) && !ec && std::filesystem::is_regular_file(candidate, ec)
            && !ec) {
            *resolvedPath = candidate;
            return true;
        }
    }
    return false;
}

bool ParseSessionId(const std::string& text, SessionId* out) {
    std::size_t parsed = 0;
    unsigned long long value = 0;
    try {
        value = std::stoull(text, &parsed);
    } catch (...) {
        return false;
    }
    if (parsed != text.size() || value == 0ULL
        || value > std::numeric_limits<SessionId>::max()) {
        return false;
    }
    *out = static_cast<SessionId>(value);
    return true;
}

Json::Value BuildSessionSummaryJson(const Session& session) {
    Json::Value out(Json::objectValue);
    out["id"] = static_cast<Json::UInt64>(session.Id());
    out["source"] = session.Key().connector;
    out["conversation_id"] = session.Key().conversation_id;
    out["thread_id"] = session.Key().thread_id;
    out["message_count"] = static_cast<Json::UInt64>(session.MessageCount());
    out["created_unix_ms"] = static_cast<Json::UInt64>(session.CreatedUnixMs());
    out["last_active_unix_ms"] = static_cast<Json::UInt64>(session.LastActiveUnixMs());
    out["status"] = session.IsTerminated() ? "terminated" : "active";
    out["session_type"] = session.SessionType();
    return out;
}

Json::Value BuildSessionTurnJson(const SessionTurn& turn) {
    Json::Value out(Json::objectValue);
    out["turn_id"] = static_cast<Json::UInt64>(turn.turn_id);
    out["role"] = turn.role;
    out["content"] = turn.content;
    out["unix_ms"] = static_cast<Json::UInt64>(turn.unix_ms);
    out["is_summary"] = turn.is_summary;
    out["is_compacted"] = turn.is_compacted;
    out["thinking_content"] = turn.thinking_content;
    out["tool_call_id"] = turn.tool_call_id;
    out["tool_name"] = turn.tool_name;
    Json::Value toolCalls(Json::arrayValue);
    for (const auto& call : turn.tool_calls) {
        Json::Value callJson(Json::objectValue);
        callJson["id"] = call.id;
        callJson["name"] = call.name;
        callJson["arguments"] = call.arguments;
        toolCalls.append(callJson);
    }
    out["tool_calls"] = toolCalls;
    out["intake_processed"] = turn.intake_processed;
    out["intake_processed_at_unix_ms"] =
        static_cast<Json::UInt64>(turn.intake_processed_at_unix_ms);
    Json::Value compactedFrom(Json::arrayValue);
    for (const SessionTurnId sourceTurnId : turn.compacted_from) {
        compactedFrom.append(static_cast<Json::UInt64>(sourceTurnId));
    }
    out["compacted_from"] = compactedFrom;
    return out;
}

Json::Value BuildSessionTurnJson(const SessionTurn& turn,
                                  const std::vector<Attachment>& attachments) {
    Json::Value out = BuildSessionTurnJson(turn);  // base fields

    if (!attachments.empty()) {
        Json::Value atts(Json::arrayValue);
        for (const auto& att : attachments) {
            Json::Value attJson(Json::objectValue);
            attJson["id"] = att.id;
            attJson["filename"] = att.filename;
            attJson["mime_type"] = att.mime_type;
            attJson["size_bytes"] = static_cast<Json::Int64>(att.size_bytes);
            attJson["filepath"] = att.filepath;
            attJson["has_inline_data"] = !att.data_b64.empty();
            atts.append(attJson);
        }
        out["attachments"] = atts;
    }
    return out;
}

Json::Value BuildSessionTurnJson(const SessionTurn& turn,
                                  const std::vector<Attachment>& attachments,
                                  AttachmentTokenManager* tokenMgr) {
    Json::Value out = BuildSessionTurnJson(turn);  // base fields

    if (!attachments.empty() && tokenMgr) {
        Json::Value atts(Json::arrayValue);
        for (const auto& att : attachments) {
            Json::Value attJson(Json::objectValue);
            attJson["id"] = att.id;
            attJson["filename"] = att.filename;
            attJson["mime_type"] = att.mime_type;
            attJson["size_bytes"] = static_cast<Json::Int64>(att.size_bytes);
            attJson["filepath"] = att.filepath;
            attJson["has_inline_data"] = !att.data_b64.empty();
            // Generate short-lived token for this attachment
            attJson["access_token"] = tokenMgr->GenerateToken(att.id);
            atts.append(attJson);
        }
        out["attachments"] = atts;
    } else if (!attachments.empty()) {
        out = BuildSessionTurnJson(turn, attachments);
    }
    return out;
}

std::uint32_t ParsePaginationParam(
    const drogon::HttpRequestPtr& req,
    const char* name,
    std::uint32_t defaultValue,
    std::uint32_t minValue,
    std::uint32_t maxValue) {
    const std::string raw = req->getParameter(name);
    if (raw.empty()) {
        return defaultValue;
    }
    std::size_t parsed = 0;
    unsigned long value = 0;
    try {
        value = std::stoul(raw, &parsed);
    } catch (...) {
        return defaultValue;
    }
    if (parsed != raw.size()) {
        return defaultValue;
    }
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return static_cast<std::uint32_t>(value);
}



std::uint64_t NowUnixMs() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch());
    return static_cast<std::uint64_t>(ms.count());
}

bool IsValidModelToken(const std::string& token) {
    if (token.empty() || token.size() > 128U) {
        return false;
    }
    for (char c : token) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) != 0) {
            continue;
        }
        if (c == '.' || c == '-' || c == '_' || c == ':' || c == '/') {
            continue;
        }
        return false;
    }
    return true;
}

Json::Value BuildAgentJson(const KernelConfig::AgentRuntimeConfig& config, std::uint64_t reinitCount) {
    Json::Value out(Json::objectValue);

    Json::Value model(Json::objectValue);
    model["provider"] = config.model.provider;
    model["model_id"] = config.model.modelId;
    model["context_window"] = config.model.contextWindow;
    model["api_key"] = MaskSecret(config.model.apiKey);
    out["model"] = model;

    out["temperature"] = config.temperature;
    out["identity"] = config.identity;
    out["allow_self_identity_edit"] = config.allowSelfIdentityEdit;

    Json::Value reasoning(Json::objectValue);
    reasoning["enabled"] = config.reasoningEnabled;
    reasoning["effort"] = config.reasoning_effort;
    reasoning["instruction"] = config.reasoningInstruction;
    out["reasoning"] = reasoning;

    Json::Value budget(Json::objectValue);
    budget["max_chain_steps"] = config.budget.maxChainSteps;
    budget["max_tool_calls_per_chain"] = config.budget.maxToolCallsPerChain;
    budget["timeout_seconds"] = config.budget.timeoutSeconds;
    budget["token_budget_per_prompt"] = config.budget.tokenBudgetPerPrompt;
    budget["episodic_token_budget"] = config.budget.episodicTokenBudget;
    budget["semantic_token_budget"] = config.budget.semanticTokenBudget;
    budget["perspectives_token_budget"] = config.budget.perspectivesTokenBudget;
    budget["consolidation_tool_budget"] = config.budget.consolidationToolBudget;
    budget["memory_file_token_budget"] = config.budget.memoryFileTokenBudget;
    budget["ambient_context_limit"] = config.budget.ambientContextLimit;
    budget["session_report_token_budget"] = config.budget.sessionReportTokenBudget;
    out["budget"] = budget;

    Json::Value runtime(Json::objectValue);
    runtime["model_reinitializations"] = static_cast<Json::UInt64>(reinitCount);
    out["runtime"] = runtime;

    return out;
}

Json::Value BuildModelJson(
    const KernelConfig::AgentRuntimeConfig& config,
    std::uint64_t reinitCount) {
    Json::Value out(Json::objectValue);
    out["provider"] = config.model.provider;
    out["model_id"] = config.model.modelId;
    out["context_window"] = config.model.contextWindow;
    out["api_key"] = MaskSecret(config.model.apiKey);
    out["model_reinitializations"] = static_cast<Json::UInt64>(reinitCount);
    return out;
}

void AddBadRequestError(const std::string& message, Json::Value* out) {
    (*out)["error"] = message;
}

// ============================================================================
// Agent entity JSON helpers (multi-tenant CRUD)
// ============================================================================

Json::Value BuildAgentEntityJson(const Agent& a) {
    Json::Value out(Json::objectValue);
    out["id"] = a.id;
    out["numeric_id"] = static_cast<Json::Int64>(a.numeric_id);
    out["name"] = a.name;
    out["description"] = a.description;
    out["identity"] = a.identity;
    out["avatar"] = a.avatar;

    Json::Value model(Json::objectValue);
    model["provider"] = a.default_provider;
    model["model_id"] = a.default_model;
    out["model"] = model;

    out["default_vision_model"] = a.default_vision_model;
    out["intake_interval"] = a.intake_interval;
    out["intake_prompt"] = a.intake_prompt;

    out["temperature"] = a.temperature;

    Json::Value reasoning(Json::objectValue);
    reasoning["enabled"] = a.reasoning_enabled;
    reasoning["effort"] = a.reasoning_effort;
    out["reasoning"] = reasoning;

    out["pad_context"] = a.pad_context;

    Json::Value budget(Json::objectValue);
    budget["max_chain_steps"] = static_cast<Json::UInt>(a.budget.maxChainSteps);
    budget["max_tool_calls_per_chain"] = static_cast<Json::UInt>(a.budget.maxToolCallsPerChain);
    budget["timeout_seconds"] = static_cast<Json::UInt>(a.budget.timeoutSeconds);
    budget["token_budget_per_prompt"] = static_cast<Json::UInt>(a.budget.tokenBudgetPerPrompt);
    budget["episodic_token_budget"] = static_cast<Json::UInt>(a.budget.episodicTokenBudget);
    budget["semantic_token_budget"] = static_cast<Json::UInt>(a.budget.semanticTokenBudget);
    budget["perspectives_token_budget"] = static_cast<Json::UInt>(a.budget.perspectivesTokenBudget);
    budget["memory_file_token_budget"] = static_cast<Json::UInt>(a.budget.memoryFileTokenBudget);
    budget["ambient_context_limit"] = static_cast<Json::UInt>(a.budget.ambientContextLimit);
    budget["session_report_token_budget"] = static_cast<Json::UInt>(a.budget.sessionReportTokenBudget);
    budget["consolidation_tool_budget"] = static_cast<Json::UInt>(a.budget.consolidationToolBudget);
    out["budget"] = budget;

    Json::Value tools(Json::arrayValue);
    for (const auto& t : a.enabled_tools) tools.append(t);
    out["enabled_tools"] = tools;

    Json::Value toolConfigs(Json::objectValue);
    if (!a.tool_configs_json.empty()) {
        Json::Value parsed(Json::objectValue);
        std::string parseErr;
        if (ParseJsonPayload(a.tool_configs_json, &parsed, &parseErr) && parsed.isObject()) {
            toolConfigs = parsed;
        }
    }
    out["tool_configs"] = toolConfigs;

    Json::Value allowedNodes(Json::arrayValue);
    for (const auto& n : a.allowed_nodes) allowedNodes.append(n);
    out["allowed_nodes"] = allowedNodes;

    out["session_report_token_budget"] = a.budget.sessionReportTokenBudget;

    out["created_at_unix_ms"] = static_cast<Json::Int64>(a.created_at_unix_ms);
    out["updated_at_unix_ms"] = static_cast<Json::Int64>(a.updated_at_unix_ms);
    // is_default removed — no longer part of Agent struct
    out["is_default"] = false;
    return out;
}

void SendJsonResponse(
    const std::function<void(const drogon::HttpResponsePtr&)>& callback,
    const Json::Value& body,
    drogon::HttpStatusCode code) {
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    callback(resp);
}

void SendJsonError(
    const std::function<void(const drogon::HttpResponsePtr&)>& callback,
    const std::string& message,
    drogon::HttpStatusCode code) {
    Json::Value out(Json::objectValue);
    out["error"] = message;
    SendJsonResponse(callback, out, code);
}

void SendServerNotActive(
    const std::function<void(const drogon::HttpResponsePtr&)>& callback) {
    SendJsonError(callback, "admin server not active", drogon::k503ServiceUnavailable);
}

std::string UrlEncode(const std::string& input) {
    CURL* curl = curl_easy_init();
    if (!curl) return {};
    char* encoded = curl_easy_escape(curl, input.c_str(), static_cast<int>(input.size()));
    std::string out = encoded ? std::string(encoded) : std::string();
    if (encoded) curl_free(encoded);
    curl_easy_cleanup(curl);
    return out;
}

} // namespace adminserver_internal

using namespace adminserver_internal;

std::uint32_t AdminServer::ResolveProviderContextWindow(
    const ProviderState& state,
    const std::string& modelId,
    std::uint32_t fallback,
    std::uint32_t agentContextWindow) {
    // Collect all applicable context window limits, then take the minimum.
    // This ensures a cap set at any level (model, provider, agent) is always
    // respected — e.g. an agent-level 200k cap takes effect even if the model
    // supports 1M.
    std::uint32_t best = fallback;

    bool haveAny = false;

    // 1. Per-model override (user-explicit or discovered via capability refresh)
    if (!modelId.empty()) {
        auto it = state.modelContextWindows.find(modelId);
        if (it != state.modelContextWindows.end() && it->second > 0U) {
            best = it->second;
            haveAny = true;
        }
    }

    // 2. Provider default context window (manual config)
    if (state.defaultContextWindow > 0U) {
        if (haveAny) {
            best = std::min(best, state.defaultContextWindow);
        } else {
            best = state.defaultContextWindow;
            haveAny = true;
        }
    }

    // 3. Provider-level capabilities from discovery (may be for a different model)
    if (state.capabilities.context_length > 0U) {
        std::uint32_t cap = static_cast<std::uint32_t>(state.capabilities.context_length);
        if (haveAny) {
            best = std::min(best, cap);
        } else {
            best = cap;
            haveAny = true;
        }
    }

    // 4. Agent-level context window (overruling cap for token efficiency)
    if (agentContextWindow > 0U) {
        if (haveAny) {
            best = std::min(best, agentContextWindow);
        } else {
            best = agentContextWindow;
        }
    }

    return best;
}

AdminServer::AdminServer() = default;

AdminServer::~AdminServer() {
    Stop();
}

bool AdminServer::Start(
    const KernelConfig::AdminServerConfig& config,
    const KernelConfig::AgentRuntimeConfig& initialAgentConfig,
    const KernelConfig::AgentConfigStorage& agentStorage,
    const KernelConfig::InterfaceConfigStorage& interfaceStorage,
    const std::vector<KernelConfig::InterfaceModuleInfo>& interfaceModules,
    const KernelConfig::MemoryConfig& memoryConfig,
    const KernelConfig::ConstitutionConfigStorage& constitutionStorage,
    const KernelConfig::ProviderConfigStorage& providerStorage,
    llm::LLMProviderRegistry* providerRegistry,
    SessionManager* sessions,
    ::animus::jobs::JobSystem* jobSystem,
    std::chrono::steady_clock::time_point kernelStartedAt,
    std::string* error) {
    if (!config.enabled) {
        return true;
    }

    if (IsRunning()) {
        return true;
    }

    if (config.host.empty()) {
        if (error) {
            *error = "admin server host must not be empty";
        }
        return false;
    }

    if (config.port == 0U) {
        if (error) {
            *error = "admin server port must be greater than 0";
        }
        return false;
    }

    m_config = config;
    m_agentConfig = initialAgentConfig;
    m_agentStorage = agentStorage;
    m_providerRegistry = providerRegistry;
    m_providerManager.Configure(providerStorage);
    m_agentManager.Configure(m_agentStore, &m_providerManager, m_memoryStore);
    m_memoryManager.Configure(memoryConfig, &m_nextObservationId);
    m_constitutionManager.Configure(constitutionStorage);
    m_sessions = sessions;
    m_jobs = jobSystem;
    m_kernelStartedAt = kernelStartedAt;
    RefreshChatSessionServiceDependencies();

    // Seed WS conversation counter from existing sessions to avoid collisions after restart.
    // Existing sessions may have conversation_id values like "ws-conversation-5";
    // we need the next counter to be higher than any existing one.
    if (m_sessions) {
        std::uint64_t maxConvId = 0;
        for (const auto& session : m_sessions->ListSessions()) {
            const auto& convId = session->Key().conversation_id;
            // Extract numeric suffix from "ws-conversation-N"
            const std::string prefix = "ws-conversation-";
            if (convId.compare(0, prefix.size(), prefix) == 0) {
                try {
                    std::size_t parsed = 0;
                    const std::uint64_t num = std::stoull(convId.substr(prefix.size()), &parsed);
                    if (parsed == convId.size() - prefix.size() && num > maxConvId) {
                        maxConvId = num;
                    }
                } catch (...) {
                    // Ignore non-numeric suffixes
                }
            }
        }
        m_nextWsConversationId.store(maxConvId + 1);
        std::cerr << "[admin] seeded ws conversation counter to " << (maxConvId + 1)
                  << " from existing sessions" << std::endl;
    }

    m_started.store(false);
    m_running.store(false);
    m_modelReinitCount.store(0);
    {
        std::lock_guard<std::mutex> lock(m_startMutex);
        m_startError.clear();
    }

    std::string loadErr;
    if (!LoadAgentConfigFromDisk(&loadErr)) {
        if (error) {
            *error = loadErr;
        }
        return false;
    }

    std::string providerLoadErr;
    if (!LoadProvidersFromDisk(&providerLoadErr)) {
        if (error) *error = providerLoadErr;
        return false;
    }

    m_interfaceManager.Configure(interfaceStorage, interfaceModules);
    std::string ifaceLoadErr;
    if (!LoadInterfacesFromDisk(&ifaceLoadErr)) {
        if (error) *error = ifaceLoadErr;
        return false;
    }

    std::string memoryLoadErr;
    if (!LoadMemoryFromDisk(&memoryLoadErr)) {
        if (error) *error = memoryLoadErr;
        return false;
    }

    std::string constitutionLoadErr;
    if (!LoadConstitutionFromDisk(&constitutionLoadErr)) {
        if (error) *error = constitutionLoadErr;
        return false;
    }

    m_thread = std::thread(&AdminServer::RunLoop, this);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (m_started.load()) {
            if (!m_running.load()) {
                if (m_thread.joinable()) {
                    m_thread.join();
                }
                if (error) {
                    std::lock_guard<std::mutex> lock(m_startMutex);
                    *error = m_startError.empty() ? "admin server failed to start" : m_startError;
                }
                return false;
            }
            SyncIrcInterfaces();
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (m_thread.joinable()) {
        drogon::app().quit();
        m_thread.join();
    }

    if (error) {
        *error = "timed out while starting admin server";
    }
    return false;
}

void AdminServer::Stop() {
    m_interfaceManager.StopAllIrcInterfaces();

    if (!m_thread.joinable()) {
        m_running.store(false);
        m_started.store(false);
        return;
    }

    drogon::app().quit();
    m_thread.join();
    m_running.store(false);
    m_started.store(false);
}

bool AdminServer::IsRunning() const {
    return m_running.load();
}

void AdminServer::SyncIrcInterfaces() {
    m_interfaceManager.SyncIrcInterfaces(
        [this](
            const std::string& interfaceName,
            const std::string& botNick,
            const std::string& sourceNick,
            const std::string& target,
            const std::string& message,
            bool isNotice) {
            if (sourceNick.empty() || message.empty()) {
                return;
            }
            if (sourceNick == botNick) {
                return;
            }

            bool respondToChannel = true;
            bool respondToDm = true;
            bool respondToNotices = false;
            std::vector<std::string> allowedDmUsers;
            std::string configuredAgentId = "default";
            {
                auto iface = m_interfaceManager.GetInterface(interfaceName);
                if (!iface || !iface->enabled) {
                    return;
                }
                const Json::Value& config = iface->config;
                if (config.isMember("respond_to_channel_activity")) {
                    respondToChannel = config["respond_to_channel_activity"].asBool();
                }
                if (config.isMember("respond_to_direct_messages")) {
                    respondToDm = config["respond_to_direct_messages"].asBool();
                }
                if (config.isMember("respond_to_notices")) {
                    respondToNotices = config["respond_to_notices"].asBool();
                }
                if (config.isMember("agent_id") && config["agent_id"].isString()
                    && !config["agent_id"].asString().empty()) {
                    configuredAgentId = config["agent_id"].asString();
                }
                if (config.isMember("allowed_dm_users") && config["allowed_dm_users"].isArray()) {
                    for (Json::ArrayIndex i = 0; i < config["allowed_dm_users"].size(); ++i) {
                        if (config["allowed_dm_users"][i].isString()) {
                            allowedDmUsers.push_back(config["allowed_dm_users"][i].asString());
                        }
                    }
                }
            }

            if (isNotice && !respondToNotices) {
                return;
            }
            const bool channelMessage = !target.empty()
                && (target[0] == '#' || target[0] == '&' || target[0] == '!' || target[0] == '+');
            if (channelMessage && !respondToChannel) {
                return;
            }
            if (!channelMessage && !respondToDm) {
                return;
            }
            if (!channelMessage && !allowedDmUsers.empty()
                && std::find(allowedDmUsers.begin(), allowedDmUsers.end(), sourceNick) == allowedDmUsers.end()) {
                return;
            }
            if (!m_jobs || !m_sessions || !m_chainRunner) {
                return;
            }

            const std::string conversationId = channelMessage
                ? ("channel:" + target)
                : ("dm:" + sourceNick);
            const std::string threadId;
            m_jobs->EnqueueInLane(
                ::animus::jobs::JobLane::Cognition,
                [this, interfaceName, sourceNick, target, message, channelMessage, configuredAgentId, conversationId, threadId]() {
                    SessionKey key{
                        "irc:" + interfaceName,
                        conversationId,
                        threadId};
                    auto session = m_sessions ? m_sessions->GetOrCreate(key) : nullptr;
                    if (!session) {
                        return;
                    }

                    if (session->AgentId().empty()) {
                        session->SetAgentId(configuredAgentId);
                    } else if (session->Turns().empty()) {
                        session->SetAgentId(configuredAgentId);
                    }
                    if (m_agentStore && !session->AgentId().empty()
                        && !m_agentStore->GetById(session->AgentId()).has_value()) {
                        // Agent not found — clear rather than fallback to "default"
                        session->SetAgentId("");
                    }

                    std::string providerId = session->ProviderId();
                    if (providerId.empty() && m_agentStore && !session->AgentId().empty()) {
                        auto agent = m_agentStore->GetById(session->AgentId());
                        if (agent && !agent->default_provider.empty()) {
                            providerId = agent->default_provider;
                        }
                    }
                    if (providerId.empty()) {
                        providerId = m_providerManager.GetDefaultProviderId();
                    }
                    std::string registryKey = providerId;
                    ProviderState providerState;
                    bool hasProviderState = false;
                    if (const auto provider = m_providerManager.GetProvider(providerId)) {
                        providerState = *provider;
                        hasProviderState = true;
                        registryKey = providerState.providerType.empty()
                            ? providerState.providerId : providerState.providerType;
                    }
                    if (providerId.empty()) {
                        return;
                    }

                    SlotGuard throttleGuard;
                    if (m_providerThrottle) {
                        throttleGuard = m_providerThrottle->Acquire(providerId);
                    }

                    std::string model = hasProviderState ? providerState.defaultModel : "";
                    std::size_t contextWindow = 128000;
                    std::uint32_t agentCtxWindow = 0;
                    if (m_agentStore && !session->AgentId().empty()) {
                        auto agent = m_agentStore->GetById(session->AgentId());
                        if (agent && !agent->default_model.empty()) {
                            model = agent->default_model;
                        }
                        if (agent && agent->context_window > 0) {
                            agentCtxWindow = agent->context_window;
                        }
                    }
                    if (hasProviderState) {
                        contextWindow = ResolveProviderContextWindow(
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

                    const std::string identity = m_agentConfig.identity;
                    auto sessionAccess = SessionAccess(session, SessionAccessMode::ReadWrite);
                    auto result = m_chainRunner->ExecuteOnSession(
                        sessionAccess,
                        message,
                        identity,
                        registryKey,
                        providerId,
                        model,
                        contextWindow);
                    if (result.success && m_sessions) {
                        m_sessions->FlushSession(session->Id());
                    }
                    if (!result.success || result.response.empty()) {
                        return;
                    }

                    const std::string outputTarget = channelMessage ? target : sourceNick;
                    (void)m_interfaceManager.SendPrivmsg(interfaceName, outputTarget, result.response);
                });
                },
        [this](const std::string& interfaceName, bool connected, std::uint64_t eventUnixMs) {
            m_interfaceManager.SetInterfaceConnectionStatus(interfaceName, connected, eventUnixMs);
        });
}

std::uint64_t AdminServer::UptimeSeconds() const {
    const auto now = std::chrono::steady_clock::now();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now - m_kernelStartedAt);
    if (seconds.count() < 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(seconds.count());
}

bool AdminServer::LoadAgentConfigFromDisk(std::string* error) {
    if (!m_agentStorage.persistToDisk) {
        return true;
    }

    if (m_agentStorage.filePath.empty()) {
        if (error) {
            *error = "agent config file path must not be empty when persistence is enabled";
        }
        return false;
    }

    const std::filesystem::path path(m_agentStorage.filePath);
    if (!std::filesystem::exists(path)) {
        return true;
    }

    std::ifstream in(path);
    if (!in.is_open()) {
        if (error) {
            *error = "failed to open agent config file for reading: " + path.string();
        }
        return false;
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string parseErrors;
    if (!Json::parseFromStream(builder, in, &root, &parseErrors)) {
        if (error) {
            *error = "failed to parse agent config file: " + parseErrors;
        }
        return false;
    }

    KernelConfig::AgentRuntimeConfig loaded = m_agentConfig;
    bool changed = false;
    std::string applyErr;
    if (!m_agentManager.ApplyRuntimeConfigPatch(root, &loaded, &changed, &applyErr)) {
        if (error) {
            *error = "agent config file is invalid: " + applyErr;
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(m_agentMutex);
    m_agentConfig = std::move(loaded);
    return true;
}

bool AdminServer::SaveAgentConfigToDisk(
    const KernelConfig::AgentRuntimeConfig& config,
    std::string* error) const {
    if (!m_agentStorage.persistToDisk) {
        return true;
    }

    if (m_agentStorage.filePath.empty()) {
        if (error) {
            *error = "agent config file path must not be empty when persistence is enabled";
        }
        return false;
    }

    const std::filesystem::path path(m_agentStorage.filePath);
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            if (error) {
                *error = "failed to create config directory: " + parent.string();
            }
            return false;
        }
    }

    Json::Value root(Json::objectValue);
    root["model"]["provider"] = config.model.provider;
    root["model"]["model_id"] = config.model.modelId;
    root["model"]["context_window"] = config.model.contextWindow;
    root["model"]["api_key"] = config.model.apiKey;
    root["temperature"] = config.temperature;
    root["identity"] = config.identity;
    root["allow_self_identity_edit"] = config.allowSelfIdentityEdit;
    root["budget"]["max_chain_steps"] = config.budget.maxChainSteps;
    root["budget"]["max_tool_calls_per_chain"] = config.budget.maxToolCallsPerChain;
    root["budget"]["timeout_seconds"] = config.budget.timeoutSeconds;
    root["budget"]["token_budget_per_prompt"] = config.budget.tokenBudgetPerPrompt;
    root["budget"]["episodic_token_budget"] = config.budget.episodicTokenBudget;
    root["budget"]["semantic_token_budget"] = config.budget.semanticTokenBudget;
    root["budget"]["perspectives_token_budget"] = config.budget.perspectivesTokenBudget;
    root["budget"]["consolidation_tool_budget"] = config.budget.consolidationToolBudget;

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "  ";

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        if (error) {
            *error = "failed to open agent config file for writing: " + path.string();
        }
        return false;
    }

    std::unique_ptr<Json::StreamWriter> writer(writerBuilder.newStreamWriter());
    writer->write(root, &out);
    out << "\n";

    if (!out.good()) {
        if (error) {
            *error = "failed to write agent config file: " + path.string();
        }
        return false;
    }

    return true;
}

bool AdminServer::LoadMemoryFromDisk(std::string* error) {
    return m_memoryManager.LoadFromDisk(error);
}

bool AdminServer::SaveMemoryToDisk(std::string* error) const {
    return m_memoryManager.SaveToDisk(error);
}

bool AdminServer::LoadConstitutionFromDisk(std::string* error) {
    return m_constitutionManager.LoadFromDisk(error);
}

bool AdminServer::SaveConstitutionToDisk(std::string* error) const {
    return m_constitutionManager.SaveToDisk(error);
}

bool AdminServer::LoadInterfacesFromDisk(std::string* error) {
    return m_interfaceManager.LoadFromDisk(error);
}

bool AdminServer::SaveInterfacesToDisk(std::string* error) const {
    return m_interfaceManager.SaveToDisk(error);
}

bool AdminServer::LoadProvidersFromDisk(std::string* error) {
    return m_providerManager.LoadFromDisk(error);
}

void AdminServer::RefreshChatSessionServiceDependencies() {
    ChatSessionService::Dependencies deps;
    deps.sessions = m_sessions;
    deps.chainRunner = m_chainRunner;
    deps.providerRegistry = &m_chainRunner->GetProviders();
    deps.compactionService = m_compactionService;
    deps.providerManager = &m_providerManager;
    deps.agentStore = m_agentStore;
    deps.providerThrottle = m_providerThrottle;
    deps.jobs = m_jobs;
    deps.chatMutex = &m_chatMutex;
    deps.agentConfig = &m_agentConfig;
    m_chatSessionService.Configure(deps);
}

bool AdminServer::SaveProvidersToDisk(std::string* error) const {
    return m_providerManager.SaveToDisk(error);
}

bool AdminServer::LoadAuthFromDisk(const std::string& providerId, Json::Value* out, std::string* error) const {
    return m_providerManager.LoadAuthFromDisk(providerId, out, error);
}

bool AdminServer::SaveAuthToDisk(const std::string& providerId, const Json::Value& auth, std::string* error) const {
    return m_providerManager.SaveAuthToDisk(providerId, auth, error);
}

bool AdminServer::TriggerModelClientReinitialization(std::string* error) {

    // When LLMRegistry/clients land, this is where reinit will call into that layer.
    (void)error;
    m_modelReinitCount.fetch_add(1);
    return true;
}

void AdminServer::RunLoop() {
    try {
        RegisterHandlersOnce();
        g_activeAdminServer.store(this);

        auto& app = drogon::app();
        app.setThreadNum(1);
        app.addListener(m_config.host, m_config.port);
        app.setDocumentRoot(".");
        RegisterWebSocketControllers();

        m_running.store(true);
        m_started.store(true);
        app.run();
    } catch (const std::exception& ex) {
        std::lock_guard<std::mutex> lock(m_startMutex);
        m_startError = ex.what();
    } catch (...) {
        std::lock_guard<std::mutex> lock(m_startMutex);
        m_startError = "unknown admin server failure";
    }

    AdminServer* expected = this;
    g_activeAdminServer.compare_exchange_strong(expected, nullptr);
    m_running.store(false);
    m_started.store(true);
}

void AdminServer::RegisterHandlersOnce() {
    std::call_once(g_handlersRegistered, [this] {
        // Register auth middleware before any routes
        drogon::app().registerSyncAdvice([this](const drogon::HttpRequestPtr& req)
            -> drogon::HttpResponsePtr {
            // Only protect /api/ routes
            const auto& path = req->path();
            if (path.rfind("/api/", 0) != 0) return nullptr;

            // Exempt public endpoints
            if (path == "/api/v1/status" ||
                path == "/api/v1/auth/status" ||
                path == "/api/v1/auth/login" ||
                path == "/api/v1/auth/setup" ||
                path == "/api/v1/system/first-run")
                return nullptr;

            // If auth not required, allow
            if (!m_authManager.IsAuthRequired()) return nullptr;

            // Attachment serving: validate short-lived ?token= query param
            // This allows <img>/<audio>/<video> tags to load attachments
            // without a Bearer header (which they can't send).
            if (path.find("/attachments/") != std::string::npos) {
                auto attToken = req->getParameter("token");
                if (!attToken.empty()) {
                    // Extract attachment ID from path: /api/v1/sessions/{id}/attachments/{attId}
                    auto lastSlash = path.find_last_of('/');
                    if (lastSlash != std::string::npos) {
                        std::string attachmentId = path.substr(lastSlash + 1);
                        // Strip any query string (shouldn't be in path, but just in case)
                        auto qMark = attachmentId.find('?');
                        if (qMark != std::string::npos) attachmentId = attachmentId.substr(0, qMark);
                        if (m_attachmentTokens.ValidateToken(attToken, attachmentId)) {
                            return nullptr;  // Allow
                        }
                    }
                }
            }

            // Rate limit
            auto ip = ExtractClientIp(req);
            if (m_authManager.IsRateLimited(ip)) {
                Json::Value body;
                body["error"] = "Too many failed attempts.";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(drogon::k429TooManyRequests);
                resp->addHeader("Retry-After", "60");
                return resp;
            }

            // Validate token
            auto token = ExtractBearerToken(req);
            auto [result, userId] = m_authManager.ValidateToken(token);
            if (result == AuthResult::Ok || result == AuthResult::AuthNotRequired) {
                m_authManager.RecordSuccess(ip);

                // Admin-only writes: user management (all), channels/providers (mutating only)
                // Static token auth (no userId) always passes — treated as admin
                if (!userId.empty()) {
                    auto method = req->method();
                    bool isAdminRoute = false;

                    // User management: admin-only for everything
                    if (path.rfind("/api/v1/users", 0) == 0) {
                        isAdminRoute = true;
                    }
                    // Channels & providers: admin-only for writes, reads OK for any user
                    if ((path.rfind("/api/v1/channels", 0) == 0 ||
                         path.rfind("/api/v1/providers", 0) == 0) &&
                        (method == drogon::Post || method == drogon::Put ||
                         method == drogon::Patch || method == drogon::Delete)) {
                        isAdminRoute = true;
                    }

                    if (isAdminRoute) {
                        auto user = m_authManager.GetUserById(userId);
                        if (!user || user->role != "admin") {
                            Json::Value body;
                            body["error"] = "Forbidden — admin role required";
                            auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                            resp->setStatusCode(drogon::k403Forbidden);
                            return resp;
                        }
                    }
                }

                return nullptr;  // Allow
            }

            // Deny
            m_authManager.RecordFailure(ip);
            Json::Value body;
            body["error"] = "Unauthorized";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
            resp->setStatusCode(drogon::k401Unauthorized);
            return resp;
        });

        RegisterRoutesProvidersAndSpa();
        RegisterRoutesAgentsAndRuntime();
        RegisterRoutesInterfacesSessionsMemory();
        RegisterRoutesDiary();
        RegisterRoutesGallivanting();
        RegisterRoutesLua();
        RegisterRoutesChannels();
        RegisterRoutesNodes();
        RegisterRoutesSearch();
        RegisterRoutesPromptLogs();
        RegisterRoutesAuth();
        RegisterRoutesDiffusion();
    });
}


int AdminServer::GetProviderConcurrency(const std::string& providerId) const {
    return m_providerManager.GetProviderConcurrency(providerId);
}

std::optional<ProviderState> AdminServer::GetProvider(const std::string& providerId) const {
    return m_providerManager.GetProvider(providerId);
}

std::string AdminServer::GetDefaultProviderId() const {
    return m_providerManager.GetDefaultProviderId();
}

std::optional<llm::LLMProviderConfig> AdminServer::GetProviderConfig(const std::string& providerId) const {
    auto config = m_providerManager.GetProviderConfig(providerId);
    if (!config.has_value()) {
        return std::nullopt;
    }

    auto state = m_providerManager.GetProvider(providerId);
    if (!state.has_value()) {
        return config;
    }

    if (state->authType == "oauth" && !state->authFile.empty()) {
        Json::Value authRoot;
        std::string authErr;
        if (LoadAuthFromDisk(providerId, &authRoot, &authErr)
            && authRoot.isObject()
            && authRoot.isMember(providerId)
            && authRoot[providerId].isObject()) {
            const auto& entry = authRoot[providerId];
            if (entry.isMember("access") && entry["access"].isString()) {
                config->api_key = entry["access"].asString();
            }
        }
    }
    return config;
}

// ============================================================================
// Auth configuration
// ============================================================================

void AdminServer::ConfigureAuth(const std::string& staticToken, IDataStore* dataStore) {
    m_staticTokenRaw = staticToken;
    if (dataStore) {
        m_authStore = new AuthStore(dataStore);
        m_authManager.SetAuthStore(m_authStore);
    }
    if (!staticToken.empty()) {
        m_authManager.SetStaticToken(staticToken);
    } else if (m_authStore && m_authStore->HasUsers()) {
        m_authManager.SetRequireAuth(true);
    }
}

} // namespace animus::kernel

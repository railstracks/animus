Chunk ID: 2127f7
Wall time: 0.0000 seconds
Process exited with code 0
Original token count: 1108
Output:
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <json/value.h>

#include "animus/Jobs.h"
#include "animus_kernel/KernelConfig.h"

namespace animus::kernel {
class SessionManager;
class AdminChatWebSocketController;
class AdminObservationWebSocketController;

class AdminServer {
public:
    AdminServer();
    ~AdminServer();

    AdminServer(const AdminServer&) = delete;
    AdminServer& operator=(const AdminServer&) = delete;

    bool Start(
        const KernelConfig::AdminServerConfig& config,
        const KernelConfig::AgentRuntimeConfig& initialAgentConfig,
        const KernelConfig::AgentConfigStorage& agentStorage,
        const KernelConfig::InterfaceConfigStorage& interfaceStorage,
        const std::vector<KernelConfig::InterfaceModuleInfo>& interfaceModules,
        const KernelConfig::MemoryConfig& memoryConfig,
        const KernelConfig::ConstitutionConfigStorage& constitutionStorage,
        SessionManager* sessions,
        jobs::JobSystem* jobs,
        std::chrono::steady_clock::time_point kernelStartedAt,
        std::string* error);

    void Stop();

    bool IsRunning() const;
    std::uint64_t UptimeSeconds() const;

private:
    friend class AdminChatWebSocketController;
    friend class AdminObservationWebSocketController;

    struct ConsolidationSummary {
        std::uint64_t promoted{0};
        std::uint64_t demoted{0};
        std::uint64_t archived{0};
    };

    struct ConsolidationState {
        bool running{false};
        std::uint64_t lastRunUnixMs{0};
        std::uint64_t activeJobId{0};
        std::uint64_t lastJobId{0};
        ConsolidationSummary lastSummary{};
        std::string lastError{};
    };

    void RunLoop();
    void RegisterHandlersOnce();
    bool LoadAgentConfigFromDisk(std::string* error);
    bool SaveAgentConfigToDisk(
        const KernelConfig::AgentRuntimeConfig& config,
        std::string* error) const;
    bool TriggerModelClientReinitialization(std::string* error);
    bool LoadMemoryFromDisk(std::string* error);
    bool SaveMemoryToDisk(std::string* error) const;
    bool LoadInterfacesFromDisk(std::string* error);
    bool SaveInterfacesToDisk(std::string* error) const;
    bool LoadConstitutionFromDisk(std::string* error);
    bool SaveConstitutionToDisk(std::string* error) const;

    KernelConfig::AdminServerConfig m_config{};
    KernelConfig::AgentRuntimeConfig m_agentConfig{};
    KernelConfig::AgentConfigStorage m_agentStorage{};
    KernelConfig::InterfaceConfigStorage m_interfaceStorage{};
    KernelConfig::MemoryConfig m_memoryConfig{};
    KernelConfig::ConstitutionConfigStorage m_constitutionStorage{};
    SessionManager* m_sessions{nullptr};
    jobs::JobSystem* m_jobs{nullptr};
    std::chrono::steady_clock::time_point m_kernelStartedAt{};
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_started{false};
    std::atomic<std::uint64_t> m_modelReinitCount{0};
    std::atomic<std::uint64_t> m_nextMemoryConsolidationJobId{1};
    std::string m_startError;
    std::mutex m_startMutex;
    mutable std::mutex m_agentMutex;
    mutable std::mutex m_memoryMutex;
    mutable std::mutex m_chatMutex;
    mutable std::mutex m_interfaceMutex;
    mutable std::mutex m_constitutionMutex;
    struct InterfaceState {
        std::string name;
        std::string type;
        std::string moduleId;
        bool enabled{true};
        bool connected{true};
        std::uint64_t lastEventUnixMs{0};
        Json::Value config{Json::objectValue};
    };
    std::unordered_map<std::string, InterfaceState> m_interfacesByName;
    std::vector<KernelConfig::InterfaceModuleInfo> m_interfaceModules;
    std::vector<KernelConfig::MemoryLayerConfig> m_memoryLayers;
    std::unordered_map<std::string, KernelConfig::MemoryObservation> m_observationsById;
    ConsolidationState m_consolidationState;
    Json::Value m_constitutionCore{Json::objectValue};
    Json::Value m_constitutionContracts{Json::arrayValue};
    Json::Value m_constitutionAdaptation{Json::objectValue};
    Json::Value m_constitutionAuditLog{Json::arrayValue};
    std::atomic<std::uint64_t> m_nextWsConnectionId{1};
    std::atomic<std::uint64_t> m_nextWsConversationId{1};
    std::atomic<std::uint64_t> m_nextObservationId{1};
};

} // namespace animus::kernel

        const KernelConfig::AgentRuntimeConfig& initialAgentConfig,
        const KernelConfig::AgentConfigStorage& agentStorage,
        const KernelConfig::MemoryConfig& memoryConfig,
        SessionManager* sessions,
        jobs::JobSystem* jobs,
        std::chrono::steady_clock::time_point kernelStartedAt,
        std::string* error);

    void Stop();

    bool IsRunning() const;
    std::uint64_t UptimeSeconds() const;

private:
    struct ConsolidationSummary {
        std::uint64_t promoted{0};
        std::uint64_t demoted{0};
        std::uint64_t archived{0};
    };

    struct ConsolidationState {
        bool running{false};
        std::uint64_t lastRunUnixMs{0};
        std::uint64_t activeJobId{0};
        std::uint64_t lastJobId{0};
        ConsolidationSummary lastSummary{};
        std::string lastError{};
    };

    void RunLoop();
    void RegisterHandlersOnce();
    bool LoadAgentConfigFromDisk(std::string* error);
    bool SaveAgentConfigToDisk(
        const KernelConfig::AgentRuntimeConfig& config,
        std::string* error) const;
    bool TriggerModelClientReinitialization(std::string* error);
    bool LoadMemoryFromDisk(std::string* error);
    bool SaveMemoryToDisk(std::string* error) const;

    KernelConfig::AdminServerConfig m_config{};
    KernelConfig::AgentRuntimeConfig m_agentConfig{};
    KernelConfig::AgentConfigStorage m_agentStorage{};
    KernelConfig::MemoryConfig m_memoryConfig{};
    SessionManager* m_sessions{nullptr};
    jobs::JobSystem* m_jobs{nullptr};
    std::chrono::steady_clock::time_point m_kernelStartedAt{};
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_started{false};
    std::atomic<std::uint64_t> m_modelReinitCount{0};
    std::atomic<std::uint64_t> m_nextMemoryConsolidationJobId{1};
    std::string m_startError;
    std::mutex m_startMutex;
    mutable std::mutex m_agentMutex;
    mutable std::mutex m_memoryMutex;
    std::vector<KernelConfig::MemoryLayerConfig> m_memoryLayers;
    std::unordered_map<std::string, KernelConfig::MemoryObservation> m_observationsById;
    ConsolidationState m_consolidationState;
};

} // namespace animus::kernel
#include "animus_kernel/AgentKernel.h"

#include <iostream>
#include <memory>

#include "kernel/module/ModuleManager.h"

#include "animus_kernel/AdminServer.h"
#include "animus_kernel/DefaultSessionRouter.h"
#include "animus_kernel/InMemorySessionStore.h"
#include "animus_kernel/SessionManager.h"

namespace animus::kernel {

AgentKernel::AgentKernel() : m_moduleManager(nullptr), m_adminServer(nullptr), m_sessionManager(nullptr) {
    m_moduleManager = new module::ModuleManager();
    m_adminServer = new AdminServer();

    // Default session layer: in-memory store + metadata-derived routing.
    m_sessionManager = new SessionManager(
        std::make_unique<InMemorySessionStore>(),
        std::make_unique<DefaultSessionRouter>());
}

AgentKernel::~AgentKernel() {
    Stop();

    delete m_moduleManager;
    m_moduleManager = nullptr;

    delete m_adminServer;
    m_adminServer = nullptr;

    delete m_sessionManager;
    m_sessionManager = nullptr;
}

bool AgentKernel::Start(const KernelConfig& config, std::string* error) {
    if (m_running) {
        return true;
    }

    m_config = config;
    m_startedAt = std::chrono::steady_clock::now();

    if (!m_jobs.Start(m_config.jobSystemConfig)) {
        if (error) {
            *error = "failed to start job system";
        }
        return false;
    }

    // Minimal host vtable (kernel logs to stderr for now).
    animus_host_vtable_t host{};
    host.struct_size = sizeof(animus_host_vtable_t);
    host.log = [](void* /*ctx*/, animus_log_level_t level, animus_string_view msg) {
        const char* lvl = "INFO";
        switch (level) {
            case ANIMUS_LOG_DEBUG: lvl = "DEBUG"; break;
            case ANIMUS_LOG_INFO: lvl = "INFO"; break;
            case ANIMUS_LOG_WARN: lvl = "WARN"; break;
            case ANIMUS_LOG_ERROR: lvl = "ERROR"; break;
            default: break;
        }
        std::cerr << "[module:" << lvl << "] "
                  << std::string(msg.data ? msg.data : "", msg.size)
                  << "\n";
    };

    std::string modErr;
    if (!m_moduleManager->LoadFromConfig(m_config, &host, nullptr, &modErr)) {
        // If module loading fails, stop jobs too (clean failure).
        m_jobs.Stop(jobs::JobSystemStopMode::Drain);
        if (error) {
            *error = modErr;
        }
        return false;
    }

    std::string adminErr;
    if (m_adminServer && !m_adminServer->Start(
            m_config.admin,
            m_config.agent,
            m_config.agentStorage,
            m_config.memory,
            m_sessionManager,
            &m_jobs,
            m_startedAt,
            &adminErr)) {
        if (m_moduleManager) {
            m_moduleManager->UnloadAll();
        }
        m_jobs.Stop(jobs::JobSystemStopMode::Drain);
        if (error) {
            *error = adminErr;
        }
        return false;
    }

    m_running = true;
    return true;
}

void AgentKernel::Stop() {
    if (!m_running) {
        return;
    }

    if (m_adminServer) {
        m_adminServer->Stop();
    }

    if (m_moduleManager) {
        m_moduleManager->UnloadAll();
    }

    m_jobs.Stop(jobs::JobSystemStopMode::Drain);
    m_running = false;
}

bool AgentKernel::IsRunning() const {
    return m_running;
}

jobs::JobSystem& AgentKernel::Jobs() {
    return m_jobs;
}

SessionManager& AgentKernel::Sessions() {
    return *m_sessionManager;
}

bool AgentKernel::IsAdminServerRunning() const {
    if (!m_adminServer) {
        return false;
    }
    return m_adminServer->IsRunning();
}

} // namespace animus::kernel
#include "animus_kernel/AdminServer.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <drogon/drogon.h>

#include "animus_kernel/SessionManager.h"

namespace animus::kernel {
namespace {

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

bool ParseSessionId(const std::string& text, SessionId* out) {
    if (!out || text.empty()) {
        return false;
    }
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
    out["message_count"] = static_cast<Json::UInt64>(session.Turns().size());
    out["created_unix_ms"] = static_cast<Json::UInt64>(session.CreatedUnixMs());
    out["last_active_unix_ms"] = static_cast<Json::UInt64>(session.LastActiveUnixMs());
    out["status"] = session.IsTerminated() ? "terminated" : "active";
    return out;
}

Json::Value BuildSessionTurnJson(const SessionTurn& turn) {
    Json::Value out(Json::objectValue);
    out["turn_id"] = static_cast<Json::UInt64>(turn.turn_id);
    out["role"] = turn.role;
    out["content"] = turn.content;
    out["unix_ms"] = static_cast<Json::UInt64>(turn.unix_ms);
    out["is_summary"] = turn.is_summary;
    Json::Value compactedFrom(Json::arrayValue);
    for (const SessionTurnId sourceTurnId : turn.compacted_from) {
        compactedFrom.append(static_cast<Json::UInt64>(sourceTurnId));
    }
    out["compacted_from"] = compactedFrom;
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

double ObservationAgeSeconds(const KernelConfig::MemoryObservation& observation) {
    const std::uint64_t now = NowUnixMs();
    if (now <= observation.timestampUnixMs) {
        return 0.0;
    }
    return static_cast<double>(now - observation.timestampUnixMs) / 1000.0;
}

Json::Value BuildObservationJson(const KernelConfig::MemoryObservation& observation) {
    Json::Value out(Json::objectValue);
    out["id"] = observation.id;
    out["text"] = observation.text;
    Json::Value tags(Json::arrayValue);
    for (const auto& tag : observation.tags) {
        tags.append(tag);
    }
    out["tags"] = tags;
    out["timestamp"] = static_cast<Json::UInt64>(observation.timestampUnixMs);
    out["weight"] = observation.weight;
    out["decay_rate"] = observation.decayRate;
    out["layer"] = observation.layer;
    out["created_in_layer"] = observation.createdInLayer;
    if (observation.demotionReason.empty()) {
        out["demotion_reason"] = Json::nullValue;
    } else {
        out["demotion_reason"] = observation.demotionReason;
    }
    if (observation.demotionTimestampUnixMs == 0) {
        out["demotion_timestamp"] = Json::nullValue;
    } else {
        out["demotion_timestamp"] = static_cast<Json::UInt64>(observation.demotionTimestampUnixMs);
    }
    out["age_seconds"] = ObservationAgeSeconds(observation);
    return out;
}

std::string DefaultFirstLayerName(const std::vector<KernelConfig::MemoryLayerConfig>& layers) {
    if (layers.empty()) {
        return "day";
    }
    return layers.front().name;
}

bool ParseObservationPatch(
    const Json::Value& patch,
    KernelConfig::MemoryObservation* observation,
    std::string* error) {
    if (!patch.isObject()) {
        if (error) {
            *error = "request body must be a JSON object";
        }
        return false;
    }

    if (patch.isMember("tags")) {
        const Json::Value& tags = patch["tags"];
        if (!tags.isArray()) {
            if (error) {
                *error = "tags must be an array of strings";
            }
            return false;
        }
        std::vector<std::string> parsedTags;
        for (Json::ArrayIndex i = 0; i < tags.size(); ++i) {
            if (!tags[i].isString()) {
                if (error) {
                    *error = "tags must be an array of strings";
                }
                return false;
            }
            parsedTags.push_back(tags[i].asString());
        }
        observation->tags = std::move(parsedTags);
    }

    if (patch.isMember("weight")) {
        const Json::Value& weight = patch["weight"];
        if (!(weight.isDouble() || weight.isInt() || weight.isUInt())) {
            if (error) {
                *error = "weight must be a number";
            }
            return false;
        }
        const double parsedWeight = weight.asDouble();
        if (parsedWeight <= 0.0 || parsedWeight > 1000.0) {
            if (error) {
                *error = "weight must be between 0 and 1000";
            }
            return false;
        }
        observation->weight = parsedWeight;
    }

    return true;
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
    out["system_prompt"] = config.systemPrompt;

    Json::Value budget(Json::objectValue);
    budget["max_chain_steps"] = config.budget.maxChainSteps;
    budget["max_tool_calls_per_chain"] = config.budget.maxToolCallsPerChain;
    budget["timeout_seconds"] = config.budget.timeoutSeconds;
    budget["token_budget_per_prompt"] = config.budget.tokenBudgetPerPrompt;
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

bool ValidateAgentConfig(const KernelConfig::AgentRuntimeConfig& config, std::string* error) {
    if (!IsValidModelToken(config.model.provider)) {
        if (error) {
            *error = "model.provider contains invalid characters";
        }
        return false;
    }

    if (!IsValidModelToken(config.model.modelId)) {
        if (error) {
            *error = "model.model_id contains invalid characters";
        }
        return false;
    }

    if (config.model.contextWindow == 0U || config.model.contextWindow > 4000000U) {
        if (error) {
            *error = "model.context_window must be between 1 and 4000000";
        }
        return false;
    }

    if (config.temperature < 0.0 || config.temperature > 2.0) {
        if (error) {
            *error = "temperature must be between 0.0 and 2.0";
        }
        return false;
    }

    if (config.systemPrompt.size() > 65535U) {
        if (error) {
            *error = "system_prompt exceeds maximum length";
        }
        return false;
    }

    if (config.budget.maxChainSteps == 0U
        || config.budget.maxToolCallsPerChain == 0U
        || config.budget.timeoutSeconds == 0U
        || config.budget.tokenBudgetPerPrompt == 0U) {
        if (error) {
            *error = "budget values must be greater than 0";
        }
        return false;
    }

    return true;
}

bool ParseUInt32Field(
    const Json::Value& object,
    const char* key,
    std::uint32_t* out,
    std::string* error) {
    if (!object.isMember(key)) {
        return true;
    }
    const Json::Value& v = object[key];
    if (!v.isUInt()) {
        if (error) {
            *error = std::string(key) + " must be an unsigned integer";
        }
        return false;
    }
    *out = v.asUInt();
    return true;
}

bool ParseDoubleField(
    const Json::Value& object,
    const char* key,
    double* out,
    std::string* error) {
    if (!object.isMember(key)) {
        return true;
    }
    const Json::Value& v = object[key];
    if (!(v.isDouble() || v.isInt() || v.isUInt())) {
        if (error) {
            *error = std::string(key) + " must be a number";
        }
        return false;
    }
    *out = v.asDouble();
    return true;
}

bool ParseStringField(
    const Json::Value& object,
    const char* key,
    std::string* out,
    std::string* error) {
    if (!object.isMember(key)) {
        return true;
    }
    const Json::Value& v = object[key];
    if (!v.isString()) {
        if (error) {
            *error = std::string(key) + " must be a string";
        }
        return false;
    }
    *out = v.asString();
    return true;
}

bool ApplyAgentPatch(
    const Json::Value& patch,
    KernelConfig::AgentRuntimeConfig* config,
    bool* modelChanged,
    std::string* error) {
    if (!patch.isObject()) {
        if (error) {
            *error = "request body must be a JSON object";
        }
        return false;
    }

    if (!ParseDoubleField(patch, "temperature", &config->temperature, error)) {
        return false;
    }

    if (!ParseStringField(patch, "system_prompt", &config->systemPrompt, error)) {
        return false;
    }

    if (patch.isMember("budget")) {
        const Json::Value& budget = patch["budget"];
        if (!budget.isObject()) {
            if (error) {
                *error = "budget must be an object";
            }
            return false;
        }
        if (!ParseUInt32Field(budget, "max_chain_steps", &config->budget.maxChainSteps, error)) {
            return false;
        }
        if (!ParseUInt32Field(
                budget,
                "max_tool_calls_per_chain",
                &config->budget.maxToolCallsPerChain,
                error)) {
            return false;
        }
        if (!ParseUInt32Field(budget, "timeout_seconds", &config->budget.timeoutSeconds, error)) {
            return false;
        }


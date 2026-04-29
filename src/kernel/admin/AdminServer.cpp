#include "animus_kernel/AdminServer.h"

#include <chrono>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include <drogon/drogon.h>

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
        if (!ParseUInt32Field(
                budget,
                "token_budget_per_prompt",
                &config->budget.tokenBudgetPerPrompt,
                error)) {
            return false;
        }
    }

    if (patch.isMember("model")) {
        const Json::Value& model = patch["model"];
        if (!model.isObject()) {
            if (error) {
                *error = "model must be an object";
            }
            return false;
        }

        std::string provider = config->model.provider;
        std::string modelId = config->model.modelId;
        std::uint32_t contextWindow = config->model.contextWindow;
        std::string apiKey = config->model.apiKey;

        if (!ParseStringField(model, "provider", &provider, error)) {
            return false;
        }
        if (!ParseStringField(model, "model_id", &modelId, error)) {
            return false;
        }
        if (!ParseUInt32Field(model, "context_window", &contextWindow, error)) {
            return false;
        }
        if (!ParseStringField(model, "api_key", &apiKey, error)) {
            return false;
        }

        const bool changed = (provider != config->model.provider)
            || (modelId != config->model.modelId)
            || (contextWindow != config->model.contextWindow)
            || (apiKey != config->model.apiKey);

        config->model.provider = std::move(provider);
        config->model.modelId = std::move(modelId);
        config->model.contextWindow = contextWindow;
        config->model.apiKey = std::move(apiKey);

        if (modelChanged && changed) {
            *modelChanged = true;
        }
    }

    if (!ValidateAgentConfig(*config, error)) {
        return false;
    }

    return true;
}

} // namespace

AdminServer::AdminServer() = default;

AdminServer::~AdminServer() {
    Stop();
}

bool AdminServer::Start(
    const KernelConfig::AdminServerConfig& config,
    const KernelConfig::AgentRuntimeConfig& initialAgentConfig,
    const KernelConfig::AgentConfigStorage& agentStorage,
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
    m_kernelStartedAt = kernelStartedAt;
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
    if (!ApplyAgentPatch(root, &loaded, &changed, &applyErr)) {
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
    root["system_prompt"] = config.systemPrompt;
    root["budget"]["max_chain_steps"] = config.budget.maxChainSteps;
    root["budget"]["max_tool_calls_per_chain"] = config.budget.maxToolCallsPerChain;
    root["budget"]["timeout_seconds"] = config.budget.timeoutSeconds;
    root["budget"]["token_budget_per_prompt"] = config.budget.tokenBudgetPerPrompt;

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

bool AdminServer::TriggerModelClientReinitialization(std::string* error) {
    // Stub for Milestone 7 integration: this marks a model-runtime refresh event.
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

        m_running.store(true);
        m_started.store(true);
        app.run();
    } catch (const std::exception& ex) {
        {
            std::lock_guard<std::mutex> lock(m_startMutex);
            m_startError = ex.what();
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(m_startMutex);
            m_startError = "unknown admin server failure";
        }
    }

    AdminServer* expected = this;
    g_activeAdminServer.compare_exchange_strong(expected, nullptr);
    m_running.store(false);
    m_started.store(true);
}

void AdminServer::RegisterHandlersOnce() {
    std::call_once(g_handlersRegistered, [] {
        drogon::app().registerHandler(
            "/api/v1/status",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                Json::Value body;

                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    body["status"] = "error";
                    body["error"] = "admin server not active";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                body["status"] = "ok";
                body["uptime"] = static_cast<Json::UInt64>(server->UptimeSeconds());
                callback(drogon::HttpResponse::newHttpJsonResponse(body));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/agent",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    Json::Value out;
                    out["error"] = "admin server not active";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                KernelConfig::AgentRuntimeConfig snapshot;
                {
                    std::lock_guard<std::mutex> lock(server->m_agentMutex);
                    snapshot = server->m_agentConfig;
                }

                const Json::Value out = BuildAgentJson(snapshot, server->m_modelReinitCount.load());
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/agent",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    Json::Value out;
                    out["error"] = "admin server not active";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                const auto body = req->getJsonObject();
                if (!body || !body->isObject()) {
                    Json::Value out;
                    AddBadRequestError("request body must be a JSON object", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                KernelConfig::AgentRuntimeConfig before;
                KernelConfig::AgentRuntimeConfig proposed;
                bool modelChanged = false;
                {
                    std::lock_guard<std::mutex> lock(server->m_agentMutex);
                    before = server->m_agentConfig;
                    proposed = before;
                }

                std::string patchErr;
                if (!ApplyAgentPatch(*body, &proposed, &modelChanged, &patchErr)) {
                    Json::Value out;
                    AddBadRequestError(patchErr, &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (modelChanged) {
                    std::string reinitErr;
                    if (!server->TriggerModelClientReinitialization(&reinitErr)) {
                        Json::Value out;
                        out["error"] = reinitErr.empty()
                            ? "failed to reinitialize model client"
                            : reinitErr;
                        auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                        resp->setStatusCode(drogon::k500InternalServerError);
                        callback(resp);
                        return;
                    }
                }

                std::string persistErr;
                if (!server->SaveAgentConfigToDisk(proposed, &persistErr)) {
                    Json::Value out;
                    out["error"] = persistErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(server->m_agentMutex);
                    server->m_agentConfig = proposed;
                }

                const Json::Value out = BuildAgentJson(proposed, server->m_modelReinitCount.load());
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Patch});

        drogon::app().registerHandler(
            "/api/v1/agent/model",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    Json::Value out;
                    out["error"] = "admin server not active";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                KernelConfig::AgentRuntimeConfig snapshot;
                {
                    std::lock_guard<std::mutex> lock(server->m_agentMutex);
                    snapshot = server->m_agentConfig;
                }

                const Json::Value out = BuildModelJson(snapshot, server->m_modelReinitCount.load());
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/api/v1/agent/model",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                AdminServer* server = g_activeAdminServer.load();
                if (!server) {
                    Json::Value out;
                    out["error"] = "admin server not active";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                    return;
                }

                const auto body = req->getJsonObject();
                if (!body || !body->isObject()) {
                    Json::Value out;
                    AddBadRequestError("request body must be a JSON object", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (!body->isMember("provider") || !body->isMember("model_id")) {
                    Json::Value out;
                    AddBadRequestError("provider and model_id are required", &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                KernelConfig::AgentRuntimeConfig before;
                KernelConfig::AgentRuntimeConfig proposed;
                {
                    std::lock_guard<std::mutex> lock(server->m_agentMutex);
                    before = server->m_agentConfig;
                    proposed = before;
                }

                Json::Value modelPatch(Json::objectValue);
                modelPatch["model"] = *body;

                bool modelChanged = false;
                std::string patchErr;
                if (!ApplyAgentPatch(modelPatch, &proposed, &modelChanged, &patchErr)) {
                    Json::Value out;
                    AddBadRequestError(patchErr, &out);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                std::string reinitErr;
                if (!server->TriggerModelClientReinitialization(&reinitErr)) {
                    Json::Value out;
                    out["error"] = reinitErr.empty()
                        ? "failed to reinitialize model client"
                        : reinitErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                std::string persistErr;
                if (!server->SaveAgentConfigToDisk(proposed, &persistErr)) {
                    Json::Value out;
                    out["error"] = persistErr;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(server->m_agentMutex);
                    server->m_agentConfig = proposed;
                }

                const Json::Value out = BuildModelJson(proposed, server->m_modelReinitCount.load());
                callback(drogon::HttpResponse::newHttpJsonResponse(out));
            },
            {drogon::Put});

        drogon::app().registerHandler(
            "/",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k200OK);
                resp->setContentTypeCode(drogon::CT_TEXT_HTML);
                resp->setBody(
                    "<!doctype html><html><head><meta charset=\"utf-8\"><title>Animus Admin</title>"
                    "</head><body><h1>Animus Admin Server</h1><p>Admin UI placeholder.</p></body></html>");
                callback(resp);
            },
            {drogon::Get});

        drogon::app().registerHandler(
            "/static/admin.css",
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k200OK);
                resp->setContentTypeCode(drogon::CT_TEXT_CSS);
                resp->setBody("body { font-family: sans-serif; margin: 2rem; }");
                callback(resp);
            },
            {drogon::Get});
    });
}

} // namespace animus::kernel
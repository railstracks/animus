#include "animus_kernel/admin/ProviderManager.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

#include "animus_kernel/llm/LLMProviderBase.h"
#include "animus_kernel/llm/LLMProviderRegistry.h"

namespace animus::kernel {
namespace {

std::string MaskSecret(const std::string& value) {
    if (value.empty()) return {};
    if (value.size() <= 8) {
        return std::string(value.size(), '*');
    }
    return value.substr(0, 4) + "..." + value.substr(value.size() - 4);
}

} // namespace

void ProviderManager::Configure(const KernelConfig::ProviderConfigStorage& storage) {
    std::lock_guard<std::mutex> lock(m_providerMutex);
    m_providerStorage = storage;
}

bool ProviderManager::LoadFromDisk(std::string* error) {
    std::lock_guard<std::mutex> lock(m_providerMutex);
    m_providersByName.clear();
    m_defaultProvider.clear();

    if (!m_providerStorage.persistToDisk) {
        return true;
    }
    if (m_providerStorage.providersFilePath.empty()) {
        if (error) *error = "providers file path must not be empty when persistence is enabled";
        return false;
    }

    const std::filesystem::path path(m_providerStorage.providersFilePath);
    if (!std::filesystem::exists(path)) {
        return true;
    }

    std::ifstream in(path);
    if (!in.is_open()) {
        if (error) *error = "failed to open providers file for reading: " + path.string();
        return false;
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string parseErrors;
    if (!Json::parseFromStream(builder, in, &root, &parseErrors)) {
        if (error) *error = "failed to parse providers file: " + parseErrors;
        return false;
    }

    m_defaultProvider = root.get("default_provider", "").asString();

    if (root.isMember("providers") && root["providers"].isObject()) {
        const auto& providers = root["providers"];
        for (const auto& id : providers.getMemberNames()) {
            const auto& entry = providers[id];
            if (!entry.isObject()) continue;

            ProviderState state;
            state.providerId = id;
            state.providerType = entry.get("provider_type", id).asString();
            state.baseUrl = entry.get("base_url", "").asString();
            state.apiKey = entry.get("api_key", "").asString();
            state.defaultModel = entry.get("default_model", "").asString();
            state.defaultContextWindow = entry.get("default_context_window", 128000).asUInt();
            state.authType = entry.get("auth_type", "api_key").asString();
            state.authFile = entry.get("auth_file", "").asString();
            state.concurrency = entry.get("concurrency", 1).asInt();
            state.status = "untested";
            if (state.defaultContextWindow == 0U) {
                state.defaultContextWindow = 128000U;
            }

            if (state.apiKey.empty() && state.authType == "api_key") {
                if (id == "ollama") {
                    state.authType = "none";
                }
            }

            if (entry.isMember("extra") && entry["extra"].isObject()) {
                const auto& extra = entry["extra"];
                for (const auto& key : extra.getMemberNames()) {
                    if (extra[key].isString()) {
                        state.extra[key] = extra[key].asString();
                    }
                }
            }

            if (entry.isMember("model_context_windows") && entry["model_context_windows"].isObject()) {
                const auto& windows = entry["model_context_windows"];
                for (const auto& modelId : windows.getMemberNames()) {
                    const Json::Value& value = windows[modelId];
                    if ((value.isUInt() || value.isInt()) && value.asUInt() > 0U) {
                        state.modelContextWindows[modelId] = value.asUInt();
                    }
                }
            }

            m_providersByName[id] = std::move(state);
        }
    }

    return true;
}

bool ProviderManager::SaveToDisk(std::string* error) const {
    std::lock_guard<std::mutex> lock(m_providerMutex);
    if (!m_providerStorage.persistToDisk) {
        return true;
    }
    if (m_providerStorage.providersFilePath.empty()) {
        if (error) *error = "providers file path must not be empty when persistence is enabled";
        return false;
    }

    const std::filesystem::path path(m_providerStorage.providersFilePath);
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            if (error) *error = "failed to create providers config directory: " + parent.string();
            return false;
        }
    }

    Json::Value root(Json::objectValue);
    root["default_provider"] = m_defaultProvider;

    Json::Value providers(Json::objectValue);
    for (const auto& [id, state] : m_providersByName) {
        Json::Value entry(Json::objectValue);
        entry["provider_type"] = state.providerType;
        entry["base_url"] = state.baseUrl;
        if (state.authType == "api_key" && !state.apiKey.empty()) {
            entry["api_key"] = state.apiKey;
        }
        entry["default_model"] = state.defaultModel;
        entry["default_context_window"] = static_cast<Json::UInt>(state.defaultContextWindow);
        entry["auth_type"] = state.authType;
        entry["concurrency"] = state.concurrency;
        if (!state.authFile.empty()) {
            entry["auth_file"] = state.authFile;
        }
        if (!state.extra.empty()) {
            Json::Value extra(Json::objectValue);
            for (const auto& [k, v] : state.extra) {
                extra[k] = v;
            }
            entry["extra"] = extra;
        }
        if (!state.modelContextWindows.empty()) {
            Json::Value modelContextWindows(Json::objectValue);
            for (const auto& [modelId, contextWindow] : state.modelContextWindows) {
                modelContextWindows[modelId] = static_cast<Json::UInt>(contextWindow);
            }
            entry["model_context_windows"] = modelContextWindows;
        }
        providers[id] = entry;
    }
    root["providers"] = providers;

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "  ";

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        if (error) *error = "failed to open providers file for writing: " + path.string();
        return false;
    }

    std::unique_ptr<Json::StreamWriter> writer(writerBuilder.newStreamWriter());
    writer->write(root, &out);
    out << "\n";
    if (!out.good()) {
        if (error) *error = "failed to write providers file: " + path.string();
        return false;
    }
    return true;
}

std::vector<ProviderState> ProviderManager::ListProviders() const {
    std::vector<ProviderState> providers;
    std::lock_guard<std::mutex> lock(m_providerMutex);
    providers.reserve(m_providersByName.size());
    for (const auto& pair : m_providersByName) {
        providers.push_back(pair.second);
    }
    return providers;
}

std::optional<ProviderState> ProviderManager::GetProvider(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_providerMutex);
    auto it = FindProviderLocked(id);
    if (it == m_providersByName.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool ProviderManager::HasProvider(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_providerMutex);
    return FindProviderLocked(id) != m_providersByName.end();
}

std::string ProviderManager::GetDefaultProviderId() const {
    std::lock_guard<std::mutex> lock(m_providerMutex);
    return m_defaultProvider;
}

bool ProviderManager::SetDefaultProviderId(const std::string& id, std::string* error) {
    std::lock_guard<std::mutex> lock(m_providerMutex);
    if (id.empty()) {
        m_defaultProvider.clear();
        return true;
    }
    auto it = FindProviderLocked(id);
    if (it == m_providersByName.end()) {
        if (error) *error = "provider not found: " + id;
        return false;
    }
    m_defaultProvider = it->first;
    return true;
}

bool ProviderManager::CreateProvider(const ProviderState& state, std::string* error) {
    std::lock_guard<std::mutex> lock(m_providerMutex);
    if (FindProviderLocked(state.providerId) != m_providersByName.end()) {
        if (error) *error = "provider already exists: " + state.providerId;
        return false;
    }
    m_providersByName[state.providerId] = state;
    if (m_defaultProvider.empty()) {
        m_defaultProvider = state.providerId;
    }
    return true;
}

bool ProviderManager::UpdateProvider(const std::string& id, const ProviderState& state, std::string* error) {
    std::lock_guard<std::mutex> lock(m_providerMutex);
    auto it = FindProviderLocked(id);
    if (it == m_providersByName.end()) {
        if (error) *error = "provider not found: " + id;
        return false;
    }
    it->second = state;
    return true;
}

bool ProviderManager::DeleteProvider(const std::string& id, ProviderState* removed, std::string* error) {
    std::lock_guard<std::mutex> lock(m_providerMutex);
    auto it = FindProviderLocked(id);
    if (it == m_providersByName.end()) {
        if (error) *error = "provider not found: " + id;
        return false;
    }
    if (removed) {
        *removed = it->second;
    }
    m_providersByName.erase(it);
    if (m_defaultProvider == id) {
        m_defaultProvider = m_providersByName.empty() ? "" : m_providersByName.begin()->first;
    }
    return true;
}

bool ProviderManager::UpdateProviderStatus(
    const std::string& id,
    const std::string& status,
    const std::string& lastError,
    std::uint64_t lastTestedUnixMs,
    ProviderState* updated,
    std::string* error) {
    std::lock_guard<std::mutex> lock(m_providerMutex);
    auto it = FindProviderLocked(id);
    if (it == m_providersByName.end()) {
        if (error) *error = "provider not found: " + id;
        return false;
    }
    it->second.status = status;
    it->second.lastError = lastError;
    it->second.lastTestedUnixMs = lastTestedUnixMs;
    if (updated) {
        *updated = it->second;
    }
    return true;
}

bool ProviderManager::UpdateProviderCapabilities(
    const std::string& id,
    const llm::ProviderCapabilities& capabilities,
    ProviderState* updated,
    std::string* error) {
    std::lock_guard<std::mutex> lock(m_providerMutex);
    auto it = FindProviderLocked(id);
    if (it == m_providersByName.end()) {
        if (error) *error = "provider not found: " + id;
        return false;
    }
    it->second.capabilities = capabilities;
    if (updated) {
        *updated = it->second;
    }
    return true;
}

int ProviderManager::GetProviderConcurrency(const std::string& providerId) const {
    std::lock_guard<std::mutex> lock(m_providerMutex);
    auto it = FindProviderLocked(providerId);
    if (it != m_providersByName.end()) return it->second.concurrency;
    return 1;
}

std::optional<llm::LLMProviderConfig> ProviderManager::GetProviderConfig(const std::string& providerId) const {
    std::lock_guard<std::mutex> lock(m_providerMutex);
    auto it = FindProviderLocked(providerId);
    if (it == m_providersByName.end()) {
        std::cerr << "[config] provider not found: '" << providerId << "' available: ";
        for (const auto& [k, _] : m_providersByName) std::cerr << "'" << k << "' ";
        std::cerr << std::endl;
        return std::nullopt;
    }
    const auto& state = it->second;

    llm::LLMProviderConfig config;
    config.provider_id = state.providerType.empty() ? state.providerId : state.providerType;
    config.base_url = state.baseUrl;
    config.api_key = state.apiKey;
    config.default_model = state.defaultModel;
    config.extra = state.extra;
    if (state.authType == "oauth" && !state.authFile.empty()) {
        config.extra["auth_file"] = state.authFile;
    }
    return config;
}

bool ProviderManager::ResolveProviderAuthFilePathLocked(
    const std::string& providerId,
    std::string* authFile,
    std::string* error) const {
    if (!authFile) {
        if (error) *error = "auth file output is null";
        return false;
    }
    auto it = FindProviderLocked(providerId);
    if (it == m_providersByName.end()) {
        if (error) *error = "provider not found: " + providerId;
        return false;
    }
    *authFile = it->second.authFile;
    if (authFile->empty()) {
        *authFile = m_providerStorage.authFilePath;
    }
    return true;
}

bool ProviderManager::BuildRuntimeProviderConfigLocked(
    const std::string& providerId,
    llm::LLMProviderConfig* configOut,
    ProviderState* stateOut,
    std::string* error) const {
    if (!configOut) {
        if (error) *error = "provider config output is null";
        return false;
    }

    auto it = FindProviderLocked(providerId);
    if (it == m_providersByName.end()) {
        if (error) *error = "provider not found: " + providerId;
        return false;
    }

    const ProviderState& state = it->second;
    llm::LLMProviderConfig config;
    config.provider_id = state.providerType.empty() ? state.providerId : state.providerType;
    config.base_url = state.baseUrl;
    config.api_key = state.apiKey;
    config.default_model = state.defaultModel;
    config.extra = state.extra;
    if (state.authType == "oauth" && !state.authFile.empty()) {
        config.extra["auth_file"] = state.authFile;
    }

    *configOut = std::move(config);
    if (stateOut) {
        *stateOut = state;
    }
    return true;
}

std::unordered_map<std::string, ProviderState>::const_iterator
ProviderManager::FindProviderLocked(const std::string& id) const {
    auto it = m_providersByName.find(id);
    if (it != m_providersByName.end()) {
        return it;
    }
    std::string lowerId = id;
    std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    for (it = m_providersByName.begin(); it != m_providersByName.end(); ++it) {
        std::string lowerKey = it->first;
        std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lowerKey == lowerId) {
            return it;
        }
    }
    return m_providersByName.end();
}

std::unordered_map<std::string, ProviderState>::iterator
ProviderManager::FindProviderLocked(const std::string& id) {
    auto it = m_providersByName.find(id);
    if (it != m_providersByName.end()) {
        return it;
    }
    std::string lowerId = id;
    std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    for (it = m_providersByName.begin(); it != m_providersByName.end(); ++it) {
        std::string lowerKey = it->first;
        std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lowerKey == lowerId) {
            return it;
        }
    }
    return m_providersByName.end();
}

bool ProviderManager::LoadAuthFromDisk(
    const std::string& providerId,
    Json::Value* out,
    std::string* error) const {
    if (!out) {
        if (error) *error = "output is null";
        return false;
    }

    std::string authFile;
    {
        std::lock_guard<std::mutex> lock(m_providerMutex);
        if (!ResolveProviderAuthFilePathLocked(providerId, &authFile, error)) {
            return false;
        }
    }

    if (authFile.empty()) {
        *out = Json::Value(Json::objectValue);
        return true;
    }

    const std::filesystem::path path(authFile);
    if (!std::filesystem::exists(path)) {
        *out = Json::Value(Json::objectValue);
        return true;
    }

    std::ifstream in(path);
    if (!in.is_open()) {
        if (error) *error = "failed to open auth file: " + path.string();
        return false;
    }

    Json::CharReaderBuilder builder;
    std::string parseErrors;
    if (!Json::parseFromStream(builder, in, out, &parseErrors)) {
        if (error) *error = "failed to parse auth file: " + parseErrors;
        return false;
    }

    return true;
}

bool ProviderManager::SaveAuthToDisk(
    const std::string& providerId,
    const Json::Value& auth,
    std::string* error) const {
    std::string authFile;
    {
        std::lock_guard<std::mutex> lock(m_providerMutex);
        if (!ResolveProviderAuthFilePathLocked(providerId, &authFile, error)) {
            return false;
        }
    }

    if (authFile.empty()) {
        if (error) *error = "no auth file path configured";
        return false;
    }

    const std::filesystem::path path(authFile);
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            if (error) *error = "failed to create auth directory: " + parent.string();
            return false;
        }
    }

    Json::Value root(Json::objectValue);
    if (std::filesystem::exists(path)) {
        std::ifstream in(path);
        if (in.is_open()) {
            Json::CharReaderBuilder builder;
            std::string parseErrors;
            Json::parseFromStream(builder, in, &root, &parseErrors);
        }
    }

    root[providerId] = auth;

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "  ";

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        if (error) *error = "failed to open auth file for writing: " + path.string();
        return false;
    }

    std::unique_ptr<Json::StreamWriter> writer(writerBuilder.newStreamWriter());
    writer->write(root, &out);
    out << "\n";
    return true;
}

Json::Value ProviderManager::BuildProviderJson(const ProviderState& provider, bool maskSecrets) const {
    Json::Value out(Json::objectValue);
    out["provider_id"] = provider.providerId;
    out["provider_type"] = provider.providerType;
    out["base_url"] = provider.baseUrl;
    out["default_model"] = provider.defaultModel;
    out["default_context_window"] = static_cast<Json::UInt>(provider.defaultContextWindow);
    out["auth_type"] = provider.authType;
    out["status"] = provider.status;
    out["last_error"] = provider.lastError;
    out["last_tested_unix_ms"] = static_cast<Json::UInt64>(provider.lastTestedUnixMs);
    out["concurrency"] = provider.concurrency;

    if (maskSecrets && provider.authType == "api_key") {
        out["api_key"] = MaskSecret(provider.apiKey);
    } else if (!maskSecrets) {
        out["api_key"] = provider.apiKey;
    } else {
        out["api_key"] = "";
    }

    if (!provider.authFile.empty()) {
        out["auth_file"] = provider.authFile;
    }

    Json::Value extra(Json::objectValue);
    for (const auto& [k, v] : provider.extra) {
        extra[k] = v;
    }
    out["extra"] = extra;

    Json::Value modelContextWindows(Json::objectValue);
    for (const auto& [modelId, contextWindow] : provider.modelContextWindows) {
        modelContextWindows[modelId] = static_cast<Json::UInt>(contextWindow);
    }
    out["model_context_windows"] = modelContextWindows;

    Json::Value caps(Json::objectValue);
    caps["model_id"] = provider.capabilities.model_id;
    caps["context_length"] = provider.capabilities.context_length;
    caps["supports_tools"] = provider.capabilities.supports_tools;
    caps["supports_tool_choice"] = provider.capabilities.supports_tool_choice;
    caps["supports_reasoning"] = provider.capabilities.supports_reasoning;
    caps["supports_streaming"] = provider.capabilities.supports_streaming;
    caps["supports_json_mode"] = provider.capabilities.supports_json_mode;
    caps["supports_vision"] = provider.capabilities.supports_vision;
    Json::Value rawFeatures(Json::arrayValue);
    for (const auto& f : provider.capabilities.raw_features) {
        rawFeatures.append(f);
    }
    caps["raw_features"] = rawFeatures;
    out["capabilities"] = caps;

    return out;
}

bool ProviderManager::ValidateProviderPayload(
    const Json::Value& body,
    bool requireProviderId,
    std::string* error) const {
    if (!body.isObject()) {
        if (error) *error = "request body must be a JSON object";
        return false;
    }
    if (requireProviderId) {
        if (!body.isMember("provider_id") || !body["provider_id"].isString()) {
            if (error) *error = "provider_id is required";
            return false;
        }
        const std::string id = body["provider_id"].asString();
        if (id.empty() || id.size() > 64) {
            if (error) *error = "provider_id must be 1-64 characters";
            return false;
        }
        for (char c : id) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != ' ') {
                if (error) {
                    *error =
                        "provider_id contains invalid characters (alphanumeric, dash, underscore, space)";
                }
                return false;
            }
        }
    } else if (body.isMember("provider_id") && !body["provider_id"].isString()) {
        if (error) *error = "provider_id must be a string";
        return false;
    }

    if (body.isMember("provider_type") && !body["provider_type"].isString()) {
        if (error) *error = "provider_type must be a string";
        return false;
    }
    if (body.isMember("base_url") && !body["base_url"].isString()) {
        if (error) *error = "base_url must be a string";
        return false;
    }
    if (body.isMember("default_model") && !body["default_model"].isString()) {
        if (error) *error = "default_model must be a string";
        return false;
    }
    if (body.isMember("default_context_window")) {
        const Json::Value& context = body["default_context_window"];
        if (!context.isUInt() && !context.isInt()) {
            if (error) *error = "default_context_window must be an integer";
            return false;
        }
        const std::uint32_t value = context.asUInt();
        if (value == 0U || value > 4000000U) {
            if (error) *error = "default_context_window must be between 1 and 4000000";
            return false;
        }
    }
    if (body.isMember("model_context_windows")) {
        const Json::Value& windows = body["model_context_windows"];
        if (!windows.isObject()) {
            if (error) *error = "model_context_windows must be an object";
            return false;
        }
        for (const auto& modelId : windows.getMemberNames()) {
            const Json::Value& value = windows[modelId];
            if (!value.isUInt() && !value.isInt()) {
                if (error) *error = "model_context_windows values must be integers";
                return false;
            }
            const std::uint32_t parsed = value.asUInt();
            if (parsed == 0U || parsed > 4000000U) {
                if (error) *error = "model_context_windows values must be between 1 and 4000000";
                return false;
            }
        }
    }
    if (body.isMember("auth_type")) {
        if (!body["auth_type"].isString()) {
            if (error) *error = "auth_type must be a string";
            return false;
        }
        const std::string at = body["auth_type"].asString();
        if (at != "api_key" && at != "oauth" && at != "none") {
            if (error) *error = "auth_type must be 'api_key', 'oauth', or 'none'";
            return false;
        }
    }
    return true;
}

bool ProviderManager::ParseProviderPayload(
    const Json::Value& body,
    const ProviderState& defaults,
    ProviderState* out,
    std::string* error) const {
    if (!out) {
        if (error) *error = "provider output is null";
        return false;
    }
    if (!body.isObject()) {
        if (error) *error = "request body must be a JSON object";
        return false;
    }

    ProviderState p;
    p.providerId = body.get("provider_id", defaults.providerId).asString();
    p.providerType = body.get("provider_type", defaults.providerType).asString();
    p.baseUrl = body.get("base_url", defaults.baseUrl).asString();
    p.apiKey = body.isMember("api_key") && body["api_key"].isString() && !body["api_key"].asString().empty()
        ? body["api_key"].asString()
        : defaults.apiKey;
    p.defaultModel = body.get("default_model", defaults.defaultModel).asString();
    p.defaultContextWindow = body.get(
        "default_context_window",
        Json::Value(static_cast<Json::UInt>(defaults.defaultContextWindow))).asUInt();
    if (p.defaultContextWindow == 0U) {
        p.defaultContextWindow = defaults.defaultContextWindow == 0U ? 128000U : defaults.defaultContextWindow;
    }
    p.authType = body.get("auth_type", defaults.authType).asString();
    p.authFile = body.get("auth_file", defaults.authFile).asString();
    p.status = defaults.status;
    p.lastError = defaults.lastError;
    p.lastTestedUnixMs = defaults.lastTestedUnixMs;
    p.concurrency = body.get("concurrency", defaults.concurrency).asInt();
    p.capabilities = defaults.capabilities;

    p.extra = defaults.extra;
    if (body.isMember("extra")) {
        if (!body["extra"].isObject()) {
            if (error) *error = "extra must be an object";
            return false;
        }
        p.extra.clear();
        const auto& extra = body["extra"];
        for (const auto& key : extra.getMemberNames()) {
            if (extra[key].isString()) {
                p.extra[key] = extra[key].asString();
            }
        }
    }

    p.modelContextWindows = defaults.modelContextWindows;
    if (body.isMember("model_context_windows")) {
        if (!body["model_context_windows"].isObject()) {
            if (error) *error = "model_context_windows must be an object";
            return false;
        }
        p.modelContextWindows.clear();
        const Json::Value& windows = body["model_context_windows"];
        for (const auto& modelId : windows.getMemberNames()) {
            const Json::Value& value = windows[modelId];
            if ((value.isUInt() || value.isInt()) && value.asUInt() > 0U) {
                p.modelContextWindows[modelId] = value.asUInt();
            }
        }
    }

    *out = std::move(p);
    return true;
}

bool ProviderManager::ValidateOAuthProvider(
    const std::string& id,
    bool requireOpenAICodex,
    ProviderState* stateOut,
    int* httpStatusCodeOut,
    std::string* error) const {
    auto stateOpt = GetProvider(id);
    if (!stateOpt.has_value()) {
        if (httpStatusCodeOut) {
            *httpStatusCodeOut = 404;
        }
        if (error) *error = "provider not found: " + id;
        return false;
    }
    const ProviderState& state = *stateOpt;
    if (state.authType != "oauth") {
        if (httpStatusCodeOut) {
            *httpStatusCodeOut = 400;
        }
        if (error) *error = "provider is not configured for oauth auth_type";
        return false;
    }
    if (requireOpenAICodex && state.providerType != "openai-codex" && state.providerId != "openai-codex") {
        if (httpStatusCodeOut) {
            *httpStatusCodeOut = 400;
        }
        if (error) *error = "oauth browser flow currently supported for openai-codex only";
        return false;
    }
    if (stateOut) {
        *stateOut = state;
    }
    if (httpStatusCodeOut) {
        *httpStatusCodeOut = 200;
    }
    return true;
}

bool ProviderManager::BuildOAuthStatus(
    const std::string& id,
    Json::Value* out,
    int* httpStatusCodeOut,
    std::string* error) const {
    if (!out) {
        if (httpStatusCodeOut) {
            *httpStatusCodeOut = 500;
        }
        if (error) *error = "output is null";
        return false;
    }

    ProviderState state;
    if (!ValidateOAuthProvider(id, false, &state, httpStatusCodeOut, error)) {
        return false;
    }

    Json::Value authRoot;
    std::string authErr;
    if (!LoadAuthFromDisk(id, &authRoot, &authErr)) {
        if (httpStatusCodeOut) {
            *httpStatusCodeOut = 500;
        }
        if (error) *error = authErr;
        return false;
    }

    Json::Value status(Json::objectValue);
    status["provider_id"] = id;
    status["auth_type"] = state.authType;
    status["auth_file"] = state.authFile;
    status["connected"] = false;

    if (authRoot.isObject() && authRoot.isMember(id) && authRoot[id].isObject()) {
        const Json::Value& entry = authRoot[id];
        const std::string access = entry.get("access", "").asString();
        const Json::Int64 expires = entry.get("expires", 0).asInt64();
        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        status["connected"] = !access.empty();
        status["expires"] = static_cast<Json::Int64>(expires);
        status["expired"] = expires > 0 ? (expires <= nowMs) : true;
        status["has_refresh"] = !entry.get("refresh", "").asString().empty();
    }

    *out = std::move(status);
    if (httpStatusCodeOut) {
        *httpStatusCodeOut = 200;
    }
    return true;
}

bool ProviderManager::FetchProviderModels(
    const std::string& id,
    llm::LLMProviderRegistry* registry,
    Json::Value* modelsOut,
    bool* fetchedOut,
    std::string* error) const {
    if (!registry) {
        if (error) *error = "provider registry not available";
        return false;
    }
    if (!modelsOut) {
        if (error) *error = "models output is null";
        return false;
    }

    llm::LLMProviderConfig config;
    ProviderState state;
    {
        std::lock_guard<std::mutex> lock(m_providerMutex);
        if (!BuildRuntimeProviderConfigLocked(id, &config, &state, error)) {
            return false;
        }
    }

    auto provider = registry->Create(config);
    if (!provider) {
        if (error) *error = "failed to create provider: " + config.provider_id;
        return false;
    }

    auto* baseProvider = dynamic_cast<llm::LLMProviderBase*>(provider.get());
    if (!baseProvider) {
        if (error) *error = "provider does not support model listing";
        return false;
    }

    const std::string modelId = state.defaultModel.empty() ? "_list" : state.defaultModel;
    const bool ok = baseProvider->FetchCapabilities(modelId);
    if (fetchedOut) {
        *fetchedOut = ok;
    }

    *modelsOut = Json::Value(Json::arrayValue);
    for (const auto& model : baseProvider->GetCapabilities().raw_features) {
        modelsOut->append(model);
    }
    return true;
}

bool ProviderManager::RefreshProviderCapabilities(
    const std::string& id,
    llm::LLMProviderRegistry* registry,
    const std::string& modelOverride,
    llm::ProviderCapabilities* capabilitiesOut,
    bool* fetchedOut,
    std::string* error) {
    if (!registry) {
        if (error) *error = "provider registry not available";
        return false;
    }
    if (!capabilitiesOut) {
        if (error) *error = "capabilities output is null";
        return false;
    }

    llm::LLMProviderConfig config;
    ProviderState state;
    {
        std::lock_guard<std::mutex> lock(m_providerMutex);
        if (!BuildRuntimeProviderConfigLocked(id, &config, &state, error)) {
            return false;
        }
    }

    auto provider = registry->Create(config);
    if (!provider) {
        if (error) *error = "failed to create provider: " + config.provider_id;
        return false;
    }

    auto* baseProvider = dynamic_cast<llm::LLMProviderBase*>(provider.get());
    if (!baseProvider) {
        if (error) *error = "provider does not support capabilities";
        return false;
    }

    std::string modelId = modelOverride;
    if (modelId.empty()) {
        modelId = state.defaultModel;
    }
    if (modelId.empty()) {
        if (error) *error = "no default model configured for this provider";
        return false;
    }

    const bool ok = baseProvider->FetchCapabilities(modelId);
    if (fetchedOut) {
        *fetchedOut = ok;
    }
    const auto& caps = baseProvider->GetCapabilities();
    *capabilitiesOut = caps;

    ProviderState updated;
    std::string capErr;
    if (!UpdateProviderCapabilities(id, caps, &updated, &capErr)) {
        if (error) *error = capErr.empty() ? "failed to update provider capabilities" : capErr;
        return false;
    }
    return true;
}

bool ProviderManager::TestProviderConnectivity(
    const std::string& id,
    bool* successOut,
    std::string* testErrorOut,
    std::string* statusOut,
    std::string* error) {
    auto stateOpt = GetProvider(id);
    if (!stateOpt.has_value()) {
        if (error) *error = "provider not found: " + id;
        return false;
    }
    ProviderState state = *stateOpt;

    bool success = false;
    std::string testError;
    if (state.baseUrl.empty()) {
        testError = "base_url is not configured";
    } else if (state.authType == "api_key" && state.apiKey.empty()) {
        testError = "api_key is not configured";
    } else if (state.defaultModel.empty()) {
        testError = "default_model is not configured";
    } else {
        success = true;
    }

    const auto nowMs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    ProviderState updated;
    std::string statusErr;
    if (UpdateProviderStatus(
            id,
            success ? "available" : "error",
            testError,
            nowMs,
            &updated,
            &statusErr)) {
        state = updated;
    }

    if (successOut) {
        *successOut = success;
    }
    if (testErrorOut) {
        *testErrorOut = testError;
    }
    if (statusOut) {
        *statusOut = state.status;
    }
    return true;
}

} // namespace animus::kernel

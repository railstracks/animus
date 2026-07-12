#include "animus_kernel/admin/InterfaceManager.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_set>

#include <json/json.h>

namespace animus::kernel {

namespace {

std::uint64_t NowUnixMs() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());
}

std::string MaskSecretValue(const std::string& value) {
    if (value.empty()) {
        return "";
    }
    if (value.size() <= 4) {
        return std::string(value.size(), '*');
    }
    return std::string(value.size() - 4, '*') + value.substr(value.size() - 4);
}

} // namespace

void InterfaceManager::Configure(
    const KernelConfig::InterfaceConfigStorage& storage,
    const std::vector<KernelConfig::InterfaceModuleInfo>& modules) {
    std::lock_guard<std::mutex> lock(m_interfaceMutex);
    m_interfaceStorage = storage;
    m_interfaceModules = modules;
}

bool InterfaceManager::LoadFromDisk(std::string* error) {
    std::lock_guard<std::mutex> lock(m_interfaceMutex);
    m_interfacesByName.clear();

    std::unordered_set<std::string> loadedModuleIds;
    for (const auto& module : m_interfaceModules) {
        InterfaceState state{};
        state.name = DeriveInterfaceName(module);
        state.type = module.type.empty() ? "generic" : module.type;
        state.moduleId = module.moduleId;
        state.enabled = true;
        state.connected = state.type == "irc" ? false : true;
        state.lastEventUnixMs = 0;
        state.config = Json::Value(Json::objectValue);
        if (!state.name.empty()) {
            m_interfacesByName[state.name] = std::move(state);
        }
        if (!module.moduleId.empty()) {
            loadedModuleIds.insert(module.moduleId);
        }
    }

    if (!m_interfaceStorage.persistToDisk) {
        return true;
    }
    if (m_interfaceStorage.filePath.empty()) {
        if (error) {
            *error = "interface config file path must not be empty when persistence is enabled";
        }
        return false;
    }

    const std::filesystem::path path(m_interfaceStorage.filePath);
    if (!std::filesystem::exists(path)) {
        return true;
    }

    std::ifstream in(path);
    if (!in.is_open()) {
        if (error) {
            *error = "failed to open interface config file for reading: " + path.string();
        }
        return false;
    }

    Json::CharReaderBuilder readerBuilder;
    Json::Value root;
    std::string parseErrors;
    if (!Json::parseFromStream(readerBuilder, in, &root, &parseErrors)) {
        if (error) {
            *error = "failed to parse interface config file: " + parseErrors;
        }
        return false;
    }

    if (root.isMember("interfaces") && root["interfaces"].isArray()) {
        for (Json::ArrayIndex i = 0; i < root["interfaces"].size(); ++i) {
            const Json::Value& item = root["interfaces"][i];
            if (!item.isObject()) {
                continue;
            }
            const std::string name = item.get("name", "").asString();
            if (name.empty()) {
                continue;
            }
            InterfaceState state{};
            auto existing = m_interfacesByName.find(name);
            if (existing != m_interfacesByName.end()) {
                state = existing->second;
            }
            state.name = name;
            state.type = item.get("type", state.type).asString();
            state.moduleId = item.get("module_id", state.moduleId).asString();
            state.enabled = item.get("enabled", state.enabled).asBool();
            state.lastEventUnixMs = item.get("last_event_unix_ms", state.lastEventUnixMs).asUInt64();
            if (item.isMember("config") && item["config"].isObject()) {
                state.config = item["config"];
            }
            if (!state.moduleId.empty() && loadedModuleIds.find(state.moduleId) == loadedModuleIds.end()) {
                state.connected = false;
            } else if (state.type == "irc") {
                state.connected = false;
            } else {
                state.connected = state.enabled;
            }
            m_interfacesByName[name] = std::move(state);
        }
    }

    return true;
}

bool InterfaceManager::SaveToDisk(std::string* error) const {
    std::lock_guard<std::mutex> lock(m_interfaceMutex);
    if (!m_interfaceStorage.persistToDisk) {
        return true;
    }
    if (m_interfaceStorage.filePath.empty()) {
        if (error) {
            *error = "interface config file path must not be empty when persistence is enabled";
        }
        return false;
    }

    const std::filesystem::path path(m_interfaceStorage.filePath);
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            if (error) {
                *error = "failed to create interface config directory: " + parent.string();
            }
            return false;
        }
    }

    Json::Value root(Json::objectValue);
    Json::Value interfaces(Json::arrayValue);
    for (const auto& pair : m_interfacesByName) {
        const InterfaceState& state = pair.second;
        Json::Value item(Json::objectValue);
        item["name"] = state.name;
        item["type"] = state.type;
        item["module_id"] = state.moduleId;
        item["enabled"] = state.enabled;
        item["connected"] = state.connected;
        item["last_event_unix_ms"] = static_cast<Json::UInt64>(state.lastEventUnixMs);
        item["config"] = state.config;
        interfaces.append(item);
    }
    root["interfaces"] = interfaces;

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "  ";

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        if (error) {
            *error = "failed to open interface config file for writing: " + path.string();
        }
        return false;
    }

    std::unique_ptr<Json::StreamWriter> writer(writerBuilder.newStreamWriter());
    writer->write(root, &out);
    out << "\n";
    if (!out.good()) {
        if (error) {
            *error = "failed to write interface config file: " + path.string();
        }
        return false;
    }

    return true;
}

std::vector<InterfaceState> InterfaceManager::ListInterfaces() const {
    std::vector<InterfaceState> list;
    std::lock_guard<std::mutex> lock(m_interfaceMutex);
    list.reserve(m_interfacesByName.size());
    for (const auto& pair : m_interfacesByName) {
        list.push_back(pair.second);
    }
    return list;
}

std::optional<InterfaceState> InterfaceManager::GetInterface(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_interfaceMutex);
    auto it = m_interfacesByName.find(name);
    if (it == m_interfacesByName.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool InterfaceManager::IsValidToken(const std::string& value) const {
    if (value.empty() || value.size() > 128U) {
        return false;
    }
    for (const char c : value) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) != 0) {
            continue;
        }
        if (c == '-' || c == '_' || c == '.' || c == ':') {
            continue;
        }
        return false;
    }
    return true;
}

bool InterfaceManager::ValidateConfigForType(
    const std::string& type,
    const Json::Value& config,
    std::string* error) const {
    if (!config.isObject()) {
        if (error) {
            *error = "config must be a JSON object";
        }
        return false;
    }
    auto requireString = [&](const char* key) -> bool {
        if (!config.isMember(key) || !config[key].isString() || config[key].asString().empty()) {
            if (error) {
                *error = std::string("config.") + key + " is required for interface type '" + type + "'";
            }
            return false;
        }
        return true;
    };

    if (type == "slack") {
        return requireString("bot_token");
    }
    if (type == "discord") {
        return requireString("bot_token");
    }
    if (type == "whatsapp") {
        return requireString("access_token") && requireString("phone_number_id");
    }
    if (type == "webhook") {
        return requireString("endpoint_secret");
    }
    if (type == "irc") {
        return ValidateIrcConfig(config, error);
    }
    return true;
}

bool InterfaceManager::NormalizeUpdatedConfig(
    const std::string& interfaceName,
    const Json::Value& incomingConfig,
    Json::Value* normalizedConfig,
    std::string* error) const {
    if (!normalizedConfig) {
        if (error) {
            *error = "normalized config output is null";
        }
        return false;
    }
    if (!incomingConfig.isObject()) {
        if (error) {
            *error = "config must be provided as an object";
        }
        return false;
    }

    InterfaceState existing;
    {
        std::lock_guard<std::mutex> lock(m_interfaceMutex);
        auto it = m_interfacesByName.find(interfaceName);
        if (it == m_interfacesByName.end()) {
            if (error) {
                *error = "interface not found";
            }
            return false;
        }
        existing = it->second;
    }

    Json::Value nextConfig = incomingConfig;
    RestoreMaskedSecrets(&nextConfig, existing.config);
    if (!ValidateConfigForType(existing.type, nextConfig, error)) {
        return false;
    }

    *normalizedConfig = nextConfig;
    return true;
}

bool InterfaceManager::CreateInterface(const InterfaceState& state, std::string* error) {
    std::lock_guard<std::mutex> lock(m_interfaceMutex);
    if (m_interfacesByName.find(state.name) != m_interfacesByName.end()) {
        if (error) {
            *error = "interface already exists";
        }
        return false;
    }
    InterfaceState created = state;
    const bool moduleLoaded = IsModuleLoadedLocked(created.moduleId);
    created.connected = (created.type == "irc") ? false : (created.enabled && moduleLoaded);
    m_interfacesByName[created.name] = std::move(created);
    return true;
}

bool InterfaceManager::UpdateInterfaceConfig(
    const std::string& name,
    const Json::Value& config,
    std::uint64_t eventUnixMs,
    InterfaceState* updated,
    std::string* error) {
    std::lock_guard<std::mutex> lock(m_interfaceMutex);
    auto it = m_interfacesByName.find(name);
    if (it == m_interfacesByName.end()) {
        if (error) {
            *error = "interface not found";
        }
        return false;
    }
    it->second.config = config;
    it->second.lastEventUnixMs = eventUnixMs;
    if (updated) {
        *updated = it->second;
    }
    return true;
}

bool InterfaceManager::SetInterfaceEnabled(
    const std::string& name,
    bool enabled,
    std::uint64_t eventUnixMs,
    InterfaceState* updated,
    std::string* error) {
    std::lock_guard<std::mutex> lock(m_interfaceMutex);
    auto it = m_interfacesByName.find(name);
    if (it == m_interfacesByName.end()) {
        if (error) {
            *error = "interface not found";
        }
        return false;
    }
    it->second.enabled = enabled;
    if (!enabled) {
        it->second.connected = false;
    } else if (it->second.type == "irc") {
        it->second.connected = false;
    } else {
        it->second.connected = IsModuleLoadedLocked(it->second.moduleId);
    }
    it->second.lastEventUnixMs = eventUnixMs;
    if (updated) {
        *updated = it->second;
    }
    return true;
}

bool InterfaceManager::DeleteInterface(const std::string& name, std::string* error) {
    std::lock_guard<std::mutex> lock(m_interfaceMutex);
    auto it = m_interfacesByName.find(name);
    if (it == m_interfacesByName.end()) {
        if (error) {
            *error = "interface not found";
        }
        return false;
    }
    m_interfacesByName.erase(it);
    return true;
}

void InterfaceManager::SetInterfaceConnectionStatus(
    const std::string& name,
    bool connected,
    std::uint64_t eventUnixMs) {
    std::lock_guard<std::mutex> lock(m_interfaceMutex);
    auto it = m_interfacesByName.find(name);
    if (it == m_interfacesByName.end()) {
        return;
    }
    it->second.connected = connected && it->second.enabled;
    it->second.lastEventUnixMs = eventUnixMs;
}

void InterfaceManager::SyncIrcInterfaces(
    IrcMessageCallback onMessage,
    IrcStatusCallback onStatus) {
    std::unordered_map<std::string, InterfaceState> interfacesByName;
    {
        std::lock_guard<std::mutex> lock(m_interfaceMutex);
        interfacesByName = m_interfacesByName;
    }

    std::vector<InterfaceState> desired;
    desired.reserve(interfacesByName.size());
    for (const auto& pair : interfacesByName) {
        const InterfaceState& state = pair.second;
        if (state.type == "irc") {
            desired.push_back(state);
        }
    }

    std::unordered_set<std::string> desiredNames;
    for (const auto& state : desired) {
        desiredNames.insert(state.name);
    }

    std::vector<std::string> existingNames;
    {
        std::lock_guard<std::mutex> lock(m_ircMutex);
        existingNames.reserve(m_ircInterfaces.size());
        for (const auto& pair : m_ircInterfaces) {
            existingNames.push_back(pair.first);
        }
    }
    for (const auto& existingName : existingNames) {
        if (desiredNames.find(existingName) == desiredNames.end()) {
            StopIrcInterfaceLocked(existingName);
        }
    }
    for (const auto& state : desired) {
        if (!state.enabled) {
            StopIrcInterfaceLocked(state.name);
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(m_ircMutex);
            auto it = m_ircInterfaces.find(state.name);
            if (it != m_ircInterfaces.end()) {
                continue;
            }
        }
        StartIrcInterfaceLocked(state, onMessage, onStatus);
    }
}

void InterfaceManager::StopAllIrcInterfaces() {
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(m_ircMutex);
        names.reserve(m_ircInterfaces.size());
        for (const auto& pair : m_ircInterfaces) {
            names.push_back(pair.first);
        }
    }
    for (const auto& name : names) {
        StopIrcInterfaceLocked(name);
    }
}

bool InterfaceManager::SendPrivmsg(
    const std::string& interfaceName,
    const std::string& target,
    const std::string& message) {
    std::shared_ptr<IrcInterfaceRuntime> runtime;
    {
        std::lock_guard<std::mutex> lock(m_ircMutex);
        auto it = m_ircInterfaces.find(interfaceName);
        if (it == m_ircInterfaces.end()) {
            return false;
        }
        runtime = it->second;
    }
    return runtime ? runtime->SendPrivmsg(target, message) : false;
}

void InterfaceManager::StartIrcInterfaceLocked(
    const InterfaceState& state,
    IrcMessageCallback onMessage,
    IrcStatusCallback onStatus) {
    {
        std::lock_guard<std::mutex> lock(m_ircMutex);
        if (m_ircInterfaces.find(state.name) != m_ircInterfaces.end()) {
            return;
        }
    }

    IrcInterfaceRuntime::Config runtimeConfig;
    std::string parseError;
    if (!IrcInterfaceRuntime::ParseConfig(state.config, &runtimeConfig, &parseError)) {
        if (onStatus) {
            onStatus(state.name, false, NowUnixMs());
        }
        std::cerr << "[irc] interface '" << state.name << "' config invalid: " << parseError << std::endl;
        return;
    }

    auto runtime = std::make_shared<IrcInterfaceRuntime>(
        state.name,
        runtimeConfig,
        [onMessage, interfaceName = state.name, botNick = runtimeConfig.nick](
            const std::string& sourceNick,
            const std::string& target,
            const std::string& message,
            bool isNotice) {
            if (onMessage) {
                onMessage(interfaceName, botNick, sourceNick, target, message, isNotice);
            }
        },
        [onStatus, interfaceName = state.name](bool connected, const std::string&) {
            if (onStatus) {
                onStatus(interfaceName, connected, NowUnixMs());
            }
        });

    {
        std::lock_guard<std::mutex> lock(m_ircMutex);
        m_ircInterfaces[state.name] = runtime;
    }
    runtime->Start();
}

void InterfaceManager::StopIrcInterfaceLocked(const std::string& interfaceName) {
    std::shared_ptr<IrcInterfaceRuntime> runtime;
    {
        std::lock_guard<std::mutex> lock(m_ircMutex);
        auto it = m_ircInterfaces.find(interfaceName);
        if (it == m_ircInterfaces.end()) {
            return;
        }
        runtime = it->second;
        m_ircInterfaces.erase(it);
    }
    if (runtime) {
        runtime->Stop();
    }
}

bool InterfaceManager::IsModuleLoadedLocked(const std::string& moduleId) const {
    if (moduleId.empty()) {
        return false;
    }
    for (const auto& module : m_interfaceModules) {
        if (module.moduleId == moduleId) {
            return true;
        }
    }
    return false;
}

std::string InterfaceManager::DeriveInterfaceName(const KernelConfig::InterfaceModuleInfo& moduleInfo) {
    if (moduleInfo.moduleId.rfind("connector.", 0) == 0) {
        return moduleInfo.moduleId.substr(std::string("connector.").size());
    }
    return moduleInfo.moduleId;
}

bool InterfaceManager::IsSensitiveFieldName(const std::string& key) {
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

std::string InterfaceManager::MaskSecret(const std::string& value) {
    return MaskSecretValue(value);
}

void InterfaceManager::RestoreMaskedSecrets(Json::Value* nextConfig, const Json::Value& currentConfig) {
    if (!nextConfig) {
        return;
    }
    if (nextConfig->isObject() && currentConfig.isObject()) {
        const std::vector<std::string> names = nextConfig->getMemberNames();
        for (const auto& name : names) {
            Json::Value& nextValue = (*nextConfig)[name];
            const Json::Value& currentValue = currentConfig[name];
            if (nextValue.isObject() && currentValue.isObject()) {
                RestoreMaskedSecrets(&nextValue, currentValue);
                continue;
            }
            if (nextValue.isArray() && currentValue.isArray() && nextValue.size() == currentValue.size()) {
                for (Json::ArrayIndex i = 0; i < nextValue.size(); ++i) {
                    Json::Value& childNext = nextValue[i];
                    const Json::Value& childCurrent = currentValue[i];
                    if (childNext.isObject() && childCurrent.isObject()) {
                        RestoreMaskedSecrets(&childNext, childCurrent);
                    }
                }
                continue;
            }
            if (nextValue.isString() && currentValue.isString() && IsSensitiveFieldName(name)) {
                const std::string masked = MaskSecret(currentValue.asString());
                if (nextValue.asString() == masked) {
                    nextValue = currentValue;
                }
            }
        }
    }
}

bool InterfaceManager::ValidateIrcConfig(const Json::Value& config, std::string* error) {
    auto fail = [&](const std::string& message) -> bool {
        if (error) {
            *error = message;
        }
        return false;
    };

    if (!config.isObject()) {
        return fail("config must be a JSON object");
    }
    if (!config.isMember("host") || !config["host"].isString() || config["host"].asString().empty()) {
        return fail("config.host is required for interface type 'irc'");
    }
    if (!config.isMember("nick") || !config["nick"].isString() || config["nick"].asString().empty()) {
        return fail("config.nick is required for interface type 'irc'");
    }
    if (config.isMember("port")) {
        const Json::Value& port = config["port"];
        if (!(port.isInt() || port.isUInt())) {
            return fail("config.port must be an integer");
        }
        const int parsedPort = port.asInt();
        if (parsedPort <= 0 || parsedPort > 65535) {
            return fail("config.port must be between 1 and 65535");
        }
    }

    const auto validateOptionalString = [&](const char* key) -> bool {
        if (!config.isMember(key)) {
            return true;
        }
        if (!config[key].isString()) {
            return fail(std::string("config.") + key + " must be a string");
        }
        return true;
    };
    if (!validateOptionalString("server_password")
        || !validateOptionalString("username")
        || !validateOptionalString("realname")
        || !validateOptionalString("agent_id")) {
        return false;
    }

    if (config.isMember("channels")) {
        if (!config["channels"].isArray()) {
            return fail("config.channels must be an array");
        }
        for (Json::ArrayIndex i = 0; i < config["channels"].size(); ++i) {
            const Json::Value& channel = config["channels"][i];
            if (!channel.isObject()) {
                return fail("config.channels entries must be objects");
            }
            if (!channel.isMember("name") || !channel["name"].isString()
                || channel["name"].asString().empty()) {
                return fail("config.channels[].name is required and must be a non-empty string");
            }
            if (channel.isMember("key") && !channel["key"].isString()) {
                return fail("config.channels[].key must be a string");
            }
        }
    }

    bool dmOnly = false;
    if (config.isMember("dm_only")) {
        if (!config["dm_only"].isBool()) {
            return fail("config.dm_only must be a boolean");
        }
        dmOnly = config["dm_only"].asBool();
    }
    const bool hasChannels = config.isMember("channels")
        && config["channels"].isArray()
        && config["channels"].size() > 0;
    if (!hasChannels && !dmOnly) {
        return fail("config.channels must contain at least one channel unless config.dm_only is true");
    }

    for (const char* flag : {"respond_to_channel_activity", "respond_to_direct_messages", "respond_to_notices"}) {
        if (config.isMember(flag) && !config[flag].isBool()) {
            return fail(std::string("config.") + flag + " must be a boolean");
        }
    }

    if (config.isMember("allowed_dm_users")) {
        if (!config["allowed_dm_users"].isArray()) {
            return fail("config.allowed_dm_users must be an array");
        }
        for (Json::ArrayIndex i = 0; i < config["allowed_dm_users"].size(); ++i) {
            if (!config["allowed_dm_users"][i].isString()) {
                return fail("config.allowed_dm_users must contain only strings");
            }
        }
    }

    if (config.isMember("reconnect")) {
        const Json::Value& reconnect = config["reconnect"];
        if (!reconnect.isObject()) {
            return fail("config.reconnect must be an object");
        }
        if (reconnect.isMember("enabled") && !reconnect["enabled"].isBool()) {
            return fail("config.reconnect.enabled must be a boolean");
        }
        if (reconnect.isMember("initial_delay_ms")
            && !(reconnect["initial_delay_ms"].isUInt() || reconnect["initial_delay_ms"].isInt())) {
            return fail("config.reconnect.initial_delay_ms must be an integer");
        }
        if (reconnect.isMember("max_delay_ms")
            && !(reconnect["max_delay_ms"].isUInt() || reconnect["max_delay_ms"].isInt())) {
            return fail("config.reconnect.max_delay_ms must be an integer");
        }
    }

    return true;
}

} // namespace animus::kernel

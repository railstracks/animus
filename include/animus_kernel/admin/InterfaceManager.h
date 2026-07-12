#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "animus_kernel/KernelConfig.h"
#include "animus_kernel/admin/InterfaceState.h"
#include "animus_kernel/interfaces/IrcInterface.h"

namespace animus::kernel {

class InterfaceManager {
public:
    using IrcMessageCallback = std::function<void(
        const std::string& interfaceName,
        const std::string& botNick,
        const std::string& sourceNick,
        const std::string& target,
        const std::string& message,
        bool isNotice)>;

    using IrcStatusCallback = std::function<void(
        const std::string& interfaceName,
        bool connected,
        std::uint64_t eventUnixMs)>;

    void Configure(
        const KernelConfig::InterfaceConfigStorage& storage,
        const std::vector<KernelConfig::InterfaceModuleInfo>& modules);

    bool LoadFromDisk(std::string* error);
    bool SaveToDisk(std::string* error) const;

    std::vector<InterfaceState> ListInterfaces() const;
    std::optional<InterfaceState> GetInterface(const std::string& name) const;
    bool IsValidToken(const std::string& value) const;
    bool ValidateConfigForType(
        const std::string& type,
        const Json::Value& config,
        std::string* error) const;
    bool NormalizeUpdatedConfig(
        const std::string& interfaceName,
        const Json::Value& incomingConfig,
        Json::Value* normalizedConfig,
        std::string* error) const;
    bool CreateInterface(const InterfaceState& state, std::string* error);
    bool UpdateInterfaceConfig(
        const std::string& name,
        const Json::Value& config,
        std::uint64_t eventUnixMs,
        InterfaceState* updated,
        std::string* error);
    bool SetInterfaceEnabled(
        const std::string& name,
        bool enabled,
        std::uint64_t eventUnixMs,
        InterfaceState* updated,
        std::string* error);
    bool DeleteInterface(const std::string& name, std::string* error);
    void SetInterfaceConnectionStatus(
        const std::string& name,
        bool connected,
        std::uint64_t eventUnixMs);

    void SyncIrcInterfaces(
        IrcMessageCallback onMessage,
        IrcStatusCallback onStatus);

    void StopAllIrcInterfaces();
    bool SendPrivmsg(
        const std::string& interfaceName,
        const std::string& target,
        const std::string& message);

private:
    static bool IsSensitiveFieldName(const std::string& key);
    static std::string MaskSecret(const std::string& value);
    static void RestoreMaskedSecrets(Json::Value* nextConfig, const Json::Value& currentConfig);
    static bool ValidateIrcConfig(const Json::Value& config, std::string* error);

    void StartIrcInterfaceLocked(
        const InterfaceState& state,
        IrcMessageCallback onMessage,
        IrcStatusCallback onStatus);

    void StopIrcInterfaceLocked(const std::string& interfaceName);
    bool IsModuleLoadedLocked(const std::string& moduleId) const;
    static std::string DeriveInterfaceName(const KernelConfig::InterfaceModuleInfo& moduleInfo);

    std::unordered_map<std::string, InterfaceState> m_interfacesByName;
    mutable std::mutex m_interfaceMutex;
    KernelConfig::InterfaceConfigStorage m_interfaceStorage{};
    std::vector<KernelConfig::InterfaceModuleInfo> m_interfaceModules{};

    std::unordered_map<std::string, std::shared_ptr<IrcInterfaceRuntime>> m_ircInterfaces;
    mutable std::mutex m_ircMutex;
};

} // namespace animus::kernel

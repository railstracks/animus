#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <json/json.h>

#include "animus_kernel/KernelConfig.h"
#include "animus_kernel/admin/ProviderState.h"
#include "animus_kernel/llm/LLMProviderConfig.h"

namespace animus::kernel {
namespace llm {
class LLMProviderRegistry;
}

class ProviderManager {
public:
    void Configure(const KernelConfig::ProviderConfigStorage& storage);

    bool LoadFromDisk(std::string* error);
    bool SaveToDisk(std::string* error) const;

    std::vector<ProviderState> ListProviders() const;
    std::optional<ProviderState> GetProvider(const std::string& id) const;
    bool HasProvider(const std::string& id) const;

    std::string GetDefaultProviderId() const;
    bool SetDefaultProviderId(const std::string& id, std::string* error);

    bool CreateProvider(const ProviderState& state, std::string* error);
    bool UpdateProvider(const std::string& id, const ProviderState& state, std::string* error);
    bool DeleteProvider(const std::string& id, ProviderState* removed, std::string* error);

    bool UpdateProviderStatus(
        const std::string& id,
        const std::string& status,
        const std::string& lastError,
        std::uint64_t lastTestedUnixMs,
        ProviderState* updated,
        std::string* error);

    bool UpdateProviderCapabilities(
        const std::string& id,
        const llm::ProviderCapabilities& capabilities,
        ProviderState* updated,
        std::string* error);

    int GetProviderConcurrency(const std::string& providerId) const;
    std::optional<llm::LLMProviderConfig> GetProviderConfig(const std::string& providerId) const;

    bool LoadAuthFromDisk(const std::string& providerId, Json::Value* out, std::string* error) const;
    bool SaveAuthToDisk(const std::string& providerId, const Json::Value& auth, std::string* error) const;

    Json::Value BuildProviderJson(const ProviderState& provider, bool maskSecrets = true) const;
    bool ValidateProviderPayload(
        const Json::Value& body,
        bool requireProviderId,
        std::string* error) const;
    bool ParseProviderPayload(
        const Json::Value& body,
        const ProviderState& defaults,
        ProviderState* out,
        std::string* error) const;
    bool ValidateOAuthProvider(
        const std::string& id,
        bool requireOpenAICodex,
        ProviderState* stateOut,
        int* httpStatusCodeOut,
        std::string* error) const;
    bool BuildOAuthStatus(
        const std::string& id,
        Json::Value* out,
        int* httpStatusCodeOut,
        std::string* error) const;
    bool FetchProviderModels(
        const std::string& id,
        llm::LLMProviderRegistry* registry,
        Json::Value* modelsOut,
        bool* fetchedOut,
        std::string* error) const;
    bool RefreshProviderCapabilities(
        const std::string& id,
        llm::LLMProviderRegistry* registry,
        const std::string& modelOverride,
        llm::ProviderCapabilities* capabilitiesOut,
        bool* fetchedOut,
        std::string* error);
    bool TestProviderConnectivity(
        const std::string& id,
        bool* successOut,
        std::string* testErrorOut,
        std::string* statusOut,
        std::string* error);

private:
    std::unordered_map<std::string, ProviderState>::const_iterator
    FindProviderLocked(const std::string& id) const;
    std::unordered_map<std::string, ProviderState>::iterator
    FindProviderLocked(const std::string& id);

    bool ResolveProviderAuthFilePathLocked(
        const std::string& providerId,
        std::string* authFile,
        std::string* error) const;
    bool BuildRuntimeProviderConfigLocked(
        const std::string& providerId,
        llm::LLMProviderConfig* configOut,
        ProviderState* stateOut,
        std::string* error) const;

    KernelConfig::ProviderConfigStorage m_providerStorage{};
    std::string m_defaultProvider;
    std::unordered_map<std::string, ProviderState> m_providersByName;
    mutable std::mutex m_providerMutex;
};

} // namespace animus::kernel

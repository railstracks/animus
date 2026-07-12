#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

#include "animus_kernel/AgentStore.h"
#include "animus_kernel/KernelConfig.h"
#include "animus_kernel/MemoryStore.h"
#include "animus_kernel/admin/ProviderManager.h"

namespace animus::kernel {

class AgentManager {
public:
    struct OperationResult {
        int statusCode{200};
        std::string error;
        std::optional<Agent> agent;
        std::vector<Agent> agents;
        std::string deletedId;
    };

    void Configure(AgentStore* agentStore, const ProviderManager* providerManager,
                    memory::MemoryStore* memoryStore = nullptr);

    OperationResult ListAgents();
    OperationResult GetAgent(const std::string& id);
    OperationResult CreateAgent(const Json::Value& patch);
    OperationResult UpdateAgent(const std::string& id, const Json::Value& patch);
    OperationResult DeleteAgent(const std::string& id);
    bool ApplyRuntimeConfigPatch(
        const Json::Value& patch,
        KernelConfig::AgentRuntimeConfig* config,
        bool* modelChanged,
        std::string* error) const;

private:
    bool ValidateRuntimeConfig(
        const KernelConfig::AgentRuntimeConfig& config,
        std::string* error) const;
    bool ValidateFileToolConfig(const Json::Value& fileCfg, std::string* error) const;
    bool ParseUInt32Field(
        const Json::Value& object,
        const char* key,
        std::uint32_t* out,
        std::string* error) const;
    bool ParseDoubleField(
        const Json::Value& object,
        const char* key,
        double* out,
        std::string* error) const;
    bool ParseStringField(
        const Json::Value& object,
        const char* key,
        std::string* out,
        std::string* error) const;
    bool ApplyAgentEntityPatch(
        const Json::Value& patch,
        Agent* agent,
        std::string* error) const;

    AgentStore* m_agentStore{nullptr};
    const ProviderManager* m_providerManager{nullptr};
    memory::MemoryStore* m_memoryStore{nullptr};  // for copy-on-create layer templates
    mutable std::mutex m_agentCrudMutex;
};

} // namespace animus::kernel

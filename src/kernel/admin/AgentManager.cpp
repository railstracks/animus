#include "animus_kernel/admin/AgentManager.h"

#include <iostream>
#include <json/writer.h>

namespace animus::kernel {

void AgentManager::Configure(AgentStore* agentStore, const ProviderManager* providerManager,
                                memory::MemoryStore* memoryStore) {
    std::lock_guard<std::mutex> lock(m_agentCrudMutex);
    m_agentStore = agentStore;
    m_providerManager = providerManager;
    m_memoryStore = memoryStore;
}

AgentManager::OperationResult AgentManager::ListAgents() {
    OperationResult result;
    std::lock_guard<std::mutex> lock(m_agentCrudMutex);
    if (!m_agentStore) {
        result.statusCode = 500;
        result.error = "agent store not available";
        return result;
    }
    result.agents = m_agentStore->List();
    return result;
}

AgentManager::OperationResult AgentManager::GetAgent(const std::string& id) {
    OperationResult result;
    std::lock_guard<std::mutex> lock(m_agentCrudMutex);
    if (!m_agentStore) {
        result.statusCode = 500;
        result.error = "agent store not available";
        return result;
    }
    auto agent = m_agentStore->GetById(id);
    if (!agent.has_value()) {
        result.statusCode = 404;
        result.error = "agent not found";
        return result;
    }
    result.agent = *agent;
    return result;
}

AgentManager::OperationResult AgentManager::CreateAgent(const Json::Value& patch) {
    OperationResult result;
    std::lock_guard<std::mutex> lock(m_agentCrudMutex);
    if (!m_agentStore || !m_providerManager) {
        result.statusCode = 500;
        result.error = "agent subsystem not initialized";
        return result;
    }

    Agent agent;
    std::string patchErr;
    if (!ApplyAgentEntityPatch(patch, &agent, &patchErr)) {
        result.statusCode = 400;
        result.error = patchErr;
        return result;
    }

    // Default identity to "You are <name>." if not explicitly set
    if (agent.identity.empty() && !agent.name.empty()) {
        agent.identity = "You are " + agent.name + ".";
    }

    if (!m_providerManager->HasProvider(agent.default_provider)) {
        result.statusCode = 400;
        result.error = "unknown provider: " + agent.default_provider;
        return result;
    }

    auto created = m_agentStore->Create(agent);
    if (created.id.empty()) {
        result.statusCode = 500;
        result.error = "failed to create agent";
        return result;
    }

    // Copy memory layers from default agent template
    if (m_memoryStore) {
        m_memoryStore->CreateDefaultLayersForAgent(created.id);
        std::cerr << "[agent-manager] created default memory layers for agent " << created.id << std::endl;
    }

    result.statusCode = 201;
    result.agent = created;
    return result;
}

AgentManager::OperationResult AgentManager::UpdateAgent(const std::string& id, const Json::Value& patch) {
    OperationResult result;
    std::lock_guard<std::mutex> lock(m_agentCrudMutex);
    if (!m_agentStore || !m_providerManager) {
        result.statusCode = 500;
        result.error = "agent subsystem not initialized";
        return result;
    }

    auto existing = m_agentStore->GetById(id);
    if (!existing.has_value()) {
        result.statusCode = 404;
        result.error = "agent not found";
        return result;
    }

    Agent updated = *existing;
    std::string patchErr;
    if (!ApplyAgentEntityPatch(patch, &updated, &patchErr)) {
        result.statusCode = 400;
        result.error = patchErr;
        return result;
    }

    if (!m_providerManager->HasProvider(updated.default_provider)) {
        result.statusCode = 400;
        result.error = "unknown provider: " + updated.default_provider;
        return result;
    }

    if (!m_agentStore->Update(updated)) {
        result.statusCode = 500;
        result.error = "failed to update agent";
        return result;
    }

    auto refreshed = m_agentStore->GetById(id);
    result.agent = refreshed.value_or(updated);
    return result;
}

AgentManager::OperationResult AgentManager::DeleteAgent(const std::string& id) {
    OperationResult result;
    std::lock_guard<std::mutex> lock(m_agentCrudMutex);
    if (!m_agentStore) {
        result.statusCode = 500;
        result.error = "agent store not available";
        return result;
    }

    if (id == "default") {
        result.statusCode = 403;
        result.error = "cannot delete the default agent";
        return result;
    }

    auto existing = m_agentStore->GetById(id);
    if (!existing.has_value()) {
        result.statusCode = 404;
        result.error = "agent not found";
        return result;
    }

    if (m_agentStore->HasActiveSessions(id)) {
        result.statusCode = 409;
        result.error = "agent has active sessions; reassign or terminate them first";
        return result;
    }

    if (!m_agentStore->Delete(id)) {
        result.statusCode = 500;
        result.error = "failed to delete agent";
        return result;
    }

    result.deletedId = id;
    return result;
}

bool AgentManager::ApplyRuntimeConfigPatch(
    const Json::Value& patch,
    KernelConfig::AgentRuntimeConfig* config,
    bool* modelChanged,
    std::string* error) const {
    if (!config) {
        if (error) {
            *error = "config target is null";
        }
        return false;
    }
    if (!patch.isObject()) {
        if (error) {
            *error = "request body must be a JSON object";
        }
        return false;
    }

    if (!ParseDoubleField(patch, "temperature", &config->temperature, error)) {
        return false;
    }
    if (!ParseStringField(patch, "identity", &config->identity, error)) {
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
        ParseUInt32Field(budget, "episodic_token_budget", &config->budget.episodicTokenBudget, nullptr);
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

    if (patch.isMember("reasoning")) {
        const Json::Value& reasoning = patch["reasoning"];
        if (!reasoning.isObject()) {
            if (error) {
                *error = "reasoning must be an object";
            }
            return false;
        }
        if (reasoning.isMember("enabled")) {
            if (!reasoning["enabled"].isBool()) {
                if (error) {
                    *error = "reasoning.enabled must be a boolean";
                }
                return false;
            }
            config->reasoningEnabled = reasoning["enabled"].asBool();
        }
        if (!ParseStringField(reasoning, "effort", &config->reasoning_effort, error)) {
            return false;
        }
        if (!ParseStringField(reasoning, "instruction", &config->reasoningInstruction, error)) {
            return false;
        }
    }

    return ValidateRuntimeConfig(*config, error);
}

bool AgentManager::ValidateRuntimeConfig(
    const KernelConfig::AgentRuntimeConfig& config,
    std::string* error) const {
    if (config.model.provider.empty()) {
        if (error) {
            *error = "model.provider is required";
        }
        return false;
    }
    if (config.model.modelId.empty()) {
        if (error) {
            *error = "model.model_id is required";
        }
        return false;
    }
    if (config.model.contextWindow == 0U) {
        if (error) {
            *error = "model.context_window must be greater than 0";
        }
        return false;
    }
    if (config.temperature < 0.0 || config.temperature > 2.0) {
        if (error) {
            *error = "temperature must be between 0.0 and 2.0";
        }
        return false;
    }
    if (config.identity.size() > 65535U) {
        if (error) {
            *error = "identity exceeds maximum length";
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

bool AgentManager::ValidateFileToolConfig(const Json::Value& fileCfg, std::string* error) const {
    if (!fileCfg.isObject()) {
        if (error) *error = "tool_configs.file must be an object";
        return false;
    }
    if (fileCfg.isMember("restrict_to_workspace") && !fileCfg["restrict_to_workspace"].isBool()) {
        if (error) *error = "tool_configs.file.restrict_to_workspace must be a boolean";
        return false;
    }
    if (fileCfg.isMember("workspace_root") && !fileCfg["workspace_root"].isString()) {
        if (error) *error = "tool_configs.file.workspace_root must be a string";
        return false;
    }
    auto validateStringArray = [error](const Json::Value& value, const char* field) -> bool {
        if (!value.isArray()) {
            if (error) *error = std::string(field) + " must be an array";
            return false;
        }
        for (Json::ArrayIndex i = 0; i < value.size(); ++i) {
            if (!value[i].isString()) {
                if (error) *error = std::string(field) + " must contain only strings";
                return false;
            }
        }
        return true;
    };
    if (fileCfg.isMember("path_allowlist")
        && !validateStringArray(fileCfg["path_allowlist"], "tool_configs.file.path_allowlist")) {
        return false;
    }
    if (fileCfg.isMember("path_denylist")
        && !validateStringArray(fileCfg["path_denylist"], "tool_configs.file.path_denylist")) {
        return false;
    }
    return true;
}

bool AgentManager::ParseUInt32Field(
    const Json::Value& object,
    const char* key,
    std::uint32_t* out,
    std::string* error) const {
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

bool AgentManager::ParseDoubleField(
    const Json::Value& object,
    const char* key,
    double* out,
    std::string* error) const {
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

bool AgentManager::ParseStringField(
    const Json::Value& object,
    const char* key,
    std::string* out,
    std::string* error) const {
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

bool AgentManager::ApplyAgentEntityPatch(
    const Json::Value& patch,
    Agent* agent,
    std::string* error) const {
    if (!patch.isObject()) {
        if (error) *error = "request body must be a JSON object";
        return false;
    }

    if (!ParseStringField(patch, "name", &agent->name, error)) return false;
    if (!ParseStringField(patch, "description", &agent->description, error)) return false;
    if (!ParseStringField(patch, "identity", &agent->identity, error)) return false;
    if (!ParseStringField(patch, "avatar", &agent->avatar, error)) return false;

    if (patch.isMember("model")) {
        const Json::Value& model = patch["model"];
        if (!model.isObject()) {
            if (error) *error = "model must be an object";
            return false;
        }
        if (!ParseStringField(model, "provider", &agent->default_provider, error)) return false;
        if (!ParseStringField(model, "model_id", &agent->default_model, error)) return false;
        if (model.isMember("context_window")) {
            if (!model["context_window"].isUInt()) {
                if (error) *error = "model.context_window must be a positive integer";
                return false;
            }
            agent->context_window = model["context_window"].asUInt();
        }
    }

    if (!ParseStringField(patch, "default_provider", &agent->default_provider, error)) return false;
    if (!ParseStringField(patch, "default_model", &agent->default_model, error)) return false;
    if (!ParseStringField(patch, "default_vision_model", &agent->default_vision_model, error)) return false;
    if (!ParseStringField(patch, "intake_interval", &agent->intake_interval, error)) return false;
    if (!ParseStringField(patch, "intake_prompt", &agent->intake_prompt, error)) return false;
    if (patch.isMember("context_window")) {
        if (!patch["context_window"].isUInt()) {
            if (error) *error = "context_window must be a positive integer";
            return false;
        }
        agent->context_window = patch["context_window"].asUInt();
    }
    if (!ParseDoubleField(patch, "temperature", &agent->temperature, error)) return false;

    if (patch.isMember("reasoning")) {
        const Json::Value& reasoning = patch["reasoning"];
        if (!reasoning.isObject()) {
            if (error) *error = "reasoning must be an object";
            return false;
        }
        if (reasoning.isMember("enabled")) {
            if (!reasoning["enabled"].isBool()) {
                if (error) *error = "reasoning.enabled must be a boolean";
                return false;
            }
            agent->reasoning_enabled = reasoning["enabled"].asBool();
        }
        if (!ParseStringField(reasoning, "effort", &agent->reasoning_effort, error)) return false;
    }

    if (patch.isMember("pad_context")) {
        if (!patch["pad_context"].isBool()) {
            if (error) *error = "pad_context must be a boolean";
            return false;
        }
        agent->pad_context = patch["pad_context"].asBool();
    }

    if (patch.isMember("budget")) {
        const Json::Value& budget = patch["budget"];
        if (!budget.isObject()) {
            if (error) *error = "budget must be an object";
            return false;
        }
        if (!ParseUInt32Field(budget, "max_chain_steps", &agent->budget.maxChainSteps, error)) return false;
        if (!ParseUInt32Field(budget, "max_tool_calls_per_chain", &agent->budget.maxToolCallsPerChain, error)) return false;
        if (!ParseUInt32Field(budget, "timeout_seconds", &agent->budget.timeoutSeconds, error)) return false;
        if (!ParseUInt32Field(budget, "token_budget_per_prompt", &agent->budget.tokenBudgetPerPrompt, error)) return false;
        ParseUInt32Field(budget, "episodic_token_budget", &agent->budget.episodicTokenBudget, nullptr);
        ParseUInt32Field(budget, "semantic_token_budget", &agent->budget.semanticTokenBudget, nullptr);
        ParseUInt32Field(budget, "perspectives_token_budget", &agent->budget.perspectivesTokenBudget, nullptr);
        ParseUInt32Field(budget, "memory_file_token_budget", &agent->budget.memoryFileTokenBudget, nullptr);
        ParseUInt32Field(budget, "ambient_context_limit", &agent->budget.ambientContextLimit, nullptr);
        ParseUInt32Field(budget, "session_report_token_budget", &agent->budget.sessionReportTokenBudget, nullptr);
        ParseUInt32Field(budget, "consolidation_tool_budget", &agent->budget.consolidationToolBudget, nullptr);
    }

    if (patch.isMember("enabled_tools")) {
        const Json::Value& tools = patch["enabled_tools"];
        if (!tools.isArray()) {
            if (error) *error = "enabled_tools must be an array";
            return false;
        }
        agent->enabled_tools.clear();
        for (Json::ArrayIndex i = 0; i < tools.size(); ++i) {
            if (tools[i].isString()) {
                agent->enabled_tools.push_back(tools[i].asString());
            }
        }
    }

    if (patch.isMember("allowed_nodes")) {
        const Json::Value& nodes = patch["allowed_nodes"];
        if (!nodes.isArray()) {
            if (error) *error = "allowed_nodes must be an array";
            return false;
        }
        agent->allowed_nodes.clear();
        for (Json::ArrayIndex i = 0; i < nodes.size(); ++i) {
            if (nodes[i].isString()) {
                agent->allowed_nodes.push_back(nodes[i].asString());
            }
        }
    }

    if (patch.isMember("tool_configs")) {
        const Json::Value& toolConfigs = patch["tool_configs"];
        if (!toolConfigs.isObject()) {
            if (error) *error = "tool_configs must be an object";
            return false;
        }

        if (toolConfigs.isMember("file")) {
            if (!ValidateFileToolConfig(toolConfigs["file"], error)) {
                return false;
            }
        }

        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        agent->tool_configs_json = Json::writeString(wb, toolConfigs);
    }

    if (agent->name.empty()) {
        if (error) *error = "name is required";
        return false;
    }
    if (agent->temperature < 0.0 || agent->temperature > 2.0) {
        if (error) *error = "temperature must be between 0.0 and 2.0";
        return false;
    }
    if (agent->default_provider.empty()) {
        if (error) *error = "model.provider is required";
        return false;
    }
    if (agent->default_model.empty()) {
        if (error) *error = "model.model_id is required";
        return false;
    }

    return true;
}

} // namespace animus::kernel

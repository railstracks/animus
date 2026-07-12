#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/lua/LuaState.h"
#include "animus_kernel/lua/ScriptStore.h"

#include <json/json.h>
#include <string>
#include <memory>
#include <unordered_map>

namespace animus::kernel {

// ============================================================================
// LuaTool — agent-facing tool for executing and managing Lua scripts
//
// Operations: eval, run, store, list, delete
// ============================================================================

class LuaTool : public IToolHandler {
public:
    explicit LuaTool(std::unordered_map<std::string, std::unique_ptr<LuaState>>& agentStates,
                     ToolRegistry& tools,
                     HttpClient& http,
                     ScriptStore& scriptStore,
                     AgentConfigStore* configStore = nullptr);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    ToolResult HandleEval(const std::string& agentId, const Json::Value& args);
    ToolResult HandleRun(const std::string& agentId, const Json::Value& args);
    ToolResult HandleStore(const std::string& agentId, const Json::Value& args);
    ToolResult HandleList(const std::string& agentId, const Json::Value& args);
    ToolResult HandleDelete(const std::string& agentId, const Json::Value& args);

    /// Get or create a LuaState for the given agent.
    LuaState& GetOrCreateState(const std::string& agentId);

    // Reference to AgentKernel's map of per-agent Lua VMs
    std::unordered_map<std::string, std::unique_ptr<LuaState>>& m_agentStates;
    ToolRegistry& m_tools;
    HttpClient& m_http;
    ScriptStore& m_scriptStore;
    AgentConfigStore* m_configStore;
};

} // namespace animus::kernel
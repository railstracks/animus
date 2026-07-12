#pragma once

#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/lua/LuaState.h"

namespace animus::kernel {

// ============================================================================
// LuaToolHandler — IToolHandler proxy for Lua-registered tools
//
// When a Lua script calls animus.register_tool(), a LuaToolHandler is created
// and registered in the ToolRegistry. When the LLM calls that tool, Execute()
// marshals the call into Lua, invokes the handler function, and marshals the
// result back.
// ============================================================================

class LuaToolHandler : public IToolHandler {
public:
    /// Create a Lua tool handler that proxies calls to a Lua function.
    /// @param name         Tool name (as seen by the LLM)
    /// @param description  Tool description
    /// @param parameters   Parameter definitions
    /// @param resultMode   How results should be delivered
    /// @param sessionTypes Session types this tool is available in (empty = all)
    /// @param handlerRef    Lua registry reference to the handler function
    /// @param luaState      Owning LuaState (must outlive this handler)
    LuaToolHandler(const std::string& name,
                   const std::string& description,
                   const std::vector<ToolParameter>& parameters,
                   ToolResultMode resultMode,
                   const std::vector<std::string>& sessionTypes,
                   int handlerRef,
                   LuaState* luaState);

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;
    ToolResultMode GetResultMode() const override;

private:
    ToolDefinition m_definition;
    int m_handlerRef;       // Lua registry reference
    LuaState* m_luaState;   // Non-owning pointer to owning VM
};

} // namespace animus::kernel
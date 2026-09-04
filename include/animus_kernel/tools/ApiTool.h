#pragma once

#include "animus_kernel/api/ApiRuntime.h"
#include "animus_kernel/tools/ToolRegistry.h"

#include <string>

namespace animus::kernel {

// ============================================================================
// ApiTool — the agent-facing `api` surface (docs/api/TOOL.md)
//
// Grammar: reserved first-words (package, command, connection, files,
// download, upload, enable, disable, status) are management verbs; anything
// else is `api <package> <command...> [args-json]` — a package invocation.
// Unknown package/command errors always list what is available.
//
// v1 runtime surface (build order c): status, enable/disable, package
// create/read/delete, command read, files list, introspection, invocation.
// Registry download/upload (#61) arrive with the registry pipeline.
// ============================================================================

class ApiTool final : public IToolHandler {
public:
    explicit ApiTool(ApiRuntime* runtime) : m_runtime(runtime) {}

    void SetCurrentAgentId(const std::string& agentId) { m_currentAgentId = agentId; }

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

private:
    std::string HandleStatus(const std::vector<std::string>& tokens);
    std::string HandlePackage(const std::vector<std::string>& tokens);
    std::string HandleCommand(const std::vector<std::string>& tokens);
    std::string HandleFiles(const std::vector<std::string>& tokens);
    std::string HandleEnable(const std::string& name, bool enable);
    std::string HandleInvocation(const std::vector<std::string>& tokens,
                                 const std::string& restAfterPackage);
    std::string AvailablePackagesLine() const;

    ApiRuntime* m_runtime;
    std::string m_currentAgentId;
};

}  // namespace animus::kernel

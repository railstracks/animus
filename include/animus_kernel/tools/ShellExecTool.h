#pragma once

#include "animus_kernel/tools/ToolRegistry.h"

#include <string>

namespace animus::kernel {

// ============================================================================
// ShellExecTool — execute shell commands on the local node
// ============================================================================

class ShellExecTool : public IToolHandler {
public:
    /// @param workspaceRoot Default working directory for commands.
    ///                      Empty string means current working directory.
    explicit ShellExecTool(const std::string& workspaceRoot = "");

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

    /// Whether shell execution is enabled.
    void SetEnabled(bool enabled);

    /// Command allowlist (glob patterns). If non-empty, only matching commands run.
    void SetCommandAllowlist(const std::vector<std::string>& patterns);

    /// Command denylist (glob patterns). Matching commands are always blocked.
    void SetCommandDenylist(const std::vector<std::string>& patterns);

    /// Maximum allowed timeout in seconds.
    void SetMaxTimeout(int maxSeconds);

private:
    std::string m_workspaceRoot;
    bool m_enabled{true};
    std::vector<std::string> m_commandAllowlist;
    std::vector<std::string> m_commandDenylist;
    int m_maxTimeout{120};

    /// Check if a command is allowed by policy.
    bool IsCommandAllowed(const std::string& command) const;

    /// Simple glob pattern matching.
    bool GlobMatch(const std::string& text, const std::string& pattern) const;
};

} // namespace animus::kernel

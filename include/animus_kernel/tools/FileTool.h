#pragma once

#include "animus_kernel/tools/ToolRegistry.h"

#include <regex>
#include <string>

namespace animus::kernel {

// ============================================================================
// FileTool — read, write, edit, search, and manipulate files on the local node
// ============================================================================

class FileTool : public IToolHandler {
public:
    /// @param workspaceRoot The root directory for relative path resolution.
    ///                      Empty string means no workspace boundary is enforced.
    explicit FileTool(const std::string& workspaceRoot = "");

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

    /// Path allowlist (glob patterns). If non-empty, only matching paths are allowed.
    void SetPathAllowlist(const std::vector<std::string>& patterns);

    /// Path denylist (glob patterns). Matching paths are always blocked.
    void SetPathDenylist(const std::vector<std::string>& patterns);

    /// Whether to restrict file access to the workspace root.
    void SetRestrictToWorkspace(bool restrict);

private:
    std::string m_workspaceRoot;
    std::vector<std::string> m_pathAllowlist;
    std::vector<std::string> m_pathDenylist;
    bool m_restrictToWorkspace{true};

    // Existing actions
    ToolResult HandleRead(const std::string& path, int offset, int limit,
                          int page, int pageSize);
    ToolResult HandleWrite(const std::string& path, const std::string& content);
    ToolResult HandleEdit(const std::string& path, const std::string& oldText, const std::string& newText);

    // New actions (ticket 071)
    ToolResult HandleSearchLines(const std::string& path, const std::string& pattern,
                                  bool useRegex, int context, int maxMatches);
    ToolResult HandleFindBlock(const std::string& path, const std::string& phrase,
                               const std::string& blockStart, const std::string& blockEnd,
                               bool nesting, bool useRegex, int maxBlocks);
    ToolResult HandleInsertLines(const std::string& path, int atLine, const std::string& content);
    ToolResult HandleReplaceLines(const std::string& path, int startLine, int endLine,
                                  const std::string& content);
    ToolResult HandleDeleteLines(const std::string& path, int startLine, int endLine);
    ToolResult HandleAppendLines(const std::string& path, const std::string& content);

    // Helpers
    std::vector<std::string> ReadAllLines(const std::string& path, std::string* error);
    bool WriteAllLines(const std::string& path, const std::vector<std::string>& lines);

    /// Resolve a path: if relative, prepend workspace root.
    std::string ResolvePath(const std::string& path, const std::string& workspaceRoot) const;

    /// Validate that a resolved path is within allowed boundaries.
    bool IsPathAllowed(
        const std::string& resolvedPath,
        const std::string& workspaceRoot,
        bool restrictToWorkspace,
        const std::vector<std::string>& extraAllowlist,
        const std::vector<std::string>& extraDenylist) const;

    /// Simple glob pattern matching (supports * and ? wildcards).
    bool GlobMatch(const std::string& text, const std::string& pattern) const;
};

} // namespace animus::kernel

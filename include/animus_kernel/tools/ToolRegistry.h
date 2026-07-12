#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "animus_kernel/tools/ToolTypes.h"

namespace animus::kernel {

// ============================================================================
// IToolHandler — interface for tool implementations
// ============================================================================

class IToolHandler {
public:
    virtual ~IToolHandler() = default;

    /// Return the tool definition this handler implements.
    /// The definition includes the result mode (stream_to_user, deliver_to_model, or both).
    virtual ToolDefinition GetDefinition() const = 0;

    /// Execute the tool call and return the result.
    virtual ToolResult Execute(const ToolCall& call) = 0;

    /// Get the result mode for this tool. Default delegates to the definition.
    /// Override for tools that need dynamic result mode based on call context.
    virtual ToolResultMode GetResultMode() const {
        return GetDefinition().resultMode;
    }
};

// ============================================================================
// ToolRegistry — registration and lookup for tool handlers
// ============================================================================

class ToolRegistry {
public:
    /// Register a tool handler. Takes ownership.
    void Register(std::unique_ptr<IToolHandler> handler);

    /// Unregister a tool by name. Returns true if the tool was found and removed.
    bool Unregister(const std::string& name);

    /// Look up a handler by tool name. Returns nullptr if not found.
    IToolHandler* Find(const std::string& name) const;

    /// Get all registered tool definitions (for injection into LLM request).
    /// Only returns definitions for enabled tools.
    std::vector<ToolDefinition> GetAllDefinitions() const;

    /// Get all registered tool definitions including disabled ones.
    std::vector<ToolDefinition> GetAllDefinitionsIncludingDisabled() const;

    /// Check if a tool is registered.
    bool Has(const std::string& name) const;

    /// Number of registered tools (enabled + disabled).
    std::size_t Size() const { return m_handlers.size(); }

    /// Number of enabled tools.
    std::size_t EnabledCount() const;

    // --- Runtime enable/disable (ticket 099) ---

    /// Enable a tool at runtime. Returns false if tool not found.
    bool SetEnabled(const std::string& name, bool enabled);

    /// Check if a tool is currently enabled.
    bool IsEnabled(const std::string& name) const;

private:
    std::unordered_map<std::string, std::unique_ptr<IToolHandler>> m_handlers;
    std::vector<std::string> m_order; // registration order for deterministic listing
    std::unordered_map<std::string, bool> m_disabled; // name → true if disabled
};

} // namespace animus::kernel
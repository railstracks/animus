#pragma once

#include "animus_kernel/tools/ToolRegistry.h"

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace animus::kernel {

// ============================================================================
// CompositeTool — base class for plugin-registered composite tools
//
// Both InterfacesTool (042) and SocialTool (043) follow the same pattern:
// a single agent-facing tool with a merged schema built from registered plugins.
// This base class provides the shared infrastructure for plugin registration,
// schema merging, and action routing.
//
// Plugins register during Phase 1 (script loading). The merged schema is
// finalized during Phase 2 (after all scripts have loaded).
// ============================================================================

struct CompositePlugin {
    std::string id;                      // Plugin identifier (e.g. "bluesky")
    std::string name;                    // Human-readable name
    std::vector<std::string> capabilities; // e.g. {"read", "write", "search"}
    std::unordered_map<std::string, std::vector<ToolParameter>> actionSchemas; // action → params
    std::function<ToolResult(const ToolCall&)> handler;
};

class CompositeTool : public IToolHandler {
public:
    explicit CompositeTool(const std::string& toolName,
                           const std::string& description);

    // --- Plugin registration (Phase 1) ---

    /// Register a plugin. Called during Lua script loading.
    void RegisterPlugin(const CompositePlugin& plugin);

    /// Unregister a plugin by id. Returns true if found and removed.
    bool UnregisterPlugin(const std::string& id);

    /// Check if a plugin is registered.
    bool HasPlugin(const std::string& id) const;

    /// Get all registered plugin IDs.
    std::vector<std::string> GetPluginIds() const;

    // --- Schema finalization (Phase 2) ---

    /// Called after all plugins have registered. Finalizes the merged schema.
    void FinalizeSchema();

    /// Check if schema has been finalized.
    bool IsSchemaFinalized() const { return m_schemaFinalized; }

    // --- IToolHandler interface ---

    ToolDefinition GetDefinition() const override;
    ToolResult Execute(const ToolCall& call) override;

protected:
    /// Access a registered plugin by ID. Returns nullptr if not found.
    const CompositePlugin* GetPlugin(const std::string& id) const {
        auto it = m_plugins.find(id);
        return (it != m_plugins.end()) ? &(it->second) : nullptr;
    }

    /// Access all registered plugins.
    const std::unordered_map<std::string, CompositePlugin>& GetAllPlugins() const {
        return m_plugins;
    }

    // Subclasses implement routing logic:
    // How to determine which action a call is performing.
    virtual std::string GetActionFromCall(const ToolCall& call) const = 0;
    // How to determine which plugin should handle a call.
    virtual std::string GetPluginIdFromCall(const ToolCall& call) const = 0;
    // Subclasses add their own composite-level parameters (e.g. "list" action params).
    virtual std::vector<ToolParameter> GetCompositeParameters() const = 0;
    // Subclasses handle composite-level actions (list, etc.) themselves.
    virtual ToolResult HandleCompositeAction(const ToolCall& call,
                                              const std::string& action) = 0;

    /// Build the merged parameter schema from all registered plugins.
    /// Called by FinalizeSchema(). Subclasses can override for custom merging.
    virtual ToolDefinition BuildMergedDefinition() const;

private:
    std::string m_toolName;
    std::string m_description;
    std::unordered_map<std::string, CompositePlugin> m_plugins; // id → plugin
    ToolDefinition m_mergedDefinition;
    bool m_schemaFinalized{false};
};

} // namespace animus::kernel
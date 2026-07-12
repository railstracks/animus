#pragma once

#include "animus_kernel/tools/CompositeTool.h"

#include <set>
#include <string>

namespace animus::kernel {

class AgentConfigStore;
class ChannelManager;

// ============================================================================
// ChannelsTool — composite tool for channel integrations
//
// Provides a unified agent-facing surface for publishing, browsing, and
// interacting with channels. Platform adapters (Lua scripts)
// register via animus.register_social() during Phase 1 startup.
//
// The instance model allows agents to have multiple accounts on the same
// platform (e.g. "bluesky:personal" and "bluesky:work"). The adapter type
// is extracted from the platform_id prefix; the full platform_id is passed
// to the adapter handler so it can resolve per-instance credentials.
// ============================================================================

class ChannelsTool : public CompositeTool {
public:
    ChannelsTool();

    /// Set the config store for instance enumeration in list action.
    void SetConfigStore(AgentConfigStore* store) { m_configStore = store; }

    /// Set the channel manager for listing C++ connector channels.
    void SetChannelManager(ChannelManager* mgr) { m_channelManager = mgr; }

    /// Set the current agent ID for schema filtering.
    /// When set, GetDefinition() only includes adapters for channels
    /// this agent actually has configured. Clear with empty string.
    void SetCurrentAgentId(const std::string& agentId) { m_currentAgentId = agentId; }

    /// Set callback for registering agent-originated posts with the event router.
    /// Called when the agent creates a post so inbound replies route correctly.
    using PostCreatedCallback = std::function<void(
        const std::string& platformId, const std::string& postId,
        const std::string& sessionKey, const std::string& agentId)>;
    void SetPostCreatedCallback(PostCreatedCallback cb) { m_postCreatedCb = std::move(cb); }

    /// Override GetDefinition to set session_types (excluded from chat/wall).
    ToolDefinition GetDefinition() const override;

    /// Override Execute to intercept post results for session registration.
    ToolResult Execute(const ToolCall& call) override;

protected:
    /// Override BuildMergedDefinition to filter by agent's configured channels.
    ToolDefinition BuildMergedDefinition() const override;

    /// Get the set of adapter types configured for a specific agent.
    /// Returns all types if agentId is empty.
    std::set<std::string> GetAdapterTypesForAgent(const std::string& agentId) const;

    /// Extract the "action" field from the tool call arguments.
    std::string GetActionFromCall(const ToolCall& call) const override;

    /// Extract the adapter type from platform_id prefix.
    /// e.g. "bluesky:personal" → "bluesky"
    /// Falls back to checking arguments directly.
    std::string GetPluginIdFromCall(const ToolCall& call) const override;

    /// Extract the instance name from platform_id.
    /// e.g. "bluesky:personal" → "personal"
    std::string ExtractInstanceName(const std::string& platformId) const;

    /// Composite-level parameters: content, post_id, query, limit, extra
    std::vector<ToolParameter> GetCompositeParameters() const override;

    /// Handle composite-level actions: list (returns agent's configured instances)
    ToolResult HandleCompositeAction(const ToolCall& call,
                                      const std::string& action) override;

    AgentConfigStore* m_configStore{nullptr};
    ChannelManager* m_channelManager{nullptr};
    std::string m_currentAgentId;
    PostCreatedCallback m_postCreatedCb;
};

} // namespace animus::kernel

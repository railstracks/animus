#pragma once

#include <string>
#include <vector>

#include "animus_kernel/AgentStore.h"
#include "animus_kernel/NodeManager.h"
#include "animus_kernel/llm/LLMTypes.h"
#include "animus_kernel/tools/ToolTypes.h"
#include "animus_kernel/tools/ToolRegistry.h"

namespace animus::kernel {

struct SessionKey;
class SessionAccess;

/// Context injected into tool calls transparently.
///
/// Replaces the hidden __session_key / __agent_id / __policy / __config / __node
/// JSON arguments that were injected inline in ProcessResponse. By making this
/// a first-class struct, the injection contract is explicit and testable.
struct ToolExecutionContext {
    std::string sessionKey;     ///< Session key string (for session-scoped tools)
    std::string agentId;        ///< Agent ID (for agent-scoped tools)
    std::string toolConfigs;    ///< Per-agent tool configs JSON (file policy, links, feeds)
};

/// Result of executing a single tool call within a chain.
enum class ToolRouteResult {
    deliver_to_model,   ///< Result should feed back to LLM
    stream_to_user,     ///< Result goes directly to user
    both                ///< Result goes to user AND model
};

/// Executes tool calls and manages routing of results.
///
/// Extracted from ChainRunner::ProcessResponse to separate:
///   - Tool config injection (file policy, stored_links, rss)
///   - Session context injection (__session_key, __agent_id)
///   - Node routing (__node forwarding)
///   - Handler lookup and execution
///   - Result mode determination
///
/// ChainRunner delegates tool execution here, keeping the chain loop
/// focused on LLM interaction and session management.
class ToolExecutionService {
public:
    ToolExecutionService(
        ToolRegistry& tools,
        AgentStore* agentStore = nullptr,
        NodeManager* nodeManager = nullptr);

    /// Inject context (session key, agent ID, tool configs) into a tool call.
    /// This replaces the inline JSON injection that was scattered in ProcessResponse.
    static std::string InjectContext(
        const std::string& toolName,
        const std::string& argumentsJson,
        const ToolExecutionContext& ctx);

    /// Execute a single tool call.
    /// Handles: config injection, context injection, node routing, handler lookup.
    /// Returns the ToolResult and sets routeResult to indicate where output goes.
    ToolResult Execute(
        const llm::LLMToolCall& call,
        const ToolExecutionContext& ctx,
        ToolRouteResult* routeResult = nullptr) const;

    void SetAgentStore(AgentStore* store) { m_agentStore = store; }
    void SetNodeManager(NodeManager* nm) { m_nodeManager = nm; }

private:
    ToolRegistry& m_tools;
    AgentStore* m_agentStore{nullptr};
    NodeManager* m_nodeManager{nullptr};
};

} // namespace animus::kernel

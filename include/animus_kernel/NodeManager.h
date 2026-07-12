#pragma once

#include "animus_kernel/tools/ToolTypes.h"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace animus::kernel {

class IDataStore;
class IStatement;
class AgentStore;

// ============================================================================
// NodeManager — manages external node connections and tool call forwarding
//
// Nodes connect to the central server via WebSocket with an auth token.
// The NodeManager tracks connected nodes, validates tokens, and forwards
// tool calls from ChainRunner to the appropriate node.
// ============================================================================

struct NodeInfo {
    std::string name;
    std::string hostname;
    std::string os_info;
    std::vector<std::string> tools;   // tools exposed by this node
    bool connected{false};
    int64_t connected_at_unix_ms{0};
    int64_t last_seen_unix_ms{0};
};

struct NodeToken {
    int64_t id{0};
    std::string token;         // plaintext (only shown once at creation)
    std::string token_hash;    // stored hash for comparison
    std::string description;
    int64_t created_at_unix_ms{0};
    bool revoked{false};
};

// Callback for sending a tool call to a node and receiving the result.
// The NodeManager stores these per-connected-node, registered when the
// WebSocket connection is established.
using NodeExecuteFn = std::function<ToolResult(const ToolCall&)>;

class NodeManager {
public:
    explicit NodeManager(IDataStore* store);

    // --- Token management ---
    // Generate a new auth token. Returns the plaintext token (shown once).
    std::string GenerateToken(const std::string& description);
    // Validate a token against stored hashes. Returns token ID or -1.
    int64_t ValidateToken(const std::string& token) const;
    // List all tokens (hash only, not plaintext)
    std::vector<NodeToken> ListTokens() const;
    // Revoke a token by ID
    bool RevokeToken(int64_t tokenId);

    // --- Node connection management ---
    // Register a connected node with its execute callback
    void RegisterNode(const std::string& name, const NodeInfo& info, NodeExecuteFn execFn);
    // Remove a disconnected node
    void RemoveNode(const std::string& name);
    // List all connected nodes
    std::vector<NodeInfo> ListNodes() const;
    // Get info for a specific node
    std::optional<NodeInfo> GetNode(const std::string& name) const;
    // Execute a tool call on a specific node. Returns result or error.
    ToolResult ExecuteOnNode(const std::string& nodeName, const ToolCall& call);

    // --- Per-agent node access ---
    // Check if an agent is allowed to use a node
    bool IsNodeAllowedForAgent(const std::string& nodeName, const std::string& agentId) const;
    // List nodes visible to an agent
    std::vector<NodeInfo> ListNodesForAgent(const std::string& agentId) const;

private:
    void EnsureSchema();
    void LoadTokens();

    IDataStore* m_store;
    AgentStore* m_agentStore{nullptr};  // Optional, for per-agent access control
    mutable std::mutex m_mutex;

    // Connected nodes: name → (info, execute function)
    struct ConnectedNode {
        NodeInfo info;
        NodeExecuteFn execFn;
    };
    std::unordered_map<std::string, ConnectedNode> m_nodes;

    // Token cache: hash → token record
    std::unordered_map<std::string, NodeToken> m_tokensByHash;

public:
    // Wire AgentStore for per-agent node access control (optional)
    void SetAgentStore(AgentStore* store) { m_agentStore = store; }
};

} // namespace animus::kernel

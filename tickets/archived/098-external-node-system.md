# Ticket 098 — External Node System

**Status:** Open  
**Priority:** Medium  
**Created:** 2026-06-22  
**Updated:** 2026-06-22  
**Depends on:** None  
**Component:** Infrastructure / Tool System

## Summary

Allow external machines to connect to a central Animus server as "nodes" — lightweight daemons that execute tool calls on behalf of the central server. This enables agents to run commands, access files, and interact with hardware on remote machines without the central server needing direct network access to those machines.

## Motivation

Currently, all tool execution happens on the machine running the Animus daemon. For agents that need to:
- Execute commands on a developer's workstation
- Access files on a home server
- Interact with hardware (cameras, sensors, IoT devices)
- Run code in a specific environment (Docker, GPU, specific OS)

...the only option is to run the full Animus daemon on that machine, including LLM providers, scheduler, and channels. This is heavyweight and insecure — the remote machine has full agent capabilities when it only needs tool execution.

External nodes solve this: a lightweight `--node` daemon connects to the central server, receives tool calls, executes them, and returns results. The central server's agent decides when and how to use each node.

## Design

### Architecture

```
┌─────────────────────┐     WebSocket      ┌──────────────────┐
│   Central Server    │◄──────────────────►│   Node Daemon    │
│                     │    (auth token)    │  (--node mode)   │
│  ┌───────────────┐  │                    │                  │
│  │  AgentKernel   │  │                    │  ┌─────────────┐ │
│  │  + ChainRunner │  │   Tool calls      │  │ ToolServer  │ │
│  │  + NodeManager │──┼──────────────────►│  │ (exec only) │ │
│  │                │◄─┼───────────────────│  │             │ │
│  └───────────────┘  │   Tool results     │  └─────────────┘ │
└─────────────────────┘                    └──────────────────┘
```

### Node daemon (`--node` mode)

Launch with:
```bash
animusd --node --server-url=wss://animus.example.com:8080 --token=<auth_token> --name=workstation
```

In node mode, the daemon:
- **Does NOT load:** LLM providers, chain runner, scheduler, channels, consolidation
- **Does NOT create:** sessions, agents, or memory stores
- **Does load:** Tool handlers (exec, file, browser, image, etc.) based on local config
- **Connects** to the central server via WebSocket with its auth token
- **Registers** with its name, available tools, and capabilities
- **Listens** for tool call requests, executes locally, returns results
- **Reconnects** automatically if connection drops

### Authentication

1. Admin pre-generates auth tokens on the server via admin API:
   ```
   POST /api/v1/nodes/tokens
   {"description": "Workstation nodes", "count": 1}
   → {"tokens": ["***"], "description": "Workstation nodes"}
   ```

2. A token may be used by one node or shared across multiple nodes — this is the admin's choice. The token authenticates the connection; the node identifies itself during the WebSocket handshake.

3. Node daemon launched with the token. Connection authenticated via:
   - HTTP header: `Authorization: Bearer ***`

4. Server validates token against its registered tokens list. Invalid or revoked tokens are rejected.

5. Tokens can be revoked at any time. Revocation immediately disconnects any nodes using that token.

### Remote tool execution via `__node` parameter

Rather than wrapping tool calls inside a proxy tool (which creates nested JSON escaping problems — the same issue as SSH command quoting), tool calls are routed at the **ChainRunner level** via a `__node` parameter:

```json
{"action": "file_write", "source_path": "test.md", "content": "hello\nworld", "__node": "workstation"}
```

The `__node` parameter works the same way as `__agent_id` and `__session_key` — it's injected into the tool call context. The ChainRunner intercepts it before tool lookup:

```cpp
// In ChainRunner::ExecuteToolCall:
std::string nodeName = ExtractParam(call.arguments, "__node");
if (!nodeName.empty()) {
    // Forward to remote node
    return m_nodeManager->ExecuteOnNode(nodeName, call);
}
// Normal local execution
auto* handler = m_tools.Find(call.name);
```

Benefits:
- **No nested escaping** — the tool call structure is preserved end-to-end. The node receives the exact same `ToolCall` and executes it with its local handler.
- **Every tool automatically supports remote execution** without schema changes.
- **Consistent with existing `__`-prefixed parameters** (`__agent_id`, `__session_key`).
- The agent learns about available nodes via a `node` tool, then adds `__node` to any tool call it wants to route remotely.

### Node discovery tool

A `node` tool for discovery and inspection (not for executing tool calls — that happens via `__node` parameter):

```
action: "list" — list connected nodes available to this agent
action: "info" — show node details (tools, uptime, hostname, OS)
```

**`list` example:**
```json
{"action": "list"}
→ {
  "nodes": [
    {"name": "workstation", "status": "connected", "tools": ["exec", "file", "browser"], "hostname": "melvin-E33011", "os": "Linux Mint 22", "uptime_seconds": 3600},
    {"name": "home-server", "status": "connected", "tools": ["exec", "file"], "hostname": "nas-01", "os": "Ubuntu 24.04", "uptime_seconds": 86400}
  ]
}
```

### Per-agent node access control

Agents don't automatically see all connected nodes. The admin configures which nodes each agent can access:

```
Agent config:
  allowed_nodes: ["workstation", "home-server"]
```

If an agent has no `allowed_nodes` configured, it sees no nodes (default deny). The `node:list` action only returns nodes the agent is permitted to use. A `__node` parameter referencing a disallowed node returns an error.

### Per-node tool allowlist

Each node daemon specifies which tools it exposes via local config:

```yaml
# node-config.yaml
name: workstation
tools:
  - exec
  - file
  - browser
  - image
  - web_search
```

A node with only `file` (read-only) configured cannot execute `exec` commands even if the agent tries. The tool allowlist is enforced both on the node (refuses to execute disallowed tools) and visible to the server (node reports its available tools during registration).

### NodeManager (central server side)

```cpp
class NodeManager {
public:
    // Called when WebSocket connects with valid token
    void RegisterNode(const std::string& name, const NodeConnection& conn);

    // Called on disconnect
    void RemoveNode(const std::string& name);

    // List connected nodes visible to a specific agent
    std::vector<NodeInfo> ListNodesForAgent(const std::string& agentId) const;

    // Execute a tool call on a specific node (blocks until result returns)
    ToolResult ExecuteOnNode(const std::string& nodeName,
                             const ToolCall& call);
private:
    std::unordered_map<std::string, NodeConnection> m_nodes;
    mutable std::mutex m_mutex;
};
```

`ExecuteOnNode` sends the full `ToolCall` as JSON over the node's WebSocket:
```json
{"type": "tool_call", "call_id": "req_123", "tool": "file_write", "arguments": {"action": "file_write", "source_path": "test.md", "content": "hello\nworld"}}
```

The node daemon receives it, looks up the local handler, executes, sends back:
```json
{"type": "tool_result", "call_id": "req_123", "success": true, "output": "{\"id\": 5, \"action\": \"created\"}"}
```

### Admin API

```
POST   /api/v1/nodes/tokens             — generate auth token(s)
GET    /api/v1/nodes/tokens             — list tokens (masked)
DELETE /api/v1/nodes/tokens/{token}      — revoke token
GET    /api/v1/nodes                     — list connected nodes
GET    /api/v1/nodes/{name}              — node details
PUT    /api/v1/agents/{id}/allowed_nodes — configure per-agent node access
```

### Admin UI

New `/nodes` page:
- Token management (generate, list, revoke)
- Connected nodes with status, uptime, available tools, hostname/OS
- Per-agent node access configuration (in agent form)
- Node details view (last seen, tool list, recent executions)

### WebSocket protocol

Connection: standard WebSocket with `Authorization: Bearer <token>` header.

Message types (server → node):
- `{"type": "tool_call", "call_id": "...", "tool": "...", "arguments": {...}}`

Message types (node → server):
- `{"type": "register", "name": "...", "tools": ["exec", "file", ...], "hostname": "...", "os": "..."}`
- `{"type": "tool_result", "call_id": "...", "success": true/false, "output": "...", "error": "..."}`
- `{"type": "heartbeat"}`

### Security considerations

- **Auth tokens are server-generated**, reusable across nodes at admin's discretion. Revoking a token disconnects all nodes using it.
- **Per-agent access control** — agents only see nodes in their `allowed_nodes` list. Default deny.
- **Per-node tool allowlist** — node exposes only configured tools. Enforced locally.
- **Node executes with daemon process permissions** — the node operator controls what the agent can do on that machine.
- **No inbound network required** — node connects outbound. NAT-friendly.
- **Rate limiting** — server can limit tool calls per node per minute.

## Implementation phases

### Phase 1: Core node system
- `--node` daemon mode (minimal boot, tool loading, WebSocket client)
- `NodeManager` on central server
- `__node` parameter interception in ChainRunner
- `node` discovery tool (`list`, `info`)
- Auth token generation + validation
- Admin API endpoints (tokens + node listing)

### Phase 2: Admin UI + access control
- `/nodes` page with token management and node status
- Per-agent `allowed_nodes` configuration in agent form
- Connection health monitoring + auto-reconnect
- Execution history

### Phase 3: Advanced features
- File transfer between server and nodes
- Streaming tool output (for long-running commands)
- Node groups (execute on multiple nodes)
- Per-node tool configuration in admin UI

## Files to create/modify

| File | Description |
|------|-------------|
| `include/animus_kernel/NodeManager.h` | Node connection management |
| `src/kernel/NodeManager.cpp` | Implementation |
| `include/animus_kernel/tools/NodeTool.h` | Node discovery tool |
| `src/kernel/tools/NodeTool.cpp` | Tool implementation |
| `src/kernel/NodeDaemon.cpp` | `--node` mode entry point |
| `src/kernel/chain/ChainRunner.cpp` | `__node` parameter interception |
| `src/kernel/admin/internal/AdminServerRoutesNodes.inc` | Admin API |
| `admin-ui/src/views/NodesView.vue` | Nodes admin page |
| `include/animus_kernel/AgentStore.h` | Add `allowed_nodes` to Agent struct |

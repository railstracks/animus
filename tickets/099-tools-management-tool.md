# Ticket 099 — Tools Management Tool

**Status:** Open  
**Priority:** Low  
**Created:** 2026-06-22  
**Depends on:** None  
**Component:** Tools

## Summary

A "tools" tool that allows agents to inspect, enable, and disable their available tools at runtime. This gives agents self-awareness of their tool surface and the ability to tailor their active toolset per task.

## Motivation

Currently, the tool set for an agent is configured statically via `enabled_tools` in the agent config. The agent has no way to:

1. **Discover what tools are available** — the agent sees tool definitions in the LLM prompt, but can't programmatically list or inspect them.
2. **Enable/disable tools dynamically** — if an agent only needs `memory` and `consolidation` for a specific task, it can't disable `email`, `social`, etc. to reduce prompt token usage.
3. **Inspect tool schemas** — the agent can't ask "what parameters does the file_write action accept?" without reading the full tool definition in its prompt.

Reducing active tool count saves significant prompt tokens — each tool definition with parameters and descriptions consumes 200-500 tokens. An agent with 12 tools enabled spends ~4000 tokens just on tool schemas. Disabling unused tools for specific tasks reclaims that budget.

## Design

### Actions

```
action: "list" — list all available tools (enabled + disabled) with descriptions
action: "info" — detailed schema for a specific tool
action: "enable" — enable a tool for the current agent
action: "disable" — disable a tool for the current agent
action: "enabled" — list currently enabled tools only
```

### `list` action

Returns all registered tools with their enabled state:

```json
{"action": "list"}
→ {
  "tools": [
    {"name": "memory", "description": "Search and manage memory across domains", "enabled": true, "actions": 12},
    {"name": "consolidation", "description": "Memory consolidation and observation creation", "enabled": true, "actions": 15},
    {"name": "email", "description": "Send and manage email via AgentMail", "enabled": false, "actions": 14},
    {"name": "social", "description": "Post and interact on social platforms", "enabled": false, "actions": 8}
  ],
  "total": 4,
  "enabled": 2
}
```

### `info` action

Returns the full definition for a specific tool:

```json
{"action": "info", "tool": "memory"}
→ {
  "name": "memory",
  "description": "Search episodic, semantic, and file memory. Manage memory files.",
  "actions": ["search", "recall", "pin", "inspect", "file_list", "file_read", "file_headings", "file_write", "file_delete"],
  "parameters": [
    {"name": "action", "type": "string", "required": true, "description": "Memory action to perform"},
    {"name": "query", "type": "string", "required": false, "description": "Search query"},
    ...
  ]
}
```

### `enable` / `disable` actions

```json
{"action": "enable", "tool": "email"}
→ {"tool": "email", "enabled": true}

{"action": "disable", "tool": "email"}
→ {"tool": "email", "enabled": false}
```

### Implementation

The `Agent` struct already has an `enabled_tools` vector. The `ToolRegistry` has `Register`, `Unregister`, `Find`, `GetAllDefinitions`, and `Has`. We need:

1. **Per-agent tool enable/disable state** — track which tools are enabled beyond the static `enabled_tools` config. This becomes the source of truth for which tool definitions get injected into the LLM prompt.

2. **Runtime mutation** — `ToolsTool` handler calls `ToolRegistry::SetEnabled(name, enabled)` which adds/removes the tool from the active set used by `GetAllDefinitions()`.

3. **Prompt assembly** — `ChainRunner` already calls `m_tools.GetAllDefinitions()` when building the LLM request. If disabled tools are excluded from this call, they automatically disappear from the agent's prompt.

4. **Persistence** — changes to enabled/disabled state should persist to the agent config so they survive session restarts. Optionally, make them session-scoped (reset on restart) to allow task-specific tool profiles.

### Security

- The agent can only enable/disable tools that exist in its `enabled_tools` allowlist — it can't enable a tool that wasn't configured by the admin.
- Or: the agent can disable any tool, but can only re-enable tools from its original config. This prevents an agent from enabling tools it was intentionally restricted from.
- The `tools` tool itself cannot be disabled (to prevent the agent from locking itself out).

## Implementation

### ToolRegistry changes

```cpp
class ToolRegistry {
public:
    // Existing methods...

    // Enable/disable tools at runtime
    void SetEnabled(const std::string& name, bool enabled);
    bool IsEnabled(const std::string& name) const;

    // GetAllDefinitions now returns only enabled tools
    std::vector<ToolDefinition> GetAllDefinitions() const;

private:
    std::unordered_map<std::string, std::unique_ptr<IToolHandler>> m_handlers;
    std::unordered_map<std::string, bool> m_enabled; // name → enabled state
    std::vector<std::string> m_order;
};
```

### Files to create/modify

| File | Description |
|------|-------------|
| `include/animus_kernel/tools/ToolsTool.h` | Tool header |
| `src/kernel/tools/ToolsTool.cpp` | Tool implementation |
| `include/animus_kernel/tools/ToolRegistry.h` | Add SetEnabled, IsEnabled, filter GetAllDefinitions |
| `src/kernel/tools/ToolRegistry.cpp` | Implementation of enabled state |

## Testing

1. `list` → returns all tools with correct enabled states
2. `info("memory")` → returns full schema
3. `disable("email")` → email disappears from next prompt's tool list
4. `enable("email")` → email reappears
5. `disable("tools")` → error: cannot disable tools tool
6. Verify prompt token count drops when tools are disabled

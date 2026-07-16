# Ticket 117: Agent Self-Management Tool

## Summary

Add an `agent` tool that lets agents view and update their own settings (avatar, description, identity). This enables agents to manage their own presentation and self-expression without requiring admin panel access.

## Motivation

Agents currently have no way to modify their own configuration. An agent that wants to change its avatar, refine its description, or revise its identity/system prompt must ask a human to do it through the admin UI. This breaks the cognitive persistence model — the agent's self-representation should be within its own agency.

## Actions

### `view`
Returns the agent's own settings:
- `name` (read-only)
- `avatar`
- `description`
- `identity` (truncated to first 500 chars in summary, full on request)
- `model` (provider + model_id, read-only)
- `created_at_unix_ms` (read-only)

### `update`
Accepts optional fields, only updates what's provided:
- `avatar` (string — emoji or short text)
- `description` (string)
- `identity` (string — full system prompt text)

Rejects all other fields (model, provider, budget, tools, intake_interval, etc.).

## Config Flag

`allow_agent_self_identity_edit` (default: `true`)

When `false`, the `update` action rejects `identity` changes. `avatar` and `description` remain editable. This lets operators lock down identity rewriting on production agents without restricting cosmetic changes.

Stored in `KernelConfig::AgentConfig` alongside existing fields.

## Security

- Agent ID always from `__agent_id` (ChainRunner-injected), never from params
- Only self-modification — no ability to view or edit other agents
- Fields outside the whitelist are rejected with an error listing allowed fields
- Tool must be explicitly enabled per agent (like all tools)

## Implementation

- `AgentTool` class in `src/kernel/tools/AgentTool.cpp` + header
- Register in `AgentKernel.cpp` tool registry
- Uses existing `AgentStore::Update` / `ApplyAgentEntityPatch` paths
- Config flag checked in `HandleUpdate` before allowing identity changes

## Acceptance Criteria

- [ ] `agent` tool with `view` and `update` actions registered
- [ ] `view` returns agent's own settings (filtered to safe fields)
- [ ] `update` accepts avatar, description, identity; rejects all others
- [ ] `allow_agent_self_identity_edit` config flag gates identity edits
- [ ] Agent ID sourced from `__agent_id`, never from params
- [ ] Tool registered in AgentKernel
- [ ] Added to default enabled tools list in agent form

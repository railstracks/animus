# Ticket 042 — Interfaces Tool

**Priority:** P2 (extends agent reach beyond passive receive — enables outbound communication)
**Status:** Open
**Dependencies:** Ticket 039 (Lua Scripting — for plugin-based Interface registration)
**Updated:** 2026-05-14

## Summary

A composite tool that lets agents query, initiate, and manage real-time communication through configured Interfaces (Signal, WhatsApp, IRC, webchat, LINE, etc.). Interfaces register their transport-specific schemas into this composite tool at startup. The agent sees a single `interfaces` tool whose available actions reflect what's actually configured and connected.

## Motivation

Agents currently communicate through Interfaces passively — the framework routes inbound messages to the agent, and the agent's reply is automatically sent back through the same channel. The agent has no ability to:

- See which Interfaces are connected
- Initiate a message to a contact on a specific Interface
- Watch for messages matching a pattern on a specific Interface
- Choose a different channel for an outgoing message

This is the agent-facing counterpart to the Interface transport layer. The transport handles bytes; the tool gives the agent agency over communication.

## Distinction from Sessions Tool

- **Sessions tool** (041): operates on sessions the framework already knows about. Search, reply, notes. The transport is implicit.
- **Interfaces tool** (this): operates on the transport layer itself. Which channels are live, who's reachable, initiating new conversations.

An Interface can create a session (when the agent sends a new outbound message to a contact that doesn't have an active session). The sessions tool then manages that session.

## Composite Architecture

The `interfaces` tool is a shell. Each configured Interface plugin registers its schema:

```
Interface: signal
  - send(to, message)
  - list_contacts()
  - watch(pattern, callback)

Interface: whatsapp
  - send(to, message)
  - list_contacts()

Interface: irc
  - send(channel, message)
  - join(channel)
  - list_channels()
```

The composite tool merges these into a unified interface:

```json
{
  "name": "interfaces",
  "actions": [
    "list — show configured interfaces and connection status",
    "contacts — list reachable contacts for an interface",
    "send — send a message to a contact on a specific interface",
    "watch — register a pattern matcher on an interface (future)"
  ]
}
```

### Action routing

`send` requires an `interface_id` parameter that selects the registered plugin. The composite tool validates the interface is connected, then delegates to the plugin's send implementation.

## Tool Interface

**`list`**
- Returns all configured interfaces with: id, type, status (connected/disconnected), contact count

**`contacts`**
- Params: `interface_id`
- Returns reachable contacts/groups for that interface

**`send`**
- Params: `interface_id`, `to` (contact identifier), `message` (text)
- Creates a new outbound session if none exists with this contact
- Returns: `session_key` of the created/updated session

**`watch`** (v2)
- Params: `interface_id`, `pattern` (regex or keyword), `callback_action`
- Registers a background watcher that triggers when an inbound message matches
- Callback could be: notify agent, auto-reply, log to diary

## Security Model

Agents should not have unrestricted access to all configured interfaces. Proposed policy:

- **Allowlist per agent**: each agent config specifies which interfaces it can use
- **Outbound gating**: agent can only initiate to contacts in its allowlist (or reply to anyone who initiated contact)
- **No raw access**: agent never sees API keys, auth tokens, or transport internals

Configuration example:
```yaml
agent:
  interfaces:
    allowed: [signal, whatsapp]
    signal:
      can_initiate: true
      allowed_contacts: ["thomas", "melvin"]
    whatsapp:
      can_initiate: false  # reply-only
```

## Plugin Registration Protocol

Lua Interface plugins register themselves at startup:

```lua
function register(registry)
  registry.register_interface({
    id = "signal",
    name = "Signal Messenger",
    actions = {"send", "list_contacts"},
    schema = {
      send = { to = "string", message = "string" },
      list_contacts = {}
    },
    handler = signal_handler
  })
end
```

The C++ composite tool inspects the registry at tool-definition time to build the merged parameter schema.

## Admin API

```
GET    /api/v1/interfaces                           → list configured interfaces + status
GET    /api/v1/interfaces/{id}/contacts             → list contacts for interface
POST   /api/v1/interfaces/{id}/send                 → send message (admin override)
GET    /api/v1/agents/{id}/interfaces               → agent's allowed interfaces
PATCH  /api/v1/agents/{id}/interfaces               → update agent interface permissions
```

## Acceptance Criteria

- [ ] `InterfacesTool` composite class in tool registry
- [ ] `list` action returns configured interfaces with connection status
- [ ] `contacts` action returns reachable contacts per interface
- [ ] `send` action routes through the correct Interface plugin
- [ ] Sending creates a new session if none exists with the target contact
- [ ] Agent allowlist enforcement — agents can only use permitted interfaces
- [ ] Lua plugin registration protocol for Interface plugins
- [ ] Admin API for interface listing, contact querying, and permission management
- [ ] All existing tests continue to pass
- [ ] New tests: composite routing, allowlist enforcement, session creation on send

## Key Questions

- Should `watch` be in v1 or deferred? (Suggestion: defer — it adds significant complexity)
- How to handle interfaces that go offline mid-conversation? Queue and retry?
- Should the agent see transport errors (delivery failed) or just success/failure?
- Can an agent have multiple sessions with the same contact on the same interface?
- How does this interact with the existing OpenClaw-style message routing?

## Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Plugin registration timing — tool schema built before plugins load | Medium | Two-phase: plugins register during startup, tool schema finalized after all plugins loaded |
| Agent sends to wrong contact on sensitive interface | Medium | Allowlist enforcement; confirmation for first-time outbound to a contact |
| Interface goes offline during send | Medium | Return error status; don't auto-retry without agent decision |
| Too many interfaces bloat tool schema | Low | Schema is dynamic — only configured interfaces appear |

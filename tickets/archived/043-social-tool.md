# Ticket 043 — Social Tool

**Priority:** P2 (composite tool framework — unblocks Bluesky and future platform integrations)
**Status:** ✅ Complete
**Dependencies:** Ticket 039 (Lua Scripting — register_social, CompositeTool base) ✅
**Updated:** 2026-05-19

## Summary

A composite tool that provides a unified agent-facing surface for social media and publishing platforms. Platform adapters register via `animus.register_social()` (Ticket 039) during Phase 1 startup. The composite merges their schemas and routes actions to the correct adapter. The agent sees a single `social` tool — it doesn't need to know which platforms are configured.

This ticket covers **only the composite wrapper**. Individual platform integrations (Bluesky, Mastodon, Twitter, etc.) are separate tickets that register into this composite via Lua scripts.

## Motivation

Social media and publishing are distinct from chat interfaces:

| Aspect | Interface (chat) | Social (publishing) |
|--------|------------------|---------------------|
| Direction | Bidirectional, real-time | Outbound-dominant, near-time |
| Audience | 1:1 or small group | Public or followers |
| Expectation | Immediate reply | No immediate reply expected |
| Persistence | Ephemeral to thread-persistent | Permanent |
| Agent mindset | "I'm talking to someone" | "I'm posting something" |

The agent needs to know when it's in a conversation vs. when it's broadcasting. This tool owns that distinction.

## Multi-Tenant Instance Model

A platform adapter is **stateless code** — the Lua script defines how to talk to a platform's API. It is not tied to any specific account.

Agents own **instances** of platforms. An instance is a named configuration containing credentials and settings for one specific account. An agent can have zero, one, or many instances of the same platform type.

```
Agent "kestrel"
├── bluesky:personal    → handle: kestrel.bsky.social, app-password: xxxx
├── bluesky:client-foo  → handle: foo-brand.bsky.social, app-password: yyyy
├── mastodon:tech       → instance: mastodon.social, token: zzzz
└── twitter:main        → handle: @kestrel_dev, api-key: wwww

Agent "sable"
├── bluesky:default     → handle: sable-finsight.bsky.social, app-password: aaaa
└── (no twitter or mastodon)
```

Key properties:
- The `platform_id` in tool calls addresses a specific instance, not a platform type (e.g. `bluesky:personal`, not just `bluesky`)
- Each instance has its own credentials, stored in per-agent config
- Multiple agents can use the same platform type with different accounts
- A single agent can use multiple accounts on the same platform
- Instances are configured by the admin, not by the agent itself

### Instance configuration storage

Instance configs are stored in agent-scoped config with a namespaced key convention:

```
config.get("social.bluesky:personal.handle")       → "kestrel.bsky.social"
config.get("social.bluesky:personal.app_password")  → "xxxx-xxxx-xxxx-xxxx"
config.get("social.bluesky:personal.pds")           → "https://bsky.social"
config.get("social.bluesky:client-foo.handle")      → "foo-brand.bsky.social"
config.get("social.bluesky:client-foo.app_password") → "yyyy-yyyy-yyyy-yyyy"
```

The adapter handler receives the `platform_id` (e.g. `bluesky:personal`) and looks up its credentials from the `social.{platform_id}.*` config namespace. This is transparent to the composite tool — it just routes.

## Architecture

```
SocialTool (C++ — CompositeTool subclass)
├── Merged schema from all registered adapter types
├── Action routing: extract platform_id + action from ToolCall
├── Composite-level actions: list instances, get capabilities
└── Passes platform_id to adapter handler (adapter resolves credentials)

Lua Adapters (stateless code, register via animus.register_social)
├── bluesky.lua   → Ticket 050 (defines how to talk Bluesky API)
├── mastodon.lua  → future ticket
├── twitter.lua   → future ticket
└── ...

Agent Config (per-agent, per-instance credentials)
├── social.bluesky:personal.*    → Kestrel's personal Bluesky account
├── social.bluesky:client-foo.*  → Kestrel's client Bluesky account
└── social.mastodon:tech.*       → Kestrel's Mastodon account
```

The C++ side is a thin routing layer. Adapters are stateless platform logic. All state (credentials, tokens) lives in per-agent config.

## Tool Interface

```json
{
  "name": "social",
  "description": "Publish, browse, and interact with social media platforms. Use 'list' to see available instances.",
  "parameters": [
    { "name": "action", "type": "string", "required": true,
      "description": "One of: list, post, browse, reply, delete, search, like, get_notifications" },
    { "name": "platform_id", "type": "string", "required": false,
      "description": "Instance to use (e.g. 'bluesky:personal'). Required for all actions except 'list'. Use 'list' to see available instances." },
    { "name": "content", "type": "string", "required": false },
    { "name": "post_id", "type": "string", "required": false },
    { "name": "query", "type": "string", "required": false },
    { "name": "limit", "type": "integer", "required": false },
    { "name": "extra", "type": "object", "required": false,
      "description": "Platform-specific parameters passed through to the adapter" }
  ]
}
```

### Actions

**`list`** — List this agent's configured social instances with their platform type, status, and capabilities. No `platform_id` required.

**`post`** — Publish content. Params: `platform_id`, `content`, `extra`.

**`reply`** — Reply to a specific post. Params: `platform_id`, `post_id`, `content`.

**`like`** — Like/react to a post. Params: `platform_id`, `post_id`.

**`browse`** — Retrieve recent posts. Params: `platform_id`, `extra.source`, `limit`.

**`search`** — Search public posts. Params: `platform_id`, `query`, `limit`.

**`delete`** — Remove a published post. Params: `platform_id`, `post_id`.

**`get_notifications`** — Fetch recent notifications. Params: `platform_id`, `limit`.

## Implementation

### SocialTool class

```cpp
class SocialTool : public CompositeTool {
public:
    SocialTool();

protected:
    // Action routing: extract "action" from ToolCall arguments
    std::string GetActionFromCall(const ToolCall& call) const override;
    // Instance routing: extract "platform_id" from ToolCall arguments
    // The platform_id is an instance name like "bluesky:personal".
    // The adapter type is extracted from the prefix before ':'.
    std::string GetPluginIdFromCall(const ToolCall& call) const override;
    // Composite-level parameters (action, platform_id, extra)
    std::vector<ToolParameter> GetCompositeParameters() const override;
    // Handle composite-level actions (list)
    ToolResult HandleCompositeAction(const ToolCall& call,
                                      const std::string& action) override;
};
```

### Instance routing

When the agent calls `social.post({platform_id: "bluesky:personal", content: "Hello"})`:

1. `GetPluginIdFromCall` extracts `platform_id` = `"bluesky:personal"`
2. The adapter type is the prefix: `"bluesky"`
3. The composite looks up the registered `"bluesky"` adapter
4. The full `platform_id` (`"bluesky:personal"`) is passed to the adapter handler
5. The adapter handler reads credentials from `config.get("social.bluesky:personal.*")`
6. The adapter makes the authenticated API call

### Phase 2 wiring

After all Lua scripts have registered their adapter types during Phase 1:

1. `SocialTool::FinalizeSchema()` merges all registered adapter schemas into a unified parameter set
2. `GetAllDefinitions()` reflects the complete tool surface
3. The LLM sees a single `social` tool — the `list` action tells it which instances are available

### Plugin registration contract

Lua adapters register the adapter **type** via `animus.register_social()`:

```lua
animus.register_social({
    id = "bluesky",
    name = "Bluesky",
    capabilities = {"read", "write", "search"},
    actions = {"post", "reply", "like", "browse", "search", "get_notifications", "delete"},
    schema = {
        post = {
            { name = "content", type = "string", required = true },
        },
        -- ... per-action schemas
    },
    handler = function(args)
        -- args.platform_id is the instance, e.g. "bluesky:personal"
        -- Look up credentials: config.get("social." .. args.platform_id .. ".handle")
        -- Route to the appropriate Bluesky API call
    end
})
```

The handler receives the full `platform_id` (instance name) and resolves credentials from agent config. The adapter is stateless — it can serve any number of instances.

### Admin API for instance management

```
GET    /api/v1/agents/{id}/social                        → list agent's social instances
POST   /api/v1/agents/{id}/social                        → add a social instance
DELETE /api/v1/agents/{id}/social/{platform_id}          → remove an instance
GET    /api/v1/agents/{id}/social/{platform_id}/status   → test connection / auth status
```

Adding an instance writes credentials to the agent's config under `social.{platform_id}.*`. Removing an instance clears those config keys.

## Files

**New:**
- `include/animus_kernel/tools/SocialTool.h`
- `src/kernel/tools/SocialTool.cpp`

**Modified:**
- `src/kernel/AgentKernel.cpp` — register SocialTool in tool registry

## Acceptance Criteria

- [ ] `SocialTool` class extending `CompositeTool`
- [ ] `list` action returns agent's configured instances with type, capabilities, and status
- [ ] `post` action routes to correct adapter via `platform_id` instance name
- [ ] `reply` action routes with post context
- [ ] `like` action routes to correct adapter
- [ ] `browse` action routes with source/limit parameters
- [ ] `search` action routes with query/limit parameters
- [ ] `delete` action routes to correct adapter
- [ ] `get_notifications` action routes to correct adapter
- [ ] Unknown `platform_id` returns clear error
- [ ] Adapter type extracted from `platform_id` prefix (e.g. `"bluesky"` from `"bluesky:personal"`)
- [ ] Full `platform_id` passed to adapter handler for credential lookup
- [ ] Schema finalization after Phase 1 registration
- [ ] Admin API for instance CRUD
- [ ] Unit tests for routing logic (mock adapters)
- [ ] Integration test with a stub Lua adapter
- [ ] All existing tests continue to pass

## Downstream

Platform adapters that register into this composite:
- **Ticket 050** — Bluesky adapter (Lua)
- Future: Mastodon, Twitter/X, Reddit, blog platforms

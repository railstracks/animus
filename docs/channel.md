# Channel Integration Guide

This document describes the architecture and integration points for adding a new channel
connector to Animus. A "channel" is any platform the agent can receive messages from and
send replies to (IRC, Telegram, VK, Discord, WhatsApp, Slack, etc.). Email is handled
by a separate dedicated tool and is NOT a channel type.

---

## Overview

Adding a channel requires touching up to **seven areas**:

1. **C++ connector loop** — background thread that receives messages from the platform
2. **StartChannel routing** — spawn the loop when the channel config is loaded
3. **SendReply routing** — send auto-replies back to the platform
4. **System prompt context** — `ChannelContextProvider` tells the agent where it is
5. **Lua adapter** — `channels` tool actions for the agent to use proactively
6. **C++ bridge actions (optional)** — for actions that need ChannelManager access
7. **Admin UI** — channel type dropdown + config form fields

---

## 1. Connector Loop

Each connector runs in its own `std::thread`, managed by `PollerState`. The loop lives
as a method on `ChannelManager` (e.g. `SlackSocketModeLoop`, `DiscordGatewayLoop`).

### PollerState fields

| Field | Purpose |
|-------|---------|
| `channel_name` | Config instance name (e.g. `"slack"`, `"discord"`) |
| `channel_type` | Platform identifier (e.g. `"slack"`, `"discord"`) |
| `config` | `Json::Value` with all channel-specific settings |
| `agent_id` | Which agent to dispatch to (default: `"default"`) |
| `active` | Set to `false` to signal shutdown |
| `thread` | The running thread (joined on destruction) |
| `consecutive_errors` | For backoff tracking |
| `ws_connected` | Whether the WebSocket is live |
| `lp_server`, `lp_key`, `lp_ts` | Long Poll state (VK, Telegram) |

### Loop pattern

```
void ChannelManager::XxxLoop(PollerState* state) {
    // 1. Read config (tokens, channels, etc.)
    // 2. Resolve bot identity if needed
    while (state->active) {
        // 3. Connect / poll for messages
        // 4. For each inbound message:
        //    a. Filter (self, bots, subtypes)
        //    b. Check mention/response config
        //    c. Build routing key
        //    d. Build ReplyTarget
        //    e. Call m_dispatch(...)
        // 5. Handle errors, back off, reconnect
    }
}
```

### Critical rules

- **Always check `state->active`** in loops and sleep loops. Daemon shutdown sets this to false.
- **Never store raw pointers to stack-local variables in `state->config`**. If the loop
  restarts (reconnect), those pointers dangle. Use heap allocation or null them before
  returning from the inner function. (WhatsApp crash, June 7.)
- **Always `std::move` the PollerState into `m_pollers`** after spawning the thread.
  Forgetting this destroys the unique_ptr on scope exit while the thread is still running,
  which calls `std::terminate` on a joinable thread. (Slack crash, June 7.)
- **`SSL_free()` must happen AFTER `readThread_.join()`**. Freeing SSL state while
  the read thread is still in `SSL_read` causes a crash. (WhatsApp crash, June 7.)
- **Drogon timer callbacks on WebSocket connections** — always null-check
  `getConnection()` and use a `gatewayAlive` flag set to false before calling
  `wsPtr->stop()`. (Discord crash, June 7.)
- **Catch-all exceptions** in the outer loop. Uncaught exceptions in threads call
  `std::terminate`.

---

## 2. StartChannel Routing

In `ChannelManager::StartChannel()`, add a branch for your channel type:

```cpp
} else if (state.type == "slack") {
    auto poller = std::make_unique<PollerState>();
    poller->channel_name = state.name;
    poller->channel_type = state.type;
    poller->config = state.config;
    poller->agent_id = GetString(state.config, "agent_id", "default");
    poller->next_attempt = std::chrono::steady_clock::now();
    poller->active = true;

    std::string name = state.name;
    poller->thread = std::thread(&ChannelManager::SlackSocketModeLoop, this, poller.get());

    {
        std::lock_guard<std::mutex> lock(m_pollersMutex);
        m_pollers[name] = std::move(poller);   // <-- DO NOT FORGET THIS
    }
}
```

Also add the loop method declaration in `ChannelManager.h`.

---

## 3. SendReply Routing

In `ChannelManager::SendReply()`, add a branch for your channel type. This is the path
that auto-routes the agent's text response back to the originating conversation.

```cpp
if (target.channel_type == "slack") {
    // 1. Look up the channel config to get the bot token
    // 2. Build the API request (e.g. chat.postMessage)
    // 3. Include thread_ts from target.reply_to_comment if present
    // 4. Send the HTTP request
    // 5. Check for API errors (Slack returns 200 with {ok: false})
    return;
}
```

### ReplyTarget fields

| Field | Purpose |
|-------|---------|
| `channel_name` | Config instance name |
| `channel_type` | Platform identifier |
| `type` | `Chat` (DM/channel) or `Wall` (post/comment) |
| `peer_id` | Channel ID or user ID to send to |
| `post_id` | For wall-style replies |
| `reply_to_comment` | Thread ID for threaded replies |
| `group_id` | Community/group ID (VK) |
| `irc_target` | IRC: channel or nick to send to |
| `interface_name` | IRC: interface name for SendPrivmsg |
| `email_thread_id` | Email: thread ID for reply threading |
| `email_inbox_id` | Email: which inbox to send from |

---

## 4. Session Key Format

The session key determines session routing and deduplication. It flows through:

1. **Connector loop** builds a routing key and passes it to `m_dispatch`
2. **AgentKernel** prefixes it with `"channel:"` to form the connector:
   ```cpp
   SessionKey key{"channel:" + sessionKey, ""};
   ```
3. **ChannelContextProvider** parses the connector to inject platform context

### Routing key format

Use `chat:<platform>:<target>` for chat-based channels:

```
chat:slack:C0B057FKB70          → channel message
chat:slack:1780836565.380429    → threaded reply (thread_ts)
```

Legacy formats still in use:
- `peer:<id>` — Telegram, VK DMs
- `post:<id>` — VK wall posts
- `peer:<id>:<thread_id>` — Telegram topics
- `post:<id>:comment:<comment_id>` — VK wall comments

### Connector parsing (ChannelContextProvider)

The provider splits on `:` and extracts:
```
channel:chat:slack:C0B057FKB70
  ↑      ↑     ↑
prefix  type  channelName  → matches BuildChannelContext("slack")
```

---

## 5. System Prompt Context (ChannelContextProvider)

File: `src/kernel/context/ChannelContextProvider.cpp`

Add a branch in `BuildChannelContext()` for your platform:

```cpp
} else if (channelName == "slack") {
    platformName = "Slack";
    routingInstructions =
        "Your text response will be automatically sent as a reply in the originating "
        "Slack channel or DM.";
    socialToolNote =
        "Do NOT use the channels tool to send replies -- your text output IS the reply. "
        "The channels tool is only for performing additional operations on platforms "
        "(searching, browsing, managing reactions, etc.).";
}
```

Three pieces to provide:
- **`platformName`** — human-readable name ("Slack", "Discord")
- **`routingInstructions`** — how the agent's text response gets delivered
- **`socialToolNote`** (optional) — when to NOT use the channels tool for sending

---

## 6. Lua Adapter

File: `scripts/<platform>.lua`

Provides programmatic access to platform features (search, browse, react, delete, etc.)
via the `channels` tool. The adapter is a stateless Lua script that calls platform
REST APIs using `animus.http_get` / `animus.http_post`.

### Registration pattern

```lua
animus.register_channel({
    id = "slack",
    name = "Slack",
    capabilities = {"read", "write", "search", "react"},
    actions = {"post", "reply", "browse", "search", ...},
    schema = {
        post = {
            { name = "channel_id", type = "string", required = true,
              description = "Channel ID to post to" },
            { name = "content", type = "string", required = true,
              description = "Message text" },
        },
        reply = {
            { name = "channel_id", type = "string", required = true,
              description = "Channel ID" },
            { name = "message_id", type = "string", required = true,
              description = "Message ID to reply to" },
            { name = "content", type = "string", required = true,
              description = "Reply text" },
        },
        -- ... one entry per action
    },
    handler = function(args)
        -- Route to the appropriate action function
    end
})
```

**Note:** `animus.register_social()` still works as a backward-compatible alias,
but `register_channel` is the canonical name.

### Schema format

The `schema` field MUST be a **dictionary of action names to parameter arrays**:

```lua
-- ✅ Correct: dict-of-actions format
schema = {
    post = {
        { name = "content", type = "string", required = true,
          description = "Message text" },
    },
    search = {
        { name = "query", type = "string", required = true,
          description = "Search query" },
    },
}

-- ❌ Wrong: flat array format (causes "invalid key to 'next'" Lua error)
schema = {
    { name = "action", type = "string", required = true, ... },
    { name = "content", type = "string", required = false, ... },
}
```

The C++ `ParseActionSchemas` function iterates the schema table with `lua_next`,
expecting string keys (action names). Flat integer-keyed arrays cause runtime errors.

### Handler args

| Field | Description |
|-------|-------------|
| `args.action` | The action name (e.g. "post", "search") |
| `args.platform_id` | Instance ID (e.g. `"slack:workspace"`) |
| `args.channel_id` | Target channel/conversation |
| `args.content` | Message text |
| `args.query` | Search query |
| `args.*` | Action-specific parameters |

### Return format

```lua
-- Success
return {
    success = true,
    output = "Human-readable summary",
    -- Action-specific fields (message_id, results, etc.)
}

-- Error — use "error" key, NOT "output"
return {
    success = false,
    error = "Description of what went wrong",
}
```

**Important:** The C++ `LuaToolHandler` reads the `error` key for failures, not `output`.
Using `output` for errors produces the generic "unknown Lua handler error" message.

### Config access

Per-instance config is stored under `channels.<platform_id>.<key>`:
```lua
local function cfg_key(platform_id, key)
    return "channels." .. platform_id .. "." .. key
end
local token = config.get(cfg_key(platform_id, "bot_token"))
```

Legacy `social.` prefix keys are still supported for backward compatibility, but all
new adapters should use `channels.`.

### Lua sandbox constraints

The Animus Lua sandbox blocks `require()`, `io`, and `os.execute`. Available globals:

| Global | Purpose |
|--------|---------|
| `json.encode(value)` | Serialize to JSON string |
| `json.decode(string)` | Parse JSON string |
| `config.get(key)` | Read from AgentConfigStore |
| `animus.http_get(url)` | HTTP GET request |
| `animus.http_post(url, body, headers)` | HTTP POST request |
| `log.info/warn/error(msg)` | Logging |
| `animus.register_channel(table)` | Register adapter (canonical) |
| `animus.register_social(table)` | Register adapter (legacy alias) |

**Never use `require("json")`** — `json.encode`/`json.decode` are already globals.

### Rate limit tracking

Track per-method rate limits via response headers. Persist to config on near-limit
or 429 responses only (not every request).

---

## 7. C++ Bridge Actions (When Lua Isn't Enough)

Some actions need access to C++ state that the Lua sandbox can't reach:

- **IRC `post`** — needs `ChannelManager::SendReply` to route through the IrcLoop socket
- **IRC `list_channels`** — needs `ChannelState.config` to read joined channels
- **IRC `get_channel`** — needs `ChannelState.config` for channel metadata
- **WhatsApp `send_message`** — needs the gateway loop's encryption state (future)

### Pattern

In `ChannelsTool::Execute()`, intercept the action before delegating to
`CompositeTool::Execute`:

```cpp
std::string adapterType = ExtractAdapterType(platformId);
if (adapterType == "irc" && m_channelManager) {
    if (action == "post") {
        ChannelManager::ReplyTarget rt;
        rt.channel_name = ExtractInstanceName(platformId);
        rt.channel_type = "irc";
        rt.irc_target = target;
        m_channelManager->SendReply(rt, content);
        // ... return result
    }
    if (action == "list_channels") {
        auto chState = m_channelManager->GetChannel(ExtractInstanceName(platformId));
        // ... read from chState->config
    }
}
```

### When to use C++ bridge vs Lua

| Criterion | Lua adapter | C++ bridge |
|-----------|------------|------------|
| Calls external REST API | ✅ | ❌ |
| Needs AgentConfigStore credentials | ✅ | ❌ |
| Needs ChannelState.config | ❌ | ✅ |
| Needs SendReply / IrcLoop socket | ❌ | ✅ |
| Needs gateway encryption state | ❌ | ✅ |
| Pure data transformation | ✅ | Either |

---

## 8. Admin UI

File: `admin-ui/src/views/ChannelsView.vue`

### Changes needed

1. **`channelTypes`** — add `{ title: 'Slack', value: 'slack' }` to the array
2. **`ChannelType`** union — add `| 'slack'`
3. **`formData`** — add reactive fields for your config (e.g. `slack_bot_token`)
4. **`buildConfig()`** — convert form fields to the JSON config object
5. **`openEdit()`** — populate form fields from existing config
6. **Template** — add a `<template v-if="formData.type === 'slack'">` block with form fields

### Three-place rule for form fields

When adding a form field in Vue, you must add it in **three places**:
1. `formData` initialization — reactive state
2. `buildConfig()` — serialization to JSON config
3. `<template>` — the actual form input element

Missing any of these causes silent failures (field exists in state but is never
persisted, or persisted but never rendered).

### Boolean checkbox gotcha

Vuetify checkboxes produce JS `boolean` values, but `GetString()` in C++ requires
JSON **string** values. Always convert:

```typescript
respond_to_all_messages: String(formData.value.slack_respond_to_all_messages),
// "true" or "false" (string), not true/false (boolean)
```

### i18n gotcha

The `@` character is special in vue-i18n message format (linked messages). Escape it:

```typescript
respondToMentions: "Respond to {'@'}mentions",
```

---

## 9. Config Schema

Channel config is stored as a JSON object per instance. Common fields:

| Field | Type | Purpose |
|-------|------|---------|
| `agent_id` | string | Which agent to dispatch to |
| `bot_token` | string | Platform bot/user token |
| `respond_to_mentions` | string ("true"/"false") | Only respond when @mentioned |
| `respond_to_all_messages` | string ("true"/"false") | Respond to all messages |

Platform-specific fields:
- **Slack:** `app_token` (Socket Mode), `threaded_replies`, `monitored_channels`
- **Discord:** `application_id`, `monitored_channels`
- **WhatsApp:** auth dir, signal manager
- **VK:** `group_id`, `access_token`
- **Telegram:** bot token
- **IRC:** `nick`, `host`, `port`, `channels` (array), `server_password`, `allowed_dm_users`

---

## 10. Common Pitfalls

| Pitfall | Symptom | Fix |
|---------|---------|-----|
| Missing `m_pollers` move | `std::terminate` on startup | Add `m_pollers[name] = std::move(poller)` |
| Dangling stack pointers | Crash after reconnect | Null config entries before function returns |
| `SSL_free` before `join()` | Crash during reconnect | Close socket → join thread → `SSL_free()` |
| Timer on destroyed WS | SIGSEGV on disconnect | Check `gatewayAlive` flag + null-check `getConnection()` |
| Boolean vs string config | Setting silently ignored | Use `String()` in UI, `GetString()` in C++ |
| `@` in i18n strings | vue-i18n compile error | Escape as `{'@'}` |
| Binary WS frames | Events silently dropped | Handle both Text and Binary frame types |
| Missing bot event subscriptions | No events delivered | Subscribe to `message.*` bot events in app config |
| Bot own messages echoed | Duplicate responses | Filter by `bot_id` (not just user_id) |
| Flat schema array | "invalid key to 'next'" Lua error | Use dict-of-actions: `{ action = { params } }` |
| `require("json")` in Lua | "attempt to call nil value" | Use globals: `json.encode`/`json.decode` |
| `{success=false, output=...}` | "unknown Lua handler error" | Use `error` key, not `output`, for failures |
| Missing form field in template | Field never renders | Add to formData, buildConfig, AND template |
| Stale `libanimus_kernel.so` | C++ changes don't take effect | CMake `LIBRARY_OUTPUT_DIRECTORY` must point to `dist/` |
| `SetChannelManager(nullptr)` | ChannelManager features broken | Wire after creation in `Initialize()`, not in constructor |
| `ch.connected` not updated | Shows "not connected" when connected | Use `IsChannelConnected()` for live status |

---

## 11. File Checklist

When adding a new channel, touch these files:

```
include/animus_kernel/ChannelManager.h          # Loop declaration
src/kernel/ChannelManager.cpp                    # StartChannel, SendReply, loop implementation
src/kernel/context/ChannelContextProvider.cpp    # System prompt context
scripts/<platform>.lua                          # Channels tool adapter
src/kernel/tools/ChannelsTool.cpp                # C++ bridge actions (if needed)
admin-ui/src/views/ChannelsView.vue             # Channel form
admin-ui/src/i18n/locales/en.ts                 # Form labels
CMakeLists.txt                                  # If adding new source files
tickets/<NNN>-<platform>-adapter.md             # Design ticket
```

---

## 12. Current Channel Matrix

| Platform | C++ Connector | Lua Adapter | C++ Bridge | Status |
|----------|--------------|-------------|------------|--------|
| Bluesky | PollingLoop | ✅ bluesky.lua | — | Working |
| Discord | GatewayLoop | ✅ discord.lua | — | Working |
| IRC | IrcLoop | ✅ irc.lua | post, list_channels, get_channel | Working |
| Slack | SocketMode + Polling | ✅ slack.lua | — | Working |
| Telegram | LongPollLoop | ✅ telegram.lua | — | Working |
| Twitter | (not connected) | ✅ twitter.lua | — | Exists, not active |
| VK | LongPollLoop | ✅ vk.lua | — | Working |
| WhatsApp | GatewayLoop | ✅ whatsapp.lua | send_message (future) | Working, send limited |
| Email | — | — | — | Separate tool, not a channel |
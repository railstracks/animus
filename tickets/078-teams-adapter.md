# Ticket 078 — Teams Adapter

**Priority:** P3 (enterprise integration, requires C++ connector — larger scope)
**Dependencies:** Ticket 074 (channels tool rename) ✅, Ticket 039 (Lua scripting) ✅
**Created:** 2026-06-08
**Status:** Draft — awaiting Teams Bot Framework documentation from Melvin

## Summary

A full channel integration for Microsoft Teams, implementing bi-directional messaging via the Bot Framework. Unlike Moltbook/MoltX which are poll-based REST APIs, Teams requires a persistent websocket connection to the Bot Framework relay — making this a C++ connector loop project comparable to the Slack or Discord adapters in scope.

## Motivation

- **Enterprise presence** — Teams is the dominant enterprise chat platform. An Animus agent on Teams can serve as an internal assistant.
- **Validates Bot Framework pattern** — the Bot Framework websocket pattern (similar to Slack Socket Mode) is reusable for other Microsoft services.
- **Rich interaction model** — messages, threads, channels, mentions, adaptive cards, DMs.

## Architecture Overview

Teams bots use the **Microsoft Bot Framework** with the **Bot Builder** protocol. The bot connects to the Bot Framework relay service via:

1. **WebSocket** (preferred) — persistent connection to `wss://europe.api.botframework.com/v3/conversations/connect`
2. **Outgoing webhook** (limited) — HTTP POST to a defined endpoint. No DM support, no threads.

The WebSocket path is the right one for a full channel.

### Authentication

Teams bots authenticate via **OAuth2 client credentials** (Azure AD app registration):

```
POST https://login.microsoftonline.com/{tenant_id}/oauth2/v2.0/token
Body: grant_type=client_credentials&client_id={app_id}&client_secret={app_secret}&scope=https://api.botframework.com/.default
Response: { "access_token": "...", "expires_in": 3600 }
```

The `access_token` is used as `Authorization: Bearer <token>` for Bot Framework API calls. Token refresh is needed every ~60 minutes.

### Message Flow

**Inbound:**
1. Bot Framework sends an `Activity` to the bot via the websocket connection
2. Activity types: `message`, `conversationUpdate`, `messageReaction`, etc.
3. The connector loop parses the activity, builds a `ReplyTarget`, dispatches to Animus

**Outbound:**
1. Animus generates a text reply
2. `ChannelManager::SendReply` routes to the Teams connector
3. Connector calls `POST https://api.botframework.com/v3/conversations/{conversationId}/activities/{activityId}`
4. Or uses the existing websocket to send the reply

### Adaptive Cards

Teams supports Adaptive Cards for rich content. This is a follow-up feature (not in initial scope), but the architecture should allow for it.

## Connector Design (C++)

### StartChannel

```cpp
} else if (state.type == "teams") {
    auto poller = std::make_unique<PollerState>();
    poller->channel_name = state.name;
    poller->channel_type = state.type;
    poller->config = state.config;
    poller->agent_id = GetString(state.config, "agent_id", "default");
    poller->next_attempt = std::chrono::steady_clock::now();
    poller->active = true;

    std::string name = state.name;
    poller->thread = std::thread(&ChannelManager::TeamsLoop, this, poller.get());

    {
        std::lock_guard<std::mutex> lock(m_pollersMutex);
        m_pollers[name] = std::move(poller);
    }
}
```

### TeamsLoop Pattern

```
1. Read config (app_id, app_password, tenant_id)
2. Obtain OAuth2 token (client_credentials flow)
3. Connect to Bot Framework websocket
4. While active:
   a. Receive Activity from websocket
   b. Filter: ignore bot's own messages, system messages
   c. Parse: extract text, mentions, conversation info
   d. Build routing key: "chat:teams:{conversation_id}"
   e. Build ReplyTarget with channel info
   f. Call m_dispatch(...)
5. Handle token refresh (every ~55 min)
6. Handle reconnection with backoff
```

### Config Fields

| Field | Required | Description |
|-------|----------|-------------|
| `app_id` | Yes | Azure AD application (client) ID |
| `app_password` | Yes | Azure AD client secret |
| `tenant_id` | No | Azure AD tenant (default: common) |
| `monitored_channels` | No | Channel IDs to respond in (empty = all) |
| `respond_to_all_messages` | No | "true"/"false" string (default: "false") |

## Lua Adapter

A Teams Lua adapter provides proactive actions:

| Action | API Call | Description |
|--------|----------|-------------|
| `post` | Send activity to conversation | Send a message to a channel |
| `reply` | Send activity with replyToId | Threaded reply |
| `browse` | Get channel messages | Read recent messages |
| `get_channel` | Get channel info | Channel metadata |
| `list_channels` | List team channels | Enumerate channels in a team |
| `search` | Search messages | Full-text search in channels |
| `get_profile` | Get user info | Look up a Teams user |

Many of these need the Bot Framework REST API (not the Teams client API). The Lua adapter will use `animus.http_get/http_post` with the OAuth2 token from the C++ connector's token refresh.

**Challenge:** The OAuth2 token is managed by the C++ connector. The Lua adapter needs access to it. Options:
1. Store the token in `AgentConfigStore` after each refresh (C++ writes, Lua reads)
2. Add a C++ bridge action for Teams that handles authenticated requests (like the IRC bridge)

**Recommendation:** Option 1 for simplicity. The C++ connector updates the token in config after each refresh.

## Admin UI

### Form Fields

| Field | Type | Description |
|-------|------|-------------|
| `app_id` | text | Azure AD application (client) ID |
| `app_password` | password | Azure AD client secret |
| `tenant_id` | text | Azure AD tenant ID (default: common) |
| `respond_to_all_messages` | checkbox | Respond to all messages, not just @mentions |

### No Self-Registration

Unlike Moltbook/MoltX, Teams accounts cannot be created from the Admin UI. The Azure app registration must be done manually in the Azure Portal:

1. Create an Azure AD app registration
2. Create a client secret
3. Add Bot Framework as an API permission
4. Configure the bot's messaging endpoint
5. Paste the `app_id` and `app_password` into the Animus Admin UI

This should be documented in the channel type's help text.

## Session Key Format

```
chat:teams:{conversation_id}           → channel conversation
chat:teams:{conversation_id}:{thread}  → threaded conversation
```

The `conversation_id` comes from the Activity's `conversation.id` field. Threads use the activity's `id` as the thread identifier.

## Implementation Checklist

- [ ] `src/kernel/ChannelManager.cpp` — TeamsLoop, StartChannel branch, SendReply branch
- [ ] `src/kernel/ChannelManager.h` — TeamsLoop declaration
- [ ] `scripts/teams.lua` — Lua adapter for proactive actions
- [ ] `src/kernel/tools/ChannelsTool.cpp` — C++ bridge for authenticated requests (if needed)
- [ ] `src/kernel/context/ChannelContextProvider.cpp` — Add "teams" context
- [ ] `admin-ui/src/views/ChannelsView.vue` — Add teams channel type + form
- [ ] `admin-ui/src/i18n/locales/en.ts` — Add form labels
- [ ] OAuth2 token management (refresh, storage in config)
- [ ] Bot Framework websocket connection + reconnection
- [ ] Activity parsing and routing

## Open Questions

1. **Bot Framework SDK vs raw protocol?** We could use the Bot Builder REST API directly (like Slack) or use a C++ Bot Framework SDK if one exists. Raw protocol is preferred for consistency.
2. **WebSocket library?** The existing codebase uses Drogon for Slack's Socket Mode. Teams would use the same approach — Drogon WebSocket client to the Bot Framework relay.
3. **Adaptive Cards?** Out of scope for v1, but the Activity format supports them. Design the adapter to not preclude them later.
4. **Multi-tenant?** The `tenant_id` field supports different Azure AD tenants. A single Animus instance could connect to multiple Teams tenants.
5. **Documentation pending:** Melvin will provide specific Bot Framework documentation. This ticket will be updated accordingly.

## Notes

- Teams uses `conversation.id` which includes the channel and thread context in a single string. Parsing this correctly is critical for routing.
- Teams bot mentions use `<at>BotName</at>` XML tags in the message text. These need to be stripped for the agent.
- Teams has a 28KB message size limit. Long responses may need to be split.
- The Bot Framework token expires every ~60 minutes. The connector must refresh proactively.
- Teams channels use GUIDs, not human-readable names. The adapter should maintain a name→GUID mapping for the agent's convenience.
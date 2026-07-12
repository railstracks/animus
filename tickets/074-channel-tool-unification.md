# Ticket 074 — Channel Tool Unification

**Priority:** P2 (UX consistency — agents can't interact with connected channels they can't see)
**Dependencies:** Ticket 061 (Slack Adapter) ✅
**Created:** 2026-06-07

## Problem

The `social` tool (registered as `animus.register_social()`) only covers 5 of 9 connected channels. WhatsApp, Telegram, and IRC have full C++ connectors with bidirectional messaging but no Lua adapter — meaning the agent can receive messages on these platforms but cannot use the tool to proactively browse, search, or send to them. The tool name `social` is also misleading for IRC and doesn't cover the full scope.

Additionally, agents in chat/wall sessions are told they're on a platform but can't verify this with the tool — the Slack channel shows 4 platforms instead of 7+ because three connected adapters have no registration.

## Current State

| Channel | C++ Connector | Lua Adapter | Tool Actions | Connected |
|---------|--------------|-------------|-------------|-----------|
| Bluesky | ✅ PollingLoop | ✅ bluesky.lua | post, reply, like, browse, search, get_notifications, delete | ✅ (reports disconnected — side bug) |
| Discord | ✅ GatewayLoop | ✅ discord.lua | post, reply, browse, react, delete, edit, get_channel, get_message, list_channels, get_guild, list_guilds, typing, gateway_info | ✅ |
| VK | ✅ LongPollLoop | ✅ vk.lua | post, reply, like, delete, send_message, get_messages, get_conversations, get_long_poll_server | ✅ |
| Slack | ✅ SocketMode + Polling | ✅ slack.lua | post, reply, browse, search, react, get_channel, list_channels, list_members, delete, edit, auth_test | ✅ |
| Twitter | ✅ (inactive) | ✅ twitter.lua | post, reply, like, browse, search, delete | ❌ (OAuth not active) |
| WhatsApp | ✅ GatewayLoop | ❌ **Missing** | — | ✅ |
| Telegram | ✅ LongPollLoop | ❌ **Missing** | — | ✅ |
| IRC | ✅ IrcLoop | ❌ **Missing** | — | ✅ |
| Email | ✅ WSPollLoop | ✅ (separate tool) | — | ✅ |

## Changes

### 1. Rename `social` → `channels`

The tool is about channel interactions, not just "social media". IRC and Telegram aren't social platforms.

**C++ changes:**
- `CompositeTool("social"` → `CompositeTool("channels"` in `SocialTool.cpp`
- Rename `SocialTool.cpp` → `ChannelsTool.cpp`, `SocialTool.h` → `ChannelsTool.h`
- Update tool description to reference "channels" instead of "social media platforms"
- Update `session_types` comment (currently says "social tool")

**Lua changes:**
- `animus.register_social()` → `animus.register_channel()` in all adapters
- Config key prefix changes: `social.<platform_id>.<key>` → `channels.<platform_id>.<key>`
- All 5 existing adapters (bluesky, discord, slack, twitter, vk) updated

**Agent-facing impact:**
- Tool name in prompts: `social` → `channels`
- Action schema stays the same (platform_id, action, etc.)
- Old config keys need migration or backward-compatible reads

### 2. Add WhatsApp Adapter (`scripts/whatsapp.lua`)

WhatsApp has no public REST API — messages are sent via the C++ gateway loop. The adapter wraps the gateway's outbound queue.

**Proposed actions:**
- `send_message` — queue message for gateway loop to encrypt and send
- `get_conversations` — list recent chats (from gateway state)
- `get_messages` — retrieve recent messages in a chat

**Auth:** Handled entirely by C++ gateway (Signal Protocol, QR pairing). No tokens in Lua.

**Challenge:** Most WhatsApp operations require the gateway loop's internal state. The adapter may need to expose gateway state via a shared interface or limit itself to `send_message` (which already works through the outbound queue in `SendReply`). Full browsing may require a C++ query API.

**Minimal viable:** `send_message` only, using the existing `m_dispatch` outbound path. This gives agents proactive sending without browsing capability.

### 3. Add Telegram Adapter (`scripts/telegram.lua`)

Telegram Bot API is straightforward REST.

**Proposed actions:**
- `send_message` — `sendMessage` API
- `get_messages` — `getUpdates` or `getChat` + `getChatHistory`
- `get_conversations` — list chats the bot is in
- `get_me` — `getMe` (bot identity)

**Auth:** Bot token from config (`channels.telegram:PERSONAL.bot_token`)

**Rate limits:** Telegram Bot API: 30 msg/sec to same chat, 20 msg/sec across chats. No monthly caps.

### 4. Add IRC Adapter (`scripts/irc.lua`)

IRC is simple — the C++ loop already handles connection, joins, and message routing.

**Proposed actions:**
- `post` — send PRIVMSG to a channel or user
- `join` — join a channel
- `part` — leave a channel
- `list_channels` — list channels the bot is in
- `get_channel` — get info about a channel (topic, users)

**Auth:** Handled by C++ IrcLoop. Config has server, nick, channels.

**Challenge:** IRC doesn't have a REST API. The adapter needs to send commands through the C++ layer (similar to WhatsApp's outbound queue pattern).

**Minimal viable:** `post` (send message) + `list_channels`. Join/part via admin config.

### 5. Fix Bluesky Disconnected Status

Bluesky shows as "Disconnected" in the admin UI despite being connected and functional. Likely a status reporting issue in the connector loop or admin API.

### 6. Config Key Migration

Current config keys use `social.<platform_id>.<key>` prefix. After rename:
- New keys: `channels.<platform_id>.<key>`
- Backward-compatible reads: try `channels.` first, fall back to `social.`
- Migration: one-time config migration on startup, or just support both indefinitely

## Implementation Order

1. **Rename** — `social` → `channels` in C++ and all Lua adapters (foundation change)
2. **Telegram adapter** — simplest REST API, proves the pattern
3. **IRC adapter** — needs C++ bridge for command dispatch
4. **WhatsApp adapter** — needs C++ bridge for outbound queue
5. **Bluesky disconnected status fix** — investigate and fix

## Acceptance Criteria

- [ ] Tool renamed from `social` to `channels` in C++ and all Lua adapters
- [ ] Config keys migrated from `social.` to `channels.` prefix (with backward compat)
- [ ] `scripts/telegram.lua` registered with `send_message`, `get_messages`, `get_conversations`, `get_me`
- [ ] `scripts/irc.lua` registered with `post`, `list_channels`
- [ ] `scripts/whatsapp.lua` registered with `send_message` (minimal)
- [ ] Bluesky reports correct connected status
- [ ] Agent in Slack channel sees all connected platforms when listing available channels
- [ ] Clean build (C++ + Vue SPA + Lua adapters)

## Side Bugs

- **Bluesky falsely reports disconnected** — status reporting issue, connector is functional
- **Agent confusion about reply method** — fixed by `ChannelContextProvider` Slack branch (Ticket 061)
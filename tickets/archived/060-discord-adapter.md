# Ticket 060 — Discord Adapter

**Created:** 2026-05-27
**Status:** Complete (Phase 2)
**Priority:** High
**Depends on:** 056 (Channels Refactor), 043 (Social Tool), 039 (Lua Scripting)

## Summary

Implement Discord as a first-class channel in Animus, supporting both guild channel messages and DMs via the Discord Gateway WebSocket protocol. Includes a Lua social adapter for platform-specific API operations.

## Motivation

Discord is one of the most widely-used messaging platforms. With the Channels refactor (056) providing a unified connector architecture, adding Discord support is a natural next step. Discord's Gateway WebSocket protocol provides real-time event delivery without polling, making it more efficient than REST-based approaches.

## Implementation Plan

### Phase 1: Lua Social Adapter (Discord REST API)
- Lua script (`discord.lua`) implementing 13 actions via REST API v10
- Actions: list_guilds, list_channels, send_message, edit_message, delete_message, get_messages, add_reaction, remove_reaction, create_thread, list_threads, get_guild_member, search_messages, list_guilds
- JWT bot token auth, rate limit awareness

### Phase 2: C++ Gateway WebSocket (Real-time Events)
- `DiscordGatewayLoop` in ChannelManager — full Gateway protocol implementation
- HELLO → IDENTIFY → READY → heartbeat loop → event processing
- MESSAGE_CREATE dispatch to agent sessions via `DispatchToSession`
- SendReply via REST POST to `/channels/{id}/messages`
- Guild channel + DM support with correct routing keys

### Phase 3: Slash Commands + Interactions (Future)
- Application command registration
- Interaction payload handling
- Component interactions (buttons, select menus)

## Technical Details

### Gateway Protocol
- WebSocket endpoint: `wss://gateway.discord.gg/?v=10&encoding=json`
- Intents bitmask: 38401 (GUILDS + GUILD_MESSAGES + GUILD_MESSAGE_REACTIONS + DIRECT_MESSAGES + MESSAGE_CONTENT)
- Heartbeat with jitter on first beat
- Resume support via `resume_gateway_url` from READY payload

### Session Routing
- Guild channels: `post:CHANNEL_ID` → session key `chat:discord:CHANNEL_ID`
- DMs: `peer:DM_CHANNEL_ID` → session key `chat:discord:DM_CHANNEL_ID`
- ReplyTarget carries channel/DM info for SendReply routing

### Channel Config
Stored as `channel.discord.config` in SQLite:
- `bot_token`, `application_id`, `monitored_channels` (comma-separated IDs)
- `respond_to_dm`, `respond_to_mentions`, `agent_id`

### Rate Limits
- Gateway: 120 events/min, 1 identify/5s
- REST: ~50 req/s global, per-route buckets
- 10K invalid requests/10min → Cloudflare ban

## Acceptance Criteria

- [x] Discord Gateway WebSocket connects, heartbeats, receives events
- [x] Bot appears online in Discord client
- [x] Guild channel messages dispatched to agent sessions
- [x] DM messages dispatched to agent sessions
- [x] Agent text replies routed back to originating channel/DM
- [x] Lua social adapter with 13 actions
- [x] Admin UI Discord form renders correctly
- [x] Channel context injected into system prompt (agent knows which platform it's on)
- [ ] Slash commands (Phase 3)
- [ ] Message edit/delete events processed
- [ ] Guild member join/leave events

# Ticket 132: Bluesky Chat (DMs) Support

**Created:** 2026-07-29
**Status:** Open
**Branch:** `dev`

## Problem

Bluesky adapter currently only handles public posts (notifications, replies, search, browse). Bluesky also offers private direct messaging via `chat.bsky.convo.*` APIs. Need to add DM polling and reply support.

## Architecture

Bluesky chat uses a centralized chat service (`did:web:api.bsky.chat`) accessed via PDS service proxying. Same JWT auth, additional `atproto-proxy: did:web:api.bsky.chat#bsky_chat` header on requests.

### Key endpoints:
- `chat.bsky.convo.listConvos` — list conversations (inbox)
- `chat.bsky.convo.getMessages` — fetch messages in a convo (paginated by `rev`)
- `chat.bsky.convo.sendMessage` — send a DM
- `chat.bsky.convo.getConvoForMembers` — find/initiate a convo by member DIDs
- `chat.bsky.convo.updateRead` — mark conversation as read up to a rev

### Polling model:
Unlike notifications (single `listNotifications`), DMs need two steps:
1. `listConvos` to find conversations with `unreadCount > 0`
2. `getMessages` for each unread convo to fetch new messages (watermark per convo by `rev`)

### Routing:
- DMs use `peer:` routing keys → chat-type sessions (same as Discord/WhatsApp DMs)
- `DispatchToSession` already handles `peer:` routing
- Ticket 131 auto-reply filter applies to DMs too

## Implementation

### C++ Backend (`ChannelManager.cpp`)

1. Add `BlueskyChatPollLoop` — runs alongside `BlueskyPollLoop` in the same thread or a second thread
2. Per-convo watermark: `std::map<std::string, std::string>` mapping convoId → last seen rev
3. On new message: dispatch via `DispatchToSession(state, "peer:<convoId>", message, "chat")`
4. After dispatch: call `updateRead` to mark messages as read
5. `SendReply` for Bluesky DMs: detect `ReplyTarget::Chat` + `channel_type == "bluesky"` → call `chat.bsky.convo.sendMessage`

### PollerState additions:
- `std::map<std::string, std::string> bsky_chat_watermarks` — per-convo last-seen rev
- `std::chrono::steady_clock::time_point bsky_chat_next_poll`

### Lua adapter (`scripts/bluesky.lua`)

Add `chat_send` and `chat_list` actions:
- `chat_send`: takes `convo_id` + `content`, calls `chat.bsky.convo.sendMessage` with proxy header
- `chat_list`: takes optional `limit`, calls `chat.bsky.convo.listConvos`
- `chat_messages`: takes `convo_id` + optional `limit`, calls `chat.bsky.convo.getMessages`
- `chat_get_convo`: takes `member_did`, calls `chat.bsky.convo.getConvoForMembers`

Add `auth_chat_get` / `auth_chat_post` helpers — same as `auth_get`/`auth_post` but with `atproto-proxy: did:web:api.bsky.chat#bsky_chat` header.

### Frontend
No UI changes needed — chat polling is automatic once the Bluesky channel is configured. DMs appear as chat sessions.

### i18n
No changes needed.

## Files Affected

- `src/kernel/ChannelManager.cpp` — `BlueskyChatPollLoop`, `SendReply` DM path, `PollerState` additions
- `include/animus_kernel/ChannelManager.h` — new method declarations, `PollerState` fields
- `scripts/bluesky.lua` — chat actions, proxy header helpers

## Backwards Compatibility

Fully additive. Existing channels get DM polling automatically (the poll loop starts alongside the notification poll). No config changes required.
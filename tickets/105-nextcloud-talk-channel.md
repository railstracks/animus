# Ticket 105 — Nextcloud Talk Channel Integration

**Status:** Phase 1a Implemented (2026-07-08, commit 2bdabab)
**Report:** See below

## Problem

Animus needs a Nextcloud Talk channel to participate in group chats, respond to direct messages, and eventually act as a stenographer during calls. Nextcloud is a broad ERP/workspace suite, but the first integration surface is **Nextcloud Talk** (the chat/call module). Additional surfaces (files, calendar, activity feed) are future scope.

## Why Nextcloud Talk?

Nextcloud is self-hosted, widely deployed in EU enterprise/government, and Talk is its primary real-time communication layer. A channel here opens Animus to organizations that run their own infrastructure — exactly the self-hosted demographic Animus is designed for.

## Architecture: Two Integration Paths

Nextcloud Talk offers two distinct integration patterns. We should implement **both** in phases.

### Path A: Bot Webhook (recommended first phase)

Animus registers as a Nextcloud Talk bot via `occ talk:bot:install`. The bot receives signed webhook POSTs for every message in conversations where it's enabled.

**Pros:** Real-time (push, no polling), HMAC-SHA256 verified, official API, receives reactions + system messages, can post messages + reactions back.

**Cons:** Requires `occ` CLI access to install the bot, Animus must be reachable from the Nextcloud server (public URL or network adjacency), bot is server-wide (enabled per-conversation by moderators).

**How it works:**
1. Admin runs: `occ talk:bot:install "Animus" https://animus.local:8080/hooks/nextcloud "<secret>" --features=chat,reaction`
2. Moderator enables bot in target conversation
3. Nextcloud POSTs Activity Streams 2.0 JSON to Animus on each message
4. Animus verifies HMAC, processes message, dispatches to agent
5. Agent responds via `POST /chat/{token}` with app password or bot auth

### Path B: OCS API Polling (fallback / standalone)

Animus authenticates as a Nextcloud user (app password) and polls for new messages via long-polling.

**Pros:** No webhook URL needed (works behind NAT), no `occ` access needed, acts as a regular user (more flexibility).

**Cons:** Polling (even long-poll with 30s timeout) vs push, needs user credentials, rate limits.

## API Reference

**Base URLs:**
- Talk API: `/ocs/v2.php/apps/spreed/api/v4` (conversations, calls) and `/ocs/v2.php/apps/spreed/api/v1` (chat, reactions, bots)
- OCS envelope: all responses wrapped in `{ocs: {meta: {status, statuscode, message}, data: ...}}`
- Format: `?format=json` header or `Accept: application/json`

**Auth:**
- Bot webhook: HMAC-SHA256 signature verification (`X-Nextcloud-Talk-Signature` + `X-Nextcloud-Talk-Random` headers)
- OCS API: HTTP Basic with username + app password, or Bearer token via OAuth2

### Key Endpoints (Phase 1 — Chat)

| Action | Method | Endpoint | Notes |
|--------|--------|----------|-------|
| List conversations | GET | `/room` | Returns all conversations for the user with tokens, types, last activity |
| Get conversation | GET | `/room/{token}` | Single conversation details |
| Receive messages | GET | `/chat/{token}` | `lookIntoFuture=1` for long-poll, `lastKnownMessageId` for offset. Returns 304 when no new messages within timeout. |
| Send message | POST | `/chat/{token}` | Body: `{message, replyTo, referenceId, silent}`. 32K char limit. Returns full message object. |
| Edit message | PUT | `/chat/{token}/{messageId}` | Modify existing message |
| Delete message | DELETE | `/chat/{token}/{messageId}` | Soft delete |
| Share rich object | POST | `/chat/{token}/share` | For file/activity shares |
| Mark read | POST | `/chat/{token}/read` | Update read marker |
| Set reaction | POST/DELETE | `/reaction/{token}/{messageId}` | Add/remove emoji reaction |

### Key Endpoints (Phase 2 — Calls/Recording)

| Action | Method | Endpoint | Notes |
|--------|--------|----------|-------|
| List call participants | GET | `/call/{token}` | Who's in the active call |
| Join call | POST | `/call/{token}` | flags: audio/video/screen |
| Leave call | DELETE | `/call/{token}` | Leave conversation call |
| Start recording | POST | `/recording/{token}` | Requires moderator |
| Stop recording | DELETE | `/recording/{token}` | |
| Store recording | POST | `/recording/{token}/store` | Upload recording file |

### Conversation Types (constants)

| Type | ID | Animus treatment |
|------|----|------------------|
| One-to-one | 1 | Direct message — respond to all messages |
| Group | 2 | Group chat — respond on @mention or contextual |
| Public | 3 | Public link room — same as group |
| Note to self | 6 | Personal journal — respond to all (no other participants) |
| Changelog | 4 | System — ignore |

### Actor Types

- `users` — registered Nextcloud users
- `guests` — anonymous guests
- `bots` — registered bots (our own messages appear as this)
- `federated_users` — users from other Nextcloud instances

### Bot Webhook Payload Format

Activity Streams 2.0:
```json
{
  "type": "Create",
  "actor": {
    "type": "Person",
    "id": "users/melvin",
    "name": "Melvin Sommer"
  },
  "object": {
    "type": "Note",
    "id": "1567",
    "name": "message",
    "content": "{\"message\":\"hello world\",\"parameters\":{}}",
    "mediaType": "text/markdown"
  },
  "target": {
    "type": "Collection",
    "id": "n3xtc10ud",
    "name": "Engineering Team"
  }
}
```

Reaction payloads: `"type": "Like"` (added) or `"type": "Undo"` (removed), with `"content": "👍"`.

## Implementation Plan

### Phase 1a: OCS API Polling Channel (MVP)

Add `"nextcloud"` as a new channel type in `ChannelManager`.

**Config schema:**
```json
{
  "type": "nextcloud",
  "config": {
    "server_url": "https://cloud.example.com",
    "username": "animus-bot",
    "app_password": "XXXX-XXXX-XXXX-XXXX",
    "agent_id": "61cdbd23...",
    "watch_tokens": [],
    "respond_in_dm": true,
    "respond_in_group_on_mention": true,
    "group_mention_trigger": "@animus"
  }
}
```

**Poller state:**
- `lastKnownMessageId` per conversation token (for pagination)
- Active conversation list (refreshed periodically via GET `/room`)

**Poll loop:**
1. GET `/room` — refresh conversation list (every 60s, or on `modifiedSince`)
2. For each watched conversation token, GET `/chat/{token}?lookIntoFuture=1&lastKnownMessageId={id}&timeout=30&format=json`
3. On 200: process new messages, update `lastKnownMessageId`
4. On 304: no new messages, loop
5. On error: exponential backoff

**Message dispatch:**
- Parse OCS envelope → extract `actorType`, `actorId`, `actorDisplayName`, `message`, `token`, `messageType`
- Skip messages from `actorType: "bots"` (our own)
- Skip `systemMessage` non-empty entries (join/leave notifications)
- DM (type 1): always dispatch
- Group (type 2/3): dispatch only on @mention or `respond_in_group_on_mention: false`

**ReplyTarget mapping:**
```cpp
ReplyTarget target;
target.channel_name = channelName;
target.channel_type = "nextcloud";
target.peer_id = conversationToken;  // The token to POST back to
```

**SendReply implementation:**
```cpp
// POST {server_url}/ocs/v2.php/apps/spreed/api/v1/chat/{token}
// Body: {"message": text, "format": "json"}
// Auth: Basic {username}:{app_password}
// OCS-APIREQUEST: true header
```

### Phase 1b: Bot Webhook Receiver

Add webhook endpoint to AdminServer:
- `POST /api/v1/hooks/nextcloud` (configurable path)
- Verify HMAC-SHA256 signature using shared secret
- Parse Activity Streams 2.0 payload
- Extract conversation token, actor, message
- Dispatch to ChannelManager → agent session

This is the preferred production path — push over pull. But it requires Animus to be reachable from the Nextcloud server.

### Phase 2: Call Stenographer

When a call starts in a watched conversation:
1. Bot joins call (POST `/call/{token}`) with audio-only flag
2. Polls participant list (GET `/call/{token}`) for presence tracking
3. If recording enabled, watches for recording completion notification
4. Fetches recording file, runs through transcription pipeline
5. Posts summary + transcript excerpts to the conversation

**Deferred** — requires separate speech-to-text integration.

### Phase 3: Beyond Talk

- **Files:** WebDAV access for document reading/writing
- **Activity feed:** OCS Activity API for timeline awareness
- **Calendar:** CalDAV for scheduling/proactive reminders
- **Deck:** Project board integration
- **Tasks:** Task list management

Each is a separate future ticket.

## Files to Create/Modify

### New files
- `include/animus_kernel/nextcloud/NextcloudTalkApi.h` — HTTP client wrapper for OCS endpoints
- `src/kernel/nextcloud/NextcloudTalkApi.cpp` — implementation
- `include/animus_kernel/nextcloud/NextcloudChannel.h` — channel connector
- `src/kernel/nextcloud/NextcloudChannel.cpp` — polling loop, message dispatch

### Modified files
- `src/kernel/ChannelManager.cpp` — add `nextcloud` type to `Initialize()` poller creation, `SendReply()`, and config validation
- `src/kernel/admin/internal/AdminServerRoutesChannels.inc` — add nextcloud to UI channel type dropdown
- `admin-ui/src/i18n/locales/en/channels.ts` — add nextcloud labels

### Admin UI
- Channels page already supports dynamic channel types — just need to add `"nextcloud"` to the type list
- Config form should show: Server URL, Username, App Password, Agent, Watch tokens (optional), Mention trigger

## Dependencies

- **libcurl** — already used for HTTP (HttpClient abstraction)
- **OpenSSL/HMAC** — already available for webhook signature verification
- **jsoncpp** — already used for JSON parsing

No new external dependencies.

## Scope

### In scope (Phase 1a) — ✅ Implemented:
- [x] Nextcloud Talk polling channel (`"nextcloud"` channel type)
- [x] Conversation list sync (GET `/room`)
- [x] Message polling with long-poll (`lookIntoFuture=1`, 30s timeout)
- [x] Message dispatch to agent sessions (DM + @mention in groups)
- [x] Reply via POST `/chat/{token}`
- [ ] HMAC webhook receiver endpoint (Phase 1b — deferred to follow-up)
- [x] Config validation
- [x] Admin UI channel type support

### Deferred:
- Call participation / stenographer (Phase 2)
- File/Calendar/Activity integration (Phase 3)
- Federation (cross-instance conversations)
- Reaction posting (receiving reactions is included in webhook path)
- Message editing/deletion by the bot
- Rich object sharing (file references, etc.)
- Markdown rendering (Talk supports markdown — need to map to/from Animus format)

## Testing

- Manual: configure against a Nextcloud instance, send messages, verify responses
- The polling loop can be tested without a real Nextcloud by mocking OCS responses
- Webhook signature verification can be unit-tested with known input + secret

## Estimated Complexity

Medium-large. New channel connector (~400-500 lines C++), webhook receiver (~150 lines), UI changes (~50 lines). The main complexity is the OCS envelope parsing and conversation state management.

## References

- Talk API docs: https://nextcloud-talk.readthedocs.io/en/latest/
- Bot webhook docs: https://nextcloud-talk.readthedocs.io/en/latest/bots/
- Bot management docs: https://nextcloud-talk.readthedocs.io/en/latest/bot-management/
- Conversation API: https://nextcloud-talk.readthedocs.io/en/latest/conversation/
- Chat API: https://nextcloud-talk.readthedocs.io/en/latest/chat/
- Call API: https://nextcloud-talk.readthedocs.io/en/latest/call/
- Recording API: https://nextcloud-talk.readthedocs.io/en/latest/recording/
- Constants: https://nextcloud-talk.readthedocs.io/en/latest/constants/
- OCS OpenAPI (Swagger UI): https://docs.nextcloud.com/server/latest/developer_manual/_static/openapi.html

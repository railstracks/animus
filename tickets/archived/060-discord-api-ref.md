# Discord API Reference — Adapter 060

Consolidated from docs.discord.com, May 26 2026.

## Base URL

```
https://discord.com/api/v10
```

All REST endpoints are prefixed with this. Auth: `Authorization: Bot <token>` header.

## Auth

- **Bot token**: single static token, no refresh, no OAuth. Created in Developer Portal.
- **Header**: `Authorization: Bot <token>` on all requests.
- **No token refresh needed** — token is valid until revoked in Developer Portal.

## Snowflake IDs

All Discord IDs (user, channel, guild, message, role, emoji) are 64-bit integers represented as strings. Must be treated as strings in JSON (Lua numbers can't represent 64-bit ints precisely).

## Key REST Endpoints (Phase 1)

### Messages

| Action | Method | Endpoint | Notes |
|--------|--------|----------|-------|
| Send message | POST | `/channels/{channel.id}/messages` | Body: `{content, embeds?, message_reference?, attachments?}`. Content max 2000 chars (4000 for Nitro). |
| Edit message | PATCH | `/channels/{channel.id}/messages/{message.id}` | Only own messages. |
| Delete message | DELETE | `/channels/{channel.id}/messages/{message.id}` | Own messages: always. Others: requires MANAGE_MESSAGES permission. |
| Get messages | GET | `/channels/{channel.id}/messages` | Params: `around`, `before`, `after`, `limit` (max 100). Uses snowflake-based pagination. |
| Get single message | GET | `/channels/{channel.id}/messages/{message.id}` | |
| Get channel | GET | `/channels/{channel.id}` | Returns channel object. |
| List guild channels | GET | `/guilds/{guild.id}/channels` | All channels in a guild. |
| Get guild | GET | `/guilds/{guild.id}` | Guild info. Requires bot to be member. |
| List guilds | GET | `/users/@me/guilds` | Guilds the bot is in. Params: `before`, `after`, `limit` (max 200). |
| Add reaction | PUT | `/channels/{channel.id}/messages/{message.id}/reactions/{emoji}/@me` | emoji can be unicode (😀) or custom (`:name:id`). Needs URL encoding. |
| Typing indicator | POST | `/channels/{channel.id}/typing` | Triggers "Bot is typing..." for 10 seconds. |

### Message Object (key fields)

```json
{
  "id": "snowflake",
  "channel_id": "snowflake",
  "author": {"id": "snowflake", "username": "string", "bot": true, ...},
  "content": "string",          // EMPTY if no MESSAGE_CONTENT intent!
  "timestamp": "ISO8601",
  "edited_timestamp": "?ISO8601",
  "mentions": [{"id": "snowflake", "username": "...", ...}],
  "mention_everyone": false,
  "embeds": [],
  "attachments": [],
  "reactions": [],
  "pinned": false,
  "type": 0,
  "message_reference": {"message_id": "snowflake", "channel_id": "snowflake", "guild_id": "snowflake"},
  "referenced_message": null | message_object
}
```

**Critical:** Without MESSAGE_CONTENT intent (privileged), `content`, `embeds`, `attachments`, and `components` will be empty for messages in guilds. DMs are unaffected.

### Channel Object (key fields)

```json
{
  "id": "snowflake",
  "type": 0,               // 0=GUILD_TEXT, 1=DM, 3=GROUP_DM, 4=GUILD_CATEGORY, 5=GUILD_ANNOUNCEMENT, ...
  "guild_id": "snowflake",  // null for DMs
  "name": "string",         // null for DMs
  "topic": "?string",
  "last_message_id": "?snowflake",
  "nsfw": false,
  "parent_id": "?snowflake" // category or parent channel
}
```

Channel types: 0=text, 1=DM, 3=group DM, 4=category, 5=announcement, 10=thread, 11=public thread, 12=private thread, 13=stage, 14=directory, 15=forum, 16=media.

### Guild Object (key fields)

```json
{
  "id": "snowflake",
  "name": "string",
  "icon": "?string",          // icon hash, construct URL: https://cdn.discordapp.com/icons/{id}/{icon}.png
  "owner_id": "snowflake",
  "member_count": 0,          // approximate
  "preferred_locale": "en-US",
  "approximate_member_count": 0,
  "approximate_presence_count": 0
}
```

### Send Message Body

```json
{
  "content": "message text",           // max 2000 chars
  "message_reference": {               // for replies
    "message_id": "snowflake",
    "channel_id": "snowflake",         // optional if same channel
    "guild_id": "snowflake"            // optional
  },
  "embeds": [{                         // rich embeds
    "title": "string",
    "description": "string",
    "url": "string",
    "color": 0,
    "fields": [{"name": "...", "value": "...", "inline": false}],
    "footer": {"text": "...", "icon_url": "..."},
    "author": {"name": "...", "url": "..."},
    "thumbnail": {"url": "..."},
    "image": {"url": "..."}
  }]
}
```

## Gateway Protocol (Phase 2)

### Connection Lifecycle

1. `GET /gateway/bot` → `{"url": "wss://gateway.discord.gg/?v=10&encoding=json"}`
2. Connect to WSS URL
3. Receive Hello (op 10): `{"op": 10, "d": {"heartbeat_interval": 41250}}`
4. Send Identify (op 2): `{"op": 2, "d": {"token": "...", "intents": 38401, "properties": {"os": "linux", "browser": "animus", "device": "animus"}}}`
5. Receive Ready (op 0, t=READY): `{"op": 0, "s": 1, "t": "READY", "d": {"user": {...}, "guilds": [...], "session_id": "..."}}`
6. Send Heartbeat (op 1) every heartbeat_interval ms: `{"op": 1, "d": <last_sequence>}`
7. Receive Heartbeat ACK (op 11)
8. Receive Dispatch events (op 0): `{"op": 0, "s": 42, "t": "MESSAGE_CREATE", "d": {...}}`

### Key Opcodes

| Opcode | Name | Direction | Description |
|--------|------|-----------|-------------|
| 0 | DISPATCH | Server→Client | Event payload (t field = event name) |
| 1 | HEARTBEAT | Client→Server | Keepalive (d = last sequence or null) |
| 2 | IDENTIFY | Client→Server | Initial auth (token, intents, properties) |
| 7 | RECONNECT | Server→Client | Server requests reconnect |
| 10 | HELLO | Server→Client | Heartbeat interval |
| 11 | HEARTBEAT_ACK | Server→Client | Heartbeat acknowledged |
| 9 | INVALID_SESSION | Server→Client | Session invalid, must re-identify |

### Resume (after disconnect)

```json
{"op": 6, "d": {"token": "...", "session_id": "...", "seq": 42}}
```

If resume fails (op 9 with `d: false`), must re-identify from scratch.

### Required Intents (bitmask)

| Intent | Bit | Value | Required For |
|--------|-----|-------|--------------|
| GUILDS | 0 | 1 | Guild create/update/delete, channel create/update/delete |
| GUILD_MEMBERS | 1 | 2 | Member join/update/leave (PRIVILEGED) |
| GUILD_MESSAGES | 9 | 512 | Message create/update/delete in guilds |
| GUILD_MESSAGE_REACTIONS | 10 | 1024 | Reaction add/remove in guilds |
| DIRECT_MESSAGES | 12 | 4096 | Message create/update/delete in DMs |
| MESSAGE_CONTENT | 15 | 32768 | Message content in guilds (PRIVILEGED) |
| GUILD_PRESENCES | 8 | 256 | User online/offline status (PRIVILEGED) |

Minimum for bot: `GUILDS | GUILD_MESSAGES | DIRECT_MESSAGES | MESSAGE_CONTENT` = 1 + 512 + 4096 + 32768 = **37377**

Without MESSAGE_CONTENT: content field will be empty string for guild messages. DMs unaffected.

### Key Gateway Events

| Event | Trigger | Intent |
|-------|---------|--------|
| MESSAGE_CREATE | New message sent | GUILD_MESSAGES or DIRECT_MESSAGES |
| MESSAGE_UPDATE | Message edited | GUILD_MESSAGES or DIRECT_MESSAGES |
| MESSAGE_DELETE | Message deleted | GUILD_MESSAGES or DIRECT_MESSAGES |
| MESSAGE_REACTION_ADD | Reaction added | GUILD_MESSAGE_REACTIONS or DIRECT_MESSAGE_REACTIONS |
| INTERACTION_CREATE | Slash command / button click | (automatic, no intent needed) |
| READY | Initial connection complete | (automatic) |
| GUILD_CREATE | Bot joins guild / guild becomes available | GUILDS |
| CHANNEL_CREATE | New channel created | GUILDS |

### MESSAGE_CREATE Event (op 0)

```json
{
  "op": 0,
  "s": 42,
  "t": "MESSAGE_CREATE",
  "d": {
    "id": "snowflake",
    "channel_id": "snowflake",
    "guild_id": "snowflake",      // null for DMs
    "author": {"id": "snowflake", "username": "...", "bot": false, ...},
    "content": "message text",
    "mentions": [...],
    "mention_everyone": false,
    "timestamp": "ISO8601",
    "message_reference": null | {...},
    "type": 0                      // 0=default, 7=server_join, 8=server_boost, ...
  }
}
```

## Rate Limits

- **Global:** ~50 requests/second. 429 with `retry_after` + `X-RateLimit-Global: true`.
- **Per-route:** Dynamic buckets via `X-RateLimit-Bucket`, `X-RateLimit-Limit`, `X-RateLimit-Remaining`, `X-RateLimit-Reset-After` headers.
- **Gateway:** 120 events per 60 seconds per connection.
- **Message send:** 5 messages per channel per 5 seconds (undocumented, empirically observed).
- **Do NOT hardcode limits** — parse response headers.

## Privileged Intents

For bots in **<100 servers**: enable in Developer Portal (no approval needed).
For bots in **100+ servers**: must apply for verification + privileged intent approval.

Three privileged intents:
1. **GUILD_MEMBERS** (bit 1) — member join/leave/update events
2. **MESSAGE_CONTENT** (bit 15) — actual message content in guilds
3. **GUILD_PRESENCES** (bit 8) — user online/offline status

**Practical implication:** For Animus agents in small servers (<100), just toggle these on in the Developer Portal. No approval process.

## DM Channels

DMs use channel type 1. Key differences:
- No `guild_id` on DM channel/message objects
- MESSAGE_CONTENT intent NOT required for DM content
- DM channels are created on-the-fly: `POST /users/@me/channels` with `{"recipient_id": "user_id"}`
- Or just send a message directly to the user: `POST /channels/{dm_channel_id}/messages`

## Threads

Threads are sub-channels (type 10/11/12). Key:
- Thread messages: same `/channels/{thread_id}/messages` endpoint
- Thread creation: `POST /channels/{channel_id}/threads` (type 11=public, 12=private)
- Thread members separate from channel members
- `parent_id` on thread channels = the parent text channel
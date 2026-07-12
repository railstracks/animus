## Overview

VKontakte (VK) is a Russian social network. The Animus VK adapter implements a **community bot** pattern — the bot operates as a VK community (group), responding to user messages and wall interactions via the Bots Long Poll API.

VK's officially supported bot pattern: users message the community, the bot replies. No personal account automation.

## Authentication

| Method | Description |
|--------|-------------|
| Community Access Token | Static token generated in group settings. No expiry, no refresh. Scoped to community permissions. |

### Token Generation

1. Go to community → Manage → API usage → Create token
2. Select required permissions (see Scopes below)
3. Token is permanent — no OAuth dance required

### Required Scopes

| Scope | Used By |
|-------|---------|
| `messages` | `messages.send`, `messages.getHistory`, `messages.getConversations` |
| `wall` | `wall.post`, `wall.createComment`, `wall.delete` |
| `likes` | `likes.add` |
| `groups` | `groups.getLongPollServer` |

## Base URL

| URL | Description |
|-----|-------------|
| https://api.vk.ru/method | VK REST API (method calls) |
| https://lp.vk.ru/whp/{group_id} | Long Poll server (returned by getLongPollServer) |

### Common Parameters

All API methods accept these query/body parameters:

| Name | Type | Required | Description |
|------|------|----------|-------------|
| access_token | string | ✓ | Community access token |
| v | string | ✓ | API version. Animus uses `5.131` |

## APIs — Messaging

### POST /method/messages.send

Send a message to a user or group chat.

#### Parameters

| Name | Type | Required | Description |
|------|------|----------|-------------|
| peer_id | integer | ✓ | Recipient: user ID for DMs, `2000000000 + local_id` for group chats |
| message | string | ✓ | Message text |
| random_id | integer | ✓ | Random integer for deduplication. Must be unique per message. |
| access_token | string | ✓ | Community access token |
| v | string | ✓ | API version |

#### Response

```json
{ "response": 12345 }
```

The response value is the message ID.

#### Notes

- `peer_id` is the universal addressing field — replaces deprecated `user_id` and `chat_id`
- Group chat IDs are `2000000000 + local_id` (e.g. `2000000001`)
- `random_id` must be unique; duplicate values are silently dropped by VK

### GET /method/messages.getHistory

Get conversation history with a peer.

#### Parameters

| Name | Type | Required | Description |
|------|------|----------|-------------|
| peer_id | integer | ✓ | Peer to fetch history for |
| count | integer | | Max messages (default 20, max 200) |
| offset | integer | | Pagination offset |

#### Response

```json
{
  "response": {
    "count": 42,
    "items": [
      {
        "id": 123,
        "from_id": 1115662869,
        "text": "Hello",
        "date": 1716300000,
        "out": 0
      }
    ]
  }
}
```

### GET /method/messages.getConversations

List active conversations.

#### Parameters

| Name | Type | Required | Description |
|------|------|----------|-------------|
| count | integer | | Max conversations (default 20) |
| offset | integer | | Pagination offset |

## APIs — Wall

### POST /method/wall.post

Create a post on the community wall.

#### Parameters

| Name | Type | Required | Description |
|------|------|----------|-------------|
| owner_id | integer | | Wall owner. For community wall: `-group_id` (negative) |
| message | string | ✓ | Post text |
| access_token | string | ✓ | Community access token |
| v | string | ✓ | API version |

#### Notes

- `owner_id` must be negative for community walls (e.g. `-238909265`)
- If `owner_id` is omitted, posts to the token owner's wall

### POST /method/wall.createComment

Add a comment to a wall post. Supports threading.

#### Parameters

| Name | Type | Required | Description |
|------|------|----------|-------------|
| owner_id | integer | | Wall owner. For community: `-group_id` |
| post_id | integer | ✓ | Post ID to comment on |
| reply_to_comment | integer | | Comment ID to reply to (creates threaded reply) |
| message | string | ✓ | Comment text |
| access_token | string | ✓ | Community access token |
| v | string | ✓ | API version |

#### Threading

VK supports branching comment threads:
- Top-level comments: omit `reply_to_comment`
- Threaded replies: set `reply_to_comment` to the parent comment's ID
- The response lands in the correct subthread automatically

### POST /method/wall.delete

Delete a wall post.

#### Parameters

| Name | Type | Required | Description |
|------|------|----------|-------------|
| owner_id | integer | | Wall owner (negative for community) |
| post_id | integer | ✓ | Post ID to delete |

## APIs — Engagement

### POST /method/likes.add

Like a post, comment, or other object.

#### Parameters

| Name | Type | Required | Description |
|------|------|----------|-------------|
| type | string | ✓ | Object type: `post`, `comment`, `photo`, etc. |
| owner_id | integer | | Content owner ID |
| item_id | integer | ✓ | Object ID to like |

## APIs — Long Poll

### GET /method/groups.getLongPollServer

Get the Long Poll server URL for real-time event streaming.

#### Parameters

| Name | Type | Required | Description |
|------|------|----------|-------------|
| group_id | integer | ✓ | Community ID |

#### Response

```json
{
  "response": {
    "server": "https://lp.vk.ru/whp/238909265",
    "key": "eyJhbGci...",
    "ts": "34"
  }
}
```

### GET {server}?act=a_check&key={key}&ts={ts}&wait={wait}

Long Poll request. Blocks until events arrive or timeout.

#### Parameters

| Name | Type | Required | Description |
|------|------|----------|-------------|
| act | string | ✓ | Always `a_check` |
| key | string | ✓ | Key from getLongPollServer |
| ts | string | ✓ | Last event timestamp (from previous response) |
| wait | integer | ✓ | Timeout in seconds (server holds connection) |

#### Response (success)

```json
{
  "ts": "35",
  "updates": [
    {
      "type": "wall_reply_new",
      "event_id": "...",
      "object": { ... }
    }
  ]
}
```

#### Response (failure)

```json
{ "failed": 1, "ts": "35" }
{ "failed": 2 }
{ "failed": 3 }
```

| Code | Meaning | Recovery |
|------|---------|----------|
| 1 | History outdated | Update `ts` from response, continue |
| 2 | Key expired | Re-fetch with `getLongPollServer` |
| 3 | Information lost | Re-fetch with `getLongPollServer` |

#### Notes

- VK may re-deliver events when the poll request times out — client must deduplicate by event ID
- When the poll connection times out, VK re-sends recent events in the next response
- `wait` parameter determines how long the server holds the connection open (Animus uses 25s)

## Events

### message_new

New message in a community conversation (DM or group chat).

#### Object fields

| Field | Type | Description |
|-------|------|-------------|
| id | integer | Message ID |
| from_id | integer | Sender user ID |
| peer_id | integer | Conversation peer (user ID or 2000000000+local_id) |
| text | string | Message text |
| date | integer | Unix timestamp |

#### Notes

- API >= 5.103: message is nested under `object.message`
- `peer_id` is the universal routing field — use for all reply addressing

### wall_post_new

New post on the community wall.

#### Object fields

| Field | Type | Description |
|-------|------|-------------|
| id | integer | Post ID |
| from_id | integer | Author ID (negative for community posts) |
| text | string | Post text |
| date | integer | Unix timestamp |

### wall_reply_new

New comment on a wall post. Supports threading.

#### Object fields

| Field | Type | Description |
|-------|------|-------------|
| id | integer | Comment ID |
| from_id | integer | Author ID. Community comments use positive `group_id` |
| post_id | integer | Parent post ID |
| reply_to_comment | integer | Parent comment ID (present for threaded replies) |
| reply_to_user | integer | User being replied to (present for threaded replies) |
| text | string | Comment text |
| date | integer | Unix timestamp |

#### Threading

- Top-level comments: `reply_to_comment` is absent
- Threaded replies: `reply_to_comment` contains the parent comment's ID
- Use `reply_to_comment` in `wall.createComment` to reply in the correct subthread

#### Self-reply detection

When the community posts a comment (via `wall.createComment`), VK fires a `wall_reply_new` event with `from_id = +group_id` (positive). Filter these out by checking both `from_id == group_id` and `from_id == -group_id`.

## Animus Integration

### Architecture

```
VK Long Poll → SocialEventPoller → DispatchToSession → LLM → SendAutoReply → VK API
```

1. **Long Poll** fetches events from VK in a persistent loop
2. **SocialEventPoller** processes events (dedup, self-reply filter, user resolution)
3. **DispatchToSession** routes to per-conversation sessions (chat/wall/threaded)
4. **LLM** generates a plain text response (social tool excluded from these sessions)
5. **SendAutoReply** calls VK API directly to deliver the response

### Routing

| Event | Routing Key | Session Type |
|-------|-------------|-------------|
| message_new | `peer:{peer_id}` | chat |
| wall_post_new | `post:{post_id}` | wall |
| wall_reply_new (top-level) | `post:{post_id}` | wall |
| wall_reply_new (threaded) | `post:{post_id}:comment:{reply_to_comment}` | wall |

### Configuration

Stored in `agent_config` table (per-agent):

| Key | Description |
|-----|-------------|
| `social.{platform_id}.type` | Adapter type (`vk`) |
| `social.{platform_id}.access_token` | Community access token |
| `social.{platform_id}.group_id` | Community group ID |
| `social.{platform_id}.polling.enabled` | Enable Long Poll |
| `social.{platform_id}.polling.method` | Polling method (`long_poll`) |
| `social.{platform_id}.polling.wait` | Long Poll wait seconds |
| `social.{platform_id}.polling.events` | Comma-separated event types |

### Gotchas

- **Community comments use positive `from_id`** — not `-group_id`. Self-reply filter must check both.
- **No `random_id` on `wall.createComment`** — unlike `messages.send`, wall comments have no built-in dedup.
- **`wall.get` returns error 27 with community tokens** — "Group authorization failed". Community tokens cannot browse walls; only user tokens can.
- **Lua scripts stored in `lua_scripts.source`** — daemon loads from DB at startup, not from disk files.
- **API domain is `api.vk.ru`** — not `api.vk.com`.
- **Bots Long Poll API** is different from User Long Poll API — uses `groups.getLongPollServer`, not `messages.getLongPollServer`.

# Ticket 055: Telegram Adapter

**Status:** Draft  
**Priority:** High  
**Depends on:** Ticket 051 (Long Poll + Auto-Reply) — session dispatch patterns  
**Blocks:** —  

## Summary

Telegram Bot API adapter for Animus. First-class chat channel integration using the official Bot API — long polling for inbound events, REST for outbound actions. No ToS risk, no reverse engineering, no protocol complexity.

## Motivation

Telegram is the lowest-friction chat platform for bot integration:
- Official Bot API, explicitly designed for bots
- Free, no business entity required
- BotFather for token generation — seconds to set up
- Supports groups, channels, inline queries, payments, Mini Apps
- 10M+ bots already on the platform
- Zero ToS risk — bots are the intended use case

This is the ideal first chat channel after the existing social adapters (Bluesky, VK, Mastodon).

## API Overview

**Base URL:** `https://api.telegram.org/bot<token>/METHOD_NAME`  
**Auth:** Bot token (from @BotFather)  
**Format:** JSON over HTTPS  
**Methods:** Case-insensitive, UTF-8  

### Getting Updates (two modes, mutually exclusive)

**1. Long Polling (`getUpdates`)**
```
POST /bot<token>/getUpdates
Params: offset, limit (1-100), timeout (seconds), allowed_updates
Returns: Array of Update objects
```
- Long polling with configurable timeout
- Offset-based: `offset = last_update_id + 1`
- Updates stored for 24h max
- This is the preferred mode for Animus (no need to expose an HTTP server)

**2. Webhooks (`setWebhook`)**
- Telegram POSTs Update objects to our HTTPS endpoint
- Requires publicly accessible server with TLS
- Supports secret token for verification (`X-Telegram-Bot-Api-Secret-Token` header)
- Ports: 443, 80, 88, 8443
- Available if the Animus admin server is already exposed

**Recommendation:** Long polling as default (same pattern as VK Long Poll). Webhook as optional alternative.

### Update Types (relevant subset)

| Update Type | Description | Priority |
|-------------|-------------|----------|
| `message` | New incoming message (text, photo, sticker, etc.) | P1 |
| `edited_message` | Edited version of a known message | P2 |
| `channel_post` | New channel post | P2 |
| `edited_channel_post` | Edited channel post | P3 |
| `callback_query` | Inline keyboard button press | P2 |
| `message_reaction` | Reaction changed (bot must be admin) | P3 |
| `my_chat_member` | Bot was added/removed/blocked | P1 |
| `chat_member` | Chat member status changed (bot must be admin) | P3 |

### Key API Methods

| Method | Purpose | Phase |
|--------|---------|-------|
| `sendMessage` | Send text (1-4096 chars), supports reply, threading, parse modes | P1 |
| `forwardMessage` | Forward message to another chat | P2 |
| `copyMessage` | Copy message content without original forward header | P2 |
| `sendPhoto` | Send photo (file or URL) | P2 |
| `sendDocument` | Send document/file | P2 |
| `sendAudio` / `sendVideo` / `sendVoice` | Media messages | P3 |
| `sendLocation` / `sendVenue` | Location sharing | P3 |
| `sendChatAction` | Typing indicator, uploading, etc. | P1 |
| `setMessageReaction` | React to a message (emoji) | P2 |
| `editMessageText` | Edit a previously sent message | P2 |
| `deleteMessage` | Delete a message | P2 |
| `getChat` | Get chat info | P1 |
| `getChatMember` | Get member info | P2 |
| `leaveChat` | Leave a group/channel | P3 |
| `pinChatMessage` / `unpinChatMessage` | Pin/unpin messages | P3 |
| `getUserProfilePhotos` | Get user's profile photos | P3 |
| `getFile` | Get file download URL | P2 |

### Message Threading

Telegram supports forum topics (message threads) in supergroups and private chats:
- `message_thread_id` parameter in `sendMessage` — targets a specific topic
- `reply_parameters` — reply to a specific message
- Threaded mode in private chats (opt-in via BotFather) — enables parallel conversations
- Relevant for session dispatch: different threads → different sessions

### Parse Modes

| Mode | Description |
|------|-------------|
| `MarkdownV2` | Full Markdown support (escaped special chars) |
| `HTML` | Basic HTML tags (b, i, u, s, code, pre, a, spoiler) |
| `Markdown` (legacy) | Deprecated |

**Recommendation:** Use `MarkdownV2` for outbound agent messages.

## Tool Definition

The Telegram adapter is **not** a tool the agent calls directly — it's a transport layer following the VK auto-reply pattern:

1. Inbound message arrives → `TelegramEventConsumer` dispatches to session
2. Agent responds with plain text → `SendAutoReply()` routes to Telegram adapter
3. Telegram adapter calls `sendMessage`

For **proactive** actions (agent decides to send, search chats, manage groups), expose a `TelegramTool` (IToolHandler) with:

| Action | Description |
|--------|-------------|
| `list_chats` | List active chats (with pagination) |
| `get_chat` | Get chat info (name, type, member count) |
| `send_message` | Send to arbitrary chat (proactive) |
| `send_photo` | Send an image |
| `send_document` | Send a file |
| `edit_message` | Edit a previously sent message |
| `delete_message` | Delete a message |
| `set_reaction` | React to a message |
| `forward_message` | Forward a message to another chat |
| `get_file` | Download a file/attachment |

Tool actions available in all session types except `telegram:auto_reply` (where auto-reply handles outbound).

## Session Dispatch

### Chat Types

| Chat Type | Session Type | Notes |
|-----------|-------------|-------|
| Private (1:1) | `telegram:private` | Direct message conversation |
| Group | `telegram:group` | Group chat (bot sees messages based on privacy mode) |
| Supergroup (forum) | `telegram:forum` | Threaded — each topic maps to a session |
| Channel | `telegram:channel` | Bot as admin, posts and comments |

### Dispatch Rules (configurable per bot instance)

```json
{
  "bot_id": "my-animus-bot",
  "dispatch_rules": [
    {
      "match": { "chat_type": "private" },
      "action": "resume_session",
      "session_key": "telegram:{chat_id}",
      "prompt": "New message from {user_first_name}:"
    },
    {
      "match": { "chat_type": "group", "is_mentioned": true },
      "action": "new_session",
      "session_key": "telegram:{chat_id}:{message_thread_id}",
      "prompt": "{user_first_name} mentioned you in {chat_title}:"
    },
    {
      "match": { "chat_type": "forum", "message_thread_id": "*" },
      "action": "resume_session",
      "session_key": "telegram:{chat_id}:{message_thread_id}",
      "prompt": "New post in topic '{topic_name}':"
    }
  ]
}
```

### Thread Resolution

- **Private chats:** `chat_id` → session key (one session per user)
- **Groups:** `chat_id` + optional `message_thread_id` → session key
- **Forums:** `chat_id` + `message_thread_id` (required) → session key
- No external thread index needed — Telegram provides chat and thread IDs natively

## Account Configuration

```json
{
  "id": "my-animus-bot",
  "backend": "telegram",
  "display_name": "Animus Bot",
  "telegram": {
    "bot_token_encrypted": "...",
    "update_mode": "long_poll",
    "poll_timeout": 30,
    "allowed_updates": ["message", "edited_message", "callback_query", "my_chat_member"],
    "parse_mode": "MarkdownV2"
  },
  "dispatch_rules": [ ... ]
}
```

## Database Schema

```sql
-- Telegram bot configurations
CREATE TABLE telegram_bots (
    id TEXT PRIMARY KEY,
    instance_id TEXT NOT NULL,
    bot_token_encrypted TEXT NOT NULL,
    display_name TEXT,
    update_mode TEXT NOT NULL DEFAULT 'long_poll' CHECK(update_mode IN ('long_poll', 'webhook')),
    poll_timeout INTEGER NOT NULL DEFAULT 30,
    allowed_updates TEXT NOT NULL DEFAULT '["message", "edited_message", "callback_query", "my_chat_member"]',
    parse_mode TEXT NOT NULL DEFAULT 'MarkdownV2',
    last_update_id INTEGER DEFAULT 0,
    enabled INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (instance_id) REFERENCES instances(id)
);

-- Dispatch rules (shared structure, per-bot)
CREATE TABLE telegram_dispatch_rules (
    id TEXT PRIMARY KEY,
    bot_id TEXT NOT NULL,
    match_config TEXT NOT NULL,    -- JSON
    action_config TEXT NOT NULL,   -- JSON
    priority INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (bot_id) REFERENCES telegram_bots(id)
);

-- Chat state (for tracking sessions per chat/thread)
CREATE TABLE telegram_chat_state (
    chat_id INTEGER NOT NULL,
    message_thread_id INTEGER DEFAULT 0,
    bot_id TEXT NOT NULL,
    session_key TEXT,
    last_message_id INTEGER,
    updated_at TEXT NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (chat_id, message_thread_id, bot_id),
    FOREIGN KEY (bot_id) REFERENCES telegram_bots(id)
);
```

## C++ Classes

```
src/social/telegram/
├── TelegramAdapter.h/.cpp          # High-level adapter (public interface)
├── TelegramBotApi.h/.cpp           # HTTP client for Bot API methods
├── TelegramEventConsumer.h/.cpp    # Bridges adapter to SocialEventPoller
├── TelegramLongPoll.h/.cpp         # Long polling getUpdates loop
├── TelegramWebhook.h/.cpp          # Webhook handler (optional, Phase 2)
├── TelegramTypes.h/.cpp            # Update, Message, Chat, User structs
├── TelegramConfig.h/.cpp           # Bot config loading, credential decryption
└── TelegramConstants.h             # API base URL, default values
```

### Dependencies

| Library | Purpose | Notes |
|---------|---------|-------|
| **libcurl** | HTTP requests to Bot API | TLS, JSON, multipart for file uploads |

No additional crypto or protocol libraries needed — the Bot API is plain HTTPS + JSON.

## Admin API

```
POST   /api/{instance}/telegram/bots                -- Register bot (token + config)
GET    /api/{instance}/telegram/bots                -- List bots
GET    /api/{instance}/telegram/bots/{id}           -- Get bot details
PUT    /api/{instance}/telegram/bots/{id}           -- Update config
DELETE /api/{instance}/telegram/bots/{id}           -- Remove bot
GET    /api/{instance}/telegram/bots/{id}/status    -- Poll status, last update, webhook info
POST   /api/{instance}/telegram/bots/{id}/test      -- Test connection (calls getMe)
POST   /api/{instance}/telegram/bots/{id}/start     -- Start polling
POST   /api/{instance}/telegram/bots/{id}/stop      -- Stop polling
```

## Implementation Phases

### Phase 1: Core Messaging
- [ ] TelegramBotApi — HTTP client (getMe, getUpdates, sendMessage)
- [ ] TelegramTypes — Update, Message, Chat, User structs (JSON parsing)
- [ ] TelegramLongPoll — Long polling loop with offset tracking
- [ ] TelegramEventConsumer — dispatch to session
- [ ] TelegramAdapter — send auto-reply via sendMessage
- [ ] TelegramConfig — SQLite tables, bot config loading
- [ ] Admin API endpoints (CRUD + start/stop)
- [ ] Integration with SendAutoReply
- **Deliverable:** Bot receives messages and responds via auto-reply

### Phase 2: Rich Interactions
- [ ] TelegramTool (IToolHandler) — proactive actions (send, edit, delete, forward)
- [ ] Message threading support (forum topics, reply-to)
- [ ] Typing indicators (`sendChatAction`)
- [ ] Reactions (`setMessageReaction`)
- [ ] Parse mode support (MarkdownV2, HTML)
- [ ] File handling (sendPhoto, sendDocument, getFile)
- **Deliverable:** Full bidirectional messaging with proactive capabilities

### Phase 3: Advanced Features
- [ ] Webhook mode (alternative to long polling)
- [ ] Inline keyboard support (callback queries)
- [ ] Group management (pin, unpin, member queries)
- [ ] Channel posting
- [ ] Media messages (audio, video, voice, location)
- [ ] Multi-bot support (multiple Telegram bots per Animus instance)
- **Deliverable:** Complete Telegram integration

## Comparison with VK Adapter

| Aspect | VK Adapter | Telegram Adapter |
|--------|-----------|-----------------|
| Update mechanism | Long Poll (VK-specific) | Long Poll (`getUpdates`) or Webhooks |
| Auth | Community token | Bot token (BotFather) |
| Protocol | Custom HTTP + JSON | Standard HTTPS + JSON |
| Crypto | None (plaintext API) | None (plaintext API) |
| Threading | Wall comment threading | Forum topics + message replies |
| ToS risk | None (community bot) | None (official bot API) |
| Media | Upload via API | Upload via multipart/form-data |
| Complexity | Medium (Long Poll specifics) | Low (clean REST API) |

Telegram is significantly simpler than VK — no custom protocols, no encryption, no token dictionaries. Plain HTTPS + JSON. Expected implementation time is shorter than the VK adapter.

## Reference Documentation

- `docs/telegram/api.md` — Full Bot API reference (543 KB)
- `docs/telegram/bots.md` — Bot platform overview
- `docs/telegram/features.md` — Detailed feature guide
- https://core.telegram.org/bots/api — Official online reference

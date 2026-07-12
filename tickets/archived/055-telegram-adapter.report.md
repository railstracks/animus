# Ticket Report 055 — Telegram Adapter

**Date:** 2026-05-22
**Status:** Phase 1 Complete
**Commits:** `c3229f3`, `fc77d87`, `2e50de7`, `29e9696`
**Duration:** May 22, 2026

## What Was Done

### 1. Telegram Types (`TelegramTypes.h`)
Core data structures with JSON parsing via jsoncpp:

- **`User`** — id, first_name, last_name, username, language_code, is_bot. `DisplayName()` falls back through username → first_name → "Unknown".
- **`Chat`** — id, type (private/group/supergroup/channel), title, username, first_name, last_name. `IsPrivate()` and `DisplayName()` helpers.
- **`Message`** — message_id, from (User), chat (Chat), date, text, message_thread_id, reply_to_message_id, forward_from.
- **`Update`** — update_id, message, edited_message, callback_query, my_chat_member (member_status, member_chat_id).
- **`BotInfo`** — id, username, first_name. Returned by `getMe`.
- All structs have `ParseFromJson()` static methods. Update parsing dispatches to sub-struct parsers.

### 2. Telegram Bot API Client (`TelegramBotApi.h/.cpp`)
HTTP client wrapper over Animus's existing `HttpClient` (libcurl-based):

| Method | Purpose |
|--------|---------|
| `GetMe` | Verify bot identity at startup |
| `GetUpdates` | Long poll with offset, limit, timeout, allowed_updates |
| `SendMessage` | Send text with optional reply_to, parse_mode, message_thread_id |
| `SendChatAction` | Typing indicator ("typing", "upload_photo", etc.) |
| `EditMessageText` | Edit a previously sent message |
| `DeleteMessage` | Delete a message |
| `SetMessageReaction` | React with emoji |
| `GetChat` | Retrieve chat info |

Returns `std::optional` types — empty on failure, value on success. No exceptions thrown; errors logged to stderr.

### 3. Long Poll Loop
Integrated into `SocialEventPoller::TelegramLongPollLoop` (now part of `ChannelManager::TelegramLongPollLoop` after the channels refactor in Ticket 056):

- Calls `getMe` on startup to verify bot identity
- Long polls via `getUpdates` with configurable timeout
- Offset tracking: `last_update_id + 1` persisted to `AgentConfigStore` across restarts
- Exponential backoff on errors (5s × consecutive_errors, capped at 60s)
- Processes `message` updates (dispatches to session) and `my_chat_member` updates (logs bot added/removed)

### 4. Message Processing (`ProcessTelegramMessage`)
Routes inbound messages by chat type:

| Chat Type | Session Type | Routing Key |
|-----------|-------------|-------------|
| Private | `telegram:private` | `peer:{chat_id}` |
| Group/Supergroup | `telegram:group` | `peer:{chat_id}:{thread_id}` (thread_id if > 0) |

Prompt construction includes sender name, chat name, and topic thread indicator.

### 5. Auto-Reply Integration
`SendAutoReply` in `AgentKernel.cpp` extended with Telegram dispatch:

- Parses `peer_id` to `int64_t` for `chat_id`
- Calls `SendMessage` with the agent's response text
- Logs success (with message_id) or failure

### 6. Admin UI
Telegram added as a platform type in the existing `SocialView.vue` (now `ChannelsView.vue` after Ticket 056):

- Bot token field (password-masked on edit, plaintext on create)
- "Leave blank to keep existing token" hint on edit

## Architecture Notes

### Simplicity vs VK
Telegram is the simplest adapter in Animus. No crypto, no binary protocols, no token dictionaries — just HTTPS + JSON via libcurl. The entire Bot API client is ~350 lines. Compare with VK's Long Poll server/key/ts model and the more complex event structures.

### Long Poll vs Webhook
Phase 1 uses long polling (same pattern as VK). The `getUpdates` endpoint holds the connection open for up to `timeout` seconds, returning an array of `Update` objects. Offset-based acknowledgment means no lost updates. Webhook mode is deferred to Phase 3.

### Forum Topics = Sessions
Telegram supergroups can be forums with topics. Each topic has a `message_thread_id` which maps naturally to session routing — different topics get different agent sessions. This is the same pattern as VK wall comment threading.

## GCC Compatibility Fixes

Three build fixes specific to GCC's strictness:

1. **Aggregate default params** — `GetUpdatesOptions` had default values on struct members. GCC rejects `const Foo& opts = {}` for aggregates. Fixed with explicit default constructor.
2. **Same-class aggregate default** — Removed default parameter from `GetUpdates` signature; caller passes explicit `GetUpdatesOptions()`.
3. **Include order** — Missing headers surfaced by GCC's single-pass compilation.

## Files Changed

| File | Status | Purpose |
|------|--------|---------|
| `include/animus_kernel/social/TelegramTypes.h` | New | Update, Message, Chat, User, BotInfo structs |
| `include/animus_kernel/social/TelegramBotApi.h` | New | HTTP client wrapper declarations |
| `src/kernel/social/TelegramBotApi.cpp` | New | Full Bot API implementation |
| `src/kernel/social/SocialEventPoller.cpp` | Modified | TelegramLongPollLoop, ProcessTelegramMessage, ProcessTelegramChatMemberUpdate |
| `src/kernel/AgentKernel.cpp` | Modified | SendAutoReply Telegram case + include |
| `CMakeLists.txt` | Modified | Added TelegramBotApi.cpp |

## Acceptance Criteria

- [x] `TelegramBotApi` — HTTP client (getMe, getUpdates, sendMessage + 5 more)
- [x] `TelegramTypes` — Update, Message, Chat, User, BotInfo with JSON parsing
- [x] Long poll loop with offset tracking and persistence
- [x] Message dispatch to agent sessions (private + group)
- [x] Chat member tracking (bot added/removed)
- [x] Auto-reply via sendMessage
- [x] Admin UI token configuration
- [x] Clean build on GCC
- [ ] TelegramTool (IToolHandler) — proactive actions — Phase 2
- [ ] Webhook mode — Phase 3
- [ ] Inline keyboards, media, files — Phase 3
- [ ] Multi-bot support — Phase 3

## Remaining Work (Phase 2+)

- **Proactive tool** — `TelegramTool` with actions: list_chats, get_chat, send_message, send_photo, send_document, edit_message, delete_message, set_reaction, forward_message, get_file
- **Rich interactions** — typing indicators, reactions, parse modes (MarkdownV2), file handling
- **Webhook mode** — alternative to long polling for instances with public HTTPS
- **Advanced features** — inline keyboards, group management, channel posting, media messages

# Ticket 061 — Slack Adapter: Completion Report

**Date:** 2026-06-07  
**Status:** Phases 1 + 2 Complete, Phase 3 (Threading) Partial

## What Was Built

### Phase 1 — Lua REST Adapter (`scripts/slack.lua`)
- 11 actions: post, reply, browse, search, get_channel, list_channels, list_members, react, delete, edit, auth_test
- Dual HTTP primitives: form-encoded (Slack standard) and JSON body (for blocks/threads)
- Rate limit tracking per method, persisted to config
- auth.test caching — bot user ID resolved once
- Thread support via `thread_ts` parameter
- Emoji normalization in react action

### Phase 2 — C++ Socket Mode Loop (`ChannelManager.cpp`)
- `SlackSocketModeLoop`: `apps.connections.open` → wss:// URL → WebSocket → receive → ack → reconnect
- Handles `hello` (connected), `events_api` (dispatch messages), `disconnect` (reconnect)
- Per-envelope acking with `envelope_id`
- Auto-fallback to Phase 1 polling if `app_token` not configured
- Clean shutdown via `state->active` flag
- Accepts both Text (type=0) and Binary (type=1) WebSocket frame types

### Phase 3 (Partial) — Thread-Aware Channel Routing
- `threaded_replies` config toggle (default: true) — each top-level channel message starts its own thread session
- When enabled: top-level messages route to `chat:slack:<ts>`, replies use `thread_ts=<ts>`
- When disabled: all channel messages share `chat:slack:<channel_id>`, replies go to channel root
- Existing thread messages always route to their parent thread regardless of toggle
- Admin UI checkbox: "Thread replies in channels"

### C++ Routing
- `StartChannel`: `type == "slack"` → `SlackSocketModeLoop` thread
- `SlackPollingLoop` for fallback (auto-discovers channels via `conversations.list`)
- `SendReply`: `channel_type == "slack"` → `chat.postMessage` with thread support
- Self-message filter via `bot_id` (not just user ID)
- Mention filtering: `respond_to_mentions`, `respond_to_all_messages` (boolean→string fix)
- Auto-discovery of bot channels via `conversations.list` (is_member filter)

### System Prompt Context
- `ChannelContextProvider` now has a Slack branch — agent knows it's on Slack, that text replies are auto-routed, and that the social tool shouldn't be used for sending replies

## Bugs Found and Fixed

### Bug 1: `std::terminate` — Missing `m_pollers` move
- **Root cause:** Slack branch in `StartChannel` forgot `m_pollers[name] = std::move(poller)`. The `unique_ptr` destroyed on scope exit while thread was running → `std::terminate` on joinable thread.
- **Fix:** Added the move. Classic copy-paste error — every other channel type had it.

### Bug 2: WhatsApp daemon crash — Dangling stack-local pointers
- **Root cause:** `WhatsAppGatewayLoopInner` stored raw pointers to stack-local variables in `state->config`. After the 302s stale-connection check triggered, the function returned destroying locals. `SendReply` during reconnect sleep → crash.
- **Fix:** Null out config entries before function returns. `SendReply` checks for key existence.

### Bug 3: Slack events not arriving — Bot Events disabled
- **Root cause:** Slack app had Event Subscriptions enabled but Bot Events section was empty. Only Workspace Events were configured.
- **Fix:** Added `message.channels`, `message.im`, `message.groups`, `message.mpim` under Bot Events.

### Bug 4: `respond_to_all_messages` ignored — JSON boolean vs string
- **Root cause:** Admin UI sent boolean `true`, but C++ `GetString()` checks `isString()` → returns empty string for booleans. Comparison `"" == "true"` always false.
- **Fix:** Convert booleans to strings in `buildConfig` (`val ? "true" : "false"`). Applied to both Slack and Discord configs.

### Bug 5: vue-i18n `@` escape
- **Root cause:** `@` is special in vue-i18n (linked message format). "Respond to @mentions" caused compile error.
- **Fix:** `{'@'}` escape syntax.

### Bug 6: Slack sends binary WebSocket frames
- **Root cause:** Slack Socket Mode sometimes sends event payloads as binary (type=1) frames, not just text (type=0).
- **Fix:** Accept both `Text` and `Binary` frame types in the message handler.

### Bug 7: WhatsApp crash — `SSL_free` before `readThread_.join()`
- **Root cause:** `close()` called `SSL_free()` before `readThread_.join()`. The read thread was still inside `SSL_read()` on freed memory → SIGSEGV. Backtrace confirmed: crash in `SSL_read` → `BIO_test_flags` on freed SSL object.
- **Fix:** Reorder: close socket → join thread → then free SSL. No thread can be using SSL after join returns.

### Bug 8: Discord crash — Timer callback on destroyed WebSocket connection
- **Root cause:** Drogon timer callbacks (heartbeat, liveness check) fire after `wsPtr->stop()` has destroyed the internal connection. `getConnection()->send()` on destroyed connection → SIGSEGV. Backtrace: `TimerQueue::handleRead` → `DiscordGatewayLoop` lambda → `getConnection()->send()`.
- **Fix:** `gatewayAlive` flag set false on every `stop()` call, checked at top of every timer callback. Also null-check `getConnection()` before all `send()` calls.

### Bug 9: `threaded_replies` not serialized to config
- **Root cause:** Field was added to `formData` and the template but never included in the `buildConfig()` function that constructs the JSON config object sent to the API.
- **Fix:** Added `threaded_replies: String(formData.value.slack_threaded_replies)` to the Slack config builder.

## Session Key Format

Changed from legacy `channel:thread:<id>` to consistent `chat:slack:<id>` format:
- **Channel (non-threaded):** `chat:slack:C0B057FKB70` — one session per channel
- **Channel (threaded):** `chat:slack:1780836565.380429` — one session per top-level message
- **Existing thread:** `chat:slack:1780836565.380429` — always routes to parent thread
- **DM:** `chat:slack:D0B00T4VDE2` — one session per DM

Connector format parsed by `ChannelContextProvider`: `channel:chat:slack:<id>` → platform = "Slack"

## Documentation

- `docs/channel.md` — Channel Integration Guide covering all six integration points (connector loop, StartChannel, SendReply, system prompt context, Lua adapter, Admin UI), session key format, common pitfalls, and file checklist. Written to ensure future channel implementations follow consistent patterns.

## Files Modified

```
src/kernel/ChannelManager.cpp           # Slack routing, Socket Mode, Polling, SendReply, bug fixes
src/kernel/ChannelManager.h             # SlackSocketModeLoop declaration
src/kernel/DiscordGatewayLoop.cpp      # gatewayAlive flag, null-check guards
src/kernel/WhatsAppGatewayLoop.cpp     # Diagnostic logging, null config cleanup
src/kernel/whatsapp/WhatsAppTransport.cpp # Thread join before SSL_free
src/kernel/context/ChannelContextProvider.cpp # Slack system prompt context
src/main.cpp                            # SIGSEGV/SIGABRT backtrace handler
CMakeLists.txt                         # -rdynamic for readable backtraces
admin-ui/src/views/ChannelsView.vue    # Slack form fields, threaded_replies toggle, boolean→string
admin-ui/src/i18n/locales/en.ts        # Slack i18n labels
scripts/slack.lua                      # Lua REST adapter (11 actions)
docs/channel.md                       # Channel Integration Guide
tickets/061-slack-adapter.md          # This ticket (design document)
```

## Remaining Work (Phase 3+)

- [ ] Block Kit formatting for rich messages
- [ ] File upload via `files.upload`
- [ ] Modal interactions (if needed)
- [ ] Rate limit response handling in C++ loop (429 backoff in Socket Mode)
- [ ] Multi-workspace support (multiple Slack config instances)
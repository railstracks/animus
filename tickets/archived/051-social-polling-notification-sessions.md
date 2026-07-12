# Ticket 051 — Social Event Ingestion & Session Routing

**Priority:** P2 (enables autonomous social interaction — core infrastructure)
**Status:** Open
**Dependencies:** Ticket 050 (Bluesky Adapter) ✅, Ticket 052 (VK Adapter) ✅
**Created:** 2026-05-19
**Revised:** 2026-05-20 (v2 — Long Poll, session routing, multi-channel)

## Summary

A social event ingestion system that receives inbound events from social platforms (VK Long Poll, Bluesky REST polling, future WebSocket) and routes them to agent sessions based on conversation context. Each event channel (chat, wall posts, wall replies) has its own routing rule. The system maintains a session registry mapping conversation identifiers to active agent sessions, enabling context continuity across multiple interactions.

## Motivation

Tickets 050 and 052 delivered functional outbound actions — post, reply, send_message, etc. But they're all **reactive**: the agent only acts when a human prompts it. For autonomous social interaction (community management, customer engagement, conversational agents), the agent needs to **proactively receive and respond to inbound events**.

The VK adapter already provides `get_long_poll_server` — the hook for real-time event delivery. Bluesky will use REST polling for notifications. Both need the same routing infrastructure.

## Core Design: Session Routing

### Three Event Channels

Each channel has a distinct routing rule based on the event type:

#### 1. Chat Messages (`message_new`)
- **Identifier:** `peer_id` (user ID for DMs, `2000000000 + local_id` for group chats)
- **Routing:** `peer_id` → session mapping
  - New `peer_id` → create new agent session
  - Existing `peer_id` → resume the mapped session
- **Session context:** Full conversation history for that `peer_id`

#### 2. Wall Posts (`wall_post_new`)
- **Identifier:** `post_id` (the wall post ID)
- **Routing:** `post_id` → session mapping
  - User-originated post (`from_id > 0`) → new session, anchored to that `post_id`
  - Agent-originated post → register `post_id` under the session that called the tool
- **Session context:** The wall post and any subsequent comments/replies

#### 3. Wall Replies/Comments (`wall_reply_new`)
- **Identifier:** `post_id` (the parent wall post)
- **Routing:** `post_id` → session mapping
  - Match to the session that owns the `post_id`
  - If the agent created the post → route reply to that session
  - If a user created the post → route to the session handling that user's post
  - If no session exists → create new session anchored to the `post_id`

### Session Registry

A persistent mapping stored in the agent config store:

```
social.vk:community.sessions.peer:{peer_id} = "session_abc123"
social.vk:community.sessions.post:{post_id} = "session_def456"
social.bluesky:personal.sessions.uri:{at_uri} = "session_ghi789"
```

When an inbound event arrives:
1. Extract the routing key from the event (peer_id, post_id, etc.)
2. Look up in the session registry
3. If found → resume that session with the new message
4. If not found → create a new session, register the mapping

Sessions have a TTL (configurable, default 24h). Expired sessions are pruned from the registry.

### Agent-Originated Post Registration

When the agent calls `post` (via SocialTool), the C++ layer:
1. Captures the current session ID
2. Receives the `post_id` from the adapter response
3. Registers `social.{platform_id}.sessions.post:{post_id} = {session_id}` in the config store

This means replies to the agent's posts automatically route back to the originating session context.

## Message Formatting for Agent Prompt

When building the context for an agent session from inbound messages:

### User Resolution
1. Collect all unique `from_id` values from the message chain
2. Call the platform's user resolution API (`users.get` for VK, `getProfile` for Bluesky) **once** with all IDs
3. Format each message with the resolved name:

```
Melvin Sommer (1115662869): Hey there.
Animus Bot (-238909265): Hello! How can I help?
Melvin Sommer (1115662869): Can you check the feed?
```

### Message Format
Each message in the prompt is formatted as:
```
{display_name} ({user_id}): {message_text}
```

Bot messages use `from_id < 0` (negative for communities) and are marked as the agent's own prior output.

This gives the agent human-readable context while preserving the machine-readable IDs for API calls.

## Inbound Event Sources

### VK: Bots Long Poll API

VK's official real-time event mechanism for community bots:

1. `groups.getLongPollServer` → returns `{server, key, ts}`
2. `GET {server}?act=a_check&key={key}&ts={ts}&wait=25` → connection held open up to 25 seconds
3. Returns `{ts, updates: [...]}` on new events
4. Process events, re-poll with new `ts`

Error handling:
- `failed: 1` → resume with new `ts` from response
- `failed: 2` → re-fetch `key` via `getLongPollServer`
- `failed: 3` → re-fetch `key` + `ts` via `getLongPollServer`

**Event types to handle (VK):**
| Event | Routing Key | Action |
|-------|------------|--------|
| `message_new` | `peer_id` | Route to chat session |
| `message_reply` | `peer_id` | Route to chat session (bot's own reply) |
| `message_edit` | `peer_id` | Route to chat session (edit notification) |
| `wall_post_new` | `post_id` | Create new wall session |
| `wall_reply_new` | `post_id` | Route to wall post session |
| `like_add` | (log only) | Don't route, just log for awareness |
| `message_allow` | `user_id` | Log: user opted into messaging |
| `message_typing_state` | (ignore) | Don't route, typing indicator |

### Bluesky: REST Polling

Bluesky doesn't have Long Poll — use interval-based polling:

1. Every N seconds, call `app.bsky.notification.listNotifications`
2. Filter for new notifications since last cursor
3. Route by notification type:
   - Reply → match to the post's session via AT-URI
   - Mention → new session
   - Follow → log only
4. Update cursor in config store

### Future: WebSocket/Push (deferred)

- Bluesky JetStream for real-time event streaming
- VK Callback API (alternative to Long Poll — requires inbound HTTP server)
- These use the same routing infrastructure, just a different event source

## Architecture

### C++ Event Loop Layer

The Long Poll lifecycle is managed in C++ (not Lua):

```
SocialEventPoller (C++)
  ├── VKLongPollConnection
  │     getLongPollServer → hold → parse → re-poll
  ├── BlueskyNotificationPoller
  │     timer → listNotifications → parse → timer
  └── EventRouter
        ├── resolve user IDs → display names
        ├── look up session registry
        ├── format messages for prompt
        └── spawn/resume agent session
```

Why C++:
- Long Poll is a persistent connection — needs lifecycle management beyond Lua's scope
- The event loop integration (timer, reconnect, backoff) belongs in the daemon
- Lua adapters provide the *hook* (`get_long_poll_server`) but don't manage the loop

### Lua Adapter Role

Lua adapters provide:
1. **`get_long_poll_server`** — returns `{server, key, ts}` for the polling infrastructure
2. **Event parsing helpers** (optional) — if the raw VK event format changes, the Lua adapter can provide a normalization function
3. **User resolution** — `users.get` via the adapter's HTTP helpers

The actual Long Poll HTTP request and session routing happen in C++.

### Session Lifecycle

```
[Inbound message_new from peer_id=2000000001]
  → Registry lookup: peer:2000000001 → no session found
  → Create new isolated session
  → Fetch conversation history (get_messages, peer_id=2000000001)
  → Resolve user IDs: [1115662869] → "Melvin Sommer"
  → Format context:
      "New message in your VK community chat:
       Melvin Sommer (1115662869): Hey there."
  → Register: sessions.peer:2000000001 = "session_xyz"
  → Session processes, agent may call send_message

[Later, another message_new from peer_id=2000000001]
  → Registry lookup: peer:2000000001 → session_xyz found
  → Resume session_xyz with new message:
      "Melvin Sommer (1115662869): Can you also check notifications?"
  → Session continues with full context
```

### Session Expiry

Sessions expire after a configurable TTL (default 24h). On expiry:
- Session is removed from the registry
- Next message from that peer_id creates a fresh session
- Agent gets a "new conversation" context, not stale history

Configurable per-instance:
```
social.vk:community.session_ttl = 86400  # seconds
```

## Config Storage

### Per-Instance Polling Configuration
```
social.vk:community.polling.enabled = true
social.vk:community.polling.method = "long_poll"
social.vk:community.polling.events = ["message_new", "wall_post_new", "wall_reply_new"]
social.vk:community.polling.wait = 25
social.vk:community.polling.session_ttl = 86400

social.bluesky:personal.polling.enabled = true
social.bluesky:personal.polling.method = "rest"
social.bluesky:personal.polling.interval = 900
social.bluesky:personal.polling.events = ["notification"]
```

### Session Registry
```
social.vk:community.sessions.peer:2000000001 = "session_abc"
social.vk:community.sessions.post:42 = "session_def"
social.vk:community.polling.last_ts = "4"
social.vk:community.polling.last_key = "abc123"
social.bluesky:personal.polling.last_cursor = "1703275200::bafyrei..."
```

### Agent-Originated Post Tracking
When the agent creates a post via the social tool, the C++ layer automatically registers:
```
social.vk:community.sessions.post:{post_id} = {current_session_id}
```

This ensures replies to the agent's posts route back to the originating session.

## Admin UI

The social admin panel gets event ingestion controls:
- Enable/disable polling per instance
- Configure event types to monitor
- Set session TTL
- View active sessions and their routing keys
- View polling status (connected, last event, error state)
- Kill specific sessions (force new session on next message)

## Scope

### In scope (v1)
- VK Bots Long Poll lifecycle (connect, hold, parse, reconnect)
- Session registry with peer_id and post_id routing
- Message formatting with user name resolution
- Agent-originated post registration
- Isolated agent session spawning/resumption
- Per-instance configurable event types and session TTL
- Exponential backoff on connection errors

### Not in scope (deferred)
- Bluesky REST polling (same infrastructure, different source — v1.1)
- WebSocket/JetStream push (future ticket)
- Cross-instance aggregation
- Rate limit awareness in poll scheduling
- Human approval workflow for agent actions
- Admin UI for polling configuration (v1 uses config store directly)
- VK Callback API (alternative to Long Poll)
- Multi-bot support (one agent per community)

## Acceptance Criteria

- [ ] VK Long Poll connects via `get_long_poll_server` and holds connection
- [ ] `message_new` events route to chat sessions by `peer_id`
- [ ] `wall_post_new` events create new sessions anchored to `post_id`
- [ ] `wall_reply_new` events route to existing sessions by `post_id`
- [ ] Agent `post` action registers the returned `post_id` in the session registry
- [ ] New peer_id/post_id creates a new isolated session
- [ ] Existing peer_id/post_id resumes the mapped session
- [ ] User names resolved via `users.get` and formatted in prompt context
- [ ] Messages formatted as `{Name} ({id}): {text}`
- [ ] Session TTL enforced — expired sessions pruned from registry
- [ ] Long Poll reconnection on `failed:2` and `failed:3` errors
- [ ] Connection errors trigger exponential backoff
- [ ] Poll state (ts, key) persisted across daemon restarts
- [ ] Agent sessions have access to social tools for autonomous response

## Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Long Poll connection drops | Medium | Auto-reconnect with backoff; re-fetch key/ts on failure |
| Session explosion (many concurrent chats) | Medium | Session TTL + max concurrent sessions config |
| User resolution API rate limit | Low | Batch all IDs in single call; cache resolved names |
| VK API changes event format | Low | Pin API version (5.131); test against live API |
| Agent taking unwanted autonomous actions | Medium | Start with session-mode that logs without acting; add autonomous mode incrementally |
| Token burn from many small sessions | Medium | Aggressive session TTL; message aggregation for high-volume chats |

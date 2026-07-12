# Ticket 052 — VKontakte (VK) Community Bot Adapter

**Priority:** P3 (second social adapter — community bot pattern)
**Status:** In Progress
**Dependencies:** Ticket 050 (Bluesky Adapter) ✅, Ticket 051 (Social Polling — recommended for Long Poll)
**Created:** 2026-05-20
**Revised:** 2026-05-20 (v2 — community bot pattern)

## Summary

A Lua-based adapter for VKontakte (VK), implementing a **community chatbot**: users message a VK community (group), Animus processes the message and replies. Uses VK's officially supported Bots Long Poll API for inbound events and the standard VK API for outbound actions. No HTTP server required on our side.

## Motivation

VK is the dominant social network in Russia and the CIS region (100M+ monthly active users). VK explicitly supports community bots and provides a well-documented API for them. Unlike personal account automation (prohibited by VK's ToS), community bots are the official, supported path.

Key reasons to build this adapter:
- **Chat-as-interface**: Users message the VK community, Animus processes through its agent pipeline and replies. No custom frontend needed.
- **Officially supported**: VK provides Bots Long Poll API specifically for this use case.
- **No inbound server required**: Long Poll is outbound-only — our script polls VK's server, no public endpoint needed.
- **Validates the plugin substrate**: Second Lua social adapter, but of a fundamentally different kind (community bot vs personal account), testing the architecture's flexibility.
- **Different adapter pattern**: Bluesky (personal account, stateless JWT) vs VK Community (community token, long-poll events). Demonstrates the adapter pattern handles both.

## Design Decision: Why Community Bot

VK's Terms of Service prohibit automating personal user accounts. The 2019 restriction on `messages.send` for user tokens was deliberate enforcement. Instead, VK provides:

1. **Community tokens** — generated in group settings, don't expire, no OAuth dance
2. **Bots Long Poll API** — real-time event delivery via outbound polling
3. **Bot settings in groups** — dedicated UI for enabling bot features

This is the supported path. It also happens to be architecturally simpler (no OAuth2, no token refresh, no PKCE).

## Authentication

Community tokens are generated in group settings:
1. Navigate to Управление → Дополнительно → Работа с API
2. Click Создать ключ
3. Select required permissions
4. Copy the token string

That's it. No OAuth, no refresh, no expiry. The token persists until manually revoked.

### Stored Config Per Instance

```
social.vk:community.access_token = "vk1.a..."
social.vk:community.group_id = "123456789"
```

Two config values. That's all.

## API Surface

Base URL: `https://api.vk.ru/method/{METHOD}`
Parameters: `access_token`, `v=5.131` (pinned API version)

### Actions

| Action | VK API Method | Description |
|--------|--------------|-------------|
| `post` | `wall.post` | Post on community wall |
| `browse` | `wall.get` | Get community wall posts |
| `search` | `wall.search` | Search community wall posts |
| `reply` | `wall.createComment` | Comment on a wall post |
| `like` | `likes.add` | Like a post or comment |
| `delete` | `wall.delete` | Delete a community wall post |
| `send_message` | `messages.send` | Send message to a user (community → user) |
| `get_messages` | `messages.getHistory` | Get conversation history with a user |
| `get_conversations` | `messages.getConversations` | List active conversations |
| `get_by_conversation` | `messages.getByConversationMessageId` | Get specific messages |

### Bots Long Poll API (deferred to Ticket 051)

The Long Poll is the inbound event mechanism — how the bot learns about new messages. It works like this:

1. `groups.getLongPollServer` → returns `{ server, key, ts }`
2. Poll `https://{server}?act=a_check&key={key}&ts={ts}&wait=25`
3. Connection held open up to 25 seconds; returns immediately when events arrive
4. Process events (new messages, wall posts, comments, etc.)
5. Re-poll with updated `ts`

This is a persistent polling loop. It needs lifecycle management (reconnect on failure, key refresh) that belongs in Ticket 051's polling infrastructure. The adapter provides the hook (`get_long_poll_server` action) but doesn't manage the loop itself.

### Community Bot Conversation Levels

When added to multi-user conversations (беседы), the bot has three access levels:

| Level | Can See | Can Do |
|-------|---------|--------|
| Mention-only (default) | Messages that @mention the bot | Send messages |
| Full access (admin-granted) | All messages | Send messages |
| Admin (creator-granted) | All messages | Send + manage conversation |

For 1-on-1 community messages, the bot sees everything. For беседы, mention-only is default — user must explicitly grant access.

### Rate Limits

- ~3 requests/second per community token
- `wall.post`: 50 posts/day
- `messages.send`: varies by community verification level
- Conservative backoff on any 429 or `too_many_requests` error

## Scope

### In scope (v1)
- Community token auth (simple token from config)
- Wall: post, browse, search, reply (comment), like, delete
- Messaging: send, get history, list conversations
- Long poll server info retrieval (for Ticket 051 integration)
- Bot conversation level awareness

### Not in scope (future)
- Photo/media upload
- VK Stories
- Keyboard/button responses
- Multi-community management
- Callback API (alternative to Long Poll — requires HTTP server)
- Group chat (беседы) management beyond basic messaging
- Audio/video content

## Architecture Notes

### Adapter Registration

```lua
animus.register_social({
    id = "vk",
    name = "VKontakte",
    capabilities = {"read", "write", "search", "message"},
    actions = {
        "post", "browse", "search", "reply", "like", "delete",
        "send_message", "get_messages", "get_conversations",
        "get_long_poll_server"
    },
    -- ... action schemas ...
    handler = function(args) --[[ dispatch ]] end
})
```

The `"message"` capability indicates DM/messaging support. The `get_long_poll_server` action is the hook for Ticket 051's polling infrastructure.

### Comparison with Bluesky Adapter

| Aspect | Bluesky | VK Community |
|--------|---------|--------------|
| Auth | Handle + app password → JWT | Community token (static) |
| Token expiry | JWT expires | Token doesn't expire |
| Refresh | Re-auth on 401 | Not needed |
| Inbound events | Not implemented (polling) | Long Poll (deferred to 051) |
| Identity | Personal account | Community/group |
| API style | AT Protocol (xrpc) | REST (api.vk.ru/method) |
| Primary use case | Post/search/browse | Chat + wall |

## Acceptance Criteria

- [ ] Community token loaded from config
- [ ] Wall post publishes to community wall
- [ ] Browse returns community wall posts
- [ ] Search returns matching wall posts
- [ ] Reply (comment) works on wall posts
- [ ] Like works on posts
- [ ] Delete removes community posts
- [ ] Send message delivers to a VK user
- [ ] Get messages retrieves conversation history
- [ ] Get conversations lists active conversations
- [ ] Get long poll server returns connection info for Ticket 051
- [ ] VK API errors surfaced clearly (error_code + error_msg)
- [ ] Rate limit handling with backoff

## Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| VK API docs inaccurate (autotranslated) | Medium | Test every endpoint against live API |
| Community token revoked | Low | Clear error message; admin regenerates in group settings |
| Long poll lifecycle management | Medium | Deferred to Ticket 051; adapter provides hooks only |
| API version deprecation | Low | Pin to v=5.131; monitor VK changelog |
| Bot rate limits differ from docs | Medium | Conservative defaults; log warnings on unexpected 429s |

## Research Notes

- VK API docs are autotranslated from Russian and sometimes inaccurate. Always verify against live API behavior.
- The API base URL is `api.vk.ru` (not `api.vk.com` — the `.ru` domain is current).
- Community tokens don't expire. They're revoked by deleting them in group settings.
- Bot features must be enabled in the group: Управление → Сообщения → Настройки для бота.
- Messages in the community must be enabled: Управление → Сообщения.
- `messages.send` requires a `random_id` parameter to prevent duplicate sends. Use `math.random`.
- `group_id` in API calls is the positive integer (without the minus sign used in `owner_id` for communities).
- Bots Long Poll API requires API version ≥ 5.80 for conversation (беседы) events.

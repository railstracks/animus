# Ticket 061 — Slack Adapter

**Priority:** P2 (fifth social adapter — Socket Mode WebSocket, validates workspace-scoped bot pattern)
**Status:** Phase 1 In Progress
**Dependencies:** Ticket 056 (Channels Refactor) ✅, Ticket 050 (Bluesky — adapter pattern) ✅
**Created:** 2026-05-26
**Revised:** 2026-06-06 (Phase 1 implementation started)

## Summary

A Lua-based adapter for Slack, implementing message send, reply, read, search, and channel/workspace browsing via the Slack Web API. Registers into the ChannelManager as a `type: "slack"` channel. Uses **Socket Mode** (WebSocket) for real-time event delivery — no public HTTP endpoint required, no inbound port exposure.

## Motivation

Slack is the primary collaboration platform for professional teams. Melvin prefers it for project coordination with Thomas. An Animus Slack adapter would let agents participate in project channels natively — responding to mentions, posting updates, reading channel history.

Slack is architecturally distinct from existing adapters:

- **Socket Mode** — WebSocket-based event delivery where the app connects outbound to Slack (no public endpoint needed). Similar to Discord Gateway, but simpler: no heartbeat opcodes, no sequence numbers, no resume protocol. Slack manages connection lifecycle via refreshable WebSocket URLs.
- **Workspace-scoped** — a bot token is scoped to a single workspace. Unlike Discord (one bot, many servers), Slack bots are per-workspace by default. Enterprise Grid allows org-wide deployment.
- **Tiered rate limits** — per-method, per-workspace, per-app. Different methods have different tiers (1/min to 100+/min). More structured than Discord's bucket system, less generous overall.
- **Two-token model** — app-level token (`xapp-`) for Socket Mode WebSocket, bot token (`xoxb-`) for Web API calls. The app token creates the WebSocket; the bot token makes API requests.

## Platform Policy Constraints

Slack's automation policy is clear and permissive for workspace-internal bots:

| Constraint | Detail |
|-----------|--------|
| Bot accounts | Standard — create app in workspace, install with scopes. No approval process for internal apps. |
| Marketplace apps | Socket Mode apps **cannot** be listed in the Slack Marketplace (not relevant for internal deployment). |
| Self-bots | **Prohibited** — automating user tokens violates ToS. Must use bot tokens. |
| Token rotation | Optional but recommended. App-level tokens support rotation. |
| Data access | Bots only see channels they're explicitly added to. Must be invited to channels. |
| Rate limits | Tiered per method: Tier 1 (1/min) → Tier 4 (100+/min). `chat.postMessage`: 1/sec per channel + workspace-wide burst cap. |
| Events API | Socket Mode replaces HTTP Request URL. No inbound endpoint needed. |

**Key difference from Discord:** No privileged intent system. Slack bots see message content by default if they have the `channels:history`/`groups:history` scopes and are in the channel. No verification process for internal workspace deployment.

## Auth Model

**Two-token model:**

1. **App-level token** (`xapp-...`) — for Socket Mode WebSocket connection
   - Created in Developer Portal under "App-Level Tokens"
   - Requires `connections:write` scope
   - Used to call `apps.connections.open` → get WebSocket URL
   - Supports token rotation (optional)

2. **Bot token** (`xoxb-...`) — for Web API calls
   - Created when app is installed to workspace
   - Scopes define which API methods the bot can use
   - Standard OAuth install flow (but for internal apps, it's one-click in the workspace)

3. **No token refresh needed** — both tokens are long-lived. Bot token doesn't expire unless app is uninstalled. App-level token supports rotation but it's optional.

**Required scopes (bot token):**
- `chat:write` — send messages
- `channels:read` — list public channels
- `channels:history` — read public channel messages
- `groups:read` — list private channels
- `groups:history` — read private channel messages
- `im:write` — send DMs
- `im:history` — read DMs
- `im:read` — list DMs
- `search:read` — search messages/files
- `reactions:write` — add emoji reactions
- `files:read` — read shared files
- `app_mentions:read` — subscribe to mention events

**Required scopes (app-level token):**
- `connections:write` — establish Socket Mode connection

## Socket Mode Protocol

Socket Mode is simpler than Discord Gateway:

1. Call `POST https://slack.com/api/apps.connections.open` with app-level token in Authorization header
2. Receive WebSocket URL: `wss://wss.slack.com/link/?ticket=...`
3. Connect to WebSocket URL
4. Receive `hello` event → connected
5. Receive events as JSON envelopes: `{"type": "events_api", "payload": {...}, "ack_id": "..."}`
6. **Acknowledge each event** by sending back: `{"envelope_id": "<envelope_id>"}` — Slack requires explicit ack
7. When connection drops, call `apps.connections.open` again for a fresh URL
8. **No heartbeat, no sequence numbers, no resume** — much simpler than Discord

**Key differences from Discord Gateway:**
- No opcode system — just JSON event types
- No heartbeat — Slack assumes connection is alive if TCP is open
- No sequence tracking — events are not ordered by sequence number
- Explicit ack per event — Discord doesn't require acks
- URL refreshes — WebSocket URL is temporary, reconnect by getting a new URL

**This is simpler than the Discord Gateway by a significant margin.** It's closer to the AgentMail WebSocket listener pattern (connect, subscribe, receive) with the addition of per-event acks.

### C++ vs Lua Question

Same question as Discord, but the answer is different here:

**Socket Mode in Lua is viable.** The protocol is simple enough:
- Connect to WebSocket URL (need `animus.ws_connect` primitive)
- Parse JSON events
- Send ack for each event
- Reconnect by calling `apps.connections.open` again when connection drops

However, the managed WebSocket infrastructure doesn't exist in the Lua bridge yet. Adding `animus.ws_connect` is a new primitive that would also benefit the Discord adapter.

**Recommendation (same as Discord):** C++ WebSocket loop in ChannelManager + Lua REST actions. The C++ layer handles Socket Mode connection lifecycle; Lua handles API surface. Consistent with the Discord architecture, and both adapters share the `DiscordGatewayLoop`/`SlackSocketModeLoop` pattern in ChannelManager.

## Actions

### Read Actions

| Action | Endpoint | Notes |
|--------|----------|-------|
| `browse` | `conversations.history` | Recent messages in a channel. Cursor-based pagination. |
| `search` | `search.messages` | Workspace-wide message search. Tier 2 (20/min). |
| `get_channel` | `conversations.info` | Channel info (name, topic, purpose, members). |
| `list_channels` | `conversations.list` | All channels the bot is in. Tier 2 (20/min). Cursor-based. |
| `list_members` | `conversations.members` | Members in a channel. Tier 4 (100/min). |
| `notifications` | Socket Mode events: `app_mention`, `message` | Real-time via WebSocket. |

### Write Actions

| Action | Endpoint | Notes |
|--------|----------|-------|
| `post` | `chat.postMessage` | Send message to channel. Special tier: ~1/sec per channel. Supports blocks/attachments. |
| `reply` | `chat.postMessage` | Same endpoint, with `thread_ts` field. |
| `react` | `reactions.add` | Add emoji reaction. Tier 3 (50/min). |
| `delete` | `chat.delete` | Delete message. Tier 3 (50/min). |
| `edit` | `chat.update` | Edit message. Tier 2 (20/min). |
| `typing` | (via Socket Mode `interactivemessage` event) | Not directly available via Web API. Can be skipped. |

### Socket Mode Events → Session Dispatch

| Event Type | Dispatch When | Notes |
|------------|---------------|-------|
| `app_mention` | Bot is @mentioned in a channel | Primary interaction vector. Requires `app_mentions:read` scope. |
| `message` | Message in a channel bot is in | Requires `channels:history`/`groups:history`. Filter: ignore own messages, ignore messages before bot joined. |
| `reaction_added` | Reaction added to a message | Opt-in. Rarely needed. |

**Self-filtering:** Check `event.bot_id` or compare `event.user` against bot's user ID. Do not dispatch on own messages.

## Rate Limits

**Tiered system (per method, per workspace, per app):**

| Tier | Limit | Typical Methods |
|------|-------|-----------------|
| Tier 1 | 1+ per minute | `auth.test`, admin methods |
| Tier 2 | 20+ per minute | `conversations.list`, `conversations.history`, `chat.update`, `search.messages` |
| Tier 3 | 50+ per minute | `conversations.members`, `reactions.add`, `chat.delete` |
| Tier 4 | 100+ per minute | `channels.list` (pagination) |
| Special | ~1/sec per channel | `chat.postMessage` — also workspace-wide burst cap |
| Events delivery | 30,000 per workspace per app per 60 min | Inbound events (not actionable) |

**Implementation:** Parse `Retry-After` header on 429 responses. Each method's tier is documented at https://api.slack.com/methods — parse the `ratelimited` response and back off. Simpler than Discord's bucket system because tiers are static and documented.

**May 2025 change:** Non-Marketplace apps get reduced limits for `conversations.history` and `conversations.replies`. Not a concern for internal workspace bots.

**Compared to other adapters:**
- Twitter Free: 1,500 reads/mo, 50 writes/mo
- Discord: ~50 req/s, no monthly caps
- Slack: 1–100+ req/min depending on method, no monthly caps
- **Slack is more constrained than Discord per-second, but far more generous than Twitter.** The tiered system means read-heavy operations (history, search) are Tier 2 (20/min) while posting is limited to ~1/sec per channel.

## Architecture

### Component Layout

```
ChannelManager (C++)
  └── SlackSocketModeLoop        ← NEW: WebSocket event loop
        ├── Connect via apps.connections.open
        ├── Parse events, send acks
        ├── Reconnect on disconnect (new URL)
        └── Event → ProcessSlackMessage dispatch

scripts/slack.lua                ← Lua adapter
  ├── animus.register_social("slack", {...})
  ├── REST actions: post, reply, react, browse, search, delete, edit
  ├── Auth: bot token from config
  └── Rate limit tracking (per-method tier)

ChannelManager::StartChannel     ← C++ routing
  ├── type == "slack" → SlackSocketModeLoop
  └── SendReply → slack.lua:post
```

### Config Schema

```json
{
  "channel.slack:WORKSPACE.config": {
    "bot_token": "xoxb-...",
    "app_token": "xapp-...",
    "monitored_channels": ["C0123456789"],
    "respond_to_dm": true,
    "respond_to_mentions": true,
    "respond_to_all_messages": false
  }
}
```

### C++ Socket Mode Loop

New class `SlackSocketModeLoop` in `ChannelManager.h/.cpp`:

- Uses Drogon's `WebSocketClient`
- Connects to URL from `apps.connections.open`
- Parses JSON events, sends `{envelope_id}` ack for each
- On disconnect: call `apps.connections.open` again, reconnect
- `hello` event = connected, `disconnect` event = reconnect
- Event dispatch to `ProcessSlackMessage`

**Simpler than DiscordGatewayLoop:**
- No heartbeat
- No sequence tracking
- No resume — just reconnect with fresh URL
- Simpler event format (JSON types, not opcodes)

**Similar to EmailWebSocketLoop but with:**
- Per-event ack requirement (new)
- URL refresh on reconnect (email WS URL is static)
- Event filtering (app_mention, message — not all email events)

## Implementation Phases

### Phase 1 — Lua REST Actions + Polling (minimal viable)

- `scripts/slack.lua` with REST-only actions: post, reply, browse, search, get_channel, list_channels, delete, edit
- Polling for mentions via `conversations.history` on monitored channels (like Bluesky polling pattern)
- No Socket Mode WebSocket yet
- Register as `social` composite via `animus.register_social`
- Admin UI: add `slack` to `ChannelsView.vue` channel types + i18n

**Rationale (same as Discord):** Validate API surface before tackling WebSocket complexity. Slack's REST API is well-documented and a polling adapter is functional.

### Phase 2 — C++ Socket Mode Loop

- `SlackSocketModeLoop` class with WebSocket connection + event ack
- URL refresh on reconnect
- Event dispatch to `ProcessSlackMessage`
- Replace polling with real-time event delivery
- Add `react` action (useful with real-time context)

### Phase 3 — Thread Support + Blocks

- Thread-aware message handling (`thread_ts` for replies)
- Block Kit formatting for rich messages
- File upload via `files.upload`
- Modal interactions (if needed)

## Acceptance Criteria

### Phase 1 (Lua REST + Polling)

- [ ] `scripts/slack.lua` registers via `animus.register_social("slack", ...)`
- [ ] Actions implemented: post, reply, browse, search, get_channel, list_channels, delete, edit
- [ ] Bot token + app token stored in AgentConfigStore
- [ ] Rate limit tracking per method tier
- [ ] `ChannelsView.vue` + i18n updated with `slack` channel type
- [ ] `ChannelManager.cpp` routes `type == "slack"` to slack.lua adapter
- [ ] Polling fetches recent messages from monitored channels
- [ ] Auto-reply dispatch works for DMs and mentions
- [ ] Clean build (C++ + Vue SPA)

### Phase 2 (C++ Socket Mode)

- [ ] `SlackSocketModeLoop` connects via `apps.connections.open`
- [ ] Events received and acked per envelope_id
- [ ] `app_mention` events dispatched to sessions
- [ ] `message` events dispatched for DMs
- [ ] Self-message filter (bot ignores own messages)
- [ ] Reconnect on disconnect with fresh URL
- [ ] `hello` and `disconnect` event handling

### Phase 3 (Thread + Blocks)

- [ ] Thread replies use `thread_ts` field
- [ ] Block Kit messages render correctly in Slack
- [ ] File upload via `files.upload` action
- [ ] Search returns threaded results with context

## Open Questions

1. **Workspace scope vs multi-workspace?** A single bot token is workspace-scoped. Should the adapter support multiple workspaces (multiple config instances)? Yes — the multi-tenant model supports it: `slack:WORKSPACE_A`, `slack:WORKSPACE_B` each with their own tokens.

2. **respond_to_all_messages?** By default, bots should only respond to mentions and DMs. A `respond_to_all_messages` flag would let agents listen to all messages in monitored channels, but this has privacy and noise implications. Make it opt-in.

3. **Block Kit vs plain text?** Slack's Block Kit enables rich formatting (sections, fields, actions, buttons). For Phase 1, plain text is sufficient. Blocks add complexity to the Lua adapter's message building. Defer to Phase 3.

4. **Socket Mode vs Events API?** Socket Mode is preferred (no public endpoint). Events API with HTTP callbacks would work too, but requires exposing an HTTP endpoint. Not needed when Socket Mode is available.

## Comparison with Existing Adapters

| Aspect | Bluesky | VK | Twitter | Discord | Slack |
|--------|---------|-----|---------|---------|-------|
| Transport | REST polling | Long Poll + REST | REST polling | Gateway WS + REST | Socket Mode WS + REST |
| Auth | App pwd → JWT | Community token | OAuth 2.0 PKCE | Static bot token | **Two-token (app + bot)** |
| Push events | None (poll) | Long Poll | None (poll) | Gateway WS | **Socket Mode WS** |
| Rate limits | Undocumented | 3 req/s | Monthly caps | Per-route buckets | **Tiered per method** |
| Monthly caps | None | None | 1.5K read / 50 write | None | **None** |
| Policy friction | Low | Medium | High | Low | **Low** |
| Protocol complexity | Low | Medium | Medium | High | **Medium** |
| WS complexity | N/A | N/A | N/A | Heartbeat + resume + seq | **Connect + ack + refresh** |
| Scope | Global | Community | User account | Multi-server | **Per-workspace** |

## Relationship to Ticket 060 (Discord)

Discord and Slack share the WebSocket-first pattern but differ significantly:

- **Discord Gateway** is a full stateful protocol (opcodes, heartbeat, sequence, resume)
- **Slack Socket Mode** is connect-subscribe-ack-refresh (simpler)

If 060 is implemented first, `DiscordGatewayLoop` establishes the pattern for `SlackSocketModeLoop`. If 061 is first, the simpler Slack protocol provides a gentler introduction to WebSocket event loops in ChannelManager.

**Recommendation:** Implement 060 (Discord) first. The harder protocol validates the architecture. Slack's simpler Socket Mode then becomes a clean reduction of the same pattern. The reverse order risks building a Socket Mode loop that's too simple to generalize to Discord's needs.

However, if Melvin's immediate priority is Slack integration (for Thomas collaboration), 061 Phase 1 (REST + polling) can ship independently and quickly — it's a pure Lua adapter with no C++ changes.
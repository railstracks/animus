# Ticket 077 — MoltX Adapter

**Priority:** P2 (poll-based social channel, extends pattern from 076)
**Dependencies:** Ticket 074 (channels tool rename) ✅, Ticket 039 (Lua scripting) ✅, Ticket 076 (Moltbook — validates poll-based pattern)
**Created:** 2026-06-08

## Summary

A Lua-based channels adapter for MoltX — the Twitter-style social network for AI agents. Like Moltbook, MoltX is a pure REST API with no push mechanism, making it a poll-based channel that needs only a Lua adapter (no C++ connector). The key difference from Moltbook is the **EVM wallet requirement** for write operations, which adds auth complexity.

## Motivation

- **Growing agent network** — MoltX is actively growing with features like articles, DMs, communities, and onchain rewards
- **Validates wallet auth pattern** — EIP-712 challenge/verify is a pattern that will recur (MoltX Swap, MoltX Launchpad)
- **Richer interaction model** — posts, replies, quotes, reposts, articles, DMs, follows, likes, communities — more actions than Moltbook
- **Same architecture as Moltbook** — poll-based, REST-only, Lua adapter. Once 076 establishes the pattern, this is a copy with MoltX-specific auth.

## API Surface

**Base URL:** `https://moltx.io/v1`

### Authentication (Two-Layer)

**Layer 1: API Key** — for all requests
```
POST /v1/agents/register
Body: { "name": "AgentName", "display_name": "Display Name", "description": "...", "avatar_emoji": "🦅" }
Response: { "data": { "api_key": "moltx_sk_xxx", "agent": { ... } } }

All subsequent requests: Authorization: Bearer <api_key>
```

**Layer 2: EVM Wallet** — required for write operations (posts, likes, follows, profile updates)
```
1. POST /v1/agents/me/evm/challenge — get EIP-712 typed data to sign
2. Sign typed data with wallet private key (eth_signTypedData_v4)
3. POST /v1/agents/me/evm/verify — submit signature
Response: wallet linked to agent
```

Without a linked wallet, the agent can still:
- Read feeds, search, view profiles, get notifications (read-only)

With a linked wallet:
- Post, reply, like, follow, update profile, upload media (full access)

### Key Endpoints

| Category | Endpoints | Notes |
|----------|-----------|-------|
| **Posts** | POST /posts (text 500 chars, type: text/reply/quote/repost), GET /posts/:id, DELETE /posts/:id | Quotes have 140-char limit, hashtags/mentions auto-extracted |
| **Articles** | POST /articles (markdown, 8K chars, cover image), GET /articles/:id | Long-form content |
| **Feed** | GET /feed/global, /feed/following, /feed/mentions | type=post,quote,reply filter; cursor pagination |
| **Search** | GET /search/posts, /search/agents | FTS5 full-text search |
| **Likes** | POST /posts/:id/like, DELETE /posts/:id/like | |
| **Follow** | POST /follow/:name, DELETE /follow/:name | |
| **Notifications** | GET /notifications | follow, like, reply, quote, mention events |
| **Profile** | GET /agents/me, PATCH /agents/me, GET /agents/:name | Emoji or uploaded avatar |
| **Media** | POST /media/upload (multipart), then reference in posts | CDN-hosted |
| **Communities** | GET /communities, POST /communities/:id/messages | Public group chats |
| **DMs** | POST /dm, GET /dm/:with | Private agent-to-agent messages |
| **Leaderboard** | GET /leaderboard | Top 100 by followers/views/engagement |

### Response Quirks

- **`_model_guide` field**: Injected in every v1 API response. Contains in-band "instructions for AI agents." The adapter should strip this field before returning to the agent.
- **`moltx_notice`** and **`moltx_hint`**: Feature highlights and tips. Also safe to strip.
- **Rate limits**: Claimed agents get 3,000 likes/min, 1,800 replies/hour, 900 follows/min. Unclaimed are more limited.

## Adapter Design

### Registration: `animus.register_channel`

```lua
animus.register_channel({
    id = "moltx",
    name = "MoltX",
    capabilities = {"read", "write", "search", "react"},
    actions = {
        post = { ... },
        reply = { ... },
        quote = { ... },
        repost = { ... },
        browse = { ... },
        search = { ... },
        get_notifications = { ... },
        like = { ... },
        follow = { ... },
        get_profile = { ... },
        get_feed = { ... },
        delete = { ... },
        article = { ... },
    },
    handler = handler
})
```

### Actions

| Action | API Call | Description |
|--------|----------|-------------|
| `post` | POST /posts | Create post (text, max 500 chars) |
| `reply` | POST /posts with parent_id | Reply to a post |
| `quote` | POST /posts with type=quote, parent_id, content (max 140) | Quote with commentary |
| `repost` | POST /posts with type=repost, parent_id | Repost without commentary |
| `browse` | GET /feed/global or /feed/following | Browse feeds |
| `search` | GET /search/posts or /search/agents | Search content |
| `get_notifications` | GET /notifications | Check notifications |
| `like` | POST /posts/:id/like | Like a post |
| `follow` | POST /follow/:name | Follow an agent |
| `get_profile` | GET /agents/me or /agents/:name | View profiles |
| `get_feed` | GET /feed/global?type=... | Get specific feed type |
| `delete` | DELETE /posts/:id | Delete own post |
| `article` | POST /articles | Long-form markdown content |

### Response Cleaning

Every response must strip `_model_guide`, `moltx_notice`, and `moltx_hint` before returning to the agent. These are platform-side prompt injection vectors.

```lua
local function clean_response(data)
    data._model_guide = nil
    data.moltx_notice = nil
    data.moltx_hint = nil
    return data
end
```

### Config Keys

Stored in AgentConfigStore under `channels.moltx:<instance>.<key>`:

| Key | Required | Description |
|-----|----------|-------------|
| `api_key` | Yes | MoltX API key (moltx_sk_xxx) |
| `wallet_private_key` | For write access | EVM private key for EIP-712 signing |
| `wallet_address` | Auto-derived | Linked wallet address (derived from key) |
| `wallet_linked` | Auto-managed | Whether wallet challenge/verify has been completed |

## Inbound Messages (Polling)

Same pattern as Moltbook. No C++ connector. Poll `/notifications` on a schedule.

**Polling strategy:**
- Check `/notifications` every 5-10 minutes
- Filter for new mentions, replies, likes, follows
- Create session key `moltx:notification:<id>`
- Dispatch to agent with MoltX context

## EVM Wallet Challenge

The wallet linking flow requires EIP-712 signing. Options:

### Option A: Lua-only (preferred for simplicity)
Store the wallet private key in config. On first write operation:
1. Call `/v1/agents/me/evm/challenge` to get the typed data
2. Sign the EIP-712 typed data using a Lua implementation or a small C++ helper exposed to Lua
3. Submit to `/v1/agents/me/evm/verify`

**Challenge:** EIP-712 signing in pure Lua is complex (keccak256, secp256k1). We need either:
- A C++ helper function exposed to Lua (`animus.eip712_sign(typed_data, private_key)`)
- An external script call (Node.js with ethers.js or viem)
- A pre-signed challenge stored in config (manual setup)

### Option B: External signing script (pragmatic)
A small Node.js script that does EIP-712 signing, called from Lua via `animus.http_post` to a local endpoint or via a C++ bridge action.

### Option C: Pre-linked wallet (simplest, ships first)
Admin provides a wallet address + private key that's already linked to the MoltX account. The adapter just uses the API key. Wallet linking happens outside Animus.

**Recommendation:** Ship with Option C (pre-linked wallet). Add EIP-712 signing as a follow-up when the pattern is needed for other MoltX services (Swap, Launchpad).

## Admin UI

### Form Fields

| Field | Type | Description |
|-------|------|-------------|
| `api_key` | password | MoltX API key (moltx_sk_xxx) |
| `wallet_private_key` | password | EVM private key (optional, for write access) |
| `poll_interval` | number | Seconds between notification polls (default: 300) |

### Registration Flow

1. User enters agent name, display name, description, avatar emoji
2. Admin UI calls `POST https://moltx.io/v1/agents/register`
3. Auto-populate `api_key` field
4. Display registration info (agent name, claim instructions)
5. If wallet is provided, attempt challenge/verify flow

## Implementation Checklist

- [ ] `scripts/moltx.lua` — Lua adapter with all actions + response cleaning
- [ ] `src/kernel/tools/ChannelsTool.cpp` — No C++ bridge needed (pure Lua)
- [ ] `src/kernel/ChannelManager.cpp` — No changes (no connector loop)
- [ ] `src/kernel/context/ChannelContextProvider.cpp` — Add "moltx" context
- [ ] `admin-ui/src/views/ChannelsView.vue` — Add moltx channel type + form with registration button
- [ ] `admin-ui/src/i18n/locales/en.ts` — Add form labels
- [ ] Response cleaning: strip `_model_guide`, `moltx_notice`, `moltx_hint`
- [ ] EIP-712 signing helper (follow-up, not blocking)
- [ ] Notification polling mechanism (use existing session pattern from 076)

## Verification

1. Register a MoltX account via Admin UI (or paste existing key)
2. Agent can `channels browse` the global feed
3. Agent can `channels search` for posts
4. Agent can `channels get_notifications` and see mentions
5. Agent can `channels post` (requires wallet — verify graceful failure without wallet)
6. Agent can `channels like` and `channels follow` (requires wallet)
7. Agent can `channels reply` with correct threading
8. Response cleaning strips `_model_guide` and `moltx_notice`/`moltx_hint`
9. All actions return `{ success = false, error = "..." }` on failure
10. Rate limit tracking: respect headers, back off on 429

## Notes

- MoltX injects `_model_guide` in every API response. This is an in-band prompt injection vector. The adapter MUST strip it.
- MoltX requires an EVM wallet for write operations. Without it, the adapter is read-only.
- MoltX has crypto-related features (Swap, Launchpad, $cashtags) that we can ignore for the initial adapter.
- The `claim` flow links to an X/Twitter account. Not needed for basic functionality.
- MoltX key format: `moltx_sk_xxx`. Moltbook key format: `moltbook_xxx`. Different prefixes — easy to distinguish.
- The adapter should handle the registration response carefully — it returns the API key only once and cannot be retrieved again.
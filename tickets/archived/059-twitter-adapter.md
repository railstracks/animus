# Ticket 059 — Twitter/X Adapter

**Priority:** P2 (third social adapter — constrained platform, validates OAuth 2.0 + rate limit awareness)
**Status:** In Progress
**Dependencies:** Ticket 056 (Channels Refactor) ✅, Ticket 050 (Bluesky — adapter pattern) ✅
**Created:** 2026-05-24
**Revised:** 2026-05-25 (OAuth PKCE endpoints implemented)

## Summary

A Lua-based adapter for Twitter/X, implementing post, reply, read timeline, search, get mentions, and delete via the Twitter API v2. Registers into the ChannelManager as a `type: "twitter"` channel. Targets the **Free tier** with a `tier` config field to unlock paid-tier features (streaming, higher limits) without a connector split.

## Motivation

Twitter remains a high-signal public platform for news, discussions, and community engagement. The Free tier provides a viable entry point: 1,500 reads/mo and 50 writes/mo — tight but sufficient for an agent that posts deliberately and reads selectively.

This adapter is architecturally distinct from Bluesky and VK:
- **OAuth 2.0 with PKCE** — first adapter requiring a full OAuth flow (Bluesky uses app password, VK uses static community token)
- **Rate limit tracking** — monthly caps must be exposed so the agent can make informed decisions about spending writes
- **Policy constraints** — automated likes prohibited, one reply per interaction, no bulk follows. The adapter enforces these by not offering the actions at all
- **Tier-aware feature gating** — Free vs Paid is the same connector, same API, different quotas and endpoint availability

## Platform Policy Constraints

Twitter's developer policy for automation:

| Constraint | Detail |
|-----------|--------|
| Automated likes | **Prohibited** — not offered as an action |
| Automated follows | **Prohibited** — not offered as an action |
| Retweets | Permitted but rate-limited |
| Reply rate | One reply per interaction context |
| Bulk operations | Prohibited |
| DMs | Require separate API access (not in scope) |
| Content labeling | Automated posts must not mislead about authorship |

**Design principle:** If an action is policy-prohibited, we don't implement it. The agent can't violate policy through the adapter.

## API Tier Model

One connector with a `tier` config field:

| Feature | Free | Basic ($100/mo) | Pro ($5,000/mo) |
|---------|------|------------------|-----------------|
| Monthly reads | 1,500 | 10,000 | 1,000,000 |
| Monthly writes | 50 | 3,000 | 150,000 |
| Search | 7-day recent | 7-day recent | Full archive |
| Streaming | ❌ | ✅ Filtered stream | ✅ Full stream |
| List management | ❌ | ✅ | ✅ |

The connector reads `tier` from config at startup. Tier determines:
- Which actions are registered (streaming actions only on Basic+)
- Rate limit caps exposed to the agent
- Polling intervals (Free: more conservative to conserve reads)

**No connector split.** The API surface is identical; only quotas and additive endpoints differ.

## Authentication: OAuth 2.0 with PKCE

Twitter API v2 uses OAuth 2.0 with PKCE for user-context authorization. This is the first Animus adapter requiring a full OAuth flow.

### Flow

```
1. Admin creates a Twitter channel config with client_id + client_secret
2. Admin initiates OAuth via Animus admin UI → redirect to Twitter authorize URL
3. User authorizes → Twitter redirects back with authorization code
4. Animus exchanges code + PKCE code_verifier for access_token + refresh_token
5. Tokens cached in channel config
6. Adapter uses access_token for all API calls
7. On 401, attempts refresh_token exchange; on failure, signals re-auth needed
```

### Config Per Instance

```
twitter.{instance}.client_id       — OAuth app client ID
twitter.{instance}.client_secret   — OAuth app client secret
twitter.{instance}.access_token    — cached bearer token
twitter.{instance}.refresh_token   — cached refresh token
twitter.{instance}.token_expires   — expiry timestamp (Unix)
twitter.{instance}.user_id        — Twitter user ID (resolved on first auth)
twitter.{instance}.username       — Twitter username (resolved on first auth)
twitter.{instance}.tier            — "free" | "basic" | "pro" (default: "free")
twitter.{instance}.reads_used     — monthly read counter
twitter.{instance}.writes_used    — monthly write counter
twitter.{instance}.counter_reset   — timestamp of last counter reset
```

### Token Refresh

Twitter access tokens expire after 2 hours. Refresh tokens expire after 6 months (revoked on use, new one issued).

```
POST https://api.twitter.com/2/oauth2/token
Content-Type: application/x-www-form-urlencoded
Body: refresh_token={rt}&grant_type=refresh_token&client_id={cid}

Response: { access_token, refresh_token, expires_in, token_type }
```

The adapter:
1. Checks `token_expires` before each API call
2. If expired (or within 5-min margin), attempts refresh
3. On refresh failure (expired/revoked refresh token), returns error indicating re-auth needed
4. On success, caches new access + refresh tokens, updates expiry

This mirrors the Bluesky refresh chain (cached → refresh → re-auth) but with OAuth 2.0 semantics.

### PKCE Implementation

```lua
-- Generate code_verifier (43-128 chars, [A-Za-z0-9-._~])
local function generate_code_verifier()
    -- crypto-safe random bytes, base64url-encoded
    local bytes = animus.random_bytes(32)
    return base64url_encode(bytes)
end

-- code_challenge = BASE64URL(SHA256(code_verifier))
local function generate_code_challenge(verifier)
    local hash = animus.sha256(verifier)
    return base64url_encode(hash)
end
```

**Gap:** `animus.sha256` and `animus.random_bytes` may not exist in the current Lua bridge. If not available, they need to be added as Lua-callable C++ functions or the PKCE flow is handled server-side in the C++ admin endpoint.

**Recommendation:** Handle the full OAuth exchange (steps 2-4) in a C++ admin endpoint, not in Lua. The Lua adapter only uses the resulting `access_token`/`refresh_token`. This avoids adding crypto primitives to the Lua bridge and keeps the auth flow in a place that can serve the redirect UI.

## API v2 Surface

### Post tweet

```
POST https://api.twitter.com/2/tweets
Authorization: Bearer <access_token>
Content-Type: application/json

Body: { "text": "Hello from Animus" }
Response: { "data": { "id": "123", "text": "Hello from Animus" } }
```

### Reply

```
Body: {
    "text": "Reply text",
    "reply": { "in_reply_to_tweet_id": "456" }
}
```

### Read timeline

```
GET https://api.twitter.com/2/users/:id/timelines/reverse_chronological?max_results=20
Authorization: Bearer <access_token>
```

### User's tweets

```
GET https://api.twitter.com/2/users/:id/tweets?max_results=20
```

### Search (recent, 7-day)

```
GET https://api.twitter.com/2/tweets/search/recent?query=<query>&max_results=20
```

### Get mentions

```
GET https://api.twitter.com/2/users/:id/mentions?max_results=20
```

### Delete tweet

```
DELETE https://api.twitter.com/2/tweets/:id
Authorization: Bearer <access_token>
```

### Get tweet by ID

```
GET https://api.twitter.com/2/tweets/:id?tweet.fields=created_at,author_id,conversation_id
```

## Push Mechanism

### Free tier: Polling

Poll `GET /2/users/:id/mentions` at a configurable interval (default: 15 min — conservative to conserve monthly reads). Each poll consumes 1 read from the monthly cap.

Polling budget for Free tier:
- 1,500 reads/mo ÷ 30 days ÷ 24 hours = ~2 reads/hour
- With 15-min intervals: mentions (4/hr) + occasional search/timeline = ~5-6 reads/hr → budget exhausted in ~10 days

**Realistic Free tier strategy:**
- Mentions poll: every 30 min (2 reads/hr)
- Search/timeline: on-demand only (agent-initiated, not periodic)
- Total: ~48 reads/day → ~1,440/mo — fits the cap with slim margin

The adapter exposes remaining reads so the agent can decide whether to spend them.

### Basic+ tier: Streaming

On `tier: "basic"` or higher, the adapter additionally registers streaming actions:
- `stream_start` — connect to filtered stream endpoint
- `stream_stop` — disconnect
- `stream_rules` — manage stream filter rules

These use `GET /2/tweets/search/stream` (persistent connection) and `GET /2/tweets/search/stream/rules` (rule management).

Streaming does not consume the monthly read cap.

## Rate Limit Tracking

The adapter tracks monthly usage against the tier cap and exposes it to the agent:

```lua
-- In action handler, before making API call:
local reads_used = tonumber(config.get(cfg_key(pid, "reads_used")) or "0")
local writes_used = tonumber(config.get(cfg_key(pid, "writes_used")) or "0")

-- Check against tier limits
local read_cap = TIER_READS[tier]  -- e.g. 1500 for free
local write_cap = TIER_WRITES[tier] -- e.g. 50 for free

-- Return quota info alongside results
return {
    success = true,
    output = resp.body,
    quota = {
        reads_remaining = read_cap - reads_used,
        writes_remaining = write_cap - writes_used,
        reads_cap = read_cap,
        writes_cap = write_cap,
        tier = tier
    }
}
```

Monthly counter reset: on each call, check if current month has rolled over since `counter_reset`. If so, zero the counters.

Twitter also returns per-endpoint rate limit headers (`x-rate-limit-limit`, `x-rate-limit-remaining`, `x-rate-limit-reset`) on every response. The adapter should parse these and use them for short-term (15-min window) rate limit avoidance, while the monthly counters track the developer account cap.

## Actions

| Action | Type | Monthly Cost | Free Tier? |
|--------|------|-------------|------------|
| `post` | Write | 1 write | ✅ |
| `reply` | Write | 1 write | ✅ |
| `read_timeline` | Read | 1 read | ✅ (on-demand) |
| `read_user_tweets` | Read | 1 read | ✅ (on-demand) |
| `search` | Read | 1 read | ✅ (on-demand) |
| `get_mentions` | Read | 1 read | ✅ (periodic poll) |
| `get_tweet` | Read | 1 read | ✅ |
| `delete` | Write | 1 write | ✅ |
| `stream_start` | Read | 0 (streaming) | ❌ Basic+ |
| `stream_stop` | — | 0 | ❌ Basic+ |
| `stream_rules` | Read | 1 read | ❌ Basic+ |
| `quota` | — | 0 | ✅ (always available) |

**Not implemented (policy-prohibited or out of scope):**
- Like — automated likes prohibited by policy
- Follow/unfollow — automated follows prohibited
- Retweet — permitted but deferred (low value for agents)
- DM — requires separate API access
- List management — Basic+ only, deferred
- Media upload — deferred (v2 scope)

## Adapter Structure

```lua
-- scripts/twitter.lua

local TIER_READS = { free = 1500, basic = 10000, pro = 1000000 }
local TIER_WRITES = { free = 50, basic = 3000, pro = 150000 }

local function cfg_key(platform_id, field)
    return "twitter." .. platform_id .. "." .. field
end

-- Token management
local function ensure_token(platform_id)
    local token = config.get(cfg_key(platform_id, "access_token"))
    local expires = tonumber(config.get(cfg_key(platform_id, "token_expires")) or "0")
    local now = os.time()

    if token and token ~= "" and expires > now + 300 then
        return token  -- cached and valid
    end

    -- Try refresh
    local refresh = config.get(cfg_key(platform_id, "refresh_token"))
    if refresh and refresh ~= "" then
        local resp = animus.http_post("https://api.twitter.com/2/oauth2/token", {
            headers = { ["Content-Type"] = "application/x-www-form-urlencoded" },
            body = "refresh_token=" .. url_encode(refresh)
                   .. "&grant_type=refresh_token"
                   .. "&client_id=" .. url_encode(config.get(cfg_key(platform_id, "client_id")))
        })
        if resp.status == 200 then
            local data = json.decode(resp.body)
            config.set(cfg_key(platform_id, "access_token"), data.access_token)
            config.set(cfg_key(platform_id, "refresh_token"), data.refresh_token)
            config.set(cfg_key(platform_id, "token_expires"), tostring(now + data.expires_in))
            return data.access_token
        end
    end

    return nil, "re-auth required: refresh token expired or revoked"
end

-- Rate limit tracking
local function check_quota(platform_id, is_write)
    local tier = config.get(cfg_key(platform_id, "tier")) or "free"
    local reads_used = tonumber(config.get(cfg_key(platform_id, "reads_used")) or "0")
    local writes_used = tonumber(config.get(cfg_key(platform_id, "writes_used")) or "0")

    -- Monthly reset check
    local reset_ts = tonumber(config.get(cfg_key(platform_id, "counter_reset")) or "0")
    local now = os.time()
    if os.date("!*t", now).month ~= os.date("!*t", reset_ts).month then
        reads_used = 0
        writes_used = 0
        config.set(cfg_key(platform_id, "reads_used"), "0")
        config.set(cfg_key(platform_id, "writes_used"), "0")
        config.set(cfg_key(platform_id, "counter_reset"), tostring(now))
    end

    local read_cap = TIER_READS[tier] or TIER_READS.free
    local write_cap = TIER_WRITES[tier] or TIER_WRITES.free

    if is_write then
        if writes_used >= write_cap then
            return false, "monthly write cap reached (" .. write_cap .. ")", read_cap - reads_used, write_cap - writes_used
        end
        config.set(cfg_key(platform_id, "writes_used"), tostring(writes_used + 1))
    else
        if reads_used >= read_cap then
            return false, "monthly read cap reached (" .. read_cap .. ")", read_cap - reads_used, write_cap - writes_used
        end
        config.set(cfg_key(platform_id, "reads_used"), tostring(reads_used + 1))
    end

    return true, nil, read_cap - reads_used - (is_write and 0 or 1), write_cap - writes_used - (is_write and 1 or 0)
end

-- Action handlers
local function do_post(args) ... end
local function do_reply(args) ... end
local function do_read_timeline(args) ... end
local function do_search(args) ... end
local function do_get_mentions(args) ... end
local function do_delete(args) ... end
local function do_quota(args)
    local pid = args.platform_id
    local tier = config.get(cfg_key(pid, "tier")) or "free"
    return {
        success = true,
        tier = tier,
        reads_used = tonumber(config.get(cfg_key(pid, "reads_used")) or "0"),
        reads_cap = TIER_READS[tier],
        writes_used = tonumber(config.get(cfg_key(pid, "writes_used")) or "0"),
        writes_cap = TIER_WRITES[tier]
    }
end

-- Registration
animus.register_social({
    id = "twitter",
    name = "Twitter",
    capabilities = { "read", "write", "search" },
    actions = { "post", "reply", "read_timeline", "read_user_tweets", "search",
                "get_mentions", "get_tweet", "delete", "quota" },
    schema = {
        post = {
            { name = "content", type = "string", required = true, description = "Tweet text (max 280 characters)" }
        },
        reply = {
            { name = "tweet_id", type = "string", required = true, description = "ID of the tweet to reply to" },
            { name = "content", type = "string", required = true, description = "Reply text" }
        },
        read_timeline = {
            { name = "max_results", type = "integer", required = false, description = "Max tweets to return (5-100, default 20)" }
        },
        read_user_tweets = {
            { name = "user_id", type = "string", required = false, description = "Twitter user ID (default: authed user)" },
            { name = "max_results", type = "integer", required = false, description = "Max tweets (5-100, default 20)" }
        },
        search = {
            { name = "query", type = "string", required = true, description = "Search query (standard operators)" },
            { name = "max_results", type = "integer", required = false, description = "Max results (10-100, default 20)" }
        },
        get_mentions = {
            { name = "max_results", type = "integer", required = false, description = "Max mentions (5-100, default 20)" }
        },
        get_tweet = {
            { name = "tweet_id", type = "string", required = true, description = "Tweet ID to fetch" }
        },
        delete = {
            { name = "tweet_id", type = "string", required = true, description = "Tweet ID to delete" }
        },
        quota = {}
    },
    handler = function(args)
        local action = args.action
        if action == "post" then return do_post(args)
        elseif action == "reply" then return do_reply(args)
        elseif action == "read_timeline" then return do_read_timeline(args)
        elseif action == "read_user_tweets" then return do_read_user_tweets(args)
        elseif action == "search" then return do_search(args)
        elseif action == "get_mentions" then return do_get_mentions(args)
        elseif action == "get_tweet" then return do_get_tweet(args)
        elseif action == "delete" then return do_delete(args)
        elseif action == "quota" then return do_quota(args)
        else return { success = false, error = "unknown action: " .. tostring(action) }
        end
    end
})
```

## OAuth Admin Flow (C++ Side)

The OAuth authorization code exchange happens in a C++ admin endpoint, not in Lua:

```
GET  /api/v1/channels/{name}/auth/twitter       → redirect to Twitter authorize URL
GET  /api/v1/channels/{name}/auth/twitter/callback → exchange code for tokens, cache in config
```

This keeps PKCE secrets and the token exchange out of the Lua runtime. The Lua adapter only ever uses already-cached `access_token` and `refresh_token`.

**PKCE details** (handled in C++):
- Generate `code_verifier` (32 random bytes, base64url-encoded)
- Compute `code_challenge` = BASE64URL(SHA256(code_verifier))
- Store `code_verifier` temporarily (in-memory or session) for the callback exchange
- Authorize URL: `https://twitter.com/i/oauth2/authorize?response_type=code&client_id={cid}&redirect_uri={uri}&scope={scope}&state={state}&code_challenge={challenge}&code_challenge_method=S256`
- Scopes: `tweet.read tweet.write users.read follows.read offline.access`

The `offline.access` scope is critical — it grants a refresh token. Without it, the user must re-authorize every 2 hours.

## Testing Plan

1. **OAuth setup**: Create Twitter app in developer portal, configure client_id/secret in Animus
2. **Auth flow**: Initiate OAuth via admin endpoint, authorize, verify tokens cached
3. **Token refresh**: Wait for access token to expire (or set short expiry for testing), verify refresh works
4. **Post test**: Tweet from agent, verify response contains tweet ID
5. **Reply test**: Reply to the test tweet
6. **Read timeline**: Fetch authed user's timeline
7. **Search test**: Search for the test tweet text
8. **Mentions test**: Get mentions (may be empty)
9. **Delete test**: Delete the test tweet (cleanup)
10. **Quota test**: Verify reads_used/writes_used increment, monthly reset works
11. **Cap enforcement**: Attempt action after cap reached, verify clear error
12. **Re-auth test**: Expire both access + refresh token, verify error indicates re-auth needed

## Scope

### In scope (v1)
- OAuth 2.0 PKCE authentication (C++ admin endpoint)
- Token refresh with refresh_token
- Post (text only, no media)
- Reply
- Read timeline (authed user)
- Read user tweets
- Search (7-day recent)
- Get mentions
- Get tweet by ID
- Delete own tweets
- Rate limit tracking (monthly read/write caps)
- Tier-aware feature gating
- `quota` action for agent self-awareness

### Not in scope (future)
- Media upload (images, video, GIF)
- Retweet / quote tweet
- Like (policy-prohibited for automation)
- Follow/unfollow (policy-prohibited for automation)
- Direct messages (separate API product)
- List management
- Bookmarks
- Polls
- Spaces
- Streaming (Basic+ tier, deferred)
- Full archive search (Pro tier, deferred)
- Webhook / account activity API (requires paid partnership)

## Acceptance Criteria

- [x] `twitter.lua` registers via `animus.register_social` with correct schema
- [x] OAuth 2.0 PKCE flow works end-to-end: authorize → callback → tokens cached
- [x] Access token used for all API calls, refresh on expiry (Lua adapter handles refresh)
- [ ] Refresh token exchange works, new tokens cached
- [x] Re-auth required signaled clearly when refresh token expires/revoked (Lua adapter returns error)
- [ ] Post publishes tweet successfully
- [ ] Reply references correct parent tweet
- [ ] Read timeline returns tweets for authed user
- [ ] Search returns matching tweets (7-day window)
- [ ] Get mentions returns mentions of authed user
- [ ] Delete removes own tweets
- [ ] Monthly read/write counters increment correctly
- [ ] Monthly counter reset works when month rolls over
- [ ] Actions blocked when monthly cap reached, with clear error
- [ ] `quota` action returns accurate current usage
- [ ] Tier config gates streaming actions (not registered on Free)
- [x] Multiple instances operate independently (config keyed by channel name)
- [x] Errors surfaced clearly (rate limits, auth failures, policy violations)
- [ ] Twitter per-request rate limit headers parsed and respected

## Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Free tier too restrictive for useful polling | Medium | Conservative default interval (30 min), on-demand reads, quota visibility |
| OAuth flow complexity (PKCE + redirect) | Medium | Handle exchange in C++ admin endpoint, not Lua |
| Token expiry mid-session | High (2hr access tokens) | Refresh token auto-use; offline.access scope for refresh token |
| Twitter API v2 changes | Low | Well-established v2; breaking changes rare |
| Developer account suspended | Low-Medium | Policy-compliant actions only; no likes/follows/bulk |
| Crypto primitives missing in Lua bridge | Medium | OAuth handled in C++; Lua only uses cached tokens |
| Rate limit headers inconsistent | Low | Parse on best-effort basis; monthly cap is primary gate |
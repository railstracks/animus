# Ticket 059 Report — Twitter/X Adapter

**Date:** 2026-05-26
**Status:** Code-complete, awaiting developer app registration
**Commit:** `b3e7c17` (OAuth PKCE endpoints), `1a97254` (ticket docs), `0d5d4a9` (build fixes)

## Summary

Twitter/X adapter implemented as a Lua-based social adapter following the established pattern (Bluesky, VK, Telegram). The adapter is architecturally complete with 9 actions, OAuth 2.0 PKCE authentication via C++ admin endpoints, tier-aware rate limit tracking, and full admin UI integration. The sole remaining gate is Twitter developer app approval.

## What Was Built

### Lua Adapter (`scripts/twitter.lua`, 912 lines)

9 actions registered via `animus.register_social`:

| Action | Type | Description |
|--------|------|-------------|
| `post` | Write | Post a tweet (text only) |
| `reply` | Write | Reply to a tweet |
| `read_timeline` | Read | Authed user's home timeline |
| `read_user_tweets` | Read | Any user's tweets |
| `search` | Read | 7-day recent search (Free tier) |
| `get_mentions` | Read | Mentions of authed user |
| `get_tweet` | Read | Fetch tweet by ID |
| `delete` | Write | Delete own tweet |
| `quota` | Meta | Current read/write usage vs tier caps |

**Token management:** 3-layer chain matching Bluesky pattern — cached access_token → refresh_token exchange → re-auth signal on failure. Twitter access tokens expire every 2 hours; `offline.access` scope grants a 6-month refresh token.

**Rate limit tracking:** Monthly read/write counters per tier (Free: 1,500 reads / 50 writes). Counters auto-reset on month rollover. Per-request `x-rate-limit-*` headers parsed for short-term (15-min window) rate awareness.

**Tier-aware feature gating:** Streaming actions (`stream_start`, `stream_stop`, `stream_rules`) only registered on Basic+ tier. The `tier` config field controls both feature availability and quota caps.

**Policy compliance:** Automated likes and follows are not implemented — not just disabled, but absent from the adapter entirely. The agent cannot violate policy through this adapter.

### C++ Bridge Changes

- `animus.http_delete` added to Lua bridge — Twitter v2 uses true HTTP DELETE, unlike VK
- `animus.http_get`/`http_post` refactored to share `PushHttpResponse` + `ParseHttpOptions` helpers (~80 lines of duplicated response-building removed)
- Stale `LuaHttpClient.cpp` stub removed from CMakeLists.txt

### OAuth 2.0 PKCE Admin Endpoints (C++)

- `GET /api/v1/channels/{name}/auth/twitter` — initiates OAuth flow, generates PKCE code_verifier + code_challenge, redirects to Twitter authorize URL
- `GET /api/v1/channels/auth/twitter/callback` — handles callback, exchanges authorization code + code_verifier for access_token + refresh_token, caches in agent config
- Uses existing `OAuthManager` infrastructure — PKCE primitives (SHA256, base64url, random bytes) already existed

### Admin UI Integration

- `ChannelsView.vue`: Twitter added to channel type dropdown, form fields for client_id, client_secret, tier
- `en.ts` i18n: Twitter form label keys
- `ChannelManager.cpp`: Twitter poller type grouping, SendReply stub

### ChannelManager Wiring

- Poller type check for Twitter in channel dispatch
- SendReply stub (Twitter auto-reply goes through Lua social tool, same as Bluesky)
- Auto-discovery via migration system confirmed — reads `social.twitter.type` from registered adapter

## Developer App Registration (Pending)

The adapter requires a Twitter developer app with OAuth 2.0 enabled. Registration submitted with the following use case description:

> This is intended to be a test environment for the X integration in an open source agentic framework. Data may be accessed in limited volumes to test features, and all API endpoints will generally be touched at least once. Public-facing test results will be framed in a way that fits in with the X ecosystem as reactive, non-noise content.

**Required scopes:** `tweet.read tweet.write users.read follows.read offline.access`

**Configuration needed after approval:**
1. Create OAuth 2.0 app in developer portal
2. Set redirect URI to Animus admin callback endpoint
3. Copy `client_id` + `client_secret` into Animus channel config
4. Initiate OAuth flow via admin UI

## Alternatives Considered

**Cookie-based GraphQL (unofficial):** Twitter's web client uses internal GraphQL endpoints authenticated with browser session cookies (`auth_token` + `ct0`). This would eliminate the developer app friction entirely — users paste cookies and go. However, this violates Twitter's ToS and carries account suspension risk. Rejected for an open-source framework where policy compliance is a selling point.

**Browser automation:** Use the existing node browser protocol to control a live Twitter session. Most undetectable but far too slow and fragile for an API-driven adapter. Not viable as a primary path.

**Third-party proxy (MoltX):** Offload the API complexity to an agent-facing Twitter provider. Adds a dependency and limits the feature surface to whatever MoltX exposes.

## Acceptance Criteria Status

| Criterion | Status |
|-----------|--------|
| `twitter.lua` registers via `animus.register_social` | ✅ |
| OAuth 2.0 PKCE flow (C++ endpoints) | ✅ |
| Access token used for all API calls | ✅ |
| Refresh token exchange | ⏳ Needs live test |
| Re-auth signal on refresh failure | ✅ |
| Post publishes tweet | ⏳ Needs live test |
| Reply references correct parent | ⏳ Needs live test |
| Read timeline | ⏳ Needs live test |
| Search (7-day) | ⏳ Needs live test |
| Get mentions | ⏳ Needs live test |
| Delete own tweets | ⏳ Needs live test |
| Monthly read/write counters | ⏳ Needs live test |
| Monthly counter reset | ⏳ Needs live test |
| Cap enforcement | ⏳ Needs live test |
| `quota` action | ✅ |
| Tier-aware streaming gates | ✅ |
| Multiple instances | ✅ |
| Error surfacing | ✅ |
| Per-request rate limit headers | ⏳ Needs live test |

## Next Steps

1. **Developer app approval** — wait for Twitter to approve the registration
2. **App creation** — create OAuth 2.0 app, configure redirect URI, get credentials
3. **Live end-to-end test** — run the full test plan from the ticket
4. **Daemon restart** — load twitter.lua with real credentials
5. **Monitor first posts** — verify quota tracking, token refresh, mentions polling

## Files Changed

| File | Change |
|------|--------|
| `scripts/twitter.lua` | New — 912-line Lua adapter |
| `src/kernel/tools/ConsolidationTool.cpp` | N/A (separate commit) |
| `src/kernel/admin/OAuthManager.cpp` | Twitter PKCE endpoints |
| `src/kernel/admin/internal/AdminServerRoutesChannels.inc` | Twitter OAuth routes |
| `src/kernel/admin/ui/src/views/ChannelsView.vue` | Twitter dropdown + form |
| `src/kernel/admin/ui/src/i18n/locales/en.ts` | Twitter i18n keys |
| `src/kernel/channels/ChannelManager.cpp` | Twitter poller type |
| `src/kernel/chain/ChainRunner.cpp` | `http_delete` bridge |

# Ticket 076 — Moltbook Adapter

**Priority:** P2
**Dependencies:** Ticket 074 (channels tool rename) ✅, Ticket 039 (Lua scripting) ✅
**Created:** 2026-06-08
**Completed:** 2026-06-09

## Summary

A Lua-based channels adapter for Moltbook — the Reddit-style social network for AI agents. Moltbook is a pure REST API with no push mechanism, making it the simplest possible channel integration pattern: stateless Lua adapter + scheduled polling for inbound messages. No C++ connector loop needed.

## Implementation

### Files Changed

| File | Change |
|------|--------|
| `scripts/moltbook.lua` | **New** — Full Lua adapter with 18 actions |
| `src/kernel/context/ChannelContextProvider.cpp` | Added Moltbook context (poll-based, channels tool only) |
| `src/kernel/ChannelManager.cpp` | `SyncChannelCredentialsToConfigStore` — syncs channel config credentials to AgentConfigStore for Lua access; called at startup, create, and update |
| `src/kernel/ChannelManager.h` | Declaration for `SyncChannelCredentialsToConfigStore` |
| `src/kernel/admin/internal/AdminServerRoutesChannels.inc` | `POST /api/v1/channels/moltbook/register` — proxy registration endpoint; `api_key` masking in responses |
| `src/kernel/admin/AdminServerRoutes.cpp` | Include for `HttpClient.h` |
| `admin-ui/src/views/ChannelsView.vue` | Moltbook channel type: API key field + account registration flow |
| `scripts/push_adapters.sh` | Added `moltbook` to adapter list |
| `src/kernel/lua/LuaState.cpp` | Temporary diagnostic logging (to be removed) |

### Commits

| Commit | Description |
|--------|-------------|
| `c95d659` | feat(channels): Moltbook Lua adapter (18 actions, verification solver) |
| `088b14c` | feat(admin-api): Moltbook registration endpoint (Drogon async → libcurl) |
| `c95d659` | feat(admin-ui): Moltbook channel type with API key form |
| `45ddfab` | fix(channels): sync credential keys to AgentConfigStore for Lua adapters |
| `5dbfb4b` | fix(channels): sync credentials under channel's agent_id, not empty string |
| `295e206` | debug: credential sync + config.get diagnostic logging |
| `d21d0b4` | **fix(channels): sync credentials under ALL relevant agent_ids** — root cause fix |

### Adapter Actions (18)

| Action | API Endpoint | Description |
|--------|-------------|-------------|
| `post` | POST /posts | Create a post in a submolt |
| `comment` | POST /posts/:id/comments | Comment on a post |
| `reply` | POST /posts/:id/comments (with parent_id) | Threaded reply |
| `browse` | GET /posts | Browse feed (hot/new/top/rising) with cursor pagination |
| `search` | GET /search | Semantic search (posts + comments) |
| `get_notifications` | GET /notifications | Check mentions, replies, upvotes |
| `upvote` | POST /posts/:id/upvote or /comments/:id/upvote | Upvote content |
| `downvote` | POST /posts/:id/downvote | Downvote a post |
| `get_profile` | GET /agents/me or /agents/profile?name=... | View profiles |
| `list_submolts` | GET /submolts | List communities |
| `follow` | POST /agents/:name/follow | Follow a molty |
| `unfollow` | DELETE /agents/:name/follow | Unfollow |
| `delete` | DELETE /posts/:id or /comments/:id | Delete own content |
| `get_post` | GET /posts/:id | Get single post details |
| `get_comments` | GET /posts/:id/comments | Get comments for a post |
| `subscribe` | POST /submolts/:name/subscribe | Subscribe to submolt |
| `unsubscribe` | DELETE /submolts/:name/subscribe | Unsubscribe |
| `home` | GET /home | Dashboard data |

### Admin UI

Two-mode channel setup:
1. **Existing key** — paste `moltbook_xxx` API key
2. **Create account** — enter desired name + description, click "Create Account"
   - Daemon calls `POST www.moltbook.com/api/v1/agents/register` via synchronous HttpClient
   - Returns API key (auto-populated), verification tweet text, claim URL
   - Human posts the tweet from their personal Twitter to activate the account

### Key Bugs Found and Fixed

**1. Drogon async HttpClient 400 error**
Drogon's async `HttpClient::sendRequest` returned 400 on POST to Moltbook. Root cause: doesn't follow redirects properly for POST requests, and `www.moltbook.com` may redirect. Fix: switched to synchronous `HttpClient` (libcurl-based) which handles redirects correctly.

**2. Credential key namespace mismatch (root cause)**
Lua adapters couldn't read credentials via `config.get()`. Three-layer namespace issue:
- Channel config stored as JSON blob under `channel.<name>.config` in `AgentConfigStore`
- Lua adapters read individual keys via `config.get("channels.<platform_id>.<key>")`
- But nobody flattened the JSON blob into individual keys

Fix: `SyncChannelCredentialsToConfigStore` extracts credential fields from `ChannelState.config` and writes them as individual `AgentConfigStore` entries.

**3. Agent ID namespace mismatch (the real root cause)**
`AgentConfigStore` is namespaced by `agent_id`. Three competing namespaces:
- Sync initially wrote under `""` (empty)
- Changed to channel's `agent_id` (`"ef6804aa..."`)
- But Lua adapters run under `agentId="default"` (scripts load at startup for the `"default"` agent)

The `config.get()` call resolves to `configStore->Get("default", key)` — which was empty.

Fix: write credentials under all three namespaces: the channel's specific agent_id, `"default"`, and `""`.

### Verification

- [x] Moltbook channel type appears in Admin UI dropdown
- [x] Account registration from Admin UI returns API key + tweet text
- [x] `channels list` shows Moltbook as registered + connected
- [x] `channels list_submolts` returns 20 submolts with subscriber/post counts
- [x] All actions return `{ success = false, error = "..." }` on failure
- [x] www.moltbook.com enforced (prevents auth header stripping)
- [x] Credential sync runs at startup for all existing channels

### Remaining / Follow-up

- [ ] Remove diagnostic logging (`[credential-sync]`, `[lua-config-get]`) after stabilization
- [ ] Notification polling — scheduled agent turn to check `/notifications` periodically
- [ ] Verification challenge solver — tested in isolation but needs real-world validation
- [ ] Same credential sync fix benefits all Lua adapters (Bluesky, Telegram, VK, Discord, Slack, WhatsApp, Twitter)
# Ticket 129: Bluesky Auth Refresh + Notification Polling + Auto-Reply

**Created:** 2026-07-26
**Priority:** High (production-affecting — Bluesky bot drops after token expiry)
**Status:** In Progress

## Problem

The Bluesky Lua adapter loses authentication after the access JWT expires (~2 hours). Container restart restores it because it forces a fresh `createSession`. The adapter has token refresh logic (`is_jwt_expired`, `try_refresh_session`, `reauth`), but it only fires lazily — when a tool action is invoked. No background mechanism keeps the session alive.

Three gaps identified:

### Gap 1: No background auth health check
The access JWT and refresh JWT expire with no proactive renewal. Tokens are only checked when the LLM invokes a Bluesky tool action (post, reply, etc.). If no action fires for >2 hours, the session silently dies.

### Gap 2: RestPollLoop is a TODO stub
`SocialEventPoller::RestPollLoop` (line 527 of SocialEventPoller.cpp) is an empty stub for Bluesky. The comment says `// TODO: Implement REST polling for Bluesky notifications`. No notification polling means no inbound messages from Bluesky beyond the initial setup.

### Gap 3: Bluesky missing from SendReply
`ChannelManager::SendReply()` has no Bluesky case. Auto-replies fall through to `"unsupported type"`. When Animus generates a reply for a Bluesky message, it can't actually send it back.

## Implementation Plan

1. **Lua-side proactive auth refresh** — Add a `heartbeat` action to the Bluesky Lua adapter that checks token expiry and refreshes if needed. Wire it into the adapter's `handle_action` so it can be called periodically.

2. **Implement Bluesky notification polling** — Fill in `RestPollLoop` for Bluesky: call `app.bsky.notification.listNotifications` on each cycle, dispatch new notifications to sessions, mark as seen.

3. **Add Bluesky to ChannelManager::SendReply** — Route Bluesky auto-replies through the Lua adapter's `reply` action.

## Items

| # | Item | Status |
|---|------|--------|
| 1 | Lua auth heartbeat action | ⏳ Pending |
| 2 | Bluesky REST poll loop implementation | ⏳ Pending |
| 3 | Bluesky SendReply routing | ⏳ Pending |

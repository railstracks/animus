# Ticket 118 — Implementation Report

**Ticket:** Discord Channel Tracking vs Response Separation  
**Status:** ✅ Implemented  
**Branch:** `dev`  
**Commit:** `3b50a62` (tracking separation) + `239d220` (Boolean loading fix) + `f22bc40` (i18n quoting fixes)  
**Date:** 2026-07-17  

---

## Summary

Separates Discord channel **tracking** (logging messages to sessions for context) from **responding** (triggering agent chains). Enables context-aware bot responses without token cost on every logged message.

## What Was Built

### 1. LogToSession — New ChannelManager Primitive

New `LogToSession` method on `ChannelManager` appends messages to session history without triggering agent chains. `DispatchToSession` is conceptually decomposed into `LogToSession` + `ExecuteChannelDispatch`.

- **LogCallback** — new callback type wired from `AgentKernel.cpp`, handles session creation (`GetOrCreate`), agent ID assignment, turn append (`SessionTurn{role="user"}`), and persistence (`FlushSession`).
- Zero token cost — no provider resolution, no chain execution, no LLM call.
- Logged messages appear as prior user turns when a later mention triggers `DispatchToSession` on the same channel session.

### 2. New Config Controls

| Control | Key | Type | Default | Description |
|---------|-----|------|---------|-------------|
| Track all channels | `monitor_all_channels` | bool | false | Track messages from all channels automatically |
| Tracked Channels | `monitored_channels` | string[] | [] | Track specific channel IDs (repurposed — was "respond to") |
| Respond to mentions | `respond_to_mentions` | bool | true | Respond when bot is mentioned (existing) |
| Respond to channels | `respond_to_channels` | bool | false | Respond to any message in tracked channels |

### 3. Behavior Matrix (All Four Combinations Validated)

| monitor_all | monitored_channels | respond_to_mentions | respond_to_channels | Effect |
|:-----------:|:------------------:|:-------------------:|:-------------------:|--------|
| ✗ | [] | ✓ | ✗ | Mentions only, no context (backward-compatible default) |
| ✗ | [list] | ✓ | ✗ | Mentions + context from listed channels (zero cost on logged msgs) |
| ✗ | [list] | ✓ | ✓ | Track + respond on listed channels |
| ✓ | [] | ✓ | ✗ | Track all channels, respond only on mention |
| ✓ | [] | ✓ | ✓ | Track + respond on all channels |

### 4. Dispatch Logic (DiscordGatewayLoop.cpp)

```
isTracked = monitor_all_channels || channelInMonitoredList

if (isDm && respondToDm):
    DM whitelist check → dispatch
elif (isMention && respondToMentions):
    dispatch (+ log if tracked, for session routing)
elif (isTracked && respondToChannels):
    dispatch
elif (isTracked):
    log only (LogToSession, no chain)
```

### 5. Admin UI

- Two new checkboxes added to Discord channel form: "Track all channels" and "Respond to any message in tracked channels"
- "Monitored Channels" textarea repurposed — label changed to "Tracked Channels", hint updated to clarify messages are logged without triggering responses
- 23 locales translated

### 6. Bug Fix: Boolean Config Loading

All channel form toggles (Discord, IRC, Slack) displayed as enabled regardless of saved state. Root cause: config values stored as strings (`"true"`/`"false"`) but `Boolean("false")` returns `true` in JavaScript. Fixed by replacing all `Boolean(cfg.X)` calls with `String(cfg.X) === 'true'`.

## Acceptance Criteria

- [x] `monitor_all_channels` checkbox in Discord channel config
- [x] `respond_to_channels` checkbox in Discord channel config
- [x] Tracked channels log messages to per-channel sessions without triggering agent chains
- [x] When bot is mentioned in a tracked channel, prior logged messages appear as context
- [x] `respond_to_channels` triggers agent chain on any message in tracked channels
- [x] All four combinations from the behavior matrix work correctly
- [x] DM whitelist (from v0.1.2) still works
- [x] i18n labels in 23 locales
- [x] Builds and runs without crashloop
- [x] Boolean config loading fixed across all channel types
- [x] User-tested and confirmed working

## Architecture Notes

- `LogToSession` uses the same routing key pattern (`post:CHANNEL_ID`) as `DispatchToSession`, ensuring tracked messages and dispatched messages land in the same session.
- Session key format: `chat:discord:CHANNEL_ID` — shared between log and dispatch paths.
- `LogCallback` signature: `(agentId, sessionKey, message, sessionType)` — no `ReplyTarget` needed since no response is expected.
- The `monitored_channels` semantic change (respond → track) is technically breaking, but the feature shipped same-day in v0.1.2 with no external users yet.

## Files Changed

- `include/animus_kernel/ChannelManager.h` — LogCallback type, LogToSession declaration, constructor signature
- `src/kernel/ChannelManager.cpp` — LogToSession implementation, constructor updated
- `src/kernel/AgentKernel.cpp` — LogCallback lambda, ChannelManager construction
- `src/kernel/DiscordGatewayLoop.cpp` — dispatch logic rewritten with tracking/response separation
- `admin-ui/src/views/ChannelsView.vue` — new form fields, config builder/loader, Boolean fix
- `admin-ui/src/i18n/locales/*/channels.ts` — 23 locales updated (NL + TR quoting fixed by Melvin)
- `tickets/118-discord-channel-tracking.md` — ticket updated with implementation details

## Next Steps

- Merge to `main` and tag next release (batching with other features)
- Consider applying the LogToSession/DispatchToSession decomposition to other channel adapters (IRC, Slack, Telegram) in a future ticket

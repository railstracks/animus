# Ticket 118: Discord Channel Tracking vs Response Separation

**Status:** In Progress  
**Date:** 2026-07-17  
**Author:** Melvin  
**Component:** Discord Gateway, ChannelManager  

## Problem

When `respond_to_mentions` is enabled but the bot doesn't monitor a channel, triggering on a mention misses preceding conversation context. The bot responds to the mention but has no idea what was discussed before.

Current `monitored_channels` both tracks AND responds — there's no way to log messages for context without also triggering agent chains on every message.

## Proposal

Separate **tracking** (logging messages to sessions) from **responding** (triggering agent chains). Four controls:

| Control | Type | Default | Description |
|---------|------|---------|-------------|
| `monitor_all_channels` | bool | false | Track messages from all channels automatically |
| `monitored_channels` | string[] | [] | Track messages from specific channel IDs (existing) |
| `respond_to_mentions` | bool | true | Respond when bot is mentioned (existing) |
| `respond_to_channels` | bool | false | Respond to any message in a tracked channel |

### Supported Combinations

1. **Mentions only, no tracking** — current default. Bot responds to mentions with no channel context.
2. **Mentions + tracking** — bot logs channel messages; when mentioned, has prior context. No token cost for tracking-only messages.
3. **Selective tracking + respond_to_channels** — bot tracks and responds on specific channels.
4. **All channels + respond_to_channels** — bot sees and responds to everything. User responsibility to manage token budgets.

### Behavior Matrix

| monitor_all | monitored_channels | respond_to_mentions | respond_to_channels | Effect |
|-------------|--------------------|--------------------|--------------------|--------|
| ✗ | [] | ✓ | ✗ | Mentions only, no context (current default) |
| ✗ | [list] | ✓ | ✗ | Mentions + context from listed channels |
| ✗ | [list] | ✓ | ✓ | Track + respond on listed channels |
| ✓ | [] | ✓ | ✗ | Track all channels, respond only on mention |
| ✓ | [] | ✓ | ✓ | Track + respond on all channels |
| ✗ | [] | ✗ | ✓ | *(invalid — no channels to respond in)* |

## Implementation

### C++ — ChannelManager + AgentKernel

**LogToSession (new method on ChannelManager):**
Decomposes `DispatchToSession` into:
1. Route lookup/registration → session key
2. Session `GetOrCreate` + agent ID assignment
3. `AddTurn({role="user", content=message, unix_ms=now})` + persist
4. Return (no provider resolution, no chain execution)

`DispatchToSession` becomes: `LogToSession` + `m_dispatch` (which calls `ExecuteChannelDispatch`).

Tracking-only mode calls `LogToSession` directly — message stored in session history, no token cost. When a later mention triggers `DispatchToSession` on the same channel, the chain runner sees prior logged messages as context.

**Dispatch logic in DiscordGatewayLoop MESSAGE_CREATE handler:**

```
bool isTracked = monitor_all_channels || channelInMonitoredList;
bool shouldRespond = false;
bool shouldLog = false;

if (isDm && respondToDm) {
    shouldRespond = true;  // DMs always dispatch when enabled
} else if (isMention && respondToMentions) {
    shouldRespond = true;
    shouldLog = isTracked;  // use tracked session for context
} else if (isTracked && respondToChannels) {
    shouldRespond = true;
}

if (isTracked && !shouldRespond) {
    shouldLog = true;  // track without responding
}
```

If `shouldLog` and `!shouldRespond`: call `LogToSession` (append to session, no chain).
If `shouldRespond`: call `DispatchToSession` with the channel session key (existing behavior, but now may have prior context from logged messages).

### Admin UI — ChannelsView.vue

- `monitor_all_channels` checkbox
- `respond_to_channels` checkbox
- Keep existing `monitored_channels` textarea and `respond_to_mentions` checkbox
- `respond_to_channels` hint: "Responds to any message in monitored channels"

### i18n

- `monitorAllChannels`: "Monitor all channels"
- `respondToChannels`: "Respond to any message in monitored channels"
- 23 locales

### Config keys

- `monitor_all_channels` (string "true"/"false", matching existing pattern)
- `respond_to_channels` (string "true"/"false")
- `monitored_channels` (string array, existing — repurposed as tracking list)
- `respond_to_mentions` (string "true"/"false", existing)

### Token budget

No message count limit. Existing token budgets (`context_token_budget`, `max_response_tokens`) cap prompt assembly. Users configuring `monitor_all_channels` on busy servers are responsible for managing costs via `min_response_interval` and token budget settings.

## Acceptance Criteria

- [ ] `monitor_all_channels` checkbox in Discord channel config
- [ ] `respond_to_channels` checkbox in Discord channel config
- [ ] Tracked channels log messages to per-channel sessions without triggering agent chains
- [ ] When bot is mentioned in a tracked channel, prior logged messages appear as context
- [ ] `respond_to_channels` triggers agent chain on any message in tracked channels
- [ ] All four combinations from the behavior matrix work correctly
- [ ] DM whitelist (Ticket 118 prerequisite: already merged) still works
- [ ] i18n labels in 23 locales
- [ ] Builds and runs without crashloop

## Dependencies

- v0.1.2 (Discord DM whitelist, stale connection fix)

## Notes

- `LogToSession` is a new code path — needs to append to session history without creating a chain run. The session key for channel tracking uses `post:CHANNEL_ID` (same as existing dispatch routing).
- The `monitored_channels` textarea is repurposed: previously it meant "respond to messages in these channels," now it means "track messages in these channels." The response behavior is controlled by `respond_to_channels`. This is a breaking change for anyone using `monitored_channels` today, but the feature is new enough that migration isn't a concern.
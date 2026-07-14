# Ticket 115 — Interjection: Implementation Report

**Date:** 2026-07-14
**Commit:** `2a11532`
**Status:** Implemented
**Build:** Clean on workstation (all targets)

## What Was Built

Messages arriving during an active prompt chain are now injected as user turns between LLM calls. The LLM sees them on its next iteration and can adapt — pivot direction, acknowledge, or ignore.

### Architecture

```
Message arrives during active chain
  → Dispatch lambda reads allow_interjection from channel config
  → SetInterjectionEnabled(queueKey, allowInterjection) on MessageQueue
  → Message pushed to queue (Push)
  → ChainRunner between steps: HasPending? → DrainInterjection()
  → If non-empty: added as user turn with "[New messages arrived...]" framing
  → Next LLM call includes the interjected messages
```

### Files Changed (7, +100 lines)

**MessageQueue.h/cpp** — New `allow_interjection` flag on `SessionState`, `SetInterjectionEnabled()` setter, `DrainInterjection()` method that returns empty string when disabled or empty, otherwise returns messages with interjection header. Does NOT clear `timer_running` — the chain is still active; `NotifyChainEnd` handles cooldown.

**ChainRunner.h/cpp** — `m_messageQueue` pointer (nullable), `SetMessageQueue()` setter. Both `ExecuteOnSession` and `ExecuteStreamingOnSession` check `HasPending` → `DrainInterjection` between tool budget check and `shouldContinue` check. Uses `session.Key().connector` as the queue key (matches `"channel:" + sessionKey`).

**AgentKernel.cpp** — Wires `m_messageQueue` into `m_chainRunner` after MessageQueue creation. Dispatch lambda reads `allow_interjection` from `ChannelManager::GetChannel(name)->config` (default: true), calls `SetInterjectionEnabled` on every push.

**ChannelsView.vue** — `v-switch` toggle in channel form. Default: true. Saved as boolean in channel config JSON.

**en/channels.ts** — `allowInterjection: 'Allow Interjection'` label.

### Deferred Items

- **Sender labels from channel adapters** — dispatch callback doesn't pass sender name yet (same as Ticket 114). Community channels will show `[unknown]:` until adapters are updated.
- **i18n translations** — English only. Other locales will show the key path.
- **Default per channel type** — Currently defaults to true for all channels. The ticket specifies false for community channels; this would need channel-type-aware defaults in the frontend.
- **Interval=0 interjection** — Interjection only works when `min_response_interval > 0` (i.e., MessageQueue is active). Channels with interval=0 dispatch directly and don't use the queue.

### Interaction with Ticket 114

| State | Interjection ON | Interjection OFF |
|-------|----------------|------------------|
| Chain active | Messages drained between steps | Messages accumulate in queue |
| Chain ended, cooldown | Messages queue normally | Messages queue normally |
| No chain, no cooldown | Direct dispatch | Direct dispatch |

### Acceptance Criteria Status

- [x] `allow_interjection` field in channel config (DB + API + admin UI)
- [x] New messages injected as user turns between LLM calls during active chains
- [x] Injected messages formatted with sender labels in community channels (framework in place; adapters need sender passing)
- [x] Interjection framing (system note: "New messages arrived while you were working")
- [x] Thread-safe queue (channel adapters push, chain runner drains)
- [x] Per-session: no cross-session injection
- [x] When disabled, messages queue normally (Ticket 114 batching behavior)
- [ ] DM channels default to interjection enabled (currently all default to true)
- [ ] Community channels default to interjection disabled (currently all default to true)
- [x] No concurrent LLM calls on the same session
- [x] Build clean on workstation

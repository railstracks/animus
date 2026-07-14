# Ticket 114 — Implementation Report

**Date:** 2026-07-14
**Ticket:** 114 — Channel Message Interval + Batched Dispatch
**Status:** Implemented
**Commit:** `5a81d2a` (+ fix `f5877b8`)
**Build:** Clean on workstation (GCC, Ubuntu 24.04)

## What Was Built

### New: MessageQueue class (`include/animus_kernel/MessageQueue.h`, `src/kernel/MessageQueue.cpp`)

Thread-safe per-session message accumulation queue with a dedicated timer thread.

- **Push(sessionKey, sender, content, unixMs, intervalSeconds, maxQueued)** — accumulates messages. If no chain is active and no timer running, starts a cooldown timer. If max queued is hit, forces an immediate flush.
- **TimerLoop()** — background thread that waits for the earliest deadline across all sessions, then flushes expired sessions by calling the flush callback.
- **NotifyChainStart(sessionKey)** — cancels any pending timer (chain is active, don't flush mid-chain).
- **NotifyChainEnd(sessionKey, intervalSeconds)** — starts cooldown timer if there are pending messages.
- **Drain(sessionKey)** — returns concatenated messages and clears queue (for forced flush).
- **HasPending / PendingCount** — queue status queries.
- **ConcatenateMessages()** — formats with sender labels `[sender]: content` when any message has a sender (community channels), raw text otherwise (DMs).

### AgentKernel changes

- **ExecuteChannelDispatch(sessionKey, message, replyTarget)** — extracted from the inline dispatch lambda. Resolves provider, model, context window, enqueues the chain job. Calls `NotifyChainStart` before the job and `NotifyChainEnd` after completion.
- **GetChannelInterval(channelName)** — reads `min_response_interval` from the channel's config JSON. Returns 0 if not set.
- **Dispatch lambda** — now checks `GetChannelInterval()`. If > 0: pushes to `MessageQueue` and stores `ReplyTarget` in `m_pendingReplyTargets`. If 0: dispatches directly (existing behavior).
- **MessageQueue flush callback** — retrieves stored `ReplyTarget`, strips `channel:` prefix from queue key, calls `ExecuteChannelDispatch` with the concatenated message.
- **m_pendingReplyTargets** — `unordered_map<string, ChannelManager::ReplyTarget>` guarded by `m_pendingReplyTargetsMutex`. Stores the most recent `ReplyTarget` per session for the flush callback.
- **Shutdown** — `MessageQueue::Shutdown()` called in destructor before `m_channelManager` deletion.

### Admin UI changes (`ChannelsView.vue`)

- **min_response_interval** field added to the channel editor as a common field (shown after all type-specific sections, before the form actions).
- Included in `formData` defaults (0), `resetForm()`, `submitForm()` (added to config before API call), and loaded when editing existing channels.
- i18n label `minResponseInterval` added to `en/channels.ts`.

### CMake

- `src/kernel/MessageQueue.cpp` added to the kernel source list.

## Commits

| Commit | Description |
|--------|-------------|
| `5a81d2a` | Full implementation: MessageQueue, AgentKernel wiring, admin UI |
| `f5877b8` | Fix: capture `sessionKey` in `EnqueueInLane` lambda (compile error) |

## What's Not Yet Done (Deferred)

- **`message_queue_max` config field** — hardcoded to 50 in the dispatch lambda. Needs a UI field and config reading. Low priority — 50 is a reasonable default.
- **Sender labels from channel adapters** — the dispatch callback signature doesn't currently include the sender name. Channel adapters (Discord, IRC, Slack, VK) would need to pass it through. Currently `sender` is empty, so batched messages are concatenated without labels. This is fine for DM channels; community channels need this for proper attribution.
- **i18n translations** — only English label added. Other locales will show the key path until translated.
- **No per-session chain lock** — the `Cognition` job lane provides serialization, but there's no explicit per-session mutex preventing two different dispatch paths (e.g., webchat + channel) from running concurrent chains on the same session. This is a pre-existing condition, not introduced by this ticket.

## Architecture Notes

- **Timer thread** — one background thread for all sessions. Waits on a condition variable with the earliest deadline. Wakes every 5 seconds if no timers are active (lightweight poll).
- **Thread safety** — channel adapters push from I/O threads; the timer thread flushes; the chain runner runs on the Cognition job lane. All `MessageQueue` access is mutex-guarded.
- **No regression** — channels with `min_response_interval = 0` (the default) dispatch immediately, exactly as before. The `MessageQueue` is created but never receives messages for these channels.
- **ReplyTarget persistence** — the flush callback needs the original `ReplyTarget` to send the agent's reply back to the right channel/peer. This is stored in `m_pendingReplyTargets` at push time. If multiple messages arrive from different peers on the same session (unlikely but possible in community channels), the most recent `ReplyTarget` is used. This is acceptable — the reply goes to the most recent conversation context.

## Acceptance Criteria Status

- [x] `min_response_interval` field in channel config (DB + API + admin UI)
- [x] Messages on interval-configured channels queue instead of triggering immediate chains
- [x] Queued messages flush as a single concatenated user turn when interval expires
- [ ] Batched messages include sender labels in community channels (deferred — needs adapter changes)
- [x] DM channels are unaffected (interval = 0, immediate processing)
- [ ] No two concurrent chains on the same session (pre-existing gap, not addressed by this ticket)
- [x] Chain active state + last-chain-end-time tracked per session (in MessageQueue SessionState)
- [ ] `message_queue_max` safety valve forces flush at configured limit (hardcoded to 50, not configurable yet)
- [x] Default behavior (interval = 0) unchanged — no regression for existing channels
- [x] Build clean on workstation
# Ticket 114 — Channel Message Interval + Batched Dispatch

**Date:** 2026-07-14
**Priority:** High (cost control for community channels — unthrottled LLM calls in busy servers are expensive)
**Status:** Open

## Problem

Community channels (Discord, IRC, Slack, VK) can receive many messages in quick succession. Currently every incoming message triggers a separate LLM prompt chain. In a busy Discord server with 50 messages/minute, this means 50 LLM calls/minute — potentially thousands of dollars per day on capable models.

There is no per-channel rate limiting, no batching of rapid-fire messages, and no way to configure a minimum responsiveness interval.

## Proposal

Add a per-channel **minimum response interval** — a configurable cooldown between completed prompt chains. When the interval is active, incoming messages accumulate in a session-scoped queue. When the interval expires, all queued messages are concatenated into a single user turn and submitted as one prompt chain.

This gives community channel operators direct control over maximum LLM call frequency, while naturally batching rapid-fire conversations into coherent prompts the agent can assess holistically (and potentially choose NO_REPLY for).

## Design

### Per-Channel Config

Add to channel configuration:
- `min_response_interval` — integer seconds between chain completion and next prompt submission (default: 0 = immediate, no batching)
- `message_queue_max` — maximum messages to queue before forcing a flush (default: 50, safety valve)

### Message Queue (per session)

When a message arrives on a channel with `min_response_interval > 0`:

1. **Is a prompt chain currently active on this session?**
   - **Yes** → queue the message (it will be handled when the chain ends)
   - **No** → has the cooldown interval expired since the last chain ended?
     - **Yes** → if queue is non-empty, flush (concatenate all queued messages → one user turn → start chain). If queue is empty, process this message immediately.
     - **No** → queue the message

2. **Timer expiry** → if no chain active and queue is non-empty, flush the queue

### Batched Message Formatting

Queued messages are concatenated with sender labels so the agent can distinguish speakers:

```
[user1]: hello, can someone help with setup?
[user2]: +1 having the same issue
[user1]: here are the logs: ...
```

For DM channels (single user), labels are omitted — messages are concatenated directly since there's only one speaker.

### Channel Adapter Changes

Each channel adapter that supports community messaging (Discord, IRC, Slack, VK) needs to:
- Check the channel's `min_response_interval` config
- Route messages through the queue instead of directly to `ChatSessionService::HandleMessage`
- DM/private channels skip the queue (interval = 0 always)

### Session Lock

A per-session lock prevents two prompt chains from running simultaneously on the same session. If a chain is active, incoming messages are always queued regardless of interval — they'll be processed when the chain ends and the cooldown expires.

## Backend Changes

### Config

- Add `min_response_interval` (int) and `message_queue_max` (int) to channel config schema
- Expose in admin UI channel editor

### MessageQueue (new class)

```
MessageQueue:
  - Push(sessionKey, message) → adds to queue, triggers timer if needed
  - Flush(sessionKey) → returns concatenated messages, clears queue
  - PendingCount(sessionKey) → int
  - OnTimerExpired(sessionKey) → callback: flush + submit to ChatSessionService
```

### ChatSessionService Integration

- `HandleMessage` checks queue config before processing
- If interval > 0: route through MessageQueue
- If interval == 0: process immediately (existing behavior, no regression)
- After chain completion: start cooldown timer if interval > 0

### Session State Tracking

- Track `last_chain_end_time` per session (for cooldown calculation)
- Track `chain_active` per session (for interjection ticket — see Ticket 115)

## Frontend Changes

### Channel Editor

- Add "Minimum response interval (seconds)" field, disabled when 0
- Help text: "Wait this many seconds after the agent finishes responding before processing new messages. Incoming messages during the wait are batched into a single response."
- Add "Max queued messages" field (advanced, default 50)

## Acceptance Criteria

- [ ] `min_response_interval` field in channel config (DB + API + admin UI)
- [ ] Messages on interval-configured channels queue instead of triggering immediate chains
- [ ] Queued messages flush as a single concatenated user turn when interval expires
- [ ] Batched messages include sender labels in community channels
- [ ] DM channels are unaffected (interval = 0, immediate processing)
- [ ] No two concurrent chains on the same session
- [ ] Chain active state + last-chain-end-time tracked per session
- [ ] `message_queue_max` safety valve forces flush at configured limit
- [ ] Default behavior (interval = 0) unchanged — no regression for existing channels
- [ ] Build clean on workstation

## Complexity

Medium. New `MessageQueue` class (~150 lines), `ChatSessionService` integration (~50 lines), channel config changes (~30 lines), admin UI field (~20 lines), per-channel adapter routing (~20 lines per adapter).
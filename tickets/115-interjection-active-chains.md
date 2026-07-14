# Ticket 115 — Interjection: Inject Messages Into Active Prompt Chains

**Date:** 2026-07-14
**Priority:** Medium (UX improvement — enables real-time corrections and community participation during agent processing)
**Status:** Open
**Depends on:** Ticket 114 (shares session state tracking — `chain_active`, `last_chain_end_time`, message queue infrastructure)

## Problem

When the agent is mid-prompt-chain (e.g., executing a multi-step tool sequence), it goes deaf to new messages. A user who sends "wait, don't do that" or "actually, the error is on line 42" has to wait for the entire chain to complete before the agent sees their message. In community channels, multiple users may be talking while the agent is busy, and their messages are lost from the agent's context during processing.

## Proposal

Allow new messages to be **injected into an active prompt chain** as user turns. Between LLM calls (after a tool executes, before the next LLM call), the chain runner checks a per-session message queue. If new messages have arrived, they are appended as user turns before the next LLM call. The LLM sees them on its next iteration and can adapt — pivot direction, acknowledge, or ignore.

This applies to both:
- **DM channels:** user can add corrections or context while the agent is working
- **Community channels:** users can keep talking while the agent processes, and it catches up

## Design

### Chain Runner Integration

The `ChainRunner::ExecuteOnSession` loop already iterates: LLM call → tool execution → repeat. Between tool execution and the next LLM call, add an injection check:

```
// After tool execution, before next LLM call:
if (injectQueue && injectQueue->HasPending(sessionKey)) {
    auto messages = injectQueue->Drain(sessionKey);
    for (auto& msg : messages) {
        session.AddTurn({role: "user", content: msg});
    }
    // LLM sees these on its next call — natural conversation flow
}
```

### Interjection Formatting

Injected messages are formatted the same way as batched messages (Ticket 114):
- Community channels: `[user1]: message`
- DM channels: raw message (single speaker)

### Per-Channel Config

- `allow_interjection` — boolean (default: true for DMs, false for community channels)
- Community channels may want to disable this if the agent should stay focused on the original prompt and not get distracted by ongoing chat

### Interaction with Ticket 114 (Interval/Batching)

When interjection is enabled:
- Messages arriving during an active chain are **injected** (not queued for later)
- When the chain ends, the cooldown timer from Ticket 114 starts
- Messages arriving during cooldown are **queued** (not injected — no active chain)

When interjection is disabled:
- All messages during an active chain are **queued**
- After chain ends + cooldown expires, queued messages are batched (Ticket 114 behavior)

### System Prompt Awareness

When interjection is enabled, the agent's system prompt (or a system turn injected alongside the user messages) should note that new messages may arrive during processing:

```
[New messages arrived while you were working:]
[user1]: actually, don't run that command
[user2]: the issue is on line 42
```

This framing helps the LLM understand the temporal context — these aren't responses to its last output, they're interjections during its work.

### Concurrency Safety

- Injection is safe because the chain loop is single-threaded per session
- The queue is thread-safe (channel adapters write from different threads than the chain runner reads)
- No risk of concurrent LLM calls — injection happens between calls, not during

## Backend Changes

### ChainRunner

- Accept an optional `InjectionQueue` reference
- Between tool execution and next LLM call, check queue for pending messages
- If present, drain and append as user turns with interjection framing

### MessageQueue (from Ticket 114)

- Add `HasPending(sessionKey)` and `Drain(sessionKey)` methods
- Thread-safe — channel adapters push from I/O threads, chain runner drains from processing thread

### ChatSessionService

- When `allow_interjection` is true and a chain is active, route new messages to the injection queue instead of the batch queue
- When `allow_interjection` is false, route to the batch queue (Ticket 114 behavior)

### Channel Config

- Add `allow_interjection` boolean to channel config

## Frontend Changes

### Channel Editor

- Add "Allow interjection" toggle (boolean)
- Help text: "When enabled, new messages are injected into the agent's active processing. The agent sees them on its next step and can adapt. Disable for community channels where the agent should stay focused."
- Default: enabled for DM channels, disabled for community channels

## Acceptance Criteria

- [ ] `allow_interjection` field in channel config (DB + API + admin UI)
- [ ] New messages injected as user turns between LLM calls during active chains
- [ ] Injected messages formatted with sender labels in community channels
- [ ] Interjection framing (system note: "new messages arrived while you were working")
- [ ] Thread-safe queue (channel adapters push, chain runner drains)
- [ ] Per-session: no cross-session injection
- [ ] When disabled, messages queue normally (Ticket 114 batching behavior)
- [ ] DM channels default to interjection enabled
- [ ] Community channels default to interjection disabled
- [ ] No concurrent LLM calls on the same session
- [ ] Build clean on workstation

## Complexity

Medium. ChainRunner injection check (~30 lines), MessageQueue thread-safe drain (~20 lines), ChatSessionService routing (~20 lines), channel config + UI (~15 lines). The infrastructure from Ticket 114 (MessageQueue, session state tracking) is the prerequisite — most of the plumbing is shared.
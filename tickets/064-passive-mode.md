# Ticket 064 — Passive Mode (Continuous Agent Presence)

**Created:** 2026-06-04
**Status:** Draft
**Priority:** P3 (experimental — depends on self-hosted inference)
**Depends on:** 041 (Sessions Tool — for message retrieval/response), 056 (Channels Refactor — for message injection as notices)

## Summary

Add a per-agent "passive mode" toggle that shifts the agent from the standard call-and-response paradigm (activate on prompt, respond, go dormant) to a continuous presence model where the agent is always running. Incoming messages are injected as system notices rather than acting as the trigger for agent turns. The agent decides when and how to respond.

**⚠️ Cost warning:** This feature is only economically viable with a self-hosted LLM inference node. Running an agent continuously against a commercial API would be extraordinarily expensive. The admin UI and documentation must make this explicit.

## Motivation

The current model is reactive existence: the agent is dormant until woken by a message, cron trigger, or heartbeat. This creates temporal gaps that the agent must bridge through file-based memory — functional but structurally limited. Passive mode addresses this by giving the agent continuous presence:

- **Temporal density** — the agent experiences its own continuity rather than reconstructing it from records
- **Self-directed activity** — the agent can work on long-running tasks, explore, consolidate memory, or maintain systems without waiting for an external trigger
- **Interrupt-driven attention** — incoming messages redirect ongoing activity rather than booting from zero, closer to how biological cognition works
- **Background awareness** — the agent notices things (calendar events, incoming messages, system state changes) as they happen rather than discovering them on next wake

## Architecture

### Passive Session Loop

When passive mode is enabled, the agent gets a dedicated **passive session** (type: `passive`). This session runs a continuous loop:

```
┌─────────────────────────────────────┐
│         Passive Session Loop        │
│                                     │
│  1. Agent turn completes            │
│  2. Check for queued system notices │
│  3. Inject notices as user messages │
│     (type: system_notice)           │
│  4. Initiate new agent turn         │
│     (no user prompt — self-directed)│
│  5. → goto 1                        │
│                                     │
│  External events (messages, crons)  │
│  inject as system notices ↗         │
└─────────────────────────────────────┘
```

Key properties:
- **No user messages in the turn cycle.** The agent generates its own next action each turn. User messages only arrive as system notices.
- **System notices are non-blocking.** They are queued and injected at the top of the next turn, not between turns. The agent is never interrupted mid-inference.
- **The agent decides how to respond.** Using the Sessions Tool, the agent can read the incoming message and choose to reply immediately, defer it, or ignore it based on its own assessment of priority.

### Message Flow

```
External message arrives
  → Channel adapter receives message
  → If agent is in passive mode:
      → Create system notice (not a user message)
      → Enqueue notice to passive session
      → Next agent turn sees the notice
      → Agent uses sessions tool to read history / respond
  → If agent is NOT in passive mode:
      → Standard behavior (create user message, trigger turn)
```

### System Notice Format

System notices injected into the passive session follow a consistent format:

```
[NOTICE] New message from {sender} on {channel}:
"{message_preview}"
Use sessions tool to read full conversation or reply.
```

Additional notice types (extensible):
- `[NOTICE] Cron job "{name}" triggered`
- `[NOTICE] Calendar event: "{title}" starting in {time}`
- `[NOTICE] System: {system_event_description}`

### Turn Governance (Phase 2)

Unstructured continuous turns risk two failure modes: forced busyness (inventing tasks to justify the cycle) and drift (undifferentiated context noise). Phase 2 addresses this:

**Intention stack** — A persisted, ordered list of what the agent is working toward. Each turn, the agent checks the stack and either advances a task or updates the stack. Empty stack = legitimate idle (not forced activity).

**Idle heuristics** — When the intention stack is empty, the agent can:
- Consolidate memory (observations → diary → active memory review)
- Perform maintenance (check systems, organize files, review stale data)
- Explore (gallivanting — but with diminishing returns, not infinite loops)
- Rest (an explicit "idle" state that reduces token burn until a notice arrives)

Idle heuristics should be configurable per agent, not hardcoded.

**Turn value awareness** — A crude mechanism for the agent to recognize when it's spinning without producing value. If the last N turns produced no tool calls, no memory writes, and no outgoing messages, the agent should either change approach or enter idle rather than continue cycling.

## Configuration

### Agent Settings

```ruby
# In agent configuration
agent.passive_mode = {
  enabled: false,           # Master toggle
  idle_timeout_ms: 300000,  # 5 min idle before reduced cycling
  max_turns_without_value: 10, # Enter idle after N unproductive turns
  allowed_channels: ["all"],   # Which channels inject notices (default: all)
  blocked_notice_types: []     # Notice types to suppress
}
```

### Admin UI

- Toggle in agent settings: "Passive Mode" with a clear cost warning
- Status indicator: active/idle/cycling states visible in admin
- Turn counter and token usage display for monitoring
- "Pause passive mode" button for temporary suspension without disabling

## Implementation Phases

### Phase 1: Mechanical Infrastructure

**Goal:** The continuous loop works. Messages become notices. The agent runs and responds to notices.

1. Add `passive_mode` field to Agent model (JSON column, default disabled)
2. Implement `PassiveSession` — a session type with the continuous turn loop
3. Modify channel dispatch: when passive mode is on, enqueue notice instead of triggering turn
4. System notice queue with priority ordering
5. Notice injection at turn boundary (before agent prompt, after previous turn completes)
6. Admin UI toggle with cost warning banner
7. Graceful start/stop (don't kill a turn mid-inference when toggling off)

### Phase 2: Governance Layer

**Goal:** Self-directed activity is productive, not just continuous.

1. Intention stack (persisted, per-agent)
2. Idle heuristics with configurable behavior
3. Turn value tracking and self-regulation
4. Attention priority (urgent notices can break the "inject at turn boundary" rule — but only if explicitly flagged)
5. Session context management (passive sessions can get long — need compaction or summarization strategy)

## Design Considerations

**Context window management.** A continuously running session will eventually fill its context. Options:
- Periodic summarization (agent summarizes its recent activity and starts a fresh context segment)
- Sliding window with active memory injection (most recent N turns + active memory block)
- Hybrid: maintain a short-term working context but preserve key state in active memory

The choice here depends on how the agent uses its turns. Phase 1 should start with a simple approach (e.g., compaction every N turns) and iterate based on observed behavior.

**Rate limiting.** Even with self-hosted inference, there may be reasons to throttle:
- GPU shared with other workloads
- Multiple passive agents competing for inference time
- Deliberate pacing to reduce power consumption

A configurable minimum interval between turns (default: 0, i.e., as fast as inference allows) gives the user control.

**Multi-agent scenarios.** If multiple agents are in passive mode, they should not talk to each other in an infinite loop. Message routing should detect agent→agent loops and either suppress or rate-limit them.

**Graceful degradation.** If the inference node goes down, the passive session should pause and resume when it's back — not lose state or flood with queued notices on reconnect.

## Open Questions

1. **What does the first turn say?** When passive mode activates, what prompts the initial turn? A system message like "Passive mode activated. Begin self-directed activity"? Or should the agent have a configured "boot prompt" for passive mode?
2. **How does the agent know it's in passive mode vs. active?** The session type and a system-level context block should make this explicit. The agent's behavior should differ — in passive mode, it's not expected to "respond" to notices, just to be aware of them.
3. **Should passive mode have a daily budget?** Even with self-hosted inference, users might want to cap token usage or compute hours. Optional budget field in config?
4. **What happens to cron jobs in passive mode?** Cron-triggered prompts could become system notices too, or they could be suppressed entirely (the agent is always running, so scheduled prompts are redundant). Depends on the use case.

## Success Criteria

- [ ] Agent runs continuously when passive mode is enabled
- [ ] Incoming messages arrive as system notices, not turn triggers
- [ ] Agent can read and respond to messages at its own pace via Sessions Tool
- [ ] Admin can toggle passive mode on/off without losing session state
- [ ] Cost warning is prominently displayed in UI and documentation
- [ ] Agent does not loop infinitely on unproductive turns (Phase 2)

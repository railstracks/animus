# Design: Interjection & Device Events — One Kernel Primitive

**Status:** Draft for v0.5 · Direction agreed 2026-11-05 · Pending owner red-line
**Consumers:** [Voice system](voice.md) · [Cortex](cortex.md)
**Tickets:** to be cut from this document and its audit section

---

## Intent

Give the kernel one first-class mechanism for inserting an inbound event into a
*running* prompt chain, delivered to the model at the next step boundary. Two
0.5 systems consume the same primitive:

- **Voice:** the user keeps talking while the agent is thinking/speaking. Speech
  must filter in mid-chain, not queue until the chain finishes — otherwise the
  conversation degenerates into a walkie-talkie loop (speak → process → reply →
  only then listen again), which is the disjointedness we are designing against.
- **Cortex:** a camera motion event, a geofence arrival, an obstacle threshold —
  device events must reach the agent the same way speech does: inserted
  mid-chain, visible at the next step, actionable by the running chain.
- **Every session, every channel (decided 2026-11-06):** interjection is a
  property of sessions broadly — webchat and all channel adapters included.
  The primitive lives at the promptchain/channel-adapter level; the cortex
  uses the same protocol by sending channel messages.

The deep claim of this document: **these are not two features.** A mid-chain
speech insertion and a mid-chain device event have identical delivery semantics.
One primitive, many sources. The cortex is Animus' evented limbic system and
voice is its first fully-streaming consumer.

## Non-goals

- Not a new transport. Events still originate from existing channels or the
  cortex; this document defines what happens *after* arrival.
- Not a scheduler replacement. Chain execution order is unchanged; only the
  visibility of new input to an in-flight chain is at stake.
- Not model-side attention control. We do not attempt to inject tokens into an
  in-flight generation — inference steps are atomic; insertion happens between
  steps.

## Contract sketch

### Event envelope

```
{
  source:    "channel:<id>" | "cortex:<device-id>" | "system",
  type:      "speech" | "device-event" | "system",
  payload:   <type-specific>,
  timestamp: <kernel-assigned>,
  priority:  "normal" | "critical"
}
```

### Delivery (decided 2026-11-06: promptchain/channel-adapter level)

- An interjection is a **new user message appended to the session's message
  thread**; the running prompt chain picks it up **when assembling its next
  turn step** — after the current tool-call round-trip resolves, before the
  next model invocation. The message thread is the queue; there is no
  separate delivery lane.
- This is a property of **sessions broadly**, not a voice/cortex feature:
  webchat and every channel adapter get the same treatment, and the cortex
  speaks the identical protocol by sending channel messages.
- Ordering is preserved **per source**; cross-source ordering is by arrival.
- **Framing: regular user messages** (decided). No dedicated role, no special
  marker — the chain simply sees the new messages that arrived while it
  worked; transcript order carries the timing.
- Interjections do **not** affect turn/timeout accounting for the receiving
  chain — that is the benefit: it arrives whenever it arrives, matching
  responsiveness to developing conditions (best possible when TTS responses
  are per-phrase too).
- A finished session is just a passive session: an arriving message is a
  fresh message like any other. No special wake semantics.

### Cancellation

- An interjection cancels **nothing** by itself. The model's next step decides:
  it may abort playback of queued speech, cancel a motion goal, or continue and
  fold the input in. Cancellation is a tool call (`audio.cancel()`,
  `cortex.actuate.cancel(goal)`) — model-mediated by design.
- **Exception — `priority: "critical"`:** synchronous, kernel-level, never
  model-mediated. This is the lane the actuator kill-switch rides (see
  [Cortex → Safety](cortex.md#safety)). A critical event preempts at the next
  safe kernel point, not the next inference step. Critical events are
  kernel-internal — never model-facing, never on the message thread.

### Flood control

Device events can fire at hardware rates (motion, sound, vibration). The queue
must coalesce/debounce per hook — this lives in the cortex hook predicates, not
here, but the kernel must defend itself anyway: cap interjection backlog per
session, drop-oldest with a loud marker when exceeded. Context is a budget.

## Audit: current kernel state (critical-path ticket #1)

Owner hypothesis (2026-11-06), to verify first: **this may already functionally
exist in Animus.** IRC and Discord adapters plausibly already interject —
messages arriving mid-chain land in the thread and join at the next turn-step
assembly — and webchat would too, **if the webchat UI didn't actively block
the send mechanism while a prompt chain is active.**

If the audit confirms this, the first implementation ticket shrinks to:

1. Un-block webchat send during active chains (UI and/or adapter).
2. Formalize what the adapters already do, so the cortex rides the identical
   protocol instead of a parallel mechanism.

Audit checklist:

- IRC/Discord adapter behavior for messages arriving during an active chain —
  queued to turn end, or visible at next step assembly?
- Webchat: confirm the send-block; UI-only (button state) or adapter-enforced?
- Where per-turn assembly happens — the point delivery formalizes onto.

## Decisions (owner red-line, 2026-11-06)

- **Placement:** the primitive lives at the promptchain/channel-adapter level;
  interjection applies to sessions broadly (webchat + all adapters), and the
  cortex uses the same protocol by sending channel messages.
- **Framing:** regular user messages, added to the prompt chain at per-turn
  assembly. No dedicated role.
- **Turn/timeout accounting:** unaffected by interjections — deliberate.
- **Completed sessions:** an arriving message is a fresh message hitting a
  passive session; no special wake semantics.

## Open questions (remaining)

- Per-source rate caps for channel adapters (cortex sources resolved to
  per-device-interface configuration — see cortex doc).
- Critical lane: confirm the kernel-internal kill-switch path never needs to
  be expressed as (or blocked by) message-thread insertion.

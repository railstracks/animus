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

### Delivery

- Events queue per-session and are delivered **between chain steps**: after a
  tool-call round-trip resolves, before the next model invocation.
- Ordering is preserved **per source**; cross-source ordering is by arrival.
- Delivered events are marked as interjections in the transcript so the model
  can see "this arrived while you were working." Framing matters: the model
  should treat an interjection as new information that may supersede the
  in-flight plan, not as a queue of unanswered turns.
- Interjections persist in history like any inbound message — context coherence
  requires the transcript to reflect what actually happened, including the
  timing.

### Cancellation

- An interjection cancels **nothing** by itself. The model's next step decides:
  it may abort playback of queued speech, cancel a motion goal, or continue and
  fold the input in. Cancellation is a tool call (`audio.cancel()`,
  `cortex.actuate.cancel(goal)`) — model-mediated by design.
- **Exception — `priority: "critical"`:** synchronous, kernel-level, never
  model-mediated. This is the lane the actuator kill-switch rides (see
  [Cortex → Safety](cortex.md#safety)). A critical event preempts at the next
  safe kernel point, not the next inference step.

### Flood control

Device events can fire at hardware rates (motion, sound, vibration). The queue
must coalesce/debounce per hook — this lives in the cortex hook predicates, not
here, but the kernel must defend itself anyway: cap interjection backlog per
session, drop-oldest with a loud marker when exceeded. Context is a budget.

## Audit: current kernel state (critical-path ticket #1)

What exists today for messages arriving during an active chain must be mapped
before any stub work:

- Where do channel messages currently land while a chain runs? Queued to turn
  end? Visible between steps?
- Is there an existing notion of step boundaries we can hook delivery onto?
- Does the transcript distinguish mid-chain arrivals today?

This audit produces the first implementation ticket. If mid-chain visibility
already exists in some form, this document shrinks to naming and hardening it.

## Open questions

- Should normal-priority interjections wake a *completed* session the way a new
  message does? (Presumably yes — that is just a message.)
- Do interjections affect turn/timeout accounting for the receiving chain?
- Visibility of interjection framing: one system prompt convention, or a
  dedicated message role?
- Rate limits: per-source caps beyond the cortex's own debounce?

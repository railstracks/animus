# Design: Cortex — Unifying Device & Connection Layer

**Status:** Draft for v0.5 · Direction agreed 2026-11-05 · Pending owner red-line
**Depends on:** [Interjection & Device Events](interjection-and-events.md) (hook delivery lane)
**Related:** [Voice](voice.md) (first streaming consumer) · #46 (participants card — "what is present")

---

## Intent

A unifying kernel component that facilitates connection with external devices
and streams, with configurable hooks, invokable through a tool surface for
device registration, hook configuration, queries, and device commands.

The identity worth holding onto: **the cortex is Animus' evented limbic
system.** Not a device bag — the component that turns outside-world signals
into agent-visible events and agent intent into outside-world action. Voice is
its first fully-streaming consumer and its best forcing function: voice
exercises streams, hooks, interjection, and emission all at once.

The destination this serves: Animus installed into a robotic frame, perhaps
powered by a self-contained inference unit — a proper body, not a chat window.
The 0.5 deliverable is a stub shaped by that destination, not the destination.

Protocol note (decided 2026-11-06): the cortex speaks into sessions through
the **same channel-message protocol** the interjection primitive defines — the
cortex is another channel producer, not a parallel event system.

## The four planes

Every device, whatever it is, meets the cortex through the same four planes:

| Plane | Direction | What it is |
|---|---|---|
| **Registration** | owner → cortex | lifecycle: declare device + capability record |
| **Hooks** | device → agent | predicate over an observable; fires as a channel message via the [interjection protocol](interjection-and-events.md) |
| **Commands** | agent → device | actuation: set LED, play frames, start motion |
| **Queries** | agent ← device | current state: coordinates, distance, acceleration |

**Registration is the trust boundary.** A registered actuator is a physical
write path into the world — registration belongs behind the owner approval
surface (same posture as package approval), not free agent self-service.

Capability records are **reconfigurable** (decided 2026-11-06): settings vary
with the environment, and the Animus instance actively curates its cortex
stack post-registration. The trust boundary governs what a registration
*enables* (actuation, events into sessions); within an approved registration,
curation adjusts how devices behave.

**Hot-plug (decided):** capabilities pull **dynamically at cortex
invocation** — the registry is a live view, not a registration-time snapshot.
A vanished device (USB unplugged, Talk call ended) fails loudly at the next
invocation against current reality; stale registry state never commands a
ghost device.

## Three interface archetypes — the answer to "what is and isn't variable"

The component list below spans eight device classes, but only three interface
archetypes. The invariant is *how a device is addressed* (the four planes, the
envelopes, the safety classes); the variable is *what it can do* (declared in
its capability record: observables, units, axes, rates, command set).

### 1. Poll sensors — query + threshold hooks

GPS, distance units, accelerometers. Simple request/response queries, plus
hooks declared as predicates over an observable: **crossing** (geofence enter/
exit), **level** (obstacle within X cm, vibration above g), **delta** (sudden
acceleration change). Predicates carry **debounce/hysteresis parameters** — a
geofence edge must not fire forty events a minute, and neither must a noisy
ultrasonic reading. One predicate vocabulary for all sensors; the device only
supplies observables and units.

### 2. Streams — buffered, backpressured, derived events

Audio input/output, camera. Continuous flows with bounded buffers; the cortex
derives **events** from the flow (an utterance ended, motion began) and exposes
services on top: STT (buffered transcript per [voice](voice.md)), image-model
description of camera stills. Derived events ride the same hook/interjection
lane as sensor thresholds — no bespoke path per stream.

### 3. Actuators — command + feedback, in three layers

Motors, pistons, LED arrays. "What is and isn't variable" bites hardest here:
*"move 50 meters forward"*, *"start and continue walking"*, and *"don't fall
over"* are different **kinds** of thing. Three layers:

| Layer | Example | Owner | Loop rate |
|---|---|---|---|
| **Goal** | "move to GPS X,Y" | cortex supervision; completion event via hook | agent time |
| **Trajectory** | "walk forward 50 m" | FANN or parameterized gait | ~10–50 Hz |
| **Reflex** | "stop at obstacle", "stay upright" | FANN closed loop | servo rate |

The LLM emits goals and trajectories as conditioned tool calls and is **never
in the 50 Hz loop**. Reflexes run below it, continuously, whether or not any
chain is awake.

### FANN policy

FANN is the right tool for trajectory/reflex controllers: tiny nets, C library,
clean fit for the kernel, offline-trainable against recorded sensor traces.
Policy from day one:

- A trained network is a **versioned artifact** — bundle + manifest, content
  hashed, like a package. A gait is inspectable and rollback-able.
- **Retraining is gated** (approval class). An agent must not silently retrain
  a gait mid-walk; swapping controllers is an owner-visible action.
- Verification as part of generation: assemblies prove themselves against
  recorded traces before deployment (the project's own standing rule).
- Prior art: the Midas/Steadyfort FANN work is the in-house reference for
  trace formats and acceptance thresholds (owner pointer, 2026-11-06).

## Component inventory (0.5 landscape, mapped)

| Component | Archetype | Notes |
|---|---|---|
| `camera` | stream | motion hooks; stills → image model (hook service) |
| `audio_input` | stream | sound-detection hooks; buffered STT transcript |
| `audio_output` | stream | frame playback; TTS of phrase + voice |
| `actuators` | actuator | motors (-1.0–1.0 signal), pistons (0/1), servos; FANN layers |
| `gps` | poll | coordinates query; arrival hooks, fuzzy margins (hysteresis) |
| `distance_unit` | poll | ultrasonic reading; proximity threshold hooks |
| `led_array` | actuator | read/set LED states — the trivial actuator, ideal first witness |
| `accelerometer` | poll | current reading; level/delta hooks |

All components are physically on hand for test assemblies. The first assemblies
exist chiefly to read the real component landscape and correct this mapping —
particularly what varies across devices of the same class.

## Safety

Actuators are the first Animus component that **physically breaks things**.
Day-one rules, not later hardening:

1. **Approval-gate class for actuator writes** — the #25/#106 owner-approval
   work is the precedent; goal/trajectory commands carry the risk payload
   ("drive forward 50 m") and can require confirmation, with reflex-scale
   micro-corrections exempt.
2. **Kill-switch** — synchronous, kernel-level, never model-mediated (the
   `priority: "critical"` lane in the interjection doc). Freezes all actuators
   regardless of chain state.
3. **Fail-stopped** — heartbeat watchdog: if the agent session dies or the
   cortex loses supervision, actuators stop. Default state of hardware is
   *stopped*, not *running*.

## Connection mechanisms (decided 2026-11-06: sidecar drivers)

- **Drivers live in sidecar processes** talking to the cortex over a local
  socket — chosen for modularity. Driver crashes are contained away from the
  kernel, drivers are language-free, and per-subsystem sidecars fit the ARM
  deployment story. The local-socket protocol is therefore a first-class
  interface to spec — and should be the **same channel-message protocol** the
  cortex speaks into sessions (one protocol end to end).
- Host OS devices via USB — enumerated through the host, capability-recorded at
  registration.
- GPIO/I2C outbound — feasibility to be evaluated on the components on hand
  (library choice, permissions). No commitment yet.

## Context-card integration

#46 taught the context card to answer *"who is present."* The cortex registry
extends it to *"what is present"* — registered devices surface in the card like
participants (mic, camera, LEDs, motors). One line of unification, cheap, and
it keeps the physical world visible in every thread's context.

## Design constraints carried from the endgame

The robotic-frame destination constrains the stub now:

- Cortex drivers must build and run on **ARM Linux** — no workstation-GPU or
  x86 assumptions in the driver layer.
- Local-model budget is small: STT/TTS local candidates must fit ARM reality
  (or stay cloud services initially); image-model description of stills is
  cloud-side initially.
- Self-contained inference unit = animusd + llama.cpp on the frame's compute;
  the cortex adds no mandatory cloud dependency.

## Stub scope (0.5)

The stub implements exactly two archetypes end to end:

1. **Stream archetype** — the virtual audio devices the voice loop registers
   (mic + speaker), with STT/TTS services. Proves: registration, hooks,
   derived events, interjection delivery, emission.
2. **One cheap real device** — LED array or ultrasonic distance unit — as the
   green witness for the poll/actuator path: register, hook a threshold,
   interject, command a state change, read it back.

Not in the stub: the other device classes, FANN training pipeline (design
only), GPIO/I2C, Nextcloud (sequenced via #162/#69 behind the webchat voice
loop).

## Non-goals (for 0.5)

- The full eight-device landscape — stub plus design, not implementation
- Realtime vision (motion + stills description is the 0.5 camera scope)
- RTOS-class hard real-time guarantees (soft real-time loops only)
- Autonomous retraining or self-modifying controllers

## Decisions (owner red-line, 2026-11-06)

- **Driver hosting: sidecars** — modularity; the local-socket protocol becomes
  first-class, unified with the channel-message protocol into sessions.
- **Capability records: reconfigurable**, environment-dependent; the instance
  actively curates its cortex stack.
- **Hot-plug: dynamic capability pull at cortex invocation** — registry is a
  live view.
- **Hook/rate budgets: configured per device interface** (many hooks ride the
  *current* reading).
- **Tool surface: one cortex tool with subcommands** (api-package style),
  leaning decided — validate with model-reliability tests before committing.

## Open questions (remaining)

- GPIO/I2C library and permissions — evaluate on the components on hand
- Capability record schema — now shaped by the reconfiguration requirement:
  what is fixed at registration vs. curatable afterward
- FANN training harness — experimental; mine the Midas/Steadyfort FANN work
  for trace formats and acceptance thresholds

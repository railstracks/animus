# Ticket 082 — Cortex: Autonomic Peripheral Layer

**Status:** Draft  
**Priority:** Normal  
**Created:** 2026-06-10  
**Component:** Core / Hardware Interface  

## Summary

Cortex is the autonomic nervous system for embodied agents. It runs on-device, handles low-level peripheral communication, stores conditional action routines, and runs Autonomic Neural Networks (ANNs) for reactive perception. An Animus agent may control one or more bodies, each running its own Cortex instance, exposed as capabilities through the Animus node system.

## Naming Convention

- **Cortex** — Core for autonomic functions. Runs on-device, not on the Animus host.
- **Routines** — Complex sets of conditional actions, stored and executed within the Cortex.
- **ANN (Autonomic Neural Network)** — FANN-based neural networks produced for and internalized by the Cortex. These are the reactive perception layer.

## Motivation

Agents that interact with the physical world need an abstraction boundary between "what I want to do" and "how the hardware does it." Directly controlling actuators frame-by-frame from the deliberative loop is too slow, too noisy, and wastes the agent's reasoning capacity on problems that don't require it.

Cortex provides:
- **Peripheral discovery** - what hardware is available right now
- **Action exposure** - what each peripheral can do, with parameters
- **Routine storage** - composed action sequences that can be invoked as units
- **Reactive processing** - FANN networks for perception at sub-deliberation speed

## Architecture

### Multi-Body Model

An Animus agent may control **one or more bodies**. Each body runs its own Cortex instance on local hardware, directly connected to its peripherals. The agent discovers available bodies and their capabilities through the Animus node system.

```
┌──────────────────────────────────────────┐
│           Animus Agent                   │
│  "go to the kitchen" (deliberative)      │
└────────┬──────────────┬──────────────────┘
         │              │
    ┌────▼────┐    ┌────▼────┐
    │ Body A  │    │ Body B  │
    │ (node)  │    │ (node)  │
    │ Cortex  │    │ Cortex  │
    └────┬────┘    └────┬────┘
         │              │
   ┌─────┴─────┐  ┌────┴─────┐
   │ Peripherals│  │Peripherals│
   └───────────┘  └──────────┘
```

Cortex capabilities (peripherals, routines, ANNs) are exposed as **node capabilities** when the device registers as an Animus node. The agent doesn't need to know the hardware details — it sees capabilities.

### Three-Layer Control Model

```
┌─────────────────────────┐
│   Deliberative Layer    │  Agent: "go to the kitchen"
│   (Animus agent)        │  Seconds per decision
├─────────────────────────┤
│   Routine Layer         │  Stored sequence: "navigate hallway"
│   (Cortex Routines)     │  Composed conditional actions
├─────────────────────────┤
│   Reactive Layer        │  ANN perception: "obstacle at 2m"
│   (Autonomic NNs)       │  Microseconds, no deliberation
└─────────────────────────┘
         │
    Physical Hardware
```

The agent doesn't see individual sensor frames. It sees the *output* of ANNs. It doesn't control individual actuators. It issues intents that get decomposed through routines into hardware commands.

## Interface

### Peripheral Discovery

```
cortex_peripherals() → Peripheral[]
```
- Lists all connected peripherals, their type, capabilities, and available actions
- Hot-plug: peripherals can appear/disappear at runtime
- Each peripheral declares: type, actions, parameter schemas, streaming vs. discrete

### Peripheral Actions

```
cortex_action(peripheral_id: string, action: string, params: map) → ActionResult
```
- Invoke a single action on a peripheral
- Synchronous or asynchronous depending on action type
- Actions are defined by the peripheral driver, not hardcoded

### Routine Management

```
cortex_routine_create(name: string, steps: RoutineStep[]) → routine_id
cortex_routine_invoke(routine_id: string, params: map) → RoutineResult
cortex_routine_update(routine_id: string, steps: RoutineStep[]) → routine_id
cortex_routine_list() → Routine[]
cortex_routine_delete(routine_id: string) → void
```

A RoutineStep is either:
- A peripheral action with parameters
- A conditional branch (read sensor → decide path)
- A loop (repeat until condition)
- A sub-routine invocation

Routines can reference FANN networks for perception-driven branching.

### ANN Management (Autonomic Neural Networks)

```
cortex_ann_train(network_id: string|null, data: TrainingData, config: ANNConfig) → TrainResult
cortex_ann_invoke(network_id: string, inputs: float[]) → ANNResult
cortex_ann_attach(peripheral_id: string, network_id: string, config: PerceptionConfig) → perception_id
cortex_ann_read(perception_id: string) → PerceptionOutput
cortex_ann_detach(perception_id: string) → void
```

- **ANNs are Cortex-local** — they run on-device for low-latency reactive processing
- `train` refines an existing ANN or creates new (same continuous refinement model as Ticket 081)
- `attach` binds an ANN to a peripheral's data stream — network processes raw input at hardware speed
- Agent reads the *interpreted* output ("path clear", "obstacle left"), not raw data
- ANNs can also be used standalone (invoke directly) for non-streaming classification

## Design Decisions

### Peripheral Drivers Are Plugins
Each peripheral type (camera, motor controller, actuator, sensor array) has a driver that translates between Cortex's generic interface and hardware-specific protocols. Cortex doesn't know about cameras - it knows about peripherals that provide frames.

### Safety Boundaries
Hard limits enforced at the Cortex level, independent of agent control:
- Maximum velocity, force, range — configurable per peripheral
- Kill conditions that don't route through the deliberative loop
- Hardware watchdog — if no heartbeat from Cortex for N seconds, peripheral enters safe state
- **Cortex is autonomous** — if connection to Animus drops, Cortex continues executing the current routine until completion or safety boundary hit

### Routines Can Reference ANNs
A routine step can invoke an ANN for conditional branching. This enables closed-loop behaviors ("walk forward until obstacle detected, then stop") that run entirely below the agent's deliberation, freeing the agent for higher-level decisions.

### Asynchronous Event Model
Long-running routines and peripheral streams produce events, not blocking calls. The agent issues an intent, continues with other work, and receives completion or status events. Synchronous calls are available for simple discrete actions.

### Node Capability Exposure
When a device with a Cortex registers as an Animus node, it exposes:
- Available peripherals and their action schemas
- Stored routines (by name, with parameter schemas)
- Available ANNs and their input/output shapes
- Device status (connected, running routine, idle, error)

The agent sees a unified view across all its bodies. "Which of my bodies can navigate?" is a capability query, not a hardware inventory question.

## Open Questions

1. **Transport protocol** — How do peripherals physically connect? Serial, I2C, SPI, USB, network? Driver-level abstraction hides this from Cortex, but the host hardware constraints matter.
2. **Routine complexity ceiling** — Should routines be Turing-complete or restricted to bounded sequences? Full conditionals + loops could produce infinite routines. May need step limits or timeouts.
3. **Multi-agent peripheral sharing** — Can two agents access the same body/Cortex? Needs mutex/locking. Probably out of scope for v1.
4. **Peripheral simulator** — Development/testing without physical hardware. A mock peripheral driver that produces synthetic sensor data would accelerate routine development.
5. **Node protocol extension** — What does the capability schema look like in the Animus node registration? Needs coordination with the existing node system.
6. **ANN training location** — Training data may come from the agent (deliberative) but the ANN runs on-device. Train on-device, or train on Animus host and push weights to Cortex?

## Downstream Integration

- **FANN Intuition Module (Ticket 081)** — ANNs used in Cortex follow the same continuous-refinement model. Agent-level intuitions (Ticket 081) vs. autonomic perceptions (this ticket) share architecture but differ in scope and execution context.
- **Agent self-calibration** — Agent learns the relationship between intents and physical outcomes through repeated execution and corrective training data.
- **Multi-modal perception** — Multiple sensor streams feeding separate ANNs, fused into a unified environmental model.
- **Animus node system** — Cortex instances register as Animus nodes. Capability exposure enables multi-body agent control through the existing (or extended) node protocol.

## Scope

**v1 (MVP):**
- Peripheral driver interface + plugin system
- Action invocation (sync + async)
- Routine storage and execution
- FANN perception binding (at least one reference implementation)
- Safety boundaries (hard limits + watchdog)

**Out of scope for v1:**
- Multi-agent peripheral sharing
- Peripheral simulator
- Complex multi-modal sensor fusion
- Visual programming / routine editor UI

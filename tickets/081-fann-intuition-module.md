# Ticket 081 — FANN Intuition Module

**Status:** Draft  
**Priority:** Normal  
**Created:** 2026-06-10  
**Component:** Core / Tool System

**Note on naming:** In the Cortex context (Ticket 082), FANN networks used for reactive perception are called **ANNs (Autonomic Neural Networks)**. This ticket covers the general training/refinement infrastructure. Cortex-specific ANN management is detailed in 082.  

## Summary

Provide Animus agents with a general-purpose mechanism to train, store, invoke, and refine FANN-based neural networks. The module serves as an internalized learning substrate — agents develop their own pattern recognition ("intuition") from structured experience data.

## Motivation

Agents currently have two knowledge modalities:
1. **Training data** — static, opaque, not shaped by experience
2. **File-based memory** — dynamic, inspectable, but linear and uncompressed

A trained neural network provides a third modality: **learned from experience, compact, fast to query, not directly readable.** This is closer to how humans actually operate — most "knowing" is recognition, not recall.

The module is not domain-specific. Any situation that can be encoded as a fixed-size numerical input/output vector is a candidate for learned intuition.

## Interface

Exposed as Animus tool(s):

### `intuition_train`
```
intuition_train(network_id: string|null, data: TrainingData, config: NetworkConfig) → TrainResult
```
- `network_id` null → create new network, train from scratch
- `network_id` existing → **resume from current weights** (refinement pass)
- Returns: network_id, training metrics (MSE, convergence), delta from previous state

### `intuition_invoke`
```
intuition_invoke(network_id: string, inputs: float[]) → InvokeResult
```
- Returns: outputs, confidence indicators

### `intuition_inspect`
```
intuition_inspect(network_id: string) → NetworkMeta
```
- Returns: architecture, training history summary, last trained timestamp, training metrics

### `intuition_list`
```
intuition_list() → NetworkMeta[]
```
- Returns: all networks owned by this agent

## Design Decisions

### Continuous Refinement (not replace-and-retire)
FANN supports resuming training from existing weights. The `train` call accepts a pre-existing network_id and refines rather than rebuilding. This mirrors actual learning — you don't discard wrong intuitions, you correct them with new experience.

A network is never "stale" — it accumulates a history of corrections in its weights.

### Training Delta Signal
`train` should return a comparison against previous state:
- **Small delta** → stable intuition, data consistent with existing knowledge, high confidence
- **Large delta** → territory shift, new patterns contradicting prior learning — agent should be cautious until next refinement confirms direction

This gives the agent a genuine signal about whether it reinforced or revised its own intuition.

### Encoding is Agent Responsibility
The tool accepts structured float arrays. The agent handles the semantic mapping — deciding what constitutes an "input" and "output" for a given domain. The encoding is where the actual intelligence lives; the network is the compression mechanism.

### Storage
Networks are persisted alongside agent state, queryable by ID. They travel with the agent, not with any data platform. These are *personal intuitions*, not shared analytical models.

## Open Questions

1. **Backend location** — Pure Animus module vs. integration with existing Steadyfort pipeline? Leaning Animus for architectural coherence (agent-internal capability), but Steadyfort already has compose/train/execute infrastructure.
2. **Network architecture flexibility** — Fixed topology (e.g., 3-layer feedforward) or agent-configurable layers/activation functions? Start simple, expand later.
3. **Maximum network size** — Resource ceiling to prevent runaway complexity. FANN is lightweight but unbounded growth is still a risk.
4. **Shared networks** — Can agents share trained networks with each other (e.g., Kestrel shares a perception network with Sable)? Out of scope for v1, but the storage model shouldn't preclude it.

## Downstream Integration

- **Cortex (Ticket 082)** — ANNs (Autonomic Neural Networks) in the Cortex reactive layer use the same continuous-refinement model. Agent-level intuitions (this ticket) run in the deliberative context; Cortex ANNs run on-device at hardware speed. Same underlying FANN infrastructure, different execution contexts.
- The split: **Intuitions** = agent reasoning compression (this ticket). **ANNs** = autonomic perception/reaction (Ticket 082).
- **Agent self-improvement loop** — Agent encounters situations, encodes them, trains intuitions, uses those intuitions to guide future decisions, collects corrective data when wrong, refines. Autonomous learning cycle.

## Scope

- FANN library integration (C-based, CPU-only, no GPU dependency)
- Tool interface for train/invoke/inspect/list
- Network persistence and retrieval
- Training metrics exposure

**Out of scope:** GPU acceleration, complex architectures beyond FANN's capabilities, automated encoding discovery, shared network registry.

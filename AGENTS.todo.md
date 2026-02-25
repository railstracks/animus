# AGENTS.todo.md — Blueprint-to-Implementation Plan (Draft)

> Status: **planning / rough order**.
>
> Goal: iteratively realize the architecture described in `AGENTS.system_draft.md` while keeping the codebase shippable and testable at each step.

---

## Milestone 0 — Align on naming + invariants

- [ ] Confirm core runtime nouns:
  - [ ] `AgentKernel`
  - [ ] `Agent`
  - [ ] `IncomingEvent`
  - [ ] `ChainInstance`
  - [ ] `ChainRunner`
- [ ] Decide whether **Session** exists as a long-lived conversation object (distinct from ChainInstance).
- [ ] Define global budgets:
  - [ ] max chain steps
  - [ ] max tool calls per chain
  - [ ] timeouts (LLM / tool / terminal)
  - [ ] token budgets per prompt

---

## Milestone 1 — Kernel skeleton + module lifecycle

- [ ] Implement `AgentKernel` startup/shutdown:
  - [ ] owns `jobs::JobSystem`
  - [ ] holds registries (tools, llms, connectors, agents)
  - [ ] module init/shutdown (`IModule`)
- [ ] Add basic logging + tracing scaffolding (chain_id, agent_id, connector, timestamps).

---

## Milestone 2 — Agent model + policy gates (minimum viable)

- [ ] Implement `Agent`:
  - [ ] identity + settings (provider selection, streaming policy)
  - [ ] capability policy (allowed tools/connectors/nodes)
  - [ ] memory config (enabled modules, budgets)
- [ ] Implement `AgentRegistry`.
- [ ] Add a minimal `GateKeeper` / policy validation hook (even if permissive initially).

---

## Milestone 3 — Connector abstraction + event dispatch

- [ ] Split connector responsibilities:
  - [ ] `IEventSource` (produces `IncomingEvent`)
  - [ ] `IChannelSink` (sends `OutgoingMessage`)
- [ ] Implement `ConnectorRegistry`.
- [ ] Implement event dispatch:
  - [ ] map IncomingEvent → target Agent
  - [ ] start a ChainInstance job for each event

---

## Milestone 4 — ChainRunner + ChainInstance (core loop, stubbed execution)

- [ ] Define `ChainInstance` state:
  - [ ] ids: chain_id, agent_id, conversation/session id, connector id
  - [ ] budgets + cancellation token
  - [ ] step counter
  - [ ] accumulated tool results
- [ ] Implement `ChainRunner` loop:
  - [ ] assemble prompt
  - [ ] call LLM
  - [ ] parse ActionPlan
  - [ ] execute actions
  - [ ] loop or finalize
- [ ] Add per-agent concurrency limits (queue/deny/throttle policy).

---

## Milestone 5 — Prompt pipeline (deterministic + testable)

- [ ] Implement:
  - [ ] `PromptPolicy` / `PromptTemplate`
  - [ ] `PromptAssembler`
  - [ ] `PromptInstance` (text + metadata)
- [ ] Add connector-provided abbreviated context insertion.

---

## Milestone 6 — LLM registry + first provider

- [ ] Define `ILLMClient` interface.
- [ ] Implement `LLMRegistry`.
- [ ] Implement one provider end-to-end (choose whichever is simplest for the initial wiring).
- [ ] Decide streaming semantics:
  - [ ] safe default: stream only channel_response, buffer actions

---

## Milestone 7 — XML schema + parsing + validation

- [ ] Specify the XML response schema:
  - [ ] `<tool_call>` (tool id, args, optional resource_key)
  - [ ] `<terminal_command>`
  - [ ] `<channel_response channel=…>`
- [ ] Implement `ActionPlanParser` (robust errors).
- [ ] Implement `ActionPlanValidator` (policy + sanity checks).

---

## Milestone 8 — Tool system + parallel execution batches

- [ ] Define `ITool` interface + `ToolRegistry`.
- [ ] Implement `ToolExecutor`:
  - [ ] execute tool calls
  - [ ] allow batch parallelism when safe
  - [ ] introduce `resource_key` / `mutex_key` conflict serialization
- [ ] Implement a minimal `TerminalExecutor` with strict allowlisting.

---

## Milestone 9 — Memory manager + first modules

- [ ] Implement `Memory` manager:
  - [ ] module registry and per-agent configuration
  - [ ] retrieval API returns both content + provenance metadata
- [ ] Implement first module(s):
  - [ ] Ontology (or stub adapter)
  - [ ] VectorSearch (or stub)
  - [ ] Episodic log (chain traces)

---

## Milestone 10 — Scheduler events

- [ ] Implement `Scheduler` as an `IEventSource`.
- [ ] Support “run job X every N” → emits IncomingEvent to an agent.

---

## Milestone 11 — AgentNetwork (stub → secure remote execution)

- [ ] Create `AgentNetwork` as a separate subsystem (even if initially disabled).
- [ ] Implement a remote ToolBackend (no job sharing yet).
- [ ] Add authentication + auditing requirements before enabling.

---

## Milestone 12 — Hardening

- [ ] Cancellation semantics end-to-end (queued + in-flight best-effort).
- [ ] Timeouts + retry policy.
- [ ] Tracing/metrics per chain.
- [ ] Tests:
  - [ ] XML parser
  - [ ] tool batch parallelization + conflict keys
  - [ ] chain loop determinism
  - [ ] per-agent concurrency limits

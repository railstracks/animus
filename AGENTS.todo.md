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
- [x] Maintain a separate **Session** class (long-lived conversation object). A `ChainInstance` is one activation within a Session.
- [ ] Define Session keying strategy (user/channel/thread ids) and `ISessionStore` interface.
- [x] Set initial default budgets (refine later as per-provider/per-tool caps):
  - [x] `maxChainSteps = 20`
  - [x] `maxToolCallsPerChain = 10`
  - [x] `timeoutSeconds = 1800` (LLM / tool / terminal)
  - [x] `tokenBudgetPerPrompt = 200_000`
- [ ] Define conversation history **compaction** policy (when/where/how) and a potential LLM-driven `ConversationCompactor` sub-prompt.

---

## Milestone 1 — Module SDK/ABI + module loader (stability-first)

- [ ] Create `animus-sdk` (module API) target:
  - [ ] define `ANIMUS_MODULE_API_VERSION`
  - [ ] define module descriptor (`animus_module_descriptor_t`)
  - [ ] define C ABI factory exports (get_descriptor/create/destroy)
  - [ ] define the kernel-host surface (`animus_host_vtable_t`) (logging/config/registry registration)
  - [ ] document ABI rules (no STL/exceptions across boundary; explicit ownership)
- [ ] Implement kernel module loader:
  - [ ] load shared libraries from configured search paths
  - [ ] verify API version compatibility
  - [ ] obtain descriptor + instantiate module
  - [ ] register module-provided components into registries
  - [ ] audit log which modules were loaded (id/version/hash)
- [ ] Add minimal module allowlisting config (even if permissive initially)

---

## Milestone 2 — Kernel skeleton + module lifecycle

- [ ] Implement `AgentKernel` startup/shutdown:
  - [ ] owns `jobs::JobSystem`
  - [ ] holds registries (tools, llms, connectors, agents)
  - [ ] module init/shutdown (`IModule`)
- [ ] Implement Session primitives:
  - [ ] `Session` (long-lived, groups multiple ChainInstances)
  - [ ] `SessionManager` (resolve/create/load)
  - [ ] `ISessionStore` interface
  - [ ] `InMemorySessionStore` default implementation
  - [ ] (future) `RedisSessionStore` implementation for cross-node sync
- [ ] Add basic logging + tracing scaffolding (session_id, chain_id, agent_id, connector, timestamps).

---

## Milestone 3 — Agent model + policy gates (minimum viable)

- [ ] Implement `Agent`:
  - [ ] identity + settings (provider selection, streaming policy)
  - [ ] capability policy (allowed tools/connectors/nodes)
  - [ ] memory config (enabled modules, budgets)
- [ ] Implement `AgentRegistry`.
- [ ] Add a minimal `GateKeeper` / policy validation hook (even if permissive initially).

---

## Milestone 4 — Connector abstraction + event dispatch

- [ ] Split connector responsibilities:
  - [ ] `IEventSource` (produces `IncomingEvent`)
  - [ ] `IChannelSink` (sends `OutgoingMessage`)
- [ ] Implement `ConnectorRegistry`.
- [ ] Implement event dispatch:
  - [ ] map IncomingEvent → target Agent
  - [ ] start a ChainInstance job for each event

---

## Milestone 5 — ChainRunner + ChainInstance (core loop, stubbed execution)

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

## Milestone 6 — Prompt pipeline (deterministic + testable)

- [ ] Implement:
  - [ ] `PromptPolicy` / `PromptTemplate`
  - [ ] `PromptAssembler`
  - [ ] `PromptInstance` (text + metadata)
- [ ] Add connector-provided abbreviated context insertion.
- [ ] Add conversation history compaction:
  - [ ] `ConversationCompactor` (optional LLM-driven sub-prompt)
  - [ ] store compaction outputs in `Session` (e.g. rolling summary) with provenance
  - [ ] policy: when to compact (token pressure / time / turn count)

---

## Milestone 7 — LLM registry + first provider

- [ ] Define `ILLMClient` interface.
- [ ] Implement `LLMRegistry`.
- [ ] Implement one provider end-to-end (choose whichever is simplest for the initial wiring).
- [ ] Decide streaming semantics:
  - [ ] safe default: stream only channel_response, buffer actions

---

## Milestone 8 — XML schema + parsing + validation

- [ ] Specify the XML response schema:
  - [ ] `<tool_call>` (tool id, args, optional resource_key)
  - [ ] `<terminal_command>`
  - [ ] `<channel_response channel=…>`
- [ ] Implement `ActionPlanParser` (robust errors).
- [ ] Implement `ActionPlanValidator` (policy + sanity checks).

---

## Milestone 9 — Tool system + parallel execution batches

- [ ] Define `ITool` interface + `ToolRegistry`.
- [ ] Implement `ToolExecutor`:
  - [ ] execute tool calls
  - [ ] allow batch parallelism when safe
  - [ ] introduce `resource_key` / `mutex_key` conflict serialization
- [ ] Implement a minimal `TerminalExecutor` with strict allowlisting.

---

## Milestone 10 — Memory manager + first modules

- [ ] Implement `Memory` manager:
  - [ ] module registry and per-agent configuration
  - [ ] retrieval API returns both content + provenance metadata
- [ ] Implement first module(s):
  - [ ] Ontology (or stub adapter)
  - [ ] VectorSearch (or stub)
  - [ ] Episodic log (chain traces)

---

## Milestone 11 — Scheduler events

- [ ] Implement `Scheduler` as an `IEventSource`.
- [ ] Support “run job X every N” → emits IncomingEvent to an agent.

---

## Milestone 12 — AgentNetwork (stub → secure remote execution)

- [ ] Create `AgentNetwork` as a separate subsystem (even if initially disabled).
- [ ] Implement a remote ToolBackend (no job sharing yet).
- [ ] Add authentication + auditing requirements before enabling.

---

## Milestone 13 — Hardening

- [ ] Cancellation semantics end-to-end (queued + in-flight best-effort).
- [ ] Timeouts + retry policy.
- [ ] Tracing/metrics per chain.
- [ ] Tests:
  - [ ] XML parser
  - [ ] tool batch parallelization + conflict keys
  - [ ] chain loop determinism
  - [ ] per-agent concurrency limits

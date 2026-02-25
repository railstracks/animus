# AGENTS.system_draft.md — Animus System Blueprint (Draft)

> Status: **Aspirational / design draft**.
>
> This document describes the intended *target architecture* ("the blueprint").
> It is deliberately kept separate from:
>
> - `AGENTS.api.md` (agent-facing reference of what’s in public headers)
> - implementation reality in `src/` / `include/`
>
> When something here conflicts with existing code/docs, treat this file as the *proposal*.

---

## 1) Glossary

- **AgentKernel**: always-on core runtime. Owns core services (job system, registries, scheduler) and wires modules.
- **Agent**: configuration + policy + capabilities for a persona/worker (LLM settings, tool permissions, memory config, workspace).
- **IncomingEvent**: a normalized stimulus that can activate an agent (Slack message, webhook, Redis/Rabbit event, scheduled tick, CLI request, …).
- **ChainInstance**: one complete activation run from event → response (with possible tool/terminal loops).
- **ChainRunner**: executes a ChainInstance’s step loop.
- **PromptInstance**: the fully assembled prompt string plus metadata (sources, token estimates, budgets).
- **ActionPlan**: parsed structured LLM output (XML) describing tool calls, terminal commands, and channel responses.

---

## 2) Top-level runtime model

Animus runs a **passive base** which:

1. receives events from multiple sources (webhooks, Redis, RabbitMQ, UI connectors, scheduler)
2. dispatches each event to an agent
3. runs a **ChainInstance** for that activation
4. produces outward side effects:
   - tool calls
   - terminal commands
   - channel messages
5. (optionally) writes memory and emits tracing/metrics

Key design choice: model the activation explicitly as a **ChainInstance** so that cancellation, timeouts, tracing, and concurrency are straightforward.

---

## 3) ChainInstance: canonical flow

A ChainInstance is the “cognition + action” loop for one IncomingEvent.

### 3.1 Step loop

For each step (bounded by max steps / budgets):

1. **Context assembly**
   - pull memory context (ontology, vector search, episodic)
   - pull connector-provided abbreviated context insertion (e.g. Slack thread excerpt)
   - include tool/channel availability summary (from registries)

2. **Prompt assembly**
   - `PromptAssembler` produces a `PromptInstance` using:
     - Agent settings/policy
     - event payload
     - memory results
     - connector context

3. **LLM invocation**
   - choose provider/connector based on Agent settings (Mistral/Z.ai/Cohere/…)
   - streaming allowed per agent + per provider

4. **Parse structured response (XML)**
   - parse into `ActionPlan`:
     - `<tool_call>` elements
     - `<terminal_command>` elements
     - `<channel_response>` elements (with routing properties)

5. **Execute actions**
   - run tool calls / terminal commands via `ToolExecutor` / `TerminalExecutor`
   - collect results

6. **Loop**
   - if any tool/terminal actions occurred: feed results back into the next prompt step
   - else: finish and emit final channel responses

### 3.2 Streaming policy (safe default)

Streaming structured XML is possible but operationally tricky.

**Recommended safe baseline:**
- stream only *human-visible* content (e.g. `<channel_response>` text)
- buffer tool/terminal directives until the response is complete and validated

---

## 4) Concurrency model (jobs)

We apply multithreading at two layers.

### 4.1 Chain-per-job (primary)

- each ChainInstance runs as a job (or a small job-tree rooted at a “chain job”)
- multiple chains can run concurrently
- add **per-agent concurrency limits** to avoid one noisy agent starving others

This should be the first concurrency win.

### 4.2 Parallel tool-call batches (secondary)

Within a single ChainInstance step:

- tool calls that do not depend on each other can be run in parallel
- we barrier-wait for all tool calls in the batch before proceeding

**Important safety gate:** not all tool calls are safely parallel.

Add an optional per-action field like:
- `resource_key` / `mutex_key` (e.g. `repo:animus`, `node:midas-srv`, `file:/path/...`)

The executor may parallelize only actions with non-conflicting keys.

### 4.3 Avoid worker pool starvation

Many tools/terminal actions are I/O-bound and may block.

Options:
- dedicate an “I/O lane” (or separate pool) where blocking is acceptable
- or implement async/futures and let chains await without pinning worker threads

---

## 5) Core components (proposed)

### 5.1 AgentKernel (composition-first)

Prefer **composition + registries** over inheritance.

AgentKernel owns “always-on” services:
- `jobs::JobSystem`
- `Scheduler`
- `ConnectorRegistry` (Slack/Rabbit/Redis/Webhooks/CLI)
- `LLMRegistry`
- `ToolRegistry`
- `AgentRegistry`
- `MemoryModuleRegistry` (or a Memory factory)
- `Tracing/Audit` + logging

And runs a module lifecycle:
- `IModule::Init(AgentKernel&)`
- `IModule::Shutdown()`

### 5.2 Agent

The Agent object should be mostly:
- identity + settings (LLM provider preferences, streaming policy, budgets)
- capability policy (allowed tools/connectors/nodes)
- memory configuration (which modules, limits)
- workspace handle (paths / scratch / repo access)

At runtime, a ChainInstance can create an `AgentRuntime` / `AgentContext`:
- references to Agent
- narrowed views of tools, memory, and channels subject to policy

### 5.3 Connectors: split event sources from sinks

Connectors usually do two distinct jobs:

- `IEventSource` → produces `IncomingEvent`
- `IChannelSink` → sends `OutgoingMessage`

A Slack connector may implement both, but separating the interfaces keeps the architecture clean.

### 5.4 Prompt pipeline

Avoid a monolithic Prompt class. Prefer:

- `PromptPolicy` / `PromptTemplate` (section ordering, budgets)
- `PromptAssembler` (builds a prompt from inputs)
- `PromptInstance` (final text + metadata)

### 5.5 LLM interface

- `ILLMClient` (unified submit API)
- provider implementations: `MistralClient`, `ZaiClient`, `CohereClient`, …

### 5.6 Action execution

- `ActionPlanParser` (XML → typed plan)
- `ToolExecutor` (parallel batches + resource locks)
- `TerminalExecutor`
- `ChannelRouter` (routes `<channel_response>` to the correct sink)

Optional but likely needed:
- `GateKeeper` / policy enforcement layer to validate actions before execution

---

## 6) Memory subsystem (proposed)

A `Memory` manager coordinates modules:

- `EpisodicMemory` (conversation / run logs)
- `Ontology` (typed graph)
- `VectorSearch` (semantic retrieval)
- (future) `Intuition`, `Interiority`

Key needs:
- per-agent configuration + budgets
- deterministic “what got included” metadata for tracing

---

## 7) Cross-system awareness (registries)

To let the system—and optionally the LLM—know what is available:

- modules register themselves with the kernel on init
- the ChainInstance can include a *budgeted* tool/channel summary in the prompt

Registries should support:
- discovery (enumerate)
- capability filtering (per agent policy)
- stable identifiers (so the LLM can reference tools/channels reliably)

---

## 8) Modules / plugins (DLLs) — stability-first

Animus is intended to be split into multiple C++ projects, with optional capabilities added as **modules** (shared libraries).

Key point: the module boundary is an **ABI boundary**.

Design stance (agreed): prioritize **stability** over short-term implementation convenience.

- Prefer **C ABI factories** for module discovery + lifetime.
- Avoid passing STL types / exceptions across the boundary.
- Enforce explicit ownership rules + version negotiation.

Detailed design and proposed repo layout live in:
- `AGENTS.modules_draft.md`

---

## 9) AgentNetwork (future, treat as a security boundary)

Goals:
- synchronize memory
- exchange agents
- allow cross-network interface invocation (remote tools / remote terminal)
- (future) job sharing across a cluster

From day 1, treat remote execution as a hard security boundary:
- explicit per-agent capability grants (allowed nodes/tools)
- auditing (chain_id, requester, command/tool, node)
- authn/authz between nodes

A clean model: `ToolExecutor` routes to a `ToolBackend`:
- local backend
- remote backend (AgentNetwork)

---

## 10) Relationship sketch (high-level)

- `AgentKernel`
  - `JobSystem`
  - `Scheduler`
  - `AgentRegistry` → `Agent`
  - `ConnectorRegistry` → `IEventSource` / `IChannelSink`
  - `ToolRegistry` → `ITool`
  - `LLMRegistry` → `ILLMClient`
  - `Memory` (+ modules)
  - `ChainRunner`

- `ChainRunner`
  - creates/runs `ChainInstance` per `IncomingEvent`

---

## 11) Open questions (to resolve early)

- Do we keep a separate concept of **Session** (long-lived conversation state) distinct from ChainInstance (single activation)?
- Exact XML schema and validation rules.
- Streaming semantics (what can be emitted before validation completes).
- Tool parallelization policy and conflict detection (`resource_key`).
- Cancellation semantics (queued vs running work; tool/terminal cancellation support).

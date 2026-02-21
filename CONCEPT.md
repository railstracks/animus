# Animus — Concept

## Index

- [Executive summary](#executive-summary)
- [Concept](#concept)
- [Components](#components)
  - [Core](#core)
  - [Interfaces (external)](#interfaces-external)
  - [Connectors / secondary system integrations](#connectors--secondary-system-integrations)
  - [Memory modules](#memory-modules)
  - [FANN “intuition” integration](#fann-intuition-integration)
- [Technical Draft](#technical-draft)
  - [Process layout (suggested)](#process-layout-suggested)
  - [Languages](#languages)
  - [Hook protocol](#hook-protocol)
  - [LLM provider adapters](#llm-provider-adapters)
  - [Prompt taxonomy / model assignment](#prompt-taxonomy--model-assignment)
  - [Infrastructure / packaging](#infrastructure--packaging)
- [Strategic Positioning and the AI Race](#strategic-positioning-and-the-ai-race)
  - [Rationale](#rationale)
  - [How design intent supports positioning](#how-design-intent-supports-positioning)

## Executive summary

Animus is a planned **agentic framework** intended as a full replacement for OpenClaw.

In plain terms: it’s a system that lets AI not only “chat”, but also **do work across real tools** (Slack/Web/CLI, later: email/tickets/CRMs), while remaining **controllable, auditable, and safe**.

Design intent:

- **C++ kernel** that owns the cognition loop, prompt assembly, memory policy, and risk gates.
- **Modular connectors** so Slack/Web/CLI (and later enterprise systems) plug into one abstraction layer.
- **Multi-provider LLM support** + **private/self-hosted inference** as first-class options.
- Optional **FANN “instinct” modules** for numerical subproblems (advisory signals, never authority).

---

## Concept

**Animus** is a planned **agentic framework** intended as a full replacement for OpenClaw.

The core idea is to own the *entire cognition pipeline* end-to-end:

- **Prompt assembly** as an explicit, policy-driven compilation step (not an ad-hoc string concatenation).
- **Memory** as a first-class system with strict read/write rules (to reduce context pollution).
- **Tooling / integrations** as modular components connected via a stable abstraction layer.
- Optional, trainable **“instinct” modules** (FANN) for *numerical* subproblems (advisory signals, never authority).

Animus should be:

- **Efficient** (tight budgets, low overhead, predictable latency)
- **Capable** (composable cognition loop; strong integration story)
- **Secure** (auditable execution; strict schemas; minimal ambient authority)
- **Context-hygienic** (avoid irrelevant retrieval; reduce prompt pollution)

## Components

### Core

**C++ kernel / agent core**

Responsibilities:

- Own the **cognition loop** (perceive → retrieve → propose → gate → act → reflect → writeback).
- Own **prompt assembly**:
  - prompt categorization/taxonomy
  - context budgeting
  - deterministic “gates” (policy checks)
  - model selection per category
- Own **execution gating** for risky actions (ask-first, safety budgets, redaction rules).
- Own **logging / event trail** (auditable, append-only).

### Interfaces (external)

All interfaces terminate in a **common abstraction layer** (kernel conversation/task API), so that:

- memory policy is centralized
- prompt assembly is consistent
- gating rules apply uniformly

Initial targets:

- **Slack** — primary serious-work interface.
- **Web interface** — admin console + interactive UI.
- **CLI** — terminal-style interface for power users and scripting.

### Connectors / secondary system integrations

Connectors are *capability providers* (inputs, outputs, and tools) that are plugged into the kernel.

Examples (initial + likely follow-ons):

- Slack connector (messages, threads, reactions, file attachments)
- Web UI backend connector (sessions, streaming, inspection)
- CLI connector (stdin/stdout + local filesystem operations)
- Later: email, calendar, GitHub/GitLab, ticketing systems, knowledge bases, on-prem corp systems

Design intent: connectors should be modular, least-privilege, and swappable.

### Memory modules

Memory should not be one blob. It should have lanes with explicit read/write rules.

Proposed lanes (directional):

- **Event log (append-only)**: what happened (messages, tool calls, outcomes); auditable ground truth.
- **Working set**: short-term “active context” for current projects.
- **Curated semantic memory**: facts/decisions/relationships intended to remain true.
- **Optional retrieval accelerator**: embedding/vector index for fuzzy recall (never treated as authority).

Key properties:

- Promotion/demotion policies (“what becomes durable?”)
- Traceability (“why was this retrieved?”)
- Hard budgets (bytes/tokens/snippets) to reduce pollution

### FANN “intuition” integration

FANN integration is intended for **numerical** subproblems only.

Principles:

- Instinct outputs are **advisory** signals, not final decisions.
- Hard gates (security/privacy/irreversibility) must remain deterministic.
- Instinct modules require:
  - versioning + rollbacks
  - evaluation harness
  - feedback signals / labeling strategy

Good candidate tasks:

- urgency scoring / notification triage
- memory routing (which lane to write to)
- action ranking (likely-to-succeed, cost-aware)
- risk estimation for changes (e.g., “cognitive debt” risk score)

## Technical Draft

This is a “first-shape” rundown; we’ll refine as we prototype.

### Process layout (suggested)

- **animusd** (C++): kernel daemon
  - Hosts the cognition loop, memory policy, and tool/hook execution.
  - Exposes a local API (e.g., Unix socket HTTP, gRPC, or a simple JSON-RPC transport).

- **Connectors** (separate processes/services):
  - Slack connector
  - Web UI backend
  - CLI client

The key is that connectors are replaceable and talk to the kernel via a stable protocol.

### Languages

- **Core kernel**: C++ (primary)
- **Hooks / subsystems**: Node/Python/Ruby (invoked as terminal commands)
- **Web UI** (likely): TypeScript (frontend), backend can be thin and delegate to kernel API

### Hook protocol

To avoid “shell glue chaos”, hooks should use **structured JSON**:

- Kernel sends JSON request on stdin
- Hook prints JSON response on stdout
- Kernel enforces:
  - timeouts
  - maximum output size
  - schema validation
  - exit code discipline
  - redaction-aware logging

### LLM provider adapters

We want provider adapters for:

- **OpenAI**
- **Mistral** (study required)
- **Cohere** (study required)
- **GLM** (via Z.ai)
- **Aleph Alpha** (study required; may be underpowered depending on current state)

We also want first-class support for **private/self-hosted models** (local, adjacent container/node, or remote private server). Practically, this likely means building/packaging a supporting “LLM host” component that can run an inference engine and expose a stable API that Animus can treat like any other provider.

Adapter requirements:

- capability discovery (tool calling, JSON mode, streaming, context limits)
- consistent request/response schema for the kernel
- per-prompt-category model selection
- provider failover and policy-based fallback (optional)

### Prompt taxonomy / model assignment

Prompts should be categorized (examples):

- triage/routing (fast/cheap)
- deep planning (slower/stronger)
- code review
- summarization
- memory consolidation

Category controls:

- which memory lanes are eligible
- context budget
- tool permissions
- which model/provider to use
- logging requirements ("why this model", "why this context")

### Infrastructure / packaging

- Repository ships with a **Dockerfile** so the toolchain is ready-to-run.
- Prefer multi-stage builds for minimal runtime image.
- Treat secrets (provider keys, Slack tokens) as runtime-injected (env/secret store), never baked into images.

## Strategic Positioning and the AI Race

### Rationale

European markets are actively searching for alternatives to American infrastructure, and Europe is broadly concerned about its position in the “AI race”.

If Animus can:

- support **non-US LLM providers** credibly (especially **Mistral** (EU) and **Cohere** (Canada)),
- provide OpenClaw-esque integration but with a stronger emphasis on **efficiency, security, and context hygiene**,
- prioritize “serious” connectors and workflows (Slack/Web/CLI; enterprise integrations) over novelty/toy integrations (e.g. OpenClaw's attention on Discord integration),

…then it becomes a plausible platform for deployment in more conservative environments.

### How design intent supports positioning

- **Modular provider adapters**: reduces lock-in; enables regional deployment choices.
- **Explicit cognition/prompt compiler**: better auditability and governance.
- **Mature memory system**: reduces accidental leakage and “context sprawl” in long-lived deployments.
- **C++ core**: performance + determinism; fits on-prem expectations.
- **Least-privilege connectors + strict hook protocol**: stronger security story and easier compliance.

This is not a promise of funding; it’s a plausible *narrative wedge* if we build a system that earns trust in real operational settings.

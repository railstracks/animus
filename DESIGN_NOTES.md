# Animus — Design Notes (working)

These notes are intentionally rough and will be reorganized later.

## Motivation

Build a full replacement for OpenClaw to improve:

- **Efficiency**
- **Capability**
- **Security posture**
- **Context hygiene** (reduce prompt/context pollution)

## High-level architecture

- **C++ core / agent kernel**
  - Owns cognition loop and prompt assembly.
  - Owns memory read/write policy.
  - Enforces budgets (time/tokens/context size).
  - Provides deterministic gating for risky actions.

- **Extensibility via hooks (terminal commands)**
  - Allow subsystems to be implemented and iterated in Node/Python/Ruby.
  - Kernel should invoke hooks with **structured JSON** over stdin/stdout.
  - Enforce: timeouts, size limits, schema validation, exit-code discipline, redaction-aware logging.

- **Packaging**
  - Repo ships with a Dockerfile so the full toolchain is ready-to-run.

## External interfaces (initial targets)

External interfaces should connect to a **common abstraction layer** (the kernel’s conversation/task API), so behavior is consistent and context/memory policy is centralized.

Initial targets:

- **Slack** (primary serious-work interface)
- **Web interface** (admin console + interactive UI)
- **CLI** (terminal-style interface for power users + scripting)

## LLM providers (initial targets)

Initial providers we want to support via adapters:

- **OpenAI** (API/subscription)
- **Mistral** (requires further study)
- **Cohere** (requires further study)
- **GLM** (via Z.ai)
- **Aleph Alpha** (requires further study; may be underpowered depending on current state)

We also want first-class support for **private/self-hosted models**:

- hosted locally (same machine)
- hosted on an adjacent container/node on the same network
- hosted on a remote private server (self-managed)

This likely implies a companion “LLM host” package/service that can run an inference engine and present a clean adapter surface to Animus.

Notes:
- Mistral (EU) and Cohere (Canada) are strategically interesting as non-US infrastructure options.
- Provider adapter design should support capability discovery (tool calling, JSON mode, streaming, context limits) and per-category model assignment.

## Prompt categorization + model assignment

We want to categorize prompts and optionally assign models per prompt/category, e.g.

- quick triage / routing
- deep reasoning / planning
- code review
- summarization
- memory consolidation

The kernel should treat "prompt assembly" as a compilation pipeline where:

- Category influences context lanes and budgets
- Model selection can be overridden per-category
- The system can log "why this model" for later tuning

## Positioning (tentative)

Animus should be oriented toward **serious environments** (teams/corporate sectors) with an emphasis on efficiency and functional integrations ("less toys, more work").

If we can make Mistral a serviceable LLM component and provide OpenClaw-esque integrations with stronger efficiency/security/context hygiene, there may be a compelling story for European stakeholders looking for credible alternatives in the "AI race".

## Memory (direction)

A more mature memory system should support multiple lanes (e.g. event log, working set, curated semantic memory) with explicit promotion/demotion rules.

## Instinct modules (FANN)

Explore integrating a trainable "intuition" layer using **FANN**, but only for subproblems that can be expressed numerically.

- Treat outputs as *advisory* signals, never authority.
- Good candidates: urgency scoring, routing, action ranking, risk estimation.
- Requires feedback + evaluation harness to avoid untrustworthy drift.

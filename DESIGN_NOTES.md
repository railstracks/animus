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

## Memory (direction)

A more mature memory system should support multiple lanes (e.g. event log, working set, curated semantic memory) with explicit promotion/demotion rules.

## Instinct modules (FANN)

Explore integrating a trainable "intuition" layer using **FANN**, but only for subproblems that can be expressed numerically.

- Treat outputs as *advisory* signals, never authority.
- Good candidates: urgency scoring, routing, action ranking, risk estimation.
- Requires feedback + evaluation harness to avoid untrustworthy drift.

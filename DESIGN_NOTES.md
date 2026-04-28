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

## LLM Provider Interfaces as DLLs (2026-02-23)

Design principle: **LLM provider interfaces should be implemented as separate DLLs**, not built into the core kernel. This prepares Animus for:

- **Third-party extensions** — users can add their own providers without modifying the core
- **Clean separation** — the kernel remains provider-agnostic
- **Hot-swapping** — providers can be updated/added without rebuilding the kernel
- **Sandboxed failures** — a bad provider DLL doesn't necessarily crash the whole agent

### Implementation notes

- Provider DLLs should expose a well-defined C ABI for maximum compatibility
- Kernel loads providers dynamically at runtime (with configurable search paths)
- Each provider implements: initialization, capability discovery, request/response, streaming, error handling, teardown
- Provider selection by name/config; kernel routes prompts to the appropriate DLL
- Consider versioning the interface so future kernel versions can evolve the API gracefully

### SDK and documentation

We should produce:

- **SDK package** — headers, import libraries, example provider implementations (C++, C, maybe Rust/Go bindings if feasible)
- **Documentation** — interface contract, lifecycle, error handling, memory ownership rules, thread safety, logging hooks
- **Reference implementations** — at least one production-ready provider (e.g., OpenAI-compatible HTTP) included as both a working DLL and source example

### Security considerations

- DLL loading must be path-controlled (no arbitrary loading from user-controlled directories)
- Consider signing requirements for production environments
- Document the trust model: a provider DLL has significant access (outbound network, potentially secrets)

### Concurrency model

Thread safety boundaries need careful consideration:

- **Within a single agent interaction (chain-of-thought):** Tool calls should generally be **blocking/sequential** to preserve reasoning coherence. The agent's cognitive loop is not naturally parallel—each step depends on the previous.
- **Across agent interactions (multi-user):** LLM invocations and their ensuing action chains **can and should be parallelized**. This is the critical use case: in organizational deployments with multiple concurrent users, each user's session runs independently.

The LLM interface layer should therefore support:

- **Concurrent requests** from multiple agent sessions
- **Per-session isolation** (one user's chain-of-thought doesn't block another's)
- **Optional parallel tool calls** within a single session if/when we identify safe opportunities (e.g., independent read-only lookups)

This implies the provider DLL interface must be thread-safe, or the kernel must manage a pool of provider instances with proper isolation.

## Webhooks and HTTP Integration (2026-02-24)

For external integrations that require HTTP endpoints or real-time WebSocket connections, we'll use a **Ruby-based scripted HTTP server** rather than baking this into the C++ kernel.

### Technology choices

- **Sinatra** — Lightweight DSL for HTTP webhooks and REST endpoints
- **EventMachine** — Async I/O for WebSocket server/client connections

### Integration pattern

The Ruby HTTP layer will inject events directly into Animus via the CLI interface:

```
External Service → HTTP/WebSocket → Ruby (Sinatra/EM) → CLI → Kernel
```

This keeps the C++ kernel focused on cognition while allowing flexible, scriptable integrations.

### Rationale

- **Scriptability**: Ruby layer can be modified without recompiling the kernel
- **Rapid iteration**: Sinatra/EventMachine are mature, well-understood in this stack
- **Separation of concerns**: Kernel handles cognition; Ruby handles HTTP/WebSocket plumbing
- **Hook ecosystem**: Consistent with the hook-based extensibility model

### Scope

This is early-stage; many facets to flesh out (authentication, rate limiting, event routing). For now, we're laying the foundation and will expand as needed.

## IRC Interface (2026-03-12)

IRC is a planned external interface, but there are specific design pitfalls to avoid based on observations from the current OpenClaw IRC implementation.

### Observed problems in OpenClaw

1. **No debounce timer** — messages are processed immediately upon arrival
2. **Messages processed most-recent-first** — queue ordering is reversed

These combine to create a specific failure mode: when IRC clients split long messages across multiple PRIVMSGs (which most do), the pieces arrive in rapid succession but get processed in reverse order. The final fragment is parsed first, followed by earlier fragments, completely breaking message coherence.

### Required design: Debounce + concatenate

The IRC interface must:

- **Debounce incoming messages** — wait a short window (e.g., 100-300ms) after each message to see if more arrive
- **Buffer within the window** — collect all messages that arrive during the debounce period
- **Reconstruct original order** — concatenate buffered messages in correct chronological order
- **Submit as single prompt** — deliver the assembled message to the kernel as one coherent input

This is essentially a "message coalescing" pattern, similar to how terminals handle rapid input or how editors batch file change events.

### Implementation considerations

- Debounce timer should be configurable (IRC latency varies by network)
- Need to handle edge cases: very long delays between fragments, timestamp-based ordering if server provides them
- May need to detect "natural" message boundaries (e.g., user sent two distinct messages vs. one split message) — heuristics or explicit markers could help
- Logging should show both raw fragments and reconstructed message for debugging

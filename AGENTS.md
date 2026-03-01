# AGENTS.md — Animus Project

Welcome to Animus, a C++-based agent kernel designed to replace OpenClaw with improved efficiency, capability, and security.

## What Is Animus?

Animus is a modular, extensible runtime for AI agents. It provides:

- **AgentKernel**: the always-on core runtime that owns services, registries, and module lifecycle
- **Agent**: configuration + policy + capabilities for a persona/worker (LLM settings, tool permissions, memory config)
- **Session**: long-lived conversational container spanning multiple activations
- **ChainInstance**: one complete activation run from event → response
- **ChainRunner**: executes a ChainInstance's step loop (context assembly → prompt → LLM → actions)

The kernel is **passive by default** — it receives events from connectors (Slack, webhooks, CLI, etc.), routes them to sessions, and dispatches to agents.

## Design Intent

1. **Efficiency**: C++ core with fine-grained control over cognition, prompt assembly, and memory
2. **Capability**: Modular architecture with C ABI for extensions in any language
3. **Security**: Reduced context pollution, explicit permission boundaries, isolated module loading
4. **Extensibility**: Hooks can invoke terminal commands; subsystems can be implemented in Node/Python/Ruby and iterated independently

## Architecture Overview

```
IncomingEvent → SessionRouter → Session → Agent → ChainRunner
                                                        ↓
                                              PromptAssembler → LLM → ActionPlan
                                                        ↓
                                              ToolCalls / Terminal / ChannelResponse
```

Key components:
- **Session routing**: events map to a primary session + optional read-only context sessions
- **Prompt assembly**: composes conversation history, memory, tool availability into a PromptInstance
- **Budget enforcement**: max steps, token limits, timeouts per chain
- **Compaction**: conversation history can be summarized when thresholds exceeded (planned)

## Reference Documentation

| File | Purpose |
|------|---------|
| `AGENTS.system_draft.md` | Target architecture blueprint (aspirational design) |
| `AGENTS.todo.md` | Milestone-based implementation plan and progress |
| `AGENTS.api.md` | Agent-facing API reference (public headers) |
| `AGENTS.modules_draft.md` | Module system design and ABI |
| `AGENTS.persistence.md` | Session storage and persistence model |
| `AGENTS.class-diagram.md` | Class relationship diagrams |
| `AGENTS.erd.md` | Entity-relationship diagrams for data model |

## Build

```bash
cmake -B build -S .
cmake --build build
./build/bin/animus_kernel  # (when executable ready)
```

Tests use Catch2 and are built alongside the main targets.

## Code Structure

```
include/animus_kernel/  # Public headers
src/kernel/             # Implementation
  ├── core/             # AgentKernel, registries
  ├── session/          # Session, SessionManager, SessionStore
  ├── chain/            # ChainRunner, PromptAssembler
  └── module/           # Module loader
tests/                  # Unit tests
```

## Working on This Project

- Follow the milestone plan in `AGENTS.todo.md`
- When architecture and implementation conflict, treat `AGENTS.system_draft.md` as the proposal
- All new features should have corresponding tests in `tests/`
- Maintain ABI stability for modules (see `AGENTS.modules_draft.md`)

---

*This is an active development project. Expect rapid iteration.*

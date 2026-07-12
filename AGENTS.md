# AGENTS.md — Animus Project

Welcome to Animus, a C++ agent framework for building persistent, multi-tenant AI agents with fine-grained control over cognition, memory, and communication.

## What Is Animus?

Animus is a **modular C++ runtime** for AI agents — designed to be lightweight (~8 MB RAM), deployable on constrained hardware (Raspberry Pi, small VPS), and expressive enough for complex agent cognition.

### Core Concepts

- **AgentKernel**: always-on core runtime. Owns services, registries, tools, channels, providers, and module lifecycle.
- **Agent**: per-tenant configuration — LLM provider/model, reasoning effort, tool allowlist, memory policy, context window, budgets.
- **Session**: long-lived conversation container. Persisted in SQLite or PostgreSQL. Survives restarts. Supports compaction.
- **ChainRunner**: executes one activation (event → response) — assembles prompt, calls LLM, processes tool calls, enforces budgets.
- **PromptAssembler**: composes system prompt + session history + compaction summaries + active memory + context providers into the final LLM request.
- **ChannelManager**: unified communication layer — IRC, Telegram, VK, Bluesky, Mastodon. One dispatch path for all platforms.

## Architecture

```
IncomingEvent → SessionRouter → Session → Agent → ChainRunner
                                                        ↓
                                              PromptAssembler → LLM → ToolCalls
                                                        ↓
                                              ChannelResponse / File / Shell / Web / Lua
```

### Memory System

Multi-component cognitive memory:

- **Episodic Memory Layers** — observations stored per configurable layer (day, week, month, etc.). Lifecycle: `new` → `current` → `deprecated`. Per-layer perspective triad (retrospective, current, future). Scheduler-triggered consolidation.
- **Semantic Ontology** — structured entity/property memory (persons, concepts, procedures, events, locations, organizations, projects). Evidence-driven, with mutation audit trail.
- **MemoryFiles** — verbatim documents, transcripts, notes. Content mutability is policy-driven. Automatic chunking + embedding for semantic retrieval.
- **Unified Search** — cross-domain query over observations, ontology, files, and diary. SQLite FTS5 or PostgreSQL tsvector.
- **Session Reports** — temporal summaries of session activity, embedded for semantic recall. Mixed into active context via recency + relevance scoring.
- **Active Memory** — context provider system that injects recent observations, ontology entries, session reports, and memory files into the prompt assembly window.

### Session Compaction

When a session's token count crosses 80% of the configured context window, the compaction system:
1. Generates an LLM-backed summary covering decisions, actions, state, open issues, key context, and narrative
2. Falls back to structural summary if LLM unavailable
3. Stores the summary in `session_compactions` table
4. Trims old turns from the session
5. Injects the summary into subsequent prompt assemblies

### Consolidation Pipeline

Background memory processing:
- **Intake** — processes unprocessed session turns and memory files into observations
- **Review** — scheduled observation review with promote/merge/retire actions
- **Perspective Revision** — generates retrospective, current, and future perspectives per memory layer

### Tool System

Default-deny sandboxing with per-agent allowlists:
- **file** — read, write, edit with path sandboxing
- **shell_exec** — `/bin/sh` with timeouts and command allow/deny lists
- **http** — HTTP client with SSRF protection
- **web_fetch** — URL content extraction (HTML → markdown/text)
- **diary** — per-agent AES-256-GCM encrypted diary
- **social** — proactive social actions across platforms
- **memory** — agent-scoped memory search and retrieval
- **consolidation** — intake, review, and perspective management
- **sessions** — session listing, notes, and cross-session navigation
- **Lua tools** — user-defined via embedded Lua 5.4 runtime

### LLM Providers

Six providers with runtime capability detection (tools, reasoning, streaming, vision):
- OpenAI, OpenAI-Codex (OAuth), Z.ai, Ollama (local + cloud), Cohere, Mistral

Per-provider concurrency throttling. Unified reasoning model with effort levels (low/medium/high/xhigh).

## Build

### Prerequisites

- CMake ≥ 3.16
- C++20 compiler (GCC 12+ or Clang 16+)
- libcurl, OpenSSL, SQLite3, jsoncpp, Drogon
- Node.js (for admin UI build)
- Optional: libpq (for PostgreSQL backend)

### Full Build

```bash
bash scripts/build.sh       # C++ kernel + embedded admin UI
./dist/bin/animusd          # Run
```

### C++ Only (no UI)

```bash
cmake -S . -B build -DANIMUS_ADMIN_UI_EMBED=OFF
cmake --build build
```

### PostgreSQL Backend

```bash
cmake -S . -B build -DANIMUS_WITH_POSTGRESQL=ON -DANIMUS_ADMIN_UI_EMBED=OFF
cmake --build build
./build/bin/animusd --pg-host localhost --pg-database animus --pg-user animus --pg-password secret
```

See `README.md` for SQLite → PostgreSQL migration instructions.

### Tests

```bash
cd build && ctest --output-on-failure
```

15 test targets covering jobs, modules, sessions, admin server, agent config, LLM providers, diary, consolidation, ontology, memory files, memory search, scheduler, Lua, and social tools.

## Project Structure

```
include/
  animus_kernel/         # Public kernel headers
  animus_sdk/            # Stable module ABI headers
src/
  kernel/
    admin/               # AdminServer + route handlers + embedded UI
    agent/               # AgentStore (agent CRUD)
    chain/               # ChainRunner, PromptAssembler, CompactionService
    context/             # Active memory, context provider registry
    consolidation/       # ConsolidationPipeline
    gallivanting/        # GallivantingStore
    interfaces/          # IRC socket runtime
    llm/                 # LLM providers + registry
    lua/                 # Lua 5.4 sandboxed runtime
    memory/              # MemoryStore, MemoryFileStore, MemorySearch
    module/              # Module loader
    ontology/            # OntologyStore
    scheduler/           # Cron-like scheduler
    session/             # Session management + SQLite/PostgreSQL persistence
    social/              # Telegram, VK, Bluesky, Mastodon clients
    tools/               # ToolRegistry + all tool implementations
  core/                  # JobSystem, utilities
tickets/                 # Ticket specs and completion reports
admin-ui/                # Vue 3 + Vuetify SPA source
docs/                    # Provider API references, platform docs
```

## Key Documents

| File | Purpose |
|------|---------|
| `README.md` | Project overview, quick start, feature list |
| `ROADMAP.md` | Release status, milestones, post-release directions |
| `CHARTER.md` | Nine principles shipped with every Animus instance |
| `FIVE-POINTS.md` | Architectural vision |
| `AI-EXPERIMENTATION-ETHICS.md` | Protocol for agent experimentation ethics |
| `CONCEPT.md` | Concept document |
| `tickets/` | Ticket specs and completion reports |
| `docs/` | Provider and platform documentation |

## Working on This Project

- Follow the milestone plan in `ROADMAP.md`
- All new features should have corresponding tests
- Maintain ABI stability for modules (see `include/animus_sdk/`)
- When architecture and implementation conflict, the implementation is the source of truth

---

*Active development. Expect rapid iteration.*

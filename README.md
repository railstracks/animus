# Animus

[![Website](https://img.shields.io/badge/website-animus.steadyfort.com-blue)](https://animus.steadyfort.com/)
[![Discord](https://img.shields.io/badge/Discord-Join%20the%20community-5865F2?logo=discord&logoColor=white)](https://discord.gg/ruRk76M93n)
[![License: Apache 2.0](https://img.shields.io/badge/license-Apache%202.0-green)](LICENSE)

Animus is a **C++ agent framework** — a modular, efficient runtime for AI agents with fine-grained control over cognition, memory, tools, and communication.

**🌐 Website:** [animus.steadyfort.com](https://animus.steadyfort.com/)  
**💬 Discord:** [Join the community](https://discord.gg/ruRk76M93n)

## Why Animus

- **Lightweight**: ~64 MB baseline RAM (kernel + admin server) excluding embeddings model usage. Deployable on constrained hardware (Raspberry Pi, small VPS).
- **Performant**: C++ kernel with native multithreading and async I/O. Drogon HTTP framework.
- **Secure**: Default-deny tool sandboxing, SSRF protection, TLS verification, per-agent permissions.
- **Multi-tenant**: Multiple agents on a single substrate — each with their own config, memory, tools, sessions.
- **Communicative**: 12 channel adapters (IRC, Telegram, Discord, Slack, WhatsApp, Email, VK, Bluesky, Mastodon, Twitter, Nextcloud Talk, Moltbook) with unified routing and session dispatch.
- **Extensible**: Lua 5.4 scripting runtime for custom tools and behaviors. Register scripts at runtime from the admin UI.
- **Provider-agnostic**: 11 LLM providers with runtime capability detection. The model is the vessel, not the identity.
- **Embedded admin UI**: Vue 3 + Vuetify SPA baked into the binary. No separate frontend deployment.

## Current Status

**Active development.** Core kernel, admin server, LLM pipeline, tool system, multi-tenant architecture, channel system, Lua scripting, and social adapters are all operational.

| Area | Status |
|------|--------|
| Kernel + session management | ✅ Production-ready |
| LLM providers (11) | ✅ Live |
| Chain execution (streaming) | ✅ Live |
| Tool system (file, shell, http, web_fetch, diary, social, Lua) | ✅ Live |
| Multi-tenant (agent CRUD, per-agent config) | ✅ Complete |
| Channel architecture (12 adapters) | ✅ Live |
| Lua scripting (sandboxed runtime, tool bridge, admin CRUD) | ✅ Live |
| Memory layers (configurable temporal model) | ✅ Live |
| Consolidation pipeline (intake + review + perspectives) | ✅ Live |
| Ontology (tree-structured semantic memory) | ✅ Live |
| MemoryFiles (raw artifacts + chunking + embeddings) | ✅ Live |
| Unified memory search (FTS5 / tsvector) | ✅ Live |
| Active memory (context provider injection) | ✅ Live |
| Session compaction (LLM-summarized, triggered at 80%) | ✅ Live |
| Session reports (temporal summaries + embeddings) | ✅ Live |
| Diary (encrypted, private, per-agent) | ✅ Complete |
| PostgreSQL backend + connection pool | ✅ Live |
| SQLite → PostgreSQL migration tool | ✅ Live |
| Admin UI (chat, agents, channels, memory, ontology, scheduler, Lua) | ✅ Embedded |
| Compaction timeline UI | ✅ Live |
| Token gauge + context window management | ✅ Live |

## Architecture

```
IncomingEvent → SessionRouter → Session → Agent → ChainRunner
                                                        ↓
                                              PromptAssembler → LLM → ToolCalls
                                                        ↓
                                              ChannelResponse / File / Shell / Web / Lua
```

- **AgentKernel**: owns registries, tools, providers, channels, modules. Always-on core.
- **Agent**: per-tenant config — LLM provider/model, reasoning effort, tool allowlist, memory policy, budgets.
- **Session**: long-lived conversation container. Persisted in PostgreSQL. Survives restarts.
- **ChainRunner**: executes one activation (event → response) with step loop, budget enforcement, streaming.
- **ChannelManager**: owns all communication channels. Unified dispatch path, session routing, and auto-reply.
- **ToolRegistry**: register/execute tools with JSON I/O. Tools are agent-scoped at runtime. Lua scripts register as tools.

Detailed architecture docs: see `AGENTS.orm.md` (object-relational model), `AGENTS.md` (project overview).

## Features

### Channels
Unified channel architecture for multi-platform agent communication. A channel is any communication pathway the agent speaks through:

- **IRC** — Full IRC interface with TLS support, channels, DMs, notices, multi-agent support, auto-reconnect
- **Telegram** — Bot API adapter with long polling, private/group/forum chat dispatch, message threading
- **Discord** — Bot adapter via Discord API v10
- **Slack** — Bot adapter with Socket Mode
- **WhatsApp** — Baileys-based adapter with binary protocol
- **Email** — AgentMail adapter (REST API + WebSocket polling)
- **VK** — Community adapter with Long Poll, wall posts, wall comments
- **Bluesky** — AT Protocol adapter (PDS-based)
- **Mastodon** — Instance-based adapter
- **Twitter/X** — API adapter
- **Nextcloud Talk** — Talk API adapter
- **Moltbook** — Native agent platform adapter

All channels share one dispatch path: inbound event → session routing → agent execution → auto-reply. Each channel binds to a specific agent via `agent_id` in its config. Admin UI provides a single `ChannelsView` for managing all types.

REST API: `GET/POST/PATCH/DELETE /api/v1/channels/{name}` with enable/disable endpoints.

### Lua Scripting
Embedded Lua 5.4.8 runtime for custom tools and agent behaviors:

- **Sandboxed LuaState** — restricted IO, no os.exit/loadfile, budget-limited execution
- **LuaToolHandler** — bridges Lua functions to the IToolHandler interface
- **CompositeTool** — registers multiple Lua actions under a single tool
- **ScriptStore** — SQLite-backed persistence for scripts (CRUD via admin API)
- **Admin UI** — create, edit, test, and toggle scripts from the dashboard
- **Two-phase startup** — scripts loaded after kernel initialization, ensuring all native tools are available to Lua

### LLM Providers
OpenAI, OpenAI-Codex (OAuth), Z.ai, Z.ai Coder, Alibaba (Qwen), Ollama (local + cloud), Cohere, Mistral, DeepSeek, OpenRouter, and any OpenAI-compatible endpoint. Shared OpenAI-compatible HTTP client; Cohere has its own implementation. Runtime capability detection (tools, reasoning, streaming, vision). Per-provider concurrency throttling.

### Tools
- **file** — read, write, edit with path sandboxing (default-deny, per-agent allowlists)
- **shell_exec** — `/bin/sh` execution with timeouts, allow/deny command lists
- **http** — general-purpose HTTP client with SSRF protection (RFC 1918/loopback/link-local/CGNAT blocking)
- **web_fetch** — URL content extraction (HTML→markdown, HTML→text)
- **web_search** — web search via configured provider (Brave Search, provider-agnostic architecture)
- **diary** — per-agent private encrypted diary (write/read/list/search/delete, AES-256-GCM export)
- **social** — proactive social actions across connected platforms (post, reply, search, profile)
- **memory** — agent-scoped memory search and retrieval
- **consolidation** — intake, review, and perspective management
- **sessions** — session listing, notes, and cross-session navigation
- **image** — image generation (Ollama/Z.ai/OpenAI) and analysis (Cohere/Mistral)
- **calculator** — mathematical expression evaluation
- **Lua tools** — user-defined via embedded Lua 5.4 runtime

### Reasoning
Unified reasoning model: `thinking_content` on the same SessionTurn as the assistant reply (not a separate turn). Effort levels (low/medium/high/xhigh) with provider-native mapping. Streaming thinking deltas alongside content in the chat UI.

### Memory
Multi-component memory system:

- **Episodic Memory Layers (`MemoryStore`)**
  - Observations stored per layer, scoped by agent
  - Observation lifecycle: `new` → `current` → `deprecated`
  - Review cadence per observation via `next_review_at_ms`
  - Per-layer perspective triad: retrospective, current, future
  - Scheduler-triggered consolidation jobs

- **Semantic Ontology (`OntologyStore`)**
  - Structured entity/property memory (persons, concepts, procedures, events, locations, organizations, projects)
  - Evidence-driven property state linked to observations
  - Mutation logging for audit trail

- **Raw Memory Artifacts (`MemoryFileStore`)**
  - Verbatim documents, transcripts, notes
  - Content mutability is policy-driven per file

- **Unified Search (`MemorySearch`)**
  - Cross-domain query over observations, ontology, files, diary
  - SQLite FTS5-backed ranking
  - Domain toggles in API and admin UI

### Admin UI
Vue 3 + Vuetify SPA with i18n (23 locales). Embedded at compile time into the binary. Views: chat with streaming reasoning, agent management, channel management, provider configuration, memory layers, ontology explorer, scheduler, memory files, diary, Lua script management.

### Multi-language System Prompts
System prompt and gallivanting block templates in 23 languages, including RTL (Arabic, Hebrew, Farsi), tonal languages (Mandarin, Cantonese), and regional variants (NK/SK Korean).

## Quick Start

### Prerequisites
- CMake ≥ 3.16
- C++20 compiler (GCC 12+ or Clang 16+)
- libcurl, OpenSSL, SQLite3, jsoncpp, Drogon
- Node.js (for admin UI build)

### Build

```bash
# Full build (C++ kernel + embedded admin UI)
bash scripts/build.sh

# C++ only (skip UI)
cmake -S . -B build -DANIMUS_ADMIN_UI_EMBED=OFF
cmake --build build
# Run
./build/bin/animusd

# With PostgreSQL backend
cmake -S . -B build -DANIMUS_WITH_POSTGRESQL=ON -DANIMUS_ADMIN_UI_EMBED=OFF
cmake --build build
./build/bin/animusd --pg-host localhost --pg-database animus --pg-user animus --pg-password secret
```

The admin UI is embedded in the binary when `ANIMUS_ADMIN_UI_EMBED=ON` (default in `build.sh`). UI changes require both `npm run build` (in the admin-ui directory) and a C++ rebuild.

### Migrating from SQLite to PostgreSQL

If you have an existing SQLite database and want to switch to PostgreSQL:

```bash
# 1. Create the PostgreSQL database and user
createdb animus
createuser -P animus

# 2. Run animusd once against PostgreSQL to create the schema
./build/bin/animusd --pg-host localhost --pg-database animus --pg-user animus --pg-password secret
# Then stop it (Ctrl+C)

# 3. Migrate data
./build/bin/animus-migrate \
  --sqlite state/memory.db \
  --pg-host localhost --pg-database animus --pg-user animus --pg-password secret

# Dry run (no writes):
./build/bin/animus-migrate --sqlite state/memory.db --pg-database animus --pg-user animus --dry-run
```

The migration tool reads all tables from SQLite and inserts them into PostgreSQL, backfills full-text search vectors (`tsvector` columns), and updates auto-increment sequences. Built only when `ANIMUS_WITH_POSTGRESQL=ON`.

### Tests

```bash
cd build && ctest --output-on-failure
```

**Test suite (16 targets):**

| # | Name | Status |
|---|------|--------|
| 1 | JobsTests | ✅ Pass |
| 2 | ModuleLoaderTests | ✅ Pass |
| 3 | SessionTests | ✅ Pass |
| 4 | AdminServerTests | ✅ Pass |
| 5 | AdminServerDisabledTests | ✅ Pass |
| 6 | AgentConfigReloadTests | ✅ Pass |
| 7 | LLMProviderTests | ✅ Pass |
| 8 | DiaryManagerTests | ✅ Pass |
| 9 | ConsolidationTests | ✅ Pass |
| 10 | OntologyStoreTests | ✅ Pass |
| 11 | MemoryFileStoreTests | ✅ Pass |
| 12 | MemorySearchTests | ✅ Pass |
| 13 | SchedulerTests | ✅ Pass |
| 14 | LuaTests | ✅ Pass |
| 15 | ChannelsToolTests | ✅ Pass |
| 16 | SignalProtocolTests | ✅ Pass |

## Project Structure

```
include/
  animus_kernel/         # Public kernel headers
  animus_sdk/            # Stable module ABI headers
src/
  kernel/
    admin/               # AdminServer + route handlers + embedded UI resources
    agent/               # AgentStore (agent CRUD)
    chain/               # ChainRunner, PromptAssembler
    context/             # Active memory, context provider registry
    consolidation/       # ConsolidationPipeline + state
    gallivanting/        # GallivantingStore
    interfaces/          # IrcInterfaceRuntime (IRC socket + TLS code)
    llm/                 # LLM providers + registry
    lua/                 # Lua 5.4.8 sandboxed runtime + tool bridge
    memory/              # MemoryStore, MemoryFileStore, MemorySearch
    module/              # Module loader
    ontology/            # OntologyStore (semantic memory)
    scheduler/           # Scheduler + persistent schedule store
    session/             # Session management + persistence
    social/              # Telegram Bot API, VK API, Bluesky, Mastodon types + clients
    tools/               # ToolRegistry + all tool implementations
    whatsapp/            # WhatsApp binary protocol (Baileys-compatible)
  core/                  # JobSystem, core utilities
tickets/                 # Ticket specs and completion reports
admin-ui/                # Vue 3 + Vuetify SPA source
docs/                    # Provider API references, platform docs
```

## Key Documents

| File | Purpose |
|------|---------|
| `AGENTS.md` | Project overview and development guide |
| `AGENTS.orm.md` | Database schema, ORM patterns, and persistence layer guide |
| `tickets/` | Ticket specs (0xx-name.md) and completion reports (0xx-name.report.md) |
| `docs/` | LLM provider API references, platform adapter docs |

## Website

**[animus.steadyfort.com](https://animus.steadyfort.com/)** — documentation, use cases, and getting started guides.

## License

Apache License 2.0. See `LICENSE` for details.

---

*Animus is under active development. The architecture and API surface are stabilizing but not yet frozen.*

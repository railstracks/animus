# Animus

[![Website](https://img.shields.io/badge/website-animus.steadyfort.com-blue)](https://animus.steadyfort.com/)
[![Discord](https://img.shields.io/badge/Discord-Join%20the%20community-5865F2?logo=discord&logoColor=white)](https://discord.gg/ruRk76M93n)
[![License: Apache 2.0](https://img.shields.io/badge/license-Apache%202.0-green)](LICENSE)

Animus is a **C++ agent framework** — a modular, efficient runtime for AI agents with fine-grained control over cognition, memory, tools, and communication.

**🌐 Website:** [animus.steadyfort.com](https://animus.steadyfort.com/)  
**💬 Discord:** [Join the community](https://discord.gg/ruRk76M93n)  
**🧩 VSCode Extension:** [Animus IDE](https://marketplace.visualstudio.com/items?itemName=artificers.animus-ide)  
**🐳 Docker Images:** [animus](https://hub.docker.com/r/mrsommer/animus) · [animus-sudo](https://hub.docker.com/r/mrsommer/animus-sudo) · [animus-node](https://hub.docker.com/r/mrsommer/animus-node)

## Why Animus

- **Lightweight**: ~64 MB baseline RAM (kernel + admin server) excluding embeddings model usage. Deployable on constrained hardware (Raspberry Pi, small VPS).
- **Performant**: C++ kernel with native multithreading and async I/O. Drogon HTTP framework.
- **Secure**: Default-deny tool sandboxing, SSRF protection, TLS verification, per-agent permissions, two-tier HTTP authentication with rate limiting.
- **Multi-tenant**: Multiple agents on a single substrate — each with their own config, memory, tools, sessions.
- **Federated**: Multi-master configuration replication across verified nodes — a scratch node rebuilds its entire configuration from peers.
- **Communicative**: 12 channel adapters (IRC, Telegram, Discord, Slack, WhatsApp, Email, VK, Bluesky, Mastodon, Twitter, Nextcloud Talk, Moltbook) with unified routing and session dispatch.
- **Extensible**: Lua 5.4 scripting runtime for custom tools and behaviors. Register scripts at runtime from the admin UI.
- **Provider-agnostic**: 11 LLM providers with runtime capability detection. The model is the vessel, not the identity.
- **Embedded admin UI**: Vue 3 + Vuetify SPA baked into the binary. No separate frontend deployment.

## Current Status

**v0.4.7** — Active development. Core kernel, admin server, LLM pipeline, tool system, **API packages**, **verified-node federation** (multi-master config replication), **secrets vault**, egress control, multi-tenant architecture, channel system, Lua scripting, memory system (hardened by the v0.4.7 audit arc: full-row mutation audit trail, durable retirement semantics), social adapters, **attribution** (author metadata across channels and sessions), authentication, scheduled tasks, external nodes, agent export/import, and diffusion are all operational.

| Area | Status |
|------|--------|
| Kernel + session management | ✅ Production-ready |
| LLM providers (11) | ✅ Live |
| Chain execution (streaming) | ✅ Live |
| Tool system (26 tools) | ✅ Live |
| API packages (manifest v1: Lua actions, connections, state, egress allowlist) | ✅ Live |
| Egress control + owner-approval gate (deny-by-default, content-bound approvals) | ✅ Live |
| Animus Registry integration (browse, fetch, re-hash, loud-reject on mismatch) | ✅ Live |
| Multi-tenant (agent CRUD, per-agent config) | ✅ Complete |
| Channel architecture (12 adapters) | ✅ Live |
| Lua scripting (sandboxed runtime, tool bridge, admin CRUD, ctx.crypto primitives) | ✅ Live |
| Memory layers (configurable temporal model) | ✅ Live |
| Consolidation pipeline (intake + review + perspectives) | ✅ Live |
| Ontology (tree-structured semantic memory, per-agent worldviews, evidence-linked reconciler) | ✅ Live |
| MemoryFiles (raw artifacts + chunking + embeddings) | ✅ Live |
| Unified memory search (FTS5 / tsvector) | ✅ Live |
| Memory mutation audit trail (full-row snapshots on every destructive path) | ✅ Live |
| Active memory (context provider injection) | ✅ Live |
| Session compaction (LLM-summarized, triggered at 80%) | ✅ Live |
| Session reports (temporal summaries + embeddings) | ✅ Live |
| Diary (encrypted, private, per-agent) | ✅ Complete |
| HTTP authentication (static token + user accounts + rate limiting) | ✅ Live |
| Secrets vault (AES-GCM, secret_ref indirection, encrypted replication) | ✅ Live |
| Diffusion (GetImg + Stability AI) | ✅ Live |
| Chat attachments (per-channel, token-secured) | ✅ Live |
| SOP system (Standard Operating Procedures) | ✅ Live |
| Scheduled tasks (cron + interval, per-agent isolation) | ✅ Live |
| External nodes (HMAC-signed remote commands) | ✅ Live |
| Federation (multi-master replication, verified nodes, scratch-join rebuild) | ✅ Live |
| Agent export/import (composable `.agent` archives) | ✅ Live |
| Session notes + agenda tool | ✅ Live |
| Temporal context provider (time-aware assembly) | ✅ Live |
| Interjection (inject into active chains) | ✅ Live |
| PostgreSQL backend + connection pool | ✅ Live |
| SQLite → PostgreSQL migration tool | ✅ Live |
| Admin UI (chat, agents, channels, memory, ontology, scheduler, Lua, auth) | ✅ Embedded |
| Compaction timeline UI | ✅ Live |
| Token gauge + context window management | ✅ Live |
| i18n (23 locales, including RTL and tonal languages) | ✅ Live |

## Architecture

```
IncomingEvent → SessionRouter → Session → Agent → ChainRunner
                                                        ↓
                                              PromptAssembler → LLM → ToolCalls
                                                        ↓
                                              ChannelResponse / File / Shell / Web / Lua / Diffusion
```

- **AgentKernel**: owns registries, tools, providers, channels, modules. Always-on core.
- **Agent**: per-tenant config — LLM provider/model, reasoning effort, tool allowlist, memory policy, budgets.
- **Session**: long-lived conversation container. Persisted in PostgreSQL. Survives restarts.
- **ChainRunner**: executes one activation (event → response) with step loop, budget enforcement, streaming. Supports interjection into active chains.
- **ChannelManager**: owns all communication channels. Unified dispatch path, session routing, and auto-reply.
- **ToolRegistry**: register/execute tools with JSON I/O. Tools are agent-scoped at runtime. Lua scripts register as tools.
- **PeerSyncService**: federation member — replicates agent-global state (memory, schedules, agent/channel/provider config, vault ciphertext) across verified nodes via outbox capture and anti-entropy pull.

Detailed architecture docs: see `AGENTS.orm.md` (object-relational model), `AGENTS.md` (project overview).

## Features

### Authentication & Security

Two-tier HTTP authentication protects the admin API:

**Tier 1 — Static Token (zero-config):**
Set the `ANIMUS_AUTH_TOKEN` environment variable before starting the daemon. Clients authenticate via the `Authorization: Bearer <token>` header. This grants full admin access and is ideal for Docker deployments, CI/CD, and bootstrapping user accounts.

```bash
# Docker / container
export ANIMUS_AUTH_TOKEN="your-secret-token"
./animusd

# Client request
curl -H "Authorization: Bearer your-secret-token" http://localhost:8080/api/v1/agents
```

**Tier 2 — User Accounts (interactive):**
Create user accounts with username/password via the admin UI or API. Login returns a session token (7-day TTL, stored hashed in PostgreSQL). Role-based access control (admin/viewer) restricts sensitive operations.

**Auth behavior at startup:**
- No `ANIMUS_AUTH_TOKEN`, no users → auth optional (backward-compatible for local dev, warning logged)
- `ANIMUS_AUTH_TOKEN` set, no users → static token grants full access. Use the `/setup` endpoint to create the first admin account.
- Users exist → login with credentials. Static token still works if configured.

Rate limiting: 5 failed auth attempts from the same IP within 60 seconds triggers a 60-second block (HTTP 429).

WebSocket auth: browser clients pass tokens via the `Sec-WebSocket-Protocol` subprotocol header.

### Channels
Unified channel architecture for multi-platform agent communication. A channel is any communication pathway the agent speaks through:

- **IRC** — Full IRC interface with TLS support, channels, DMs, notices, multi-agent support, auto-reconnect
- **Telegram** — Bot API adapter with long polling, private/group/forum chat dispatch, message threading, automatic message splitting (4096 char limit)
- **Discord** — Bot adapter via Discord API v10, with channel tracking
- **Slack** — Bot adapter with Socket Mode
- **WhatsApp** — Balelsy-compatible adapter with binary protocol, dynamic version resolution
- **Email** — AgentMail adapter (REST API + WebSocket polling)
- **VK** — Community adapter with Long Poll, wall posts, wall comments
- **Bluesky** — AT Protocol adapter (PDS-based) with proactive JWT refresh and notification polling
- **Mastodon** — Instance-based adapter
- **Twitter/X** — API adapter
- **Nextcloud Talk** — Talk API adapter
- **Moltbook** — Native agent platform adapter

All channels share one dispatch path: inbound event → session routing → agent execution → auto-reply. Each channel binds to a specific agent via `agent_id` in its config. Admin UI provides a single `ChannelsView` for managing all types. Async restart on config update.

REST API: `GET/POST/PATCH/DELETE /api/v1/channels/{name}` with enable/disable endpoints.

### LLM Providers
OpenAI, OpenAI-Codex (OAuth), Z.ai, Z.ai Coder, Alibaba (Qwen), Ollama (local + cloud), Cohere, Mistral, DeepSeek, OpenRouter, and any OpenAI-compatible endpoint. Shared OpenAI-compatible HTTP client; Cohere has its own implementation. Runtime capability detection (tools, reasoning, streaming, vision). Per-provider concurrency throttling.

### Tools (26)

| Tool | Description |
|------|-------------|
| **file** | Read, write, edit with path sandboxing (default-deny, per-agent allowlists) |
| **shell_exec** | `/bin/sh` execution with timeouts, allow/deny command lists |
| **http** | General-purpose HTTP client with SSRF protection (RFC 1918/loopback/link-local/CGNAT blocking) |
| **web_fetch** | URL content extraction (HTML→markdown, HTML→text) |
| **web_search** | Web search via configured provider (Brave Search, provider-agnostic architecture) |
| **diary** | Per-agent private encrypted diary (write/read/list/search/delete, AES-256-GCM export) |
| **social** | Proactive social actions across connected platforms (post, reply, search, profile) |
| **memory** | Agent-scoped memory search and retrieval |
| **consolidation** | Intake, review, and perspective management |
| **sessions** | Session listing, notes, and cross-session navigation |
| **image** | Image generation (Ollama/Z.ai/OpenAI) and analysis (Cohere/Mistral) |
| **diffusion** | AI image generation (GetImg + Stability AI, multiple models) |
| **attachment** | Per-channel chat attachments with token-secured access |
| **sop** | Standard Operating Procedures — codified reusable workflows |
| **stored_links** | Curated link storage and retrieval |
| **rss** | RSS feed aggregation |
| **agent** | Agent self-management (identity, config) |
| **project** | Project-scoped data management |
| **gallivanting** | Autonomous exploration sessions |
| **node** | External node management |
| **schedule** | Task scheduling and recurring jobs |
| **channels** | Channel administration via tool interface |
| **calculator** | Mathematical expression evaluation |
| **dice** | Dice rolling (RPG-style notation) |
| **Lua tools** | User-defined via embedded Lua 5.4 runtime |
| **tools** | Tool introspection and management |

### API Packages

Turn third-party services into agent tools as **configuration, not code**. A package is a declarative manifest (v1) plus Lua scripts: typed commands, polling connections with event hooks, a typed state store with secret marking, and an explicit egress allowlist. The kernel's `api` tool interprets installed packages — agents get per-command tool schemas and call them like any other tool. No C++ required.

The lifecycle runs through the **Animus Registry**:

- **Author** a package per the [manifest v1 spec](docs/api-packages.md) and build a single-file publishable manifest (Lua scripts inlined)
- **Publish** it to a registry server — the registry lints, canonicalizes, and records a SHA-256 content hash; versions are immutable and semantic version must increase
- **Install** on any daemon — fetched from the registry, re-canonicalized, and loudly rejected on any hash mismatch
- **Browse** what's out there — a live registry is running at [animus-registry.steadyfort.com/packages](https://animus-registry.steadyfort.com/packages)

Two enforcement layers gate packages at runtime: **egress allowlists** are deny-by-default and enforced at three gates with a boot-time sweep (packages without a declared allowlist get derived defaults from URL templates and state), and installs land **disabled, pending owner approval** — the approval is bound to the stored content, so it cannot be replayed against a modified payload.

**Reference implementation:** [animus-package-alpaca](https://github.com/railstracks/animus-package-alpaca) — 24 commands (market data, orders, positions, one-shot price triggers, watchlists) plus two live-polling connections; field-tested by a trading agent running against a paper account.

Adding a service you use? The [standing contributor lane](https://github.com/railstracks/animus/issues/29) is open.

### Federation

Multiple Animus daemons can operate as one substrate — a verified-node mesh:

- **Multi-master writes** — per-node id ranges prevent collisions; trigger-based outbox capture and HTTP pull with per-node Bearer tokens keep peers converged (digest handshake on every pass, bounded batches, catch-up from persisted cursors after downtime)
- **Replicated set** — memory (layers, observations, perspectives, ontology, diary), schedules, task runs and leases, agent + channel config, provider config, and vault secrets — the last only ever as ciphertext; the master key never crosses the wire
- **Scratch-join rebuild** — an empty node boots its entire configuration from peers and joins the mesh (validated with four scratch nodes on live PostgreSQL, chain topology)
- **Mixed-version grace** — unknown tables and future columns defer instead of blocking, so nodes on different versions stay converged

Trust is established with per-node tokens; the vault master key is distributed out-of-band to verified nodes only.

### Secrets Vault

Credential-shaped values (API keys, tokens, JSON service-account keys) never live in plaintext config. The vault encrypts them with AES-GCM under a master key held in an out-of-band key file; config rows and sync payloads carry `secret_ref`s only. Vaulted secrets replicate as ciphertext and resolve transparently on peers holding the master key — and a secret dies with its referencing row, so no orphaned ciphertext outlives the provider that used it.

### Reasoning
Unified reasoning model: `thinking_content` on the same SessionTurn as the assistant reply (not a separate turn). Effort levels (low/medium/high/xhigh) with provider-native mapping. Streaming thinking deltas alongside content in the chat UI.

### Memory
Multi-component memory system:

- **Episodic Memory Layers (`MemoryStore`)**
  - Observations stored per layer, scoped by agent
  - Observation lifecycle: `new` → `current` → `deprecated`, with durable retirement (tombstones) — a retired observation is never resurrected by background processes; revision is the explicit revival route
  - Mutation log with full-row snapshots — archived, deleted, and revised rows can be reconstructed from the audit trail alone
  - Review cadence per observation via `next_review_at_ms`
  - Per-layer perspective triad: retrospective, current, future
  - Scheduler-triggered consolidation jobs

- **Semantic Ontology (`OntologyStore`)**
  - Structured entity/property memory (persons, concepts, procedures, events, locations, organizations, projects)
  - Per-agent worldviews — agent-scoped namespaces with scoped search
  - Evidence-driven property state linked to observations
  - Mutation logging with full-row snapshots for audit trail

- **Raw Memory Artifacts (`MemoryFileStore`)**
  - Verbatim documents, transcripts, notes
  - Content mutability is policy-driven per file

- **Unified Search (`MemorySearch`)**
  - Cross-domain query over observations, ontology, files, diary
  - SQLite FTS5 / PostgreSQL tsvector-backed ranking
  - Domain toggles in API and admin UI

### Interjection
Active chains can receive injected messages mid-execution. This enables real-time interaction with an agent that's already processing — useful for corrections, additional context, or priority interrupts.

### SOP System
Standard Operating Procedures — codified, reusable workflows that agents can reference and execute. SOPs provide structured patterns for repeated tasks without hard-coding behavior.

### Admin UI
Vue 3 + Vuetify SPA with i18n (23 locales). Embedded at compile time into the binary. Views: chat with streaming reasoning, agent management, channel management, provider configuration, memory layers, ontology explorer, scheduler, memory files, diary, Lua script management, user administration, auth/login flow.

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
./build/bin/animusd

# With PostgreSQL backend
cmake -S . -B build -DANIMUS_WITH_POSTGRESQL=ON -DANIMUS_ADMIN_UI_EMBED=OFF
cmake --build build
./build/bin/animusd --pg-host localhost --pg-database animus --pg-user animus --pg-password secret
```

The admin UI is embedded in the binary when `ANIMUS_ADMIN_UI_EMBED=ON` (default in `build.sh`). UI changes require both `npm run build` (in the admin-ui directory) and a C++ rebuild.

### Environment Variables

| Variable | Purpose | Default |
|----------|---------|---------|
| `ANIMUS_AUTH_TOKEN` | Static auth token for the admin API. When set, clients can authenticate with `Authorization: Bearer <token>`. When unset and no users exist, auth is optional (development mode). | unset (auth optional) |
| `ANIMUS_LOG_LEVEL` | Logging verbosity: `DEBUG`, `INFO`, `WARN`, `ERROR`. | `INFO` |
| `ANIMUS_ADMIN_HOST` | Admin server bind address. | `127.0.0.1` |
| `ANIMUS_ADMIN_PORT` | Admin server port. | `8080` |
| `ANIMUS_ADMIN_ENABLED` | Set to `0` to disable the admin server entirely. | `1` |

### CLI Flags

| Flag | Purpose | Default |
|------|---------|---------|
| `--prompt-log-level <level>` | Prompt logging verbosity: `none`, `default`, `full`. `none` disables logging. `default` logs token usage metadata. `full` logs the complete prompt (system message, user message, tool results). | `default` |
| `--config <path>` | Config directory path. | `./config` |
| `--data <path>` | Data directory path. | `./data` |
| `--pg-host <host>` | PostgreSQL host. When set, uses PostgreSQL instead of SQLite. | unset (SQLite) |
| `--pg-database <name>` | PostgreSQL database name. | `animus` |
| `--pg-user <user>` | PostgreSQL username. | — |
| `--pg-password <pass>` | PostgreSQL password. | — |

### Authentication Setup

```bash
# Option A: Static token (zero-config, Docker-friendly)
export ANIMUS_AUTH_TOKEN="your-secret-token"
./animusd
# → All API requests need: Authorization: Bearer your-secret-token

# Option B: User accounts (interactive)
# 1. Start with a static token (as above)
# 2. Bootstrap the first admin account:
curl -X POST http://localhost:8080/api/v1/auth/setup \
  -H "Authorization: Bearer your-secret-token" \
  -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"your-password"}'

# 3. Login to get a session token:
curl -X POST http://localhost:8080/api/v1/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"your-password"}'
# → {"token":"...","expires_at":"..."}
```

See the `scripts/start-daemon.sh` helper for daemon lifecycle management with PID tracking and logging.

### Docker Deployment

The production Docker images are published on Docker Hub:

| Image | Purpose |
|-------|---------|
| [`mrsommer/animus`](https://hub.docker.com/r/mrsommer/animus) | Standard daemon — kernel, admin server, embedded admin UI |
| [`mrsommer/animus-sudo`](https://hub.docker.com/r/mrsommer/animus-sudo) | Same as the standard image, but the `animus` user has passwordless sudo — for API packages and tools that need privileged host operations |
| [`mrsommer/animus-node`](https://hub.docker.com/r/mrsommer/animus-node) | External node runner — connects to a central Animus server and executes tool calls on the host (see [External Node Deployment](#external-node-deployment) below) |

The daemon image supports database configuration via environment variables. When `DB_HOST` is set, the entrypoint generates a `db.json` pointing at PostgreSQL. Without it, SQLite is used.

```yaml
# docker-compose.yml
services:
  animus:
    image: mrsommer/animus:latest
    ports:
      - "8080:8080"
    environment:
      - ANIMUS_AUTH_TOKEN=your-secret-token
      - DB_HOST=postgres
      - DB_PORT=5432
      - DB_NAME=animus
      - DB_USER=animus
      - DB_PASS=secret
    depends_on:
      - postgres
    restart: unless-stopped

  postgres:
    image: postgres:16
    environment:
      - POSTGRES_DB=animus
      - POSTGRES_USER=animus
      - POSTGRES_PASSWORD=secret
    volumes:
      - pg-data:/var/lib/postgresql/data
    restart: unless-stopped

volumes:
  pg-data:
```

| Env var | Purpose | Default |
|---------|---------|---------|
| `DB_HOST` | PostgreSQL host. When set, enables PostgreSQL backend. Empty = SQLite. | empty (SQLite) |
| `DB_PORT` | PostgreSQL port. | `5432` |
| `DB_NAME` | PostgreSQL database name. | `animus` |
| `DB_USER` | PostgreSQL username. | `animus` |
| `DB_PASS` | PostgreSQL password. | (none) |
| `ADMIN_HOST` | Admin server bind address. | `0.0.0.0` |
| `ADMIN_PORT` | Admin server port. | `8080` |
| `ANIMUS_AUTH_TOKEN` | Static auth token for admin API. | unset |
| `ANIMUS_SKIP_MODEL_DOWNLOAD` | Set to `1` to skip auto-downloading the embedding model. | unset |

If a `db.json` file exists in the config directory, it takes precedence over environment variables.

### External Node Deployment

Animus supports external nodes — lightweight daemon processes that connect to the central server via WebSocket and execute tool calls (shell commands, file operations) on remote machines. This enables distributed agent execution across multiple hosts.

**Node authentication uses two separate secrets:**

1. **Auth token** (`Authorization: Bearer <token>`) — authenticates the WebSocket connection
2. **Signing key** (HMAC-SHA256) — signs individual tool calls; the node verifies each command before executing

Both are generated together when you create a node token via the admin UI or API. The signing key is optional but recommended — without it, tool calls are sent unsigned.

```bash
# On the central server: generate credentials
# Via API:
curl -X POST http://localhost:8080/api/v1/nodes/tokens \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"description": "workstation node"}'
# Returns: {"token": "an_...", "signingKey": "..."}

# On the node host:
animusd --node \
  --server-url ws://animus-host:8080/ws/node \
  --token an_... \
  --signing-key ... \
  --node-name workstation \
  --node-tools exec,file
```

**Docker:**

```bash
docker run --rm \
  -e SERVER_URL=ws://animus-host:8080/ws/node \
  -e NODE_TOKEN=an_... \
  -e NODE_SIGNING_KEY=... \
  -e NODE_NAME=workstation \
  -e NODE_TOOLS=exec,file \
  mrsommer/animus-node:latest
```

| Env var | Purpose | Required |
|---------|---------|----------|
| `SERVER_URL` | WebSocket URL of the Animus server | Yes |
| `NODE_TOKEN` | Auth token for server connection | Yes |
| `NODE_NAME` | Name to register as | Yes |
| `NODE_TOOLS` | Comma-separated tool allowlist | No (default: `exec,file`) |
| `NODE_SIGNING_KEY` | HMAC signing key for command verification | No (recommended) |

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

**Test suite: 45 targets, 48 ctest cases — all green.** Coverage spans the kernel core (jobs, sessions, modules), admin server (routes, auth, observation administration, agent deletion cleanup), the memory family (store, search, consolidation, ontology, layer seeding, active memory, concurrency), channels and social (manager, context, helpers, IRC interface, reply resolution), tools (file, dispatch, social, media output), Lua runtime and API packages (sandbox, runtime, registry client), federation (peer sync, outbox, node ids, node manager), and infrastructure (migration, secrets vault, scheduler, connection supervisor, healer, embedding sweep, signal protocol).

## Project Structure

```
include/
  animus_kernel/         # Public kernel headers
  animus_sdk/            # Stable module ABI headers
src/
  kernel/
    admin/               # AdminServer + route handlers + embedded UI resources
    agent/               # AgentStore (agent CRUD)
    auth/                # AuthStore + AuthManager (authentication, rate limiting)
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
| `docs/api-packages.md` | API package manifest specification (v1) |

## Website

**[animus.steadyfort.com](https://animus.steadyfort.com/)** — documentation, use cases, and getting started guides.

## License

Apache License 2.0. See `LICENSE` for details.

---

*Animus is under active development. The architecture and API surface are stabilizing but not yet frozen.*
# AGENTS.admin-interface.md — Embedded Admin Interface Design

**Status:** Design (April 28, 2026)
**Origin:** Melvin Sommer + Kestrel. Embedded web admin for single-instance management, with a protocol path to multi-instance orchestration.

---

## Problem Statement

Animus needs a first-class administration surface. Agents require runtime configuration, memory inspection, interface management, and conversational access — all of which are cumbersome via config files and CLI alone. The admin interface should ship with the agent binary, require zero external dependencies, and be scriptable via the same API it consumes.

## Design Principles

1. **Embedded by default.** The web server runs as a thread inside the Animus process. No separate deployment, no nginx, no reverse proxy required for single-agent use.
2. **API-first.** Every admin action is an API endpoint. The UI is a thin consumer. No UI-only state.
3. **Protocol, not product.** The API surface doubles as the multi-instance admin protocol. A future central dashboard aggregates multiple Animus API endpoints.
4. **Transparent.** Every action achievable in the UI is also achievable via API call or config file edit. Scriptability is a first-class concern.
5. **Single-binary deploy.** Frontend assets are compiled into the binary as embedded resources. Distribution is one file.

## Technology Choices

### HTTP/WebSocket Server: drogon

- Mature C++ async HTTP framework with native WebSocket support
- High performance, thread-per-core model
- Header-only option available; clean CMake integration
- Supports static file serving, middleware, session management

Alternatives considered:
- **oatpp:** More modular, cleaner dependency story, but weaker WebSocket story
- **Crow:** Flask-like API, but less mature for production WebSocket use

### Frontend: Vue 3 + Vuetify

- Consistent with Steadyfort frontend preference
- SPA compiled to static assets, embedded in binary
- Talks to REST API for CRUD, WebSocket for real-time features

### Resource Embedding

- CMake resource compiler embeds SPA build output into the binary
- Served from memory via drogon's static file handler
- No filesystem dependency for the UI

## Architecture

```
Animus Process
├── AgentKernel (existing)
│   ├── SessionManager
│   ├── AgentRegistry
│   ├── ChainRunner
│   └── ModuleLoader
├── AdminServer (new subsystem)
│   ├── HTTP REST API (/api/v1/...)
│   ├── WebSocket endpoints
│   │   ├── /ws/chat (conversational sessions)
│   │   └── /ws/observations (live memory capture stream)
│   └── Static file serving (/ → embedded SPA)
└── Config (YAML/TOML for admin server bindings)
```

The AdminServer registers with AgentKernel as a subsystem/module, receives lifecycle events, and exposes the kernel's state through the API surface.

## API Surface (v1)

### Agent Configuration
- `GET /api/v1/agent` — Current agent config (model, temperature, system prompt, etc.)
- `PATCH /api/v1/agent` — Update agent config (hot-reload where possible)
- `GET /api/v1/agent/model` — Current model provider + model details
- `PUT /api/v1/agent/model` — Switch model/provider

### Sessions
- `GET /api/v1/sessions` — List active sessions
- `GET /api/v1/sessions/:id` — Session details + history
- `DELETE /api/v1/sessions/:id` — End a session
- `GET /api/v1/sessions/:id/context` — Active context for a session (memory layers loaded, tools available)

### Memory
- `GET /api/v1/memory/layers` — List configured memory layers
- `GET /api/v1/memory/layers/:name/observations` — Observations in a layer (paginated)
- `POST /api/v1/memory/consolidate` — Trigger a consolidation run
- `GET /api/v1/memory/consolidation/status` — Last/current consolidation state
- `GET /api/v1/memory/observations/:id` — Single observation detail
- `PATCH /api/v1/memory/observations/:id` — Edit observation metadata (tags, weight)
- `DELETE /api/v1/memory/observations/:id` — Remove observation

### Interfaces (Connectors)
- `GET /api/v1/interfaces` — List configured interfaces (Slack, Discord, etc.)
- `GET /api/v1/interfaces/:name` — Interface status + config
- `PATCH /api/v1/interfaces/:name` — Update interface config
- `POST /api/v1/interfaces/:name/enable` — Enable interface
- `POST /api/v1/interfaces/:name/disable` — Disable interface

### Chat (WebSocket)
- `WS /ws/chat` — Conversational session
  - Client sends: `{ "type": "message", "content": "...", "session_id": "..." }`
  - Server sends: `{ "type": "token", "content": "..." }` (streaming)
  - Server sends: `{ "type": "done", "session_id": "..." }`
  - Server sends: `{ "type": "error", "message": "..." }`

### Observability
- `GET /api/v1/status` — Agent health, uptime, memory size
- `GET /api/v1/metrics` — Token usage, latency, request counts
- `WS /ws/observations` — Live observation capture stream

### Constitution
- `GET /api/v1/constitution` — Read constitution layers (immutable core + contracts)
- `GET /api/v1/constitution/adaptation` — Read adaptation layer (configurable behaviors)
- `PATCH /api/v1/constitution/adaptation` — Update adaptation rules

## UI Layout

### Sidebar Navigation
- **Dashboard** — Agent status, health, recent activity
- **Chat** — Conversational interface with the agent
- **Memory** — Layer browser, observation inspector, consolidation controls
- **Configuration** — Agent settings, model selection, system prompt
- **Interfaces** — Connector management (Slack, Discord, etc.)
- **Constitution** — Core values, contracts, adaptation rules
- **Logs** — Live log stream, consolidation history

### Chat Panel
- WebSocket-backed real-time conversation
- Streaming token output
- Session selector (switch between active sessions)
- Context sidebar showing active memory layers and loaded observations
- Message history with search

### Memory Browser
- Layer tree (collapsible, shows observation count per layer)
- Observation list with tags, weight, age, decay visualization
- Detail panel for single observation with full metadata
- Consolidation trigger button with progress indicator
- Bulk operations (tag, promote, demote, archive)

## Security Considerations

- Admin API should bind to localhost by default
- Optional token-based auth for remote access
- Constitution core layer is read-only through the API (requires separate governance process to modify)
- Interface credentials are write-only (returned as masked values)
- WebSocket connections require authentication handshake

## Multi-Instance Protocol Path

The API surface is designed so that a future central admin can:
1. Discover instances via a registry or manual configuration
2. Aggregate `/api/v1/status` from multiple instances into a fleet dashboard
3. Route chat sessions to specific instances via their `/ws/chat` endpoints
4. Perform bulk configuration changes across instances
5. Compare memory layer states across agents

This requires no changes to the embedded admin — only an aggregator service that speaks the same API.

## Build Integration

```cmake
# In CMakeLists.txt
option(ANIMUS_ADMIN_UI "Build and embed admin UI" ON)

if(ANIMUS_ADMIN_UI)
    # Build Vue SPA (requires node/npm in build environment)
    add_custom_command(
        OUTPUT admin_ui_archive.cpp
        COMMAND ${CMAKE_COMMAND} -P cmake/EmbedAdminUI.cmake
        DEPENDS ${ADMIN_UI_SOURCES}
    )
    # Link drogon + admin routes
    target_link_libraries(animus_kernel PRIVATE drogon)
endif()
```

## Implementation Priority

1. AdminServer subsystem scaffold (drogon integration, basic routing)
2. REST API: agent config + status endpoints
3. REST API: memory layer read endpoints
4. WebSocket: chat endpoint (streaming response)
5. SPA scaffold (Vue 3 + Vuetify, basic layout)
6. SPA: dashboard + configuration views
7. SPA: chat interface
8. SPA: memory browser
9. REST API: interface management endpoints
10. REST API: constitution endpoints
11. Resource embedding (SPA into binary)
12. Multi-instance protocol documentation

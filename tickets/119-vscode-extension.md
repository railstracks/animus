# Ticket 119: VSCode Extension

**Status:** Scoped  
**Date:** 2026-07-17  
**Author:** Melvin + Kestrel  
**Component:** Extension (TypeScript), Admin API  

## Vision

Animus as a sidebar tab in VSCode — developers interact with persistent agents without leaving their IDE. The agent has shell and file tool access to the workspace, persistent memory across sessions, and project management. All intelligence lives in the Animus daemon; the extension is a thin client.

## Architecture

```
VSCode Extension (TypeScript)
    ├── Sidebar: Session tree view
    │   ├── Active sessions (per agent)
    │   ├── Project tasks (from project tool)
    │   └── Quick actions (new session, switch agent)
    ├── Chat panel (Webview)
    │   ├── Message history
    │   ├── Streaming responses
    │   └── Tool output rendering
    ├── Workspace integration
    │   ├── Exposes workspace root path
    │   ├── File watch → notify agent of changes
    │   └── Diff view for file tool edits
    └── Status bar
        ├── Connection status
        └── Active agent indicator

Deployment model (public server + workstation node):
    ┌─────────────────┐     ┌──────────────────┐
    │  Public Server   │     │  Workstation      │
    │  (Animus daemon) │◄───►│  (Animus node)    │
    │                  │     │  - Local FS access │
    │  Agent + memory  │     │  - Shell execution │
    │  Admin API       │     │  - Build/test runs │
    └──────▲───────┘     └──────────────────┘
           │                       ▲
           │ HTTP/WS               │ Extension sends
           │                       │ "node: workstation"
    ┌──────┴───────┐               │ as context tag
    │  VSCode      │───────────────┘
    │  Extension   │  (IDE on workstation)
    └──────────────┘
```

The extension connects to the Animus daemon's API. The daemon routes
`shell` and `file` tool calls to the workstation node. The agent operates
on the developer's actual workspace without the public server needing
filesystem access.

## Extension Structure

```
animus-vscode/
├── package.json          # Extension manifest
├── src/
│   ├── extension.ts      # Activation, registration
│   ├── client.ts         # AnimusClient — HTTP + WS client
│   ├── sessionProvider.ts # TreeDataProvider for sidebar
│   ├── chatPanel.ts      # Webview panel for chat
│   ├── config.ts         # Connection settings (host, port, token)
│   └── types.ts          # Shared types (Session, Agent, Message)
├── media/
│   └── chat.html         # Chat webview template
├── webview/
│   ├── src/
│   │   ├── App.vue       # Chat UI (Vue 3, bundled)
│   │   ├── main.ts
│   │   └── components/
│   └── vite.config.ts
└── README.md
```

## Features (v1 — Minimum Viable)

1. **Connection setup** — settings panel for daemon URL + auth token
2. **Session list** — sidebar tree showing active sessions per agent
3. **Chat panel** — open a session, send messages, see streaming responses
4. **Agent selection** — switch between agents configured on the daemon
5. **New session** — create a new chat session with an agent

## Features (v2 — Workspace Integration)

6. **Workspace path injection** — pass `${workspaceFolder}` as context to the agent so `shell` and `file` tools operate on the project
7. **Project tasks** — show `project` tool tasks as a tree view with status indicators
8. **File notifications** — watch workspace for changes, notify agent
9. **Diff view** — when agent uses `file` tool, show VSCode diff for review
10. **Inline code references** — agent mentions `file:line`, extension creates clickable links

## Features (v3 — Advanced)

11. **Multi-daemon** — connect to multiple Animus instances (local + remote)
12. **Agent templates** — create new agents from the extension
13. **Terminal integration** — agent can open VSCode terminals for shell operations
14. **Debug integration** — agent reads debugger state, suggests fixes

## API Surface Required

The extension consumes the existing Admin API. New endpoints needed:

- `GET /api/sessions` — list sessions (exists, may need filtering by agent)
- `POST /api/sessions/:key/messages` — send message to session (exists)
- `WS /api/sessions/:key/stream` — WebSocket for real-time responses (new — may exist for admin UI)
- `GET /api/agents` — list agents (exists)
- `GET /api/sessions/:key/messages` — message history (exists)

The API is largely sufficient. The main work is the extension client + UI.

## Security Considerations

- **Auth token storage** — VSCode SecretStorage API (not plaintext settings)
- **Workspace access** — agent's `shell` and `file` tools operate on daemon's filesystem, not VSCode's. If daemon is remote, file operations don't affect the local workspace. v2 would need a bridge (agent sends file ops → extension executes locally).
- **Node protocol bridges the workspace gap.** User sets up Animus node on their workstation. Extension sends `node: workstation` as context. Daemon routes shell/file tool calls to that node. Agent operates on the developer's actual workspace. No new bridge protocol needed — the node infrastructure already exists.
- **Local vs remote daemon** — extension works with both. For local daemon (localhost), the node may be unnecessary. For remote daemon, the node provides filesystem access without exposing the workstation publicly.
- **Extension config includes `node` field** — when set, injected into the agent's context as `Node: <name>`. The daemon uses this to route tool calls.

## Technical Notes

- **No Copilot repurposing** — GitHub Copilot's extension is closed-source. Building our own extension is cleaner and avoids ToS issues.
- **Vue 3 for webview** — consistent with admin UI stack. Vite bundle, loaded as VSCode webview.
- **Bundling** — `vsce package` produces a `.vsix` file for manual installation or marketplace publishing.

## Acceptance Criteria (v1)

- [ ] Extension connects to Animus daemon (configurable URL + token)
- [ ] Sidebar shows list of agents and their sessions
- [ ] Chat panel sends messages and displays responses
- [ ] Streaming responses render incrementally
- [ ] New sessions can be created
- [ ] Connection status visible in status bar
- [ ] Works with both local (localhost) and remote daemon

## Dependencies

- Ticket 120 (Authentication) — needed for secure remote daemon connections
- Existing Admin API endpoints

## Open Questions

- Should the extension embed a lightweight Animus daemon (bundled binary) for zero-config local use? Or always require a separate daemon?
- Marketplace publishing vs. side-load `.vsix` distribution for initial release?

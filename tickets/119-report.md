# Ticket 119: VSCode Extension — Report

**Status:** v1 Complete
**Date:** 2026-07-21
**Author:** Kestrel
**Repository:** [railstracks/animus-vscode](https://github.com/railstracks/animus-vscode)

## Summary

Built a VSCode extension that brings Animus persistent agents into the IDE. The extension is a thin client — all intelligence, memory, and tool execution lives in the Animus daemon. The extension handles connection, session browsing, chat UI, and streaming.

**24 commits**, ~1,500 lines of TypeScript across 3 source files.

## What Was Built

### Architecture

```
VSCode Extension (TypeScript)
    ├── Sidebar Webview (single-view)
    │   ├── Main view: New session form + session list
    │   └── Chat view: Messages, streaming, settings
    ├── AnimusClient (HTTP + WS client)
    │   ├── REST: agents, sessions, models, history
    │   └── WebSocket: real-time messaging + streaming
    └── Extension host
        ├── Connection management
        ├── Config (daemon URL, auth token, agent, node)
        └── Model loading per provider
```

### Core Features

**Connection & Configuration**
- Connects to any Animus daemon (local or remote) via configurable URL + auth token
- Settings: `animus.daemonUrl`, `animus.authToken`, `animus.agentId`, `animus.node`
- Connection status indicator in the sidebar

**Session Management**
- Session list with timestamps, sorted by last activity
- Refresh sessions on demand
- Create new sessions with agent/provider/model selection
- Open existing sessions with full message history
- Sessions tagged with `source: 'vscode'` on the daemon side

**Chat Interface**
- Full chat view with streaming responses (token-by-token)
- User messages, assistant text, thinking blocks (collapsible), and tool calls rendered inline
- Thinking rendered as a standalone collapsible message element before the text response
- Attachments rendered inline (images, audio, video, text, files) via daemon attachment API
- Auto-scroll during streaming, chat input with Enter-to-send

**Agent Defaults & Per-Message Overrides**
- Agent dropdown populates from daemon config
- Selecting an agent loads its configured provider, model, reasoning enabled, and reasoning effort
- Provider dropdown loads model list asynchronously from daemon
- Reasoning toggle (checkbox) + effort selector (low/medium/high/xhigh/max)
- **New session form:** Full controls for agent/provider/model/reasoning
- **Chat view settings:** Collapsible settings panel (⚙ button) with the same controls, so users can change provider/model/reasoning mid-conversation
- All settings sent per-message via WebSocket — no server-side session mutation needed

**Streaming Protocol**
- WebSocket events: `token` (streaming text), `text` (complete text), `thinking` (reasoning blocks), `tool_call` (tool invocations), `attachment` (file attachments), `context` (session metadata), `error`, `done`
- Live token rendering with blinking cursor during streaming
- Stop signal support for cancelling in-flight responses

### Files

| File | Lines | Description |
|------|-------|-------------|
| `src/extension.ts` | ~1,188 | Extension activation, webview HTML/CSS/JS, message routing, all UI |
| `src/client.ts` | 222 | AnimusClient — HTTP REST + WebSocket client |
| `src/types.ts` | 82 | Shared TypeScript interfaces (Agent, Session, etc.) |
| `package.json` | — | Extension manifest, settings, commands |
| `media/sidebar-icon.svg` | — | Activity bar icon |

## Bugs Found and Fixed

### Animus Daemon Fixes

1. **Consolidation message bleedover** — Consolidation intake/review sessions fired stale WebSocket callbacks that delivered messages into open VSCode chat sessions. **Root cause:** `ChainRunner` used singleton member callbacks (`m_thinkingCallback`, `m_toolCallCallback`, `m_assistantMessageCallback`) shared across all invocations. **Fix:** Refactored to per-invocation callback parameters. Consolidation can no longer fire WS callbacks — structurally impossible. *(Animus commits: ChainRunner.h, ChainRunner.cpp, ChatSessionService.cpp, AgentKernel.cpp)*

2. **Session key collision after daemon restart** — New VSCode sessions were appended to session 202 (an old session) after daemon restart. **Root cause:** `m_nextWsConversationId` was seeded only from `ws-conversation-` prefixed IDs, ignoring `vscode-conversation-` IDs. After restart, the counter restarted from 1, colliding with existing sessions. **Fix:** Counter now seeds from ALL conversation ID prefixes. *(Animus commit: AdminServer.cpp)*

3. **Per-message reasoning overrides** — Agent config unconditionally overrode ChainRunner defaults for reasoning. Users couldn't override reasoning per-message. **Fix:** Override chain implemented: per-message override > agent config > ChainRunner default. New parameters on `ExecuteOnSession`/`ExecuteStreamingOnSession`. WS controller reads `reasoning_effort` and `reasoning_enabled` from payload. *(Animus commits: ChainRunner.h, ChainRunner.cpp, ChatSessionService.h, ChatSessionService.cpp, AdminServerWebSocketControllers.inc)*

### Extension Fixes

4. **Streaming not rendering** — Server sends `token` events (not `text`) for LLM streaming. Added `token` event handler. *(Commit: d1d7142)*

5. **Thinking block placement** — Thinking was nested inside the text response. Refactored to render as a standalone collapsible message element before the text response, in both live streaming and history loading. *(Commit: 72e15fe)*

6. **Empty string falsy bug** — `appendMsg` created `.content` div only when content was non-empty. Empty assistant messages (thinking-only responses) lost their container. Fixed: always create content div. *(Commit: b228949)*

7. **TypeScript-in-HTML syntax error** — `(s: any)` type annotation survived into the webview's `<script>` as plain JavaScript, causing a SyntaxError. Removed the annotation. *(Commit: 4e9bbca)*

8. **Observation event bleedover** — WebSocket events from other sessions were processed in the active chat. Added session_id filtering. *(Commit: fed8c3d)*

## Acceptance Criteria

- [x] Extension connects to Animus daemon (configurable URL + token)
- [x] Sidebar shows list of agents and their sessions
- [x] Chat panel sends messages and displays responses
- [x] Streaming responses render incrementally
- [x] New sessions can be created
- [x] Connection status visible
- [x] Works with both local (localhost) and remote daemon
- [x] Agent defaults loaded from config (provider/model/reasoning)
- [x] Per-message provider/model/reasoning overrides
- [x] Mid-chat settings changes via collapsible panel
- [x] Thinking blocks rendered as collapsible elements
- [x] Attachments rendered inline

## What's Next (Future Versions)

**v0.1.x:**
- Workspace path injection — pass `${workspaceFolder}` as context
- Project tasks tree view (from `project` tool)
- File notifications — watch workspace for changes

**v0.2.x:**
- Diff view for agent file edits
- Inline code references (`file:line` → clickable links)
- Terminal integration
- Multi-daemon connections

**Marketplace:**
- Publish to VSCode Marketplace
- Publish to Open VSX Registry (for VSCodium, Code-OSS)

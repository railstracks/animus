# WhatsApp Integration Architecture

How the WhatsApp companion device protocol fits into the Animus agent framework.

## Guiding Principle

**The agent just talks. The plumbing delivers.**

This is the pattern established with the VK adapter and it applies here. The WhatsApp adapter is a **transport layer** — it receives inbound messages and delivers outbound responses. The agent (via Lua or prompt chain) produces plain text. The adapter handles all protocol complexity.

No social tool, no tool calls in auto-reply sessions. The agent sees an incoming message and responds naturally. The adapter encrypts and delivers.

## Component Map

```
┌─────────────────────────────────────────────────────────────┐
│                      Animus Daemon                           │
│                                                              │
│  ┌──────────────────┐     ┌──────────────────────────────┐  │
│  │ SocialEventPoller │────▶│ WhatsAppEventConsumer        │  │
│  │                  │     │  - Decrypt inbound messages   │  │
│  │ (polls VK LP,   │     │  - Parse to UnifiedEvent      │  │
│  │  WS WebSocket,  │     │  - Dispatch to session        │  │
│  │  BSky, ...)     │     └──────────────────────────────┘  │
│  └──────────────────┘                                       │
│                          │                                   │
│                          ▼                                   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Session dispatch (per account/platform/conversation)  │   │
│  │  - Creates or resumes agent session                  │   │
│  │  - Injects inbound message as prompt                 │   │
│  │  - Agent responds with plain text                    │   │
│  └──────────────────────────────────────────────────────┘   │
│                          │                                   │
│                          ▼                                   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ AgentKernel::SendAutoReply()                          │   │
│  │  - Receives agent's text response                    │   │
│  │  - Routes to correct outbound adapter                │   │
│  │  - WhatsApp: encrypt + send via WebSocket            │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌──────────────────┐     ┌──────────────────────────────┐  │
│  │ WhatsAppAdapter   │◀───│ SendAutoReply                │  │
│  │  - Maintains WS   │     │  (calls adapter.sendMessage │  │
│  │  - Noise session  │     │   with JID + text)          │  │
│  │  - Signal state   │     └──────────────────────────────┘  │
│  │  - Pre-key pool   │                                     │
│  │  - Auth state     │                                     │
│  └──────────────────┘                                       │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

## Existing Patterns to Follow

The VK adapter established the patterns. WhatsApp reuses the same skeleton:

| Pattern | VK Implementation | WhatsApp Equivalent |
|---------|-------------------|---------------------|
| Inbound events | Long Poll (`messages.getLongPollServer`) | WebSocket receive + decrypt |
| Event type | `message_new`, `wall_reply_new` | `message` binary nodes |
| Session routing | `peer_id` → session key | JID → session key |
| Outbound delivery | `messages.send`, `wall.createComment` | Encrypt + send `message` node |
| Self-reply filter | `fromId == ±groupId` | `fromMe` flag on `WebMessageInfo` |
| Tool exclusion | `session_types = {"consolidation"}` | Same mechanism |

## New Code Needed

### C++ Classes

**`WhatsAppConnection`** — manages the WebSocket lifecycle
- Connect, disconnect, reconnect with backoff
- Noise handshake (XX pattern + certificate validation)
- Frame-level encrypt/decrypt (AES-256-GCM with counter)
- Ping/pong heartbeat (25s interval)
- Converts between raw WebSocket frames and binary nodes

**`BinaryNodeCodec`** — encode/decode the tagged tree format
- Token dictionary (compile-time constant table from Baileys' `Defaults.ts`)
- Node serialization/deserialization
- Attribute handling (JID parsing, timestamp conversion)

**`SignalSession`** — per-device Signal Protocol state
- X3DH key agreement (initiate + respond)
- Double Ratchet (encrypt + decrypt)
- Session persistence (load/save from auth directory)
- Pre-key management (generation, upload, consumption)

**`SenderKeyStore`** — per-group Sender Key state
- Sender Key generation and distribution
- Group message encryption/decryption
- Persistence (load/save from auth directory)

**`WhatsAppAdapter`** — high-level adapter implementing the existing social adapter interface
- Wraps Connection, SignalSession, SenderKeyStore
- Exposes `sendMessage(jid, text)`, `sendReaction(jid, msgId, emoji)`
- Emits events: `onMessage(jid, text, quotedMsg)`, `onPresenceUpdate(jid, type)`
- Manages auth state directory
- Handles initial device linking (QR code generation)

**`WhatsAppEventConsumer`** — bridges adapter to SocialEventPoller
- Receives decrypted events from WhatsAppAdapter
- Converts to `UnifiedEvent` (existing struct used by VK/Bluesky consumers)
- Dispatches to session via `SocialEventPoller::DispatchToSession`

### Lua Integration

The WhatsApp adapter does **not** need a Lua plugin. It's a transport layer, not a platform adapter in the CompositeTool sense. The agent interacts with it through the auto-reply pipeline:

1. Inbound message arrives → consumer creates session → agent sees prompt
2. Agent responds with text → `SendAutoReply()` routes to WhatsApp adapter
3. WhatsApp adapter encrypts and sends

If we later want the agent to *proactively* send messages (not just reply), we'd add a WhatsApp action to the social tool's Lua adapter — similar to how VK's Lua adapter exposes `messages.send`. But for the initial implementation, auto-reply is sufficient.

### Auth State Storage

Animus uses SQLite for persistence. The auth state should live in the database, not as a filesystem directory:

| Table | Maps from | Purpose |
|-------|-----------|---------|
| `whatsapp_creds` | instance_id → JSON blob | Noise keys, identity, signed pre-key, registration |
| `whatsapp_sessions` | instance_id + remote_jid → JSON blob | Signal Double Ratchet state |
| `whatsapp_prekeys` | instance_id + key_id → JSON blob | One-time pre-keys |
| `whatsapp_sender_keys` | instance_id + group_id → JSON blob | Sender Key state |
| `whatsapp_identities` | instance_id + remote_jid → public key | Trusted identity keys |

This keeps everything in the existing SQLite database per Animus tenant.

## Linking Flow (First-Time Setup)

Admin UI integration for QR code scanning:

1. Admin calls `POST /api/{instance}/whatsapp/link`
2. Animus generates Noise keypair, requests QR from WhatsApp server
3. Returns QR code image to admin UI
4. Admin scans QR with WhatsApp phone app
5. Server confirms link, credentials stored in DB
6. Adapter transitions to connected state

This mirrors how OpenClaw's WhatsApp bridge works (which Melvin has used before with Kestrel's phone number).

## Session Types

Following the VK pattern, WhatsApp sessions should be typed:
- `whatsapp:chat` — 1:1 conversations
- `whatsapp:group` — group conversations
- `consolidation` — when social tool is explicitly invoked (rare for WhatsApp)

Auto-reply sessions get `whatsapp:chat` or `whatsapp:group`. The social tool is excluded via `session_types` — same mechanism as VK.

## Message Types to Support (Phase 1)

For initial implementation, support only:

| Type | Inbound | Outbound |
|------|---------|----------|
| Text (`conversation`) | ✅ | ✅ |
| Extended text (URL previews) | ✅ | ✅ |
| Image (receive, don't generate) | ✅ | ❌ |
| Reaction | ✅ | ✅ |
| Read receipt | ❌ | ✅ (mark as read) |
| Typing indicator | ❌ | ✅ |
| Delivery/read status | ✅ | ❌ (passive) |

Media sending (images, documents, audio) is Phase 2 — it requires uploading to WhatsApp's media servers before sending the message. Complex but well-documented in Baileys.

## Dependencies on Existing Animus Code

The adapter integrates with these existing components:
- `SocialEventPoller` — event dispatch (add WhatsApp as a poller type alongside VK Long Poll)
- `AgentKernel::SendAutoReply()` — outbound routing (add WhatsApp case alongside VK)
- `SessionManager` — session creation/lookup for inbound messages
- `AdminServer` — API endpoints for QR linking, status, disconnect
- SQLite infrastructure — tables for auth state

No changes needed to the Lua plugin system, prompt chain runner, or core agent loop.

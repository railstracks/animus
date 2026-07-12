# Ticket 062 — WhatsApp Adapter (Linked Devices Protocol)

**Created:** 2026-05-29
**Status:** Active — QR code generation working, next: keep-alive + pair-success handling
**Priority:** High
**Depends on:** 056 (Channels Refactor), 060 (Discord — ChannelContextProvider pattern), 063 (Signal Protocol Library)

## Current Status (May 31)

**QR CODE GENERATED ✅** — The full end-to-end flow works through a custom TCP+TLS+WS transport:
1. Custom transport connects to `web.whatsapp.com:443` (TCP → TLSv1.3 → WS upgrade)
2. Noise XX handshake completes (client hello → server hello → client finish)
3. Transport encryption keys derived (HKDF Extract+Expand)
4. Server sends 698-byte encrypted pair-device IQ → decrypted and decoded
5. Binary node parsed with full 4-dictionary token map
6. QR pairing URL generated and ready for phone scan

**Eleven bugs found and fixed** across crypto, protobuf, binary node decoding, and transport layers. See `062-report.md` for full details.

**Next steps:** Respond to ping IQs for keep-alive, handle pair-success after QR scan, implement message flow.

## Summary

Implement WhatsApp as a first-class channel in Animus using the **Linked Devices** (WhatsApp Web companion) protocol. This connects as a linked device via QR code scan, providing real-time message send/receive with full E2E encryption — no Business API account required.

## Motivation

WhatsApp is the world's most-used messaging platform. For Animus as an agent framework, having a WhatsApp channel that works with a personal account (via companion device pairing) is far more accessible than requiring users to set up a WhatsApp Business Account, Cloud API access, and webhook infrastructure.

The Linked Devices protocol is the same approach Baileys (9.6k stars, actively maintained) uses. Baileys v7.0.0-rc13 serves as the reference implementation to replicate in C++.

## Protocol Overview

WhatsApp's Linked Devices protocol is a seven-layer stack:

### Layer 1: Transport
- `wss://web.whatsapp.com/ws/chat` — standard WebSocket
- 3-byte big-endian length prefix framing
- Optional `?ED=routingInfo` query param from persisted auth state

### Layer 2: Noise Protocol Handshake
- `Noise_XX_25519_AESGCM_SHA256` — modified XX pattern (3-way key exchange)
- Both sides authenticate with static keys
- WhatsApp root CA hardcoded (public key in `Defaults/index.ts`)
- Post-handshake: AES-256-GCM with counter-based IV for all frames
- Constants: `NOISE_WA_HEADER = [87, 65, 6, 3]`, `DICT_VERSION = 3`

### Layer 3: Binary Node Encoding
- Custom tagged tree format with token dictionary (1000+ tokens, single/double byte)
- JID addressing:
  - Personal: `user@s.whatsapp.net`
  - Group: `groupid@g.us`
  - LID: `lid@lid`
  - Device-specific: `user:device@s.whatsapp.net`
- Node types: `iq`, `message`, `receipt`, `notification`, `ib`
- Wire format: encode → noise encrypt → send; receive → noise decrypt → decode

### Layer 4: Signal Protocol (1:1 Messages)
- X3DH key agreement + Double Ratchet + AES-256-CBC
- Identity keypair (Curve25519, long-term)
- Signed pre-key (rotated, uploaded to server)
- One-time pre-keys (pool of 812 initial, replenish when server count drops below 5)
- Session records per remote device
- LID mapping: phone number JID → LID JID (WhatsApp actively migrating)

### Layer 5: Signal Protocol (Group Messages)
- Sender Keys — each member generates per-group key
- Distributes via 1:1 Signal sessions
- Group messages encrypted once (not per-recipient)

### Layer 6: Protobuf Messages
- All message structures are proto3
- Key messages: `HandshakeMessage`, `ClientPayload`, `Message`, `WebMessageInfo`, `HistorySync`
- Proto definitions ported from Baileys `WAProto/` directory

### Layer 7: Auth State (Persistence)
Must survive daemon restarts:
```
whatsapp_auth/
├── creds.json          ← noise keys, identity keypair, signed pre-key, registration ID, me (jid), routingInfo
├── app-state-sync/     ← CRDT-based contact list, settings sync
├── device-list/        ← Per-user device lists (cached from USync queries)
├── identity-key/       ← Trusted identity keys (per contact)
├── pre-key/            ← One-time pre-keys { id: { public, private } }
├── sender-key/         ← Group sender keys
└── session/            ← Signal Protocol sessions (per device JID)
```

## Connection Lifecycle

### Initial Pairing (New Device)
```
1. Connect to wss://web.whatsapp.com/ws/chat
2. Generate ephemeral Curve25519 keypair
3. Noise handshake:
   a. Send: HandshakeMessage.clientHello { ephemeral: EPHEMERAL_PUBKEY }
   b. Receive: HandshakeMessage { serverHello }
   c. Process handshake → derive shared secret
   d. Send: HandshakeMessage.clientFinish { static: keyEnc, payload: encrypted(registrationNode) }
4. Server sends QR ref data → generate QR code for user to scan
5. User scans QR via phone WhatsApp → Linked Devices → Link a device
6. Server sends pairing success → configureSuccessfulPairing()
7. Save auth state (creds with me.id, noise keys, etc.)
8. Initial history sync begins
9. Pre-key upload (812 one-time pre-keys)
10. Keep-alive starts (30s interval)
```

### Reconnection (Existing Device)
```
1. Load auth state from persistence
2. Connect WebSocket (with ?ED=routingInfo if available)
3. Noise handshake with loginNode (creds.me.id)
4. No QR scan needed — device already paired
5. Resume message processing
```

### Keep-Alive
- Ping every 30 seconds (configurable, `keepAliveIntervalMs`)
- Server responds with pong
- Timeout triggers reconnect

## Message Send Flow
```
1. Assert Signal session exists for recipient (fetch from server via USync if not)
2. Get recipient's device list via USync query
3. For each device:
   a. Encode message as protobuf (proto.Message)
   b. Encrypt with Signal Protocol (SessionCipher.encrypt → type: "pkmsg" or "msg")
   c. Build <to jid="..."><enc v="2" type="pkmsg|msg">ciphertext</enc></to>
4. If new session (type=pkmsg), include device identity in node
5. Build relay stanza:
   <message to="jid" id="msgId" type="text|media" mediatype="...">
     <enc>...</enc>  (per device)
   </message>
6. Send via noise-encoded binary node
7. Await receipt from server
```

## Message Receive Flow
```
1. Noise-decode incoming frame → BinaryNode
2. Route by node tag:
   - "message" → decrypt → decode protobuf → dispatch to agent session
   - "receipt" → delivery/read receipts
   - "iq" → query responses, key bundles, app state sync
   - "notification" → group updates, contact changes, device list changes
3. For incoming messages:
   a. For each <enc> child: decrypt with Signal Protocol
   b. Decode protobuf → proto.Message
   c. Extract sender JID, message text, timestamp
   d. Dispatch to ChannelManager → DispatchToSession
```

## Session Routing
- 1:1 chats: `peer:SENDER_JID` → session key `chat:whatsapp:SENDER_JID`
- Groups: `post:GROUP_JID` → session key `chat:whatsapp:GROUP_JID`
- ReplyTarget carries JID for SendReply routing

## Channel Config
Stored as `channel.whatsapp.config` in SQLite:
```json
{
  "auth_path": "/path/to/whatsapp_auth",
  "agent_id": "default",
  "respond_to_dm": true,
  "respond_to_groups": false,
  "monitored_groups": []
}
```

No bot token — auth is via the persisted Noise/Signal key material.

## C++ Dependencies

| Component | What Baileys Uses | C++ Equivalent |
|-----------|------------------|----------------|
| Noise Protocol | Custom (Utils/) | Hand-roll Noise XX pattern (~200 lines) or use [noise-c](https://github.com/threatstream/noise-c) |
| Signal Protocol | `libsignal` npm (Rust via N-API) | [libsignal-client C FFI](https://github.com/signalapp/libsignal) — same Rust library |
| Protobuf | `protobufjs` (dynamic) | `protobuf` (Google C++ — already in Animus build) |
| WebSocket | `ws` npm | Drogon `WebSocketClient` (used for Discord Gateway) |
| Binary node codec | Custom (WABinary/) | Port from Baileys (~500 lines) |
| Crypto primitives | Node.js `crypto` | OpenSSL (X25519, AES-256-GCM, HMAC-SHA256, HKDF) |

### libsignal C FFI
Signal's official `libsignal` library is Rust-based with C FFI support. The same library Baileys uses via Node.js N-API bindings is available as a C library. This gives us:
- X3DH key agreement
- Double Ratchet (SessionCipher)
- Pre-key generation and management
- Session record serialization
- Sender Key (group encryption)

Alternatively, the `whatsapp-rust` crate (Rust native WhatsApp library) could be linked directly, but that duplicates effort and creates a second protocol implementation to maintain.

## Implementation Phases

### Phase 1: Transport + Pairing (~800 lines)
- WebSocket connection to `wss://web.whatsapp.com/ws/chat`
- Noise XX handshake implementation
- Binary node encoder/decoder (token dictionary, tagged tree)
- QR pairing flow (registration node → QR display → pairing confirmation)
- Auth state persistence (JSON files)
- Keep-alive ping/pong

### Phase 2: Signal Protocol + Messaging (~1000 lines)
- **Uses `animus::signal` stubs from ticket 063 Phase A** (production crypto merged in 063-C)
- Pre-key generation and upload (stub)
- Session establishment (fetch key bundles, inject E2E sessions)
- Message encryption (1:1 and group, via `animus::signal` interface)
- Message decryption
- USync device discovery
- LID mapping
- Full protobuf message encode/decode
- End-to-end message routing (everything except real E2E crypto)

### Phase 3: ChannelManager Integration (~500 lines)
- `WhatsAppGatewayLoop` in ChannelManager (same pattern as DiscordGatewayLoop)
- `DispatchToSession` routing (peer for DMs, post for groups)
- `SendReply` (encrypt → relay)
- ChannelContextProvider already handles WhatsApp (no changes needed)
- Admin UI WhatsApp form section

### Phase 4: Lua Social Adapter (~400 lines)
- `whatsapp.lua` — social tool actions
- Actions: list_chats, get_messages, send_message, send_media, get_contacts, get_groups, search, react, reply, delete, edit, get_profile, set_presence

## Estimated Effort

| Layer | Lines | Difficulty | Notes |
|-------|-------|------------|-------|
| Noise handshake + transport | ~300 | Medium | Custom Noise XX implementation |
| Binary node codec | ~500 | Medium | Token dictionary, tagged tree encode/decode |
| Protobuf definitions | ~200 | Low | Port from Baileys WAProto/ |
| Signal Protocol integration | ~400 | **Hard** | libsignal C FFI, session mgmt, pre-key lifecycle, LID mapping |
| Message send/receive | ~600 | Medium | Encrypt/decrypt → relay/dispatch |
| Auth state persistence | ~200 | Medium | JSON file-based (or SQLite) |
| QR pairing flow | ~200 | Medium | Registration node, QR generation |
| ChannelManager integration | ~200 | Low | Same pattern as Discord |
| Admin UI + Lua adapter | ~500 | Low | Standard patterns |
| **Total** | **~3,100** | | |

## Risks and Mitigations

### Account Ban Risk
- **Risk:** WhatsApp ToS bans automated clients
- **Likelihood:** Behavioral, not fingerprint-based (Baileys operates for years without anti-fingerprinting)
- **Mitigation:** Rate limit sends, avoid bulk/cold outreach, respect conversation windows
- **Escalation:** If WhatsApp introduces fingerprinting, migrate to Cloud API

### Protocol Changes
- **Risk:** WhatsApp changes protocol, breaking the adapter
- **Likelihood:** Moderate — Baileys maintainers respond quickly to changes
- **Mitigation:** Pin WhatsApp version constant, monitor Baileys releases, version-gate protocol changes

### libsignal C FFI Stability
- **Risk:** Signal's C API may lag behind Node.js API
- **Mitigation:** The core operations (encrypt, decrypt, session management) are stable and unlikely to change. If C FFI is insufficient, can shell out to a small Node.js bridge process (not preferred but viable).

### Maintenance Burden
- **Risk:** Ongoing protocol maintenance becomes a dayjob
- **Mitigation:** Animus doesn't need every WhatsApp feature — just text send/receive and basic media. Scope deliberately to minimize surface area.

## Acceptance Criteria

- [ ] WebSocket connects to WhatsApp servers
- [ ] Noise handshake completes
- [ ] QR code generated for device pairing
- [ ] Device pairs successfully (scanned by phone)
- [ ] Auth state persists across daemon restarts
- [ ] Reconnection without re-pairing
- [ ] Incoming text messages decrypted and dispatched to agent sessions
- [ ] Agent text replies encrypted and sent back to originating chat
- [ ] DM and group message routing
- [ ] ChannelContextProvider injects WhatsApp context into system prompt
- [ ] Admin UI WhatsApp form section
- [ ] Lua social adapter with basic actions
- [ ] Pre-key management (generation, upload, rotation)
- [ ] Keep-alive and reconnection handling

## Deferred (Phase 5+)
- Media message send/receive (images, documents, audio, video)
- Message reactions, edits, deletes
- Read receipts
- Presence (typing indicators, online status)
- History sync (chat history on initial connection)
- Group admin operations (create, promote, settings)
- Sticker support
- Newsletter/channel support
- Multi-device identity management
- Contact sync

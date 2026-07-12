# WhatsApp Web Multi-Device Protocol

Reverse-engineered from Baileys v7.0.0-rc12. No official documentation exists.

## Overview

WhatsApp Web operates as a **companion device** linked to an existing phone account via QR code scan. The companion device has its own cryptographic identity and operates independently once linked — the phone does not need to remain online.

The protocol has seven distinct layers:

```
Layer 7: Auth State (JSON file persistence)
Layer 6: Protobuf Messages (proto3 definitions)
Layer 5: Signal Protocol — Groups (Sender Keys)
Layer 4: Signal Protocol — 1:1 (X3DH + Double Ratchet)
Layer 3: Binary Node Encoding (tagged tree + token dictionary)
Layer 2: Noise Protocol Handshake (Noise_XX_25519_AESGCM_SHA256)
Layer 1: Transport (WebSocket + length-prefix framing)
```

---

## Layer 1: Transport

**WebSocket connection:**
```
wss://web.whatsapp.com/ws/chat
```

Standard WebSocket upgrade. All frames are **binary**. The framing format is:

```
[3 bytes: big-endian length] [payload]
```

No compression at the transport level. Payload is either:
- A **binary node** (encrypted or plaintext during handshake)
- A **ping/pong** frame (0x1A for ping, response is pong)

The connection is long-lived. Heartbeat pings are sent every ~25 seconds. If no pong is received within 10 seconds, the connection is considered dead and must be re-established.

### Reconnection

Baileys implements exponential backoff reconnection. After disconnect:
1. Wait with exponential backoff (1s, 2s, 4s, 8s... capped at ~30s)
2. Re-establish WebSocket
3. Resume the Noise session (no full re-handshake if session keys are still valid)
4. Query offline messages via `iq` node with `offline` namespace

---

## Layer 2: Noise Protocol Handshake

WhatsApp uses a modified **Noise_XX_25519_AESGCM_SHA256** pattern.

### Why "Modified"

Standard Noise XX is a 3-message handshake:
1. Initiator → Responder: `e` (ephemeral public key)
2. Responder → Initiator: `e, ee, s, es` (ephemeral + static, DH computations)
3. Initiator → Responder: `s, se` (static + DH)

WhatsApp adds an extra step: after message 3, the server sends its certificate chain for validation.

### Certificate Validation

WhatsApp's root CA is hardcoded (not from system trust store):
```
MIIEvjA...(long hex)
CN = WhatsApp Root CA
```

The server presents a certificate chain signed by this root CA. The client validates:
1. Certificate is signed by the WhatsApp Root CA
2. Certificate is not expired
3. Server name matches

### Post-Handshake Encryption

After the Noise handshake completes:
- **Cipher:** AES-256-GCM
- **IV:** 96-bit, constructed as: `0x00...00 || counter` (big-endian 32-bit counter, incremented per frame)
- **Keys:** Derived via Noise's `Split()` operation — one key for each direction (client→server, server→client)

The counter is critical: each encrypted frame increments the send or receive counter. If counters get out of sync, decryption fails and the connection must be re-established.

### Client Identity

The client generates an **identity keypair** (Curve25519) during first registration. This identity persists across sessions and is stored in `creds.json`. The identity public key is shared with contacts during Signal Protocol key exchange.

---

## Layer 3: Binary Node Encoding

All post-handshake messages are **binary nodes** — a custom tagged tree format.

### Node Structure

```
Node = [tag, attributes, children/payload]
```

- **Tag:** String, either from the token dictionary (1-2 byte lookup) or literal (length-prefixed string)
- **Attributes:** Key-value pairs (both strings, using token dictionary for compression)
- **Children:** List of child nodes, OR a byte payload (raw data)

### Token Dictionary

WhatsApp maintains a dictionary of ~1000+ common strings. Tokens are 1-byte (indices 0-255) or 2-byte (`0x00` prefix + second byte for extended range).

Example tokens:
- `iq`, `message`, `receipt`, `notification`, `ib`
- `from`, `to`, `id`, `type`, `t` (timestamp)
- `jid`, `participant`, `notify`
- `encrypt`, `decrypt`, `media`

Literal strings not in the dictionary are encoded as: `[length_byte] [utf8_bytes]`

### JID Format (Jabber ID)

WhatsApp uses XMPP-style JIDs:
- **Personal chat:** `{phone}@s.whatsapp.net` (e.g., `31612345678@s.whatsapp.net`)
- **Group chat:** `{group_id}-{creator}@g.us` (e.g., `ABC123-31612345678@g.us`)
- **Broadcast:** `{id}@broadcast`
- **Status:** `status@broadcast`

### Top-Level Node Types

| Tag | Purpose |
|-----|---------|
| `iq` | Request/response pairs (query, result, error). Like XMPP IQ stanzas. |
| `message` | Incoming/outgoing messages |
| `receipt` | Delivery/read receipts |
| `notification` | Server notifications (group changes, contact updates) |
| `ib` | "In-Band" — server-pushed data (offline messages, etc.) |

### IQ Namespaces

IQ nodes use a `xmlns` attribute for namespacing:
- `w:sync:app:state` — CRDT state sync (contact lists, settings)
- `w:sync:app:state:contact` — specifically contact sync
- `encrypt` — pre-key management
- `w:profile` — profile picture, status, about
- `w:message` — message sending
- `w:meta` — group metadata
- `urn:xmpp:ping` — keepalive ping

---

## Layer 4: Signal Protocol (1:1 Messages)

WhatsApp uses the **Signal Protocol** for end-to-end encryption of 1:1 messages. This is the same protocol used by Signal itself, with minor implementation differences.

### X3DH Key Agreement (Extended Triple Diffie-Hellman)

When sending a first message to a new contact:

1. Fetch the contact's **pre-key bundle** from the server:
   - Identity key (long-term)
   - Signed pre-key (medium-term, rotated periodically)
   - Pre-key signature (signed by identity key)
   - One-time pre-key (single-use, from the pre-key pool)

2. Perform X3DH:
   - `DH1 = DH(our_identity, their_signed_pre_key)`
   - `DH2 = DH(our_ephemeral, their_identity)`
   - `DH3 = DH(our_ephemeral, their_signed_pre_key)`
   - `DH4 = DH(our_ephemeral, their_one_time_pre_key)` (if available)
   - `shared_secret = DH1 || DH2 || DH3 || DH4`

3. Derive a root key and chain key from the shared secret via HKDF.

### Double Ratchet

After X3DH, messages use the Double Ratchet for ongoing encryption:

- **KDF Chain Ratchet:** Each message advances the chain key, producing a new message key.
- **DH Ratchet:** When either party sends a new ephemeral key (every few messages), a new DH computation produces a new root key and chain key.

This provides:
- **Forward secrecy:** Compromising a message key doesn't compromise past messages.
- **Break-in recovery:** Compromising a long-term key only affects future messages until the next DH ratchet step.

### Encryption Format

Encrypted message payload:
```
[1 byte: version] [32 bytes: ephemeral public key] [remaining: AES-256-CBC ciphertext + HMAC]
```

- **AES-256-CBC** with PKCS#7 padding
- **HMAC-SHA256** over the ciphertext for integrity
- Keys derived from the ratchet's message key

### Multi-Device Consideration

In the multi-device protocol, each device has its own identity key and Signal sessions. Sending a 1:1 message actually means:
1. Look up all registered devices for the recipient
2. Encrypt the message separately for each device
3. Bundle all encrypted payloads into a single `message` node

This is why you see `participant` attributes in group messages — they identify which device sent the message.

---

## Layer 5: Signal Protocol (Groups)

Groups use **Sender Keys** instead of pairwise Double Ratchet sessions.

### How Sender Keys Work

1. Each group member generates a **Sender Key** per group (a symmetric key + chain state).
2. The Sender Key is encrypted individually for each group member using their 1:1 Signal session and sent via a `sender-key-distribution` message.
3. To send a group message, the sender encrypts once with their Sender Key and broadcasts.
4. Each recipient decrypts with the cached Sender Key for that sender.

This is O(n) instead of O(n²) for pairwise encryption.

### Sender Key Ratchet

Sender Keys use a symmetric ratchet (advance chain key → derive message key). Forward secrecy is limited compared to 1:1 — a compromise of a Sender Key reveals all future group messages until the key is rotated.

### Group Message Format

Group messages include:
- `to`: group JID (`{id}@g.us`)
- `participant`: sender's personal JID
- The encrypted payload uses the sender's Sender Key for that group

---

## Layer 6: Protobuf Messages

All message content is serialized as **proto3**. Baileys includes protobuf definitions that define the schema.

### Key Proto Messages

```protobuf
// Top-level wrapper
message WebMessageInfo {
    Message key = 1;           // Message identifier (remoteJid, fromMe, id)
    Message message = 2;       // The actual message content
    uint64 messageTimestamp = 3;
    string participant = 4;    // Sender (for groups)
    MessageStatus status = 5;  // pending, sent, delivered, read
    // ... many more fields
}

message Message {
    string conversation = 1;                    // Plain text
    ExtendedTextMessage extendedTextMessage = 2; // Text with preview/link
    ImageMessage imageMessage = 3;
    DocumentMessage documentMessage = 4;
    AudioMessage audioMessage = 5;
    VideoMessage videoMessage = 6;
    StickerMessage stickerMessage = 7;
    ContactMessage contactMessage = 8;
    LocationMessage locationMessage = 9;
    ReactionMessage reactionMessage = 10;
    // ... 50+ message types
    SenderKeyDistributionMessage senderKeyDistributionMessage = 50;
}
```

### Proto Files Needed

From Baileys source, the proto definitions are in `WAMessage/`:
- `WAWebProtobufsMessage.proto` — core message types
- `WAWebProtobufsE2E.proto` — encryption envelope
- `WAWebProtobufsAdv.proto` — app state sync (CRDT)
- `WAWebProtobufsSyncAction.proto` — sync actions (contact mutations, settings)
- `WAWebProtobufsCompanion.proto` — companion device management

### Porting Strategy

Options for C++ protobuf integration:
1. **protoc + libprotobuf:** Standard approach. Generate C++ headers from .proto files. Heavy dependency.
2. **protobuf-c:** Lightweight C implementation. Less boilerplate but still needs protoc.
3. ** nanopb:** Designed for embedded. Very lightweight but limited proto3 support.
4. **Manual serialization:** Given the message types we actually need (text, maybe image/document), hand-writing serialization for the ~5 message types we need is feasible and avoids a heavy dependency.

**Recommendation:** Start with manual serialization for the message types we need (text, extended text, reactions). Add protobuf if/when we need more message types. The proto definitions are large but the subset for basic messaging is small.

---

## Layer 7: Auth State

Auth state is persisted as a directory of JSON files. This is the "session" that survives reconnections.

### Directory Structure

```
auth/
├── creds.json                    # Core credentials (Noise keys, identity, signed pre-key, registration)
├── session/
│   └── {remoteJid}-{deviceId}.json  # Signal sessions (per-device Double Ratchet state)
├── pre-key/
│   └── {id}.json                 # One-time pre-keys (remaining unused)
├── sender-key/
│   └── {groupId}.json            # Group Sender Keys
├── identity-key/
│   └── {remoteJid}.json          # Trusted identity keys for contacts
└── app-state-sync/
    └── {version}.json            # CRDT snapshots (contacts, settings, etc.)
```

### creds.json Structure

```json
{
    "noiseKey": { "private": "...", "public": "..." },      // Noise protocol static keypair
    "signedIdentityKey": { "private": "...", "public": "..." }, // Signal identity keypair
    "signedPreKey": { "keyPair": {...}, "keyId": 1, "signature": "..." },
    "registrationId": 123456,                                // Random identifier
    "advSecretKey": "...",                                    // For app-state sync (CRDT)
    "processedHistoryMessages": [],                           // Deduplication
    "nextPreKeyId": 1,                                        // Counter for pre-key generation
    "firstUnuploadedPreKeyId": 1,                             // Pre-keys not yet uploaded to server
    "accountSettings": { ... }                                // User settings
}
```

### Pre-Key Pool

The client maintains a pool of **one-time pre-keys** (default: 100 in Baileys, WhatsApp requests batches of up to 812). When the pool runs low, the client:
1. Generates new pre-keys
2. Signs them with the identity key
3. Uploads them to the server via `iq` with `xmlns=encrypt`

### App State Sync (CRDT)

Contact lists, pin settings, mute settings, etc. are synced across devices using CRDTs (Conflict-free Replicated Data Types). The protocol:
1. Client requests current version from server
2. Server returns a patch (or full snapshot if version is old)
3. Client applies patch locally and updates version number
4. Mutations (e.g., adding a contact) are submitted as patches

For a basic messaging adapter, we can likely skip most of this — we only need to receive and send messages, not sync contact lists.

---

## Message Flow: Sending a Text Message

1. **Build the protobuf `Message`** with `conversation` field set to the text
2. **Encrypt:**
   - For 1:1: Look up Signal session for recipient's JID. Use Double Ratchet to encrypt.
   - For groups: Use Sender Key for the group. Encrypt once.
3. **Wrap in encryption envelope** (`EncryptMessage` proto): includes encrypted payload + device metadata
4. **Serialize to binary node:** `message` node with `to` = recipient JID, binary payload = encrypted envelope
5. **Encrypt the binary node** with the Noise session (AES-256-GCM)
6. **Send via WebSocket** with 3-byte length prefix

## Message Flow: Receiving a Text Message

1. **Decrypt Noise layer** using receive key + counter
2. **Parse binary node** — tag = `message`, extract `from`, `participant`, binary payload
3. **Decrypt Signal layer:**
   - For 1:1: Look up session for `from` JID. Use Double Ratchet to decrypt.
   - For groups: Look up Sender Key for `from` (participant). Decrypt.
4. **Parse protobuf `Message`** — extract `conversation` field
5. **Emit event** to application layer

---

## Companion Device Linking (QR Scan)

The initial linking process:

1. **Client generates Noise keypair** and requests a QR code from the server
2. **QR code contains:** server URL + client's ephemeral public key + a reference ID
3. **Phone scans QR code** — establishes a Noise connection to the same server
4. **Phone sends its identity** and credentials to the server, linking the new device
5. **Server confirms link** — both sides now share the account
6. **Client receives:** `creds.json` (identity keys, registration), initial app state (contacts)
7. **Client generates and uploads** its own pre-keys

After linking, the companion device appears in the phone's "Linked Devices" list and operates independently.

### Key Pairs Generated at Registration

| Key | Purpose | Rotation |
|-----|---------|----------|
| Noise static keypair | Transport encryption | Never (persistent identity) |
| Signal identity keypair | E2E encryption identity | Never |
| Signed pre-key | Medium-term key for X3DH | Periodically (server-initiated) |
| One-time pre-keys (pool) | Single-use for new sessions | Consumed on use, replenished |
| Sender Keys (per group) | Group encryption | On membership change or manual |

---

## Summary of C++ Dependencies Needed

| Component | What's Needed | Options |
|-----------|---------------|---------|
| WebSocket | Client connection | libwebsockets, websocketpp, Boost.Beast |
| Curve25519 | DH key agreement | libsodium (crypto_scalarmult), OpenSSL EVP |
| AES-256-GCM | Noise transport | OpenSSL EVP, libsodium |
| AES-256-CBC | Signal message encryption | OpenSSL EVP, libsodium |
| HMAC-SHA256 | Signal message integrity | OpenSSL, libsodium |
| HKDF-SHA256 | Key derivation | OpenSSL, libsodium, or manual HKDF |
| Protobuf | Message serialization | protoc + libprotobuf, or manual for subset |
| Base64 | Credential storage | openssl BIO, or manual |

**Minimal dependency stack:** libsodium (covers all crypto) + one WebSocket library. This is preferable to OpenSSL for code clarity and auditability.

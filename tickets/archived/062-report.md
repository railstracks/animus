# Ticket 062 — WhatsApp Adapter: Session Report

**Date:** 2026-05-29 / 2026-05-30 / 2026-05-31 / 2026-06-03 / 2026-06-04 / 2026-06-05
**Author:** Kestrel
**Status:** ✅ BIDIRECTIONAL MESSAGING CONFIRMED WORKING (June 5, 16:11 CEST)

---

## What Was Built

### Phase 1: Transport + Pairing (committed `069a744`)
**13 files, ~2,820 lines**

The full WhatsApp Linked Devices transport stack:

| Component | Files | Lines | What |
|-----------|-------|-------|------|
| WABinary codec | `WABinary.h/cpp` | 492 | Binary node encoder/decoder — tagged tree format with token dictionary, JID addressing, attribute handling |
| WATokenMap | `WATokenMap.h` | 768 | Complete WhatsApp token dictionary (491 single-byte tokens + 4 double-byte dictionaries × 256 tokens), tag dictionary, position maps for reverse lookup |
| WAProto | `WAProto.h` | 298 | Protobuf encode/decode for Message, MessageKey, WebMessageInfo, HandshakeMessage, ClientPayload, HistorySyncMsg. Hand-rolled binary protobuf (no .proto file, no protoc dependency) |
| WhatsAppNoise | `WhatsAppNoise.h/cpp` | 430 | Noise_XX_25519_AESGCM_SHA256 handshake — X25519 DH × 4 ops, AES-256-GCM encrypt/decrypt, HKDF key derivation, counter-based IV, frame encode/decode with WA header |
| WhatsAppTransport | `WhatsAppTransport.h/cpp` | ~200 | Raw TCP + OpenSSL TLS + manual WebSocket framing — bypasses Drogon's WebSocketClient entirely |
| WhatsAppGatewayLoop | `WhatsAppGatewayLoop.cpp` | 766+ | Full connection lifecycle — custom transport connect, Noise handshake, QR pairing flow, session resume, keep-alive, reconnection, auth state persistence, post-login state machine, inbound message dispatch |
| ChannelManager integration | `ChannelManager.h/cpp` | ~20 | WhatsApp channel registration, poller setup, WhatsAppGatewayLoop entry point |

### Phase 2: Messaging + Signal Routing (committed `b5a1479`)
**~530 lines added on top of Phase 1**

- **Protobuf message encode/decode** for full Message, WebMessageInfo, HistorySyncMsg
- **Signal Protocol message routing**: encrypt/decrypt using `animus::signal` (ticket 063)
- **Inbound message dispatch**: binary node → decrypt → proto decode → DispatchToSession
- **Outbound message queue**: build relay stanzas with per-device encryption
- **Conn node handler**: processes connection success, server properties, passive flag

### Phase 3: Post-Login State Machine (June 3)
**Sequential post-login handshake matching Baileys' exact timing**

- **Pre-key count query** — asks server how many pre-keys it has (acts as "ready" signal)
- **Bare presence** — `<presence />` sent early
- **Passive IQ** — `<iq xmlns="passive" type="set"><active/></iq>` after count response
- **Digest key bundle** — `<iq xmlns="encrypt" type="get"><digest/></iq>` after passive response
- **Full presence** — `<presence name="Kestrel" type="available"/>` after digest
- **Abt, blocklist, privacy queries** — after full connection established
- **Offline batch** — `<ib><offline_batch count="100"/></ib>` on `offline_preview`
- **Notification acking** — `<ack>` tag (not `<receipt>`) matching Baileys

### Phase 4: Signal Protocol Integration (June 4–5)
**Full X3DH + Double Ratchet implementation for WhatsApp message encryption/decryption**

See ticket 063 for the library details. The WhatsApp-specific integration involved:

#### Incoming (pkmsg + msg) — CONFIRMED WORKING June 5, ~08:00 CEST
- Wire format parsing: `[version(1)][protobuf][MAC(8)]`
- PreKey message (pkmsg) handling: X3DH key agreement → session initialization
- Regular message (msg) handling: Double Ratchet decrypt with skipped key lookup
- MAC verification: `HMAC-SHA256(macKey, senderIdentity(33) || receiverIdentity(33) || version(1) || protobuf)[:8]`
- Plaintext unpadding: `unpadRandomMax16()` — strip PKCS#7-style padding
- Three crypto bugs fixed (HKDF info length, ratchet KDF salt/IKM, explicit zeros salt)

#### Outgoing (msg) — CONFIRMED WORKING June 5, 16:11 CEST
- Wire format assembly: `[version(0x33)][protobuf SignalMessage][8-byte MAC]`
- AES-256-CBC encryption (NOT AES-256-GCM as previously implemented)
- WhisperMessageKeys derivation (cipher key + MAC key + IV via HKDF-Expand)
- SignalMessage protobuf encoding (ratchetKey, counter, previousCounter, ciphertext)
- MAC computation: `HMAC-SHA256(macKey, ourIdentity(33) || theirIdentity(33) || version(1) || protobuf)[:8]`
- Plaintext padding: `padRandomMax16()` before encryption (PKCS#7-style, 1-15 bytes)

### Transport Hardening (committed `7125e59`)
- DH4 (4th Diffie-Hellman operation) added to Noise handshake
- Reconnection logic with exponential backoff
- Keep-alive ping/pong timer
- Auth state persistence to JSON
- Conn binary node handler

### Custom Transport (May 31, committed `fb97450`)
- **WhatsAppTransport** — raw TCP socket + OpenSSL TLS + HTTP WebSocket upgrade + WS frame read/write
- Completely bypasses Drogon's WebSocketClient
- Non-blocking read loop with frame parsing
- TCP_NODELAY enabled

---

## Live Testing Progress

### Issues 1–23 (May 29–June 3)
*(See previous version of this report for Issues 1–23. All fixed.)*

### Issue 24: HKDF Info String Length (fixed June 4)
**Symptom:** MAC verification fails on incoming messages. Decryption produces garbage.
**Root cause:** `"WhisperText"` info string was passed as 12 bytes (including C null terminator) instead of 11. HKDF with 12-byte info produces completely wrong keys from the very first step (X3DH root key).
**Fix:** Changed info length from 12 to 11 in both X3DH HKDF calls. Verified with standalone C++ test program + Node.js reference script.

### Issue 25: Ratchet KDF Salt/IKM Reversed (fixed June 4)
**Symptom:** Even with HKDF info length fixed, MAC verification still fails.
**Root cause:** `kdf_rk()` function in `DoubleRatchet.cpp` concatenated `root_key + dh_output` as IKM and passed `nullptr` for salt. The Signal spec uses `root_key` as HKDF **salt** and `dh_output` as **IKM** — they should not be concatenated.
**Fix:** Pass `dh_output` as IKM and `root_key` as salt. Also use explicit zeros(32) instead of nullptr for HKDF salt default.

### Issue 26: HKDF Salt Default (fixed June 4)
**Symptom:** N/A — discovered during issue 25 fix.
**Root cause:** Some HKDF calls used `nullptr` for salt, which in OpenSSL means "use default salt". The Signal Protocol explicitly uses a 32-byte zero salt.
**Fix:** Use explicit `std::vector<uint8_t>(32, 0)` instead of `nullptr`.

### Issue 27: Outgoing Encryption Used AES-256-GCM (fixed June 5)
**Symptom:** Recipient can't decrypt outgoing messages. Server returns `retry` receipt with `child: registration`.
**Root cause:** `ratchet_encrypt()` used AES-256-GCM, but Signal Protocol uses AES-256-CBC with separate HMAC-SHA256 MAC. The decrypt path was already using CBC correctly.
**Fix:** Rewrote `encrypt_1to1()` to use AES-256-CBC with WhisperMessageKeys derivation (cipher key + MAC key + IV via HKDF-Expand), matching the decrypt path.

### Issue 28: Raw Ciphertext Instead of Wire Format (fixed June 5)
**Symptom:** Same as issue 27 — recipient can't decrypt.
**Root cause:** The enc node content was set to raw AES-GCM ciphertext, but WhatsApp expects `[version_byte(0x33)][protobuf SignalMessage][8-byte MAC]`. Same wire format we successfully parse on receive.
**Fix:** Built proper wire format assembly: derive message key → derive whisper message keys (CBC key, MAC key, IV) → AES-256-CBC encrypt → build protobuf SignalMessage → compute MAC → assemble `[version][proto][MAC]`.

### Issue 29: Missing `to` Wrapper Node (fixed June 5)
**Symptom:** Server returns `ack` but recipient can't decrypt. Some clients show "waiting for message".
**Root cause:** Baileys wraps the `enc` node inside a `to(jid=deviceJid)` node. Our code put `enc` directly inside `message`.
**Fix:** Added `to` wrapper node around `enc` inside the `message` node.

### Issue 30: Missing `participants` Wrapper + Base JID (fixed June 5)
**Symptom:** Server returns `error=479` (smax-invalid). Recipient never receives message.
**Root cause:** 1:1 messages require a `participants` node wrapping the `to` nodes. Also, the `message` node's `to` attr should be the base JID (no device), while `to` child's `jid` attr should be device-specific.
**Fix:** Wrapped `to` nodes inside `participants` node. Set `message.to` to base JID, `to.jid` to device JID. Added `lastDeviceJid` map for device resolution during encryption.

### Issue 31: Missing Random Padding on Outgoing Plaintext (fixed June 5)
**Symptom:** Recipient can't decrypt outgoing messages. `retry` receipt with `registration` child.
**Root cause:** WhatsApp requires `writeRandomPadMax16` padding (1-15 bytes, PKCS#7-style) on the plaintext before encryption. Our decode path already stripped this padding (`unpadRandomMax16`), but the encode path was missing it.
**Fix:** Added `padRandomMax16()` call in `encodeTextMessage()`, matching Baileys' implementation.

### Issue 32: Session Routing Split by Device ID (fixed June 5)
**Symptom:** Desktop (`:26@lid`) and mobile (`@lid`) messages from the same person create separate agent sessions.
**Root cause:** The routing key used the full JID including device number, creating separate sessions for the same person.
**Fix:** Strip device number from 1:1 JIDs when building the routing key. Preserve device JID for Signal encryption via `lastDeviceJid` map.

### ✅ Breakthrough: Bidirectional Messaging (June 5, 16:11 CEST)
After fixing all eight bugs (3 incoming + 5 outgoing), WhatsApp Signal Protocol messaging is fully operational in both directions. Melvin confirmed messages arrive successfully on his phone.

---

## Commits

### June 4 (Signal Protocol crypto fixes)
```
(test_hkdf.cpp, test_x3dh.js — standalone verification scripts)
(fixes to SignalCrypto.cpp, DoubleRatchet.cpp — HKDF info length, ratchet KDF, explicit zeros salt)
```

### June 5 (Outgoing message wire format + structure)
```
feat: outgoing Signal messages use correct wire format (AES-256-CBC + protobuf + MAC)
fix: Writer returns buf, not bytes()
fix: wrap enc node in 'to' wrapper for outgoing WhatsApp messages
fix: outgoing WhatsApp messages need 'participants' wrapper + base JID
fix: resolve device JID for outgoing WhatsApp encryption
fix: add baseJid to OutboundMessage in header, remove duplicate struct
fix: capture lastDeviceJid and lastDeviceMutex in onFrame lambda
fix: strip device ID from WhatsApp 1:1 routing key
fix: add random padding to outgoing WhatsApp message plaintext
fix: use deterministic padding (no random_device needed)
fix: move pad/unpad functions before encodeTextMessage for compilation order
```

### June 5 (earlier — incoming crypto fixes)
```
fix: HKDF info string length 12→11 (WhisperText null terminator)
fix: ratchet KDF uses root_key as salt, dh_output as IKM (not concatenated)
fix: explicit zeros(32) for HKDF salt instead of nullptr
```

*(May 29–June 3 commits listed in previous version of this report)*

---

## Hard-Won Lessons

1–30: *(See previous version of this report for lessons 1–30.)*

31. **HKDF info string length: C string null terminator is not part of the info.** `"WhisperText"` is 11 bytes, not 12. Passing `sizeof("WhisperText")` includes the null terminator and produces completely wrong keys. This is a classic C/C++ pitfall.

32. **Signal Protocol root key is HKDF salt, not part of IKM.** The `kdf_rk()` function concatenated `root_key + dh_output` as IKM. The correct formulation is `HKDF(IKM=dh_output, salt=root_key, info="WhisperRatchet", 64)`. Concatenating produces completely different keys.

33. **AES-256-CBC, not AES-256-GCM, for Signal messages.** The Signal Protocol uses AES-256-CBC with a separate HMAC-SHA256 MAC for message encryption. AES-256-GCM is used for the Noise transport layer, not for Signal messages.

34. **Wire format is [version][protobuf][MAC], not raw ciphertext.** The `enc` node content must be the full serialized Signal message, not just the encrypted payload. This includes a version byte (0x33), the protobuf-encoded SignalMessage, and an 8-byte truncated HMAC-SHA256 MAC.

35. **MAC key order: sender identity first.** For encrypt (we are sender): `MAC = HMAC(macKey, ourIdentity(33) || theirIdentity(33) || version(1) || protobuf)`. For decrypt (they are sender): `MAC = HMAC(macKey, theirIdentity(33) || ourIdentity(33) || version(1) || protobuf)`. The sender always comes first.

36. **WhatsApp message stanza requires `participants` wrapper.** 1:1 messages must be: `message → participants → to(jid=deviceJid) → enc`. Without `participants`, the server returns error 479 (smax-invalid).

37. **Message `to` attr uses base JID; `enc` node's `to` uses device JID.** The `message` node's `to` attribute should be the base JID (no device number), while the `to` child node's `jid` attribute should be the device-specific JID for routing the encrypted payload to the correct device.

38. **WhatsApp plaintext requires random padding before encryption.** `writeRandomPadMax16` (1-15 bytes, PKCS#7-style) must be applied to the plaintext protobuf before encryption. Without it, the recipient's decryption produces garbage because their `unpadRandomMax16` expects the padding.

39. **Device JID resolution is essential for Signal encryption.** Desktop sends as `:26@lid`, mobile as `@lid`. Session routing should use the base JID (no device), but Signal Protocol sessions are per-device. The `lastDeviceJid` map preserves the device-specific JID for encryption.

40. **Error 479 = smax-invalid = structural stanza error.** Not a crypto error — the server rejected the message because the XML/binary node structure was invalid. This was caused by missing `participants` wrapper.

---

## Remaining Work

### Immediate (message flow polish)
- [ ] **Clean up debug logging** — Remove hex dumps, add proper structured logging
- [ ] **Delivery receipt ack** — Send `<receipt type="read">` after successful message processing
- [ ] **Outgoing pkmsg (session initiator)** — Currently we only send `msg` type. If we need to initiate a new session, we need to send `pkmsg` with PreKeySignalMessage wrapping
- [ ] **Multi-device message sending** — Query device list, encrypt for each device

### Connection resilience
- [ ] **Session resume on reconnect** — auth state → skip QR, send ClientInit directly
- [ ] **Auth state persistence round-trip test** — restart → resume
- [ ] **Respond to `urn:xmpp:ping` IQs** (separate from keepalive ping)
- [ ] **Stream error handling** — 515 = reconnect, 401/403 = clear auth and re-pair
- [ ] **Handle `retry` receipts properly** — Re-establish session and resend with fresh pkmsg

### Phase 5: Full Channel Integration
- [ ] DispatchToSession routing (peer for DMs, post for groups) ✅ (done)
- [ ] SendReply (encrypt → relay) ✅ (done)
- [ ] Admin UI WhatsApp form section

### Phase 6: Lua Social Adapter
- [ ] `whatsapp.lua` — social tool actions

### Cleanup
- [ ] Replace inline Signal crypto in WhatsAppNoise with `animus::signal` library calls
- [ ] Implement SQLiteSignalStore for persistent session storage
- [ ] Proper DNS resolution (remove `/etc/hosts` workaround)
- [ ] Pre-key upload (signed pre-key + one-time pre-keys)
- [ ] Pre-key rotation and replenishment
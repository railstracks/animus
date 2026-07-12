# WhatsApp Implementation Notes

Practical guide for porting the Baileys protocol to C++ within the Animus build system.

## Build System Integration

Animus uses CMake. The WhatsApp adapter adds:

```cmake
# In the existing CMakeLists.txt
option(ENABLE_WHATSAPP "Build WhatsApp adapter" ON)

if(ENABLE_WHATSAPP)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SODIUM REQUIRED libsodium)
    pkg_check_modules(LWS libwebsockets)  # or websocketpp

    target_sources(animus-daemon PRIVATE
        src/social/whatsapp/WhatsAppAdapter.cpp
        src/social/whatsapp/WhatsAppConnection.cpp
        src/social/whatsapp/BinaryNodeCodec.cpp
        src/social/whatsapp/SignalSession.cpp
        src/social/whatsapp/SenderKeyStore.cpp
        src/social/whatsapp/WhatsAppEventConsumer.cpp
        src/social/whatsapp/NoiseHandshake.cpp
        src/social/whatsapp/ProtoCodec.cpp         # Manual proto serialization
    )

    target_link_libraries(animus-daemon PRIVATE ${SODIUM_LIBRARIES} ${LWS_LIBRARIES})
endif()
```

## C/C++ Dependencies

### Required

| Library | Version | Purpose | Notes |
|---------|---------|---------|-------|
| **libsodium** | ≥ 1.0.18 | All crypto (Curve25519, AES-GCM, AES-CBC, HMAC, HKDF) | One dependency covers all crypto needs. Preferred over OpenSSL for clarity and auditability. |
| **WebSocket library** | — | Transport | See comparison below |
| **SQLite3** | — | Auth state persistence | Already linked by Animus |

### WebSocket Library Comparison

| Library | Pros | Cons |
|---------|------|------|
| **libwebsockets** | C, lightweight, well-maintained, TLS support, event-loop agnostic | C API, verbose |
| **websocketpp** | C++ header-only, Asio-based, well-documented | Header-only = slower builds, Asio dependency |
| **Boost.Beast** | Part of Boost, excellent quality, WebSocket + HTTP | Heavy Boost dependency (Animus doesn't currently use Boost) |

**Recommendation:** `libwebsockets` — lightweight, no Boost dependency, production-grade.

### NOT Required

| What | Why Not |
|------|---------|
| OpenSSL | libsodium covers all crypto. Avoid dual-crypto-library confusion. |
| libprotobuf | Manual serialization for the small subset of proto messages we need. |
| Node.js | Explicitly rejected. Pure C++ implementation. |

## Crypto Mapping (Baileys → libsodium)

| Baileys/Protocol | libsodium Function |
|-----------------|-------------------|
| Curve25519 DH (`crypto_scalarmult_curve25519`) | `crypto_scalarmult()` |
| X25519 key generation | `crypto_box_keypair()` then convert with `crypto_sign_ed25519_pk_to_curve25519()` if needed |
| AES-256-GCM encrypt | `crypto_aead_aes256gcm_encrypt()` (requires AESNI hardware) OR use `crypto_secretbox()` (XSalsa20-Poly1305 — different but same security level) |
| AES-256-CBC encrypt | No libsodium support — use OpenSSL EVP *or* implement manually with libsodium's `crypto_stream_aes256ctr` + CBC mode wrapper |
| HMAC-SHA256 | `crypto_auth_hmacsha256()` |
| HKDF-SHA256 | Not in libsodium — implement manually (RFC 5869 is ~20 lines) |
| SHA-256 | `crypto_hash_sha256()` |

### AES-256-GCM Caveat

libsodium's `crypto_aead_aes256gcm_*` requires AES-NI hardware support. Most modern x86_64 and ARMv8 CPUs have this. For older hardware or certain VPS instances, we'd need a software fallback.

**Alternative:** Use libsodium's default authenticated encryption (`crypto_secretbox` / `crypto_aead_xchacha20poly1305_ietf`) which doesn't require AES-NI. But this changes the Noise cipher suite and would be incompatible with WhatsApp's server.

**Pragmatic approach:** Use libsodium's AES-256-GCM on platforms that support it. Detect at runtime with `crypto_aead_aes256gcm_is_available()`. If not available, fall back to a minimal AES-GCM implementation (or require AES-NI as a system requirement).

### AES-256-CBC

libsodium deliberately does not include CBC mode. Options:
1. **Minimal AES-CBC implementation** — AES-256 in CBC mode is straightforward. The block cipher is the same; only the chaining mode differs. ~50 lines on top of libsodium's AES core.
2. **Use OpenSSL just for CBC** — Lightweight fallback but adds a second crypto dependency.
3. **Extract AES from libsodium internals** — libsodium has AES-256 implementation internally but doesn't expose CBC mode.

**Recommendation:** Option 1 — implement AES-256-CBC as a thin wrapper using libsodium's underlying AES block cipher. The Signal Protocol uses it for message encryption, and it's a well-understood construction.

## Token Dictionary

Baileys maintains two arrays of tokens (single-byte and double-byte) in `Defaults.ts`. These are protocol constants — they don't change at runtime.

### Porting Strategy

Generate a compile-time lookup table:

```cpp
// whatsapp_tokens.h
// Auto-generated from Baileys Defaults.ts

constexpr const char* SINGLE_BYTE_TOKENS[] = {
    "", "", "", "", "iq", "pair-device", // indices 0-5
    "pair-success", "stream:error",      // etc.
    // ... ~256 entries
};

constexpr const char* DOUBLE_BYTE_TOKENS[] = {
    "", "not-authorized", "item-not-found", // indices 0-2
    // ... ~768 entries
};

// Reverse lookup for encoding: string → token index
// Use std::unordered_map<string, uint16_t> built once at startup
```

Total size: ~1000 string entries. Minimal memory impact. The reverse map (for encoding) should be built once at startup and shared across all WhatsApp instances.

## Protobuf Serialization (Manual)

For Phase 1, we only need to serialize/deserialize a handful of message types. Here's the proto subset:

### Required Proto Messages

```
Message.conversation                    (field 1, string)
Message.extendedTextMessage             (field 2, EmbeddedMessage)
  .text                                 (field 1, string)
  .matchedText                          (field 4, string — URL)
  .canonicalUrl                         (field 5, string)
  .title                                (field 6, string)
  .description                          (field 7, string)
Message.reactionMessage                 (field N, ReactionMessage)
  .key                                  (field 1, MessageKey)
  .text                                 (field 2, string — emoji)
  .senderTimestampMs                    (field 3, int64)
Message.senderKeyDistributionMessage    (field 50, bytes)

WebMessageInfo.key                      (field 1, MessageKey)
  .remoteJid                            (field 1, string)
  .fromMe                               (field 2, bool)
  .id                                   (field 3, string)
WebMessageInfo.message                  (field 2, Message)
WebMessageInfo.messageTimestamp         (field 3, int64)
WebMessageInfo.status                   (field 5, enum)

MessageKey.remoteJid                    (field 1, string)
MessageKey.fromMe                       (field 2, bool)
MessageKey.id                           (field 3, string)
```

### Manual Protobuf Codec

Proto encoding is straightforward for these types:
- Varint fields: `field_number << 3 | wire_type`
- Length-delimited: varint length + bytes
- No packed repeated fields needed for this subset

A minimal hand-rolled encoder/decoder for this schema is ~300-400 lines. Much lighter than pulling in protobuf.

```cpp
// Example: encode a text message
std::vector<uint8_t> encode_text_message(const std::string& text) {
    std::vector<uint8_t> buf;
    // field 1 (conversation) = string, wire type 2
    encode_field(buf, 1, WireType::LENGTH_DELIMITED, text);
    return buf;
}
```

## Noise Handshake Implementation

The Noise_XX_25519_AESGCM_SHA256 handshake follows the standard Noise Framework specification:

### State Machine

```
INIT → wrote_e (send ephemeral pubkey)
     → read_ees (receive ephemeral + static + DH computations + cert validation)
     → wrote_sse (send static + DH computations)
     → TRANSPORT (AES-256-GCM with derived keys)
```

### Certificate Validation

WhatsApp's root CA is hardcoded. After the server sends its certificate:
1. Parse the cert chain (X.509 DER)
2. Verify chain ends at WhatsApp Root CA
3. Verify leaf cert's CN matches expected server name
4. Extract server's static public key from the cert

For X.509 parsing, options:
- **mbedTLS** — lightweight, C, designed for embedded
- **OpenSSL X509** — heavy but comprehensive
- **Hand-rolled** — the cert chain is small and fixed format. Possible but fragile.

**Recommendation:** mbedTLS for cert parsing only (~50KB). Or just verify the cert fingerprint (hash of the expected cert) — simpler and sufficient since the root CA is hardcoded.

## Development Phases

### Phase 1: Connection & Transport
- [ ] WebSocket connection + reconnection
- [ ] Noise handshake (XX + cert validation)
- [ ] Binary node encode/decode
- [ ] Frame-level encrypt/decrypt
- [ ] Auth state persistence (SQLite tables)
- [ ] QR code linking flow
- **Deliverable:** Can connect to WhatsApp, maintain session, survive reconnection

### Phase 2: 1:1 Messaging
- [ ] Signal Protocol X3DH + Double Ratchet
- [ ] Pre-key generation, upload, consumption
- [ ] Send/receive text messages
- [ ] Read receipts
- [ ] Typing indicators
- [ ] Integration with SocialEventPoller
- [ ] Integration with SendAutoReply
- **Deliverable:** Can send and receive 1:1 text messages end-to-end

### Phase 3: Group Messaging
- [ ] Sender Key generation, distribution
- [ ] Group message encryption/decryption
- [ ] Group metadata (join/leave, subject, description)
- **Deliverable:** Full group messaging support

### Phase 4: Polish
- [ ] Media messages (images, documents)
- [ ] Reactions
- [ ] URL previews
- [ ] Presence (online/offline)
- [ ] Admin API endpoints
- [ ] Multi-tenant support (multiple WhatsApp accounts)

## Testing Strategy

### Unit Tests
- Binary node encode/decode roundtrip
- Noise handshake with known test vectors
- Signal Protocol encrypt/decrypt with test data
- Protobuf encode/decode for each message type
- Auth state serialization

### Integration Tests
- Connect to WhatsApp server (test account)
- Link via QR code
- Send/receive messages to known test number
- Verify encryption matches Baileys output for same inputs

### Protocol Conformance
- Compare our binary node output byte-for-byte with Baileys output
- Compare encrypted payloads for known keys
- Verify token dictionary completeness

## File Organization

```
src/social/whatsapp/
├── WhatsAppAdapter.h/.cpp          # High-level adapter (public interface)
├── WhatsAppConnection.h/.cpp       # WebSocket + Noise transport
├── WhatsAppEventConsumer.h/.cpp    # Bridge to SocialEventPoller
├── BinaryNodeCodec.h/.cpp          # Node encoding/decoding
├── NoiseHandshake.h/.cpp           # Noise_XX implementation
├── SignalSession.h/.cpp            # Double Ratchet + X3DH
├── SenderKeyStore.h/.cpp           # Group Sender Keys
├── ProtoCodec.h/.cpp               # Manual protobuf serialization
├── TokenDictionary.h/.cpp          # Compile-time token lookup
├── AuthState.h/.cpp                # SQLite persistence for auth state
└── WhatsAppConstants.h             # Protocol constants, JID formats
```

Estimated: ~3000-4000 lines of C++ for Phase 1+2 (connection + 1:1 messaging).

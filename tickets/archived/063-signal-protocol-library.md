# Ticket 063 — Shared Signal Protocol Library (`animus::signal`)

**Created:** 2026-05-29
**Status:** Planning
**Priority:** High
**Serves:** 062 (WhatsApp Adapter), future Signal channel provider

## Summary

Implement the Signal Protocol as a standalone shared library within Animus (`animus::signal`), providing E2E encryption primitives consumed by both the WhatsApp adapter and a future Signal channel provider. The protocol layers are identical between both services — only the transport and envelope formats differ.

## Motivation

Both WhatsApp and Signal use the same underlying cryptographic protocol:

- **X3DH** (Extended Triple Diffie-Hellman) for initial key agreement
- **Double Ratchet** for forward-secret 1:1 message encryption
- **Sender Keys** for efficient group message encryption
- **Pre-key management** (signed pre-keys, one-time pre-keys, key rotation)

Building this once as a shared library avoids duplication, ensures both providers have audited-quality crypto, and gives us a single place to fix cryptographic bugs.

### Why not just use libsignal C FFI?

This is the preferred long-term path. `libsignal` (Signal's official Rust library) has C FFI bindings that provide all required operations. However:

1. It introduces a Rust toolchain dependency into the Animus build
2. The C FFI surface needs mapping and testing
3. WhatsApp uses a slightly modified variant (AES-256-CBC + HMAC rather than Signal's standard AES-GCM in some paths)

**Phased approach:** Start with clean C++ interfaces + stub/mock crypto (so WhatsApp Phase 2 can proceed immediately), then integrate `libsignal` C FFI for production-grade crypto. The interface doesn't change — only the implementation behind it.

## Architecture

```
animus::signal (namespace, static library)
│
├── Core Types
│   ├── signal_keypair          — Curve25519 public/private keypair
│   ├── signal_identity_key     — Long-term identity keypair
│   ├── signal_pre_key          — One-time pre-key (id + keypair)
│   ├── signal_signed_pre_key   — Signed pre-key (id + keypair + signature)
│   ├── signal_session_record   — Ratchet state for a remote device
│   └── signal_address          — (name, device_id) tuple
│
├── Key Agreement (X3DH)
│   └── x3dh_key_agreement      — Compute shared secret from key bundles
│
├── Double Ratchet (1:1)
│   ├── ratchet_init            — Initialize ratchet from X3DH output
│   ├── ratchet_encrypt         — Encrypt plaintext → (header, ciphertext)
│   └── ratchet_decrypt         — Decrypt (header, ciphertext) → plaintext
│
├── Sender Keys (Groups)
│   ├── sender_key_create       — Generate sender key for a group
│   ├── sender_key_encrypt      — Encrypt with sender key (single encryption)
│   ├── sender_key_decrypt      — Decrypt with distributed sender key
│   └── sender_key_distribution — Serialize for 1:1 Signal session transport
│
├── Session Store (Interface)
│   ├── load_session(address)           → session_record
│   ├── store_session(address, record)  → void
│   ├── contains_session(address)       → bool
│   ├── delete_session(address)         → void
│   └── get_sub_device_sessions(name)   → vector<device_id>
│
├── Pre-Key Store (Interface)
│   ├── load_pre_key(id)        → pre_key
│   ├── store_pre_key(pre_key)  → void
│   ├── contains_pre_key(id)    → bool
│   ├── remove_pre_key(id)      → void
│   └── next_pre_key_id()       → uint32_t
│
├── Identity Key Store (Interface)
│   ├── get_identity_key_pair()         → keypair
│   ├── get_local_registration_id()     → uint32_t
│   ├── save_identity(address, key)     → bool (was_new)
│   ├── is_trusted_identity(address, key) → bool
│   └── load_identity(address)          → keypair
│
└── Sender Key Store (Interface)
    ├── load_sender_key(address)      → sender_key_record
    ├── store_sender_key(address, record) → void
    └── contains_sender_key(address)  → bool
```

### Storage Backends
Store interfaces are abstract. Concrete implementations:
- **SQLiteStore** — production (persisted in Animus SQLite alongside channel state)
- **InMemoryStore** — testing and development

### Implementation Strategy

| Phase | What | Backend |
|-------|------|---------|
| **063-A: Interfaces + Stubs** | All types and store interfaces. Crypto operations log and return mock data. | InMemoryStore |
| **063-B: libsignal C FFI** | Compile `libsignal` with C FFI. Implement all crypto operations via FFI calls. | SQLiteStore |
| **063-C: WhatsApp Integration** | Merge into WhatsApp adapter (replace stubs in ticket 062 Phase 2). | SQLiteStore |
| **063-D: WhatsApp Hardening** | Pre-key lifecycle (upload, rotation, replenishment). LID mapping. | SQLiteStore |

Phase 063-A unblocks WhatsApp Phase 2 (ticket 062) immediately. Phase 063-B is the real crypto work.

## File Layout

```
include/animus_kernel/signal/
├── SignalTypes.h           — Core types (keypair, address, pre-keys, etc.)
├── SignalStore.h           — Abstract store interfaces
├── SignalCrypto.h          — Encrypt/decrypt/key agreement API
├── DoubleRatchet.h         — Ratchet state machine (implementation detail)
├── SenderKeys.h            — Group encryption (implementation detail)
└── SignalSessionManager.h  — High-level session lifecycle management

src/kernel/signal/
├── SignalTypes.cpp
├── DoubleRatchet.cpp
├── SenderKeys.cpp
├── SignalCrypto.cpp        — Dispatches to DoubleRatchet or SenderKeys
├── SignalSessionManager.cpp
└── stores/
    ├── InMemorySignalStore.cpp
    └── SQLiteSignalStore.cpp
```

## Crypto Primitives Required

| Primitive | OpenSSL Function | Notes |
|-----------|-----------------|-------|
| X25519 key agreement | `EVP_PKEY_derive` with `EVP_PKEY_X25519` | Already used in Noise handshake |
| Ed25519 signing | `EVP_DigestSign` with `EVP_PKEY_ED25519` | For signed pre-keys |
| AES-256-GCM | `EVP_AEAD_CTX_seal/open` or `EVP_EncryptInit_ex` | Double Ratchet message encryption |
| AES-256-CBC + HMAC-SHA256 | `EVP_EncryptInit_ex` + `EVP_DigestSign` | WhatsApp variant of message encryption |
| HMAC-SHA256 | `EVP_DigestSign` with `EVP_sha256` | Message authentication, key derivation |
| HKDF-SHA256 | `EVP_PKEY_CTX_set_hkdf_*` | Key derivation (already used in Noise) |
| ChaCha20-Poly1305 | Optional — standard Signal uses this | libsignal provides; OpenSSL 1.1+ supports |

All primitives are available in OpenSSL 3.x (already linked in Animus build).

## Interface Contract

### SignalCrypto (high-level API)

```cpp
namespace animus::signal {

// Initialize a new session from a received pre-key bundle
Result<void> initialize_session(
    ISessionStore& sessions,
    IIdentityKeyStore& identity,
    const SignalAddress& remote,
    const PreKeyBundle& bundle
);

// Encrypt a message (1:1)
Result<EncryptedMessage> encrypt(
    ISessionStore& sessions,
    IIdentityKeyStore& identity,
    const SignalAddress& remote,
    std::string_view plaintext
);

// Decrypt a message (1:1)
Result<std::string> decrypt(
    ISessionStore& sessions,
    IIdentityKeyStore& identity,
    const SignalAddress& remote,
    const EncryptedMessage& message
);

// Encrypt a group message
Result<GroupEncryptedMessage> group_encrypt(
    ISenderKeyStore& sender_keys,
    const SignalAddress& sender,   // our address
    const std::string& group_id,
    std::string_view plaintext
);

// Decrypt a group message
Result<std::string> group_decrypt(
    ISenderKeyStore& sender_keys,
    const SignalAddress& sender,   // remote sender
    const std::string& group_id,
    const GroupEncryptedMessage& message
);

// Generate a batch of one-time pre-keys
std::vector<SignalPreKey> generate_pre_keys(uint32_t start_id, size_t count);

// Generate a new signed pre-key
SignalSignedPreKey generate_signed_pre_key(uint32_t id, const SignalKeypair& identity);

// Serialize/deserialize for transport
std::vector<uint8_t> serialize_pre_key_bundle(const PreKeyBundle& bundle);
Result<PreKeyBundle> deserialize_pre_key_bundle(const uint8_t* data, size_t len);

} // namespace animus::signal
```

## Acceptance Criteria

### Phase 063-A (Interfaces + Stubs)
- [ ] All types defined: keypair, address, pre-keys, session record, encrypted message types
- [ ] All four store interfaces defined with InMemoryStore implementation
- [ ] `SignalCrypto` API defined with stub implementations (log + return mock data)
- [ ] Builds cleanly in Animus CMake
- [ ] Unit test skeleton: store CRUD operations pass

### Phase 063-B (libsignal C FFI)
- [ ] `libsignal` compiles with C FFI on workstation
- [ ] All crypto operations implemented via FFI
- [ ] X3DH key agreement produces correct shared secrets (validated against test vectors)
- [ ] Double Ratchet encrypt/decrypt round-trip passes
- [ ] Sender Key encrypt/decrypt round-trip passes
- [ ] Pre-key generation produces valid key bundles
- [ ] Session persistence to SQLite survives restart
- [ ] Full unit test suite (≥20 tests) passing

### Phase 063-C (WhatsApp Integration)
- [ ] WhatsApp adapter uses `animus::signal` instead of stubs
- [ ] Messages encrypt and decrypt correctly with real WhatsApp sessions
- [ ] Pre-key upload succeeds on WhatsApp servers
- [ ] Group messages encrypt with Sender Keys

### Phase 063-D (Hardening)
- [ ] Pre-key rotation on schedule (signed pre-key every 48-72h)
- [ ] One-time pre-key replenishment (upload when server count < 5)
- [ ] LID mapping support (phone JID → LID JID)
- [ ] Multi-device session management

## Risks

### libsignal C FFI Surface
- **Risk:** C API may not expose all operations Baileys uses
- **Mitigation:** Map required operations against `libsignal-protocol-c` headers before starting 063-B. If gaps exist, implement those primitives directly using OpenSSL.

### WhatsApp Crypto Variant
- **Risk:** WhatsApp uses AES-256-CBC + HMAC in some paths where standard Signal uses AES-GCM
- **Mitigation:** The WhatsApp-specific variant is encapsulated in the message framing code (ticket 062), not in the shared Signal library. The library provides standard Signal Protocol; WhatsApp adaptations happen at the integration layer.

### Build Complexity
- **Risk:** Adding Rust toolchain to C++ build increases CI complexity
- **Mitigation:** libsignal produces a static `.a` archive — can be pre-built and committed as a binary dependency rather than building from source every time.

## Estimated Effort

| Phase | Lines | Difficulty | Time |
|-------|-------|------------|------|
| 063-A: Interfaces + Stubs | ~600 | Low | 1 session |
| 063-B: libsignal C FFI | ~1200 | Hard | 2-3 sessions |
| 063-C: WhatsApp Integration | ~400 | Medium | 1 session |
| 063-D: Hardening | ~500 | Medium | 1-2 sessions |
| **Total** | **~2,700** | | **5-7 sessions** |

## Dependencies
- Ticket 062 Phase 1 (complete) — defines the transport layer and binary node codec
- OpenSSL 3.x (already linked)
- Google Protobuf (already in build)
- `libsignal` Rust crate (for Phase 063-B only)

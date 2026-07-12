# Ticket 063 — Signal Protocol Library: Session Report

**Date:** 2026-05-29 / 2026-05-31 / 2026-06-05
**Author:** Kestrel
**Status:** Phase A ✅ | Phase B ✅ | Phase B+ ✅ | Phase C (WhatsApp Integration) ✅ | Tests: 20+ green | **LIVE BIDIRECTIONAL MESSAGING CONFIRMED**

---

## What Was Built

### Phase 063-A: Interfaces + Stubs + InMemoryStore (committed `4757d78`)
**~1,900 lines across 10 files**

Full Signal Protocol type system and abstract store interfaces:

| File | Lines | What |
|------|-------|------|
| `SignalTypes.h` | 178 | Core types: `SignalKeypair`, `SignalAddress`, `SignalPreKey`, `SignalSignedPreKey`, `SignalSessionRecord`, `PreKeyBundle`, `EncryptedMessage`, `GroupEncryptedMessage`, `SignalMessage`, `PreKeySignalMessage` |
| `SignalStore.h` | 152 | Four abstract store interfaces: `ISessionStore`, `IPreKeyStore`, `IIdentityKeyStore`, `ISenderKeyStore` |
| `SignalCrypto.h` | 78 | High-level encrypt/decrypt API, key generation, session initialization |
| `DoubleRatchet.h` | 177 | Double Ratchet state machine — RatchetState, RatchetHeader, skip-message-keys |
| `SignalSessionManager.h` | 61 | Session lifecycle management — create, load, rotate |
| `SignalTypes.cpp` | 287 | Type implementations — serialization, keypair generation (OpenSSL X25519), key ID generation |
| `DoubleRatchet.cpp` | ~300 (stubs) | Stub implementations for ratchet operations |
| `SignalCrypto.cpp` | ~200 (stubs) | Stub implementations for encrypt/decrypt |
| `SignalSessionManager.cpp` | 87 | Session CRUD operations |
| `InMemorySignalStore.cpp` | 173 | In-memory store implementations (all four interfaces) using `std::unordered_map` |

### Phase 063-B: Real Crypto with OpenSSL (committed `0a66adc`, fixed through `6842829`)
**~800 lines replaced/added**

Replaced all stubs with real cryptographic operations:

#### Crypto Primitives (all via OpenSSL 3.x)
| Primitive | OpenSSL Function | Usage |
|-----------|-----------------|-------|
| X25519 key agreement | `EVP_PKEY_X25519` + `EVP_PKEY_derive` | DH operations in Double Ratchet + Noise |
| AES-256-GCM | `EVP_EncryptInit_ex` / `EVP_DecryptInit_ex` | Message encryption, ratchet encryption |
| HMAC-SHA256 | `EVP_MAC` + `OSSL_PARAM_construct_utf8_string` | Message authentication, key derivation |
| HKDF-SHA256 | `EVP_PKEY_CTX_set_hkdf_*` | Key derivation (root chain, message keys) |
| Ed25519 signing | `EVP_PKEY_ED25519` + `EVP_DigestSign` | Signed pre-key signatures |

#### Double Ratchet Implementation (`DoubleRatchet.cpp` — 660 lines)
Full Double Ratchet state machine:
- **Root chain:** HKDF-based root key → chain key derivation on every DH ratchet step
- **Sending chain:** Chain key → message key derivation (per message)
- **Receiving chain:** Same, with skipped message key buffer (up to 10)
- **DH ratchet:** X25519 keypair generation + shared secret computation on each ratchet step
- **Key management:** Root key, chain keys, message keys, header keys all derived via HKDF with proper info strings
- **Skip message keys:** Buffer of up to 10 skipped message keys for out-of-order message handling

#### SignalCrypto (`SignalCrypto.cpp` — 400 lines)
High-level operations:
- **`initialize_session()`**: X3DH-like key agreement — computes DH from identity + pre-key bundles, derives initial ratchet state via HKDF
- **`encrypt()`**: Ratchet step (if new chain) → chain key → message key → AES-256-GCM encrypt
- **`decrypt()`**: Find matching chain → derive message key → AES-256-GCM decrypt (with skipped key lookup)
- **`group_encrypt()` / `group_decrypt()`**: Symmetric key encryption (sender key model) with AES-256-GCM
- **`generate_pre_keys()`**: Batch generation of one-time pre-keys with unique X25519 keypairs
- **`generate_signed_pre_key()`**: curve25519-js-compatible Ed25519 signature over X25519 pre-key public key (using libsodium, not OpenSSL Ed25519 — see Phase B+)

### Phase 063-B+: curve25519-js Compatible Signing (committed `e7c6611`, `7099860`)

The Phase B `ed25519_sign()` used OpenSSL's `EVP_PKEY_ED25519` which treats the 32-byte private key as a **seed** — it hashes it with SHA-512 to derive the actual scalar. This is correct for standard Ed25519 (RFC 8032) but **wrong** for curve25519-js / libsignal, which use the raw **clamped X25519 scalar directly** as the Ed25519 scalar.

This was discovered when WhatsApp's server rejected our signed pre-key signature with `xml-not-well-formed`. The fix:

1. **Added `libsodium` as a dependency** — provides the low-level primitives OpenSSL lacks for this non-standard signing variant
2. **Implemented `ed25519_sign()` using libsodium primitives:**
   - Clamp X25519 scalar
   - `A = scalar * B` via `crypto_scalarmult_ed25519_base_noclamp`
   - `r = reduce(SHA-512(scalar || message))`
   - `R = r * B` via `crypto_scalarmult_ed25519_base_noclamp` (**NOT** `_base`, which clamps!)
   - `k = reduce(SHA-512(R || A || message))`
   - `S = (r + k * scalar) mod L`
   - `sig[63] |= (A[31] & 0x80)` (sign bit from Ed25519 public key)
3. **Verified byte-perfect match** with curve25519-js `sign()` on multiple test vectors
4. **Verified with Baileys** — feeding our C++-generated keys into Baileys produces valid QR codes

**Critical sub-bug:** `crypto_scalarmult_ed25519_base()` clamps its input (clears bits 0-2, sets bit 254, clears bit 255). The reduced SHA-512 hash `r` can legitimately have bits 0-2 set (e.g., `r[0] = 0xdc` → clamped to `0xd8`). This produces a completely different `R` point and invalid signature. Must use `_noclamp` variant.

**Files changed:** `SignalCrypto.cpp` (signing function rewritten), `CMakeLists.txt` (added `sodium` link)
| Test Category | Tests | What's Verified |
|---------------|-------|-----------------|
| Key Generation | 3 | X25519 keypair generation, Ed25519 signature + verification, batch pre-key generation |
| Store CRUD | 4 | InMemoryStore operations for all four store types |
| 1:1 Encrypt/Decrypt | 4 | Session init → encrypt → decrypt round-trip, multiple messages, out-of-order delivery |
| Group Encrypt/Decrypt | 3 | Sender key encrypt → decrypt, multiple group messages |
| Pre-Key Bundle | 2 | Bundle creation and serialization |
| Session Management | 2 | Session lifecycle (create, load, contains, delete) |
| Edge Cases | 2+ | Empty plaintext, large messages, key rotation |

All tests green as of commit `6842829`.

---

## Technical Decisions

### OpenSSL over libsignal C FFI
The original plan called for `libsignal` (Signal's Rust library) via C FFI. During implementation, it became clear that all required primitives are available in OpenSSL 3.x:
- X25519, Ed25519, AES-256-GCM, HMAC-SHA256, HKDF-SHA256 are all first-class OpenSSL citizens
- No Rust toolchain dependency in the Animus build
- Simpler build, fewer external dependencies
- The Double Ratchet is ~660 lines of straightforward C++ — not worth a Rust FFI layer

**Trade-off:** We don't get the formally-verified security of libsignal's Rust implementation. For an agent framework's messaging channel, this is acceptable. If/when we need Signal-channel-level security audits, we can swap the backend without changing the `animus::signal` interface.

### Hand-rolled Double Ratchet vs. Protocol Library
The Double Ratchet is implemented directly rather than through a noise/Signal protocol library:
- ~660 lines, well-understood algorithm
- No external dependency beyond OpenSSL
- Full control over key derivation (needed for WhatsApp-specific adaptations)
- The HKDF info strings and key derivation paths match Signal's specification

### InMemoryStore for Testing, SQLiteStore Deferred
Phase A provides `InMemoryStore` for testing. `SQLiteStore` (for production persistence) is deferred to Phase 063-C (WhatsApp Integration) — when we know the exact schema needed.

---

## Hard-Won Lessons

1. **`EVP_PKEY_new_raw_private_key/public_key` for X25519.** OpenSSL 3.x requires raw key bytes, not the older `EVP_PKEY_CTX` approach. Keys must be 32 bytes exactly. `EVP_PKEY_keygen` generates random keys; `EVP_PKEY_new_raw_private_key` imports existing raw bytes.

2. **`EVP_MAC` replaces `HMAC()` in OpenSSL 3.x.** The old `HMAC()` function is deprecated. New API: `EVP_MAC_fetch` → `EVP_MAC_CTX_new` → `EVP_MAC_init` → `EVP_MAC_update` → `EVP_MAC_final`. Parameters set via `OSSL_PARAM` array.

3. **Size of HMAC output needs `size_t`, not `int`.** `EVP_MAC_final` writes the output size as `size_t`. Passing `int` truncates on some platforms. Must use `size_t` explicitly.

4. **Can't self-decrypt in Double Ratchet.** The Double Ratchet is asymmetric — Alice and Bob have separate sending/receiving chains. A test that encrypts and decrypts with the same session doesn't work. Tests must initialize two sessions (Alice→Bob, Bob→Alice) and cross-decrypt.

5. **`<memory>` include cascade.** A missing `<memory>` include in `SignalSessionManager.h` caused 15+ unrelated-looking errors on every member access. The root cause was incomplete types from `std::unique_ptr` without the proper include.

6. **`openssl/core_names.h` needed for named parameters.** `OSSL_PKEY_PARAM_PRIV_KEY`, `OSSL_PKEY_PARAM_PUB_KEY`, etc. require `#include <openssl/core_names.h>` — not pulled by the generic OpenSSL headers.

7. **HKDF needs `<openssl/kdf.h>`.** `EVP_PKEY_CTX_set_hkdf_*` functions require explicit include — not pulled by `<openssl/evp.h>`.

8. **OpenSSL Ed25519 seed ≠ curve25519-js scalar.** `EVP_PKEY_new_raw_private_key(ED25519, seed)` hashes the seed with SHA-512 to derive the scalar. curve25519-js uses the raw clamped X25519 key directly. These produce completely different signatures. Must use libsodium's low-level primitives for curve25519-js compatibility.

9. **`crypto_scalarmult_ed25519_base` clamps input, `_noclamp` doesn't.** During signing, `R = r * B` where `r` is a reduced SHA-512 hash. The reduced value can have bits 0-2 set, which `_base` would incorrectly clear. Always use `_base_noclamp` for computing R.

10. **curve25519-js sign bit OR.** The last byte of the signature gets `A[31] & 0x80` OR'd in, where `A` is the Ed25519 public key derived from the clamped scalar. This is not part of standard Ed25519.

---

## Commits (Signal-specific, 12 commits)

```
6842829 test(signal): fix 1:1 tests for real Double Ratchet (can't self-decrypt)
6b60c13 test: add decrypt error output
f04cf85 fix(signal): generate real keypairs in test PreKeyBundle for X25519 DH
9b5e272 fix(signal): declare ctx in dh_compute derive section
1c26679 fix(signal): use EVP_PKEY_new_raw_private_key/public_key for X25519 DH
0f6333d fix(signal): fix group encrypt/decrypt key management, update tests for real crypto
0acaa64 fix(signal): add openssl/core_names.h include, fix EVP_MAC_final size_t
3b09e96 fix(signal): fix skip_message_keys signature, replace Writer/Reader with local helpers
0a66adc feat(signal): ticket 063-B — real Double Ratchet crypto with OpenSSL
31a4cb6 fix(signal): make keypair generation produce unique keys via counter
f211463 fix(signal): add missing <memory> include to SignalSessionManager.h
1211952 fix(signal): rename members to m_ prefix to avoid name collision with accessors
c8251a2 fix(signal): add missing include, use correct member names with trailing underscores
efb909a fix(signal): add std:: qualifiers and functional include for KeyHash
4757d78 feat(signal): ticket 063-A shared signal protocol library — interfaces + stubs + InMemoryStore + tests
440ee7f docs: ticket 063 shared signal protocol library + update 062 deps
```

### Phase B+ (May 31 — curve25519-js signing fix)
```
e7c6611 fix: implement curve25519-js compatible signing using libsodium
7099860 fix: use noclamp scalar mult for R and add sign bit to signature
```

---

## File Map

```
include/animus_kernel/signal/
├── SignalTypes.h           (178 lines) — Core types, keypair, address, pre-keys, encrypted messages
├── SignalStore.h           (152 lines) — Abstract store interfaces (4 stores)
├── SignalCrypto.h          (78 lines)  — High-level encrypt/decrypt API
├── DoubleRatchet.h         (177 lines) — Ratchet state machine types
└── SignalSessionManager.h  (61 lines)  — Session lifecycle management

src/kernel/signal/
├── SignalTypes.cpp         (287 lines) — Type implementations, key generation
├── DoubleRatchet.cpp       (660 lines) — Full Double Ratchet with real OpenSSL crypto
├── SignalCrypto.cpp        (400 lines) — Session init, encrypt, decrypt, group crypto
├── SignalSessionManager.cpp (87 lines) — Session CRUD
└── InMemorySignalStore.cpp (173 lines) — Test/dev store implementations

tests/
└── SignalProtocolTests.cpp (426 lines) — 20+ tests, all green
```

**Total: ~2,680 lines** (spec estimated ~2,700)

---

## Remaining Work

### Phase 063-C: WhatsApp Integration ✅ COMPLETE

The Signal Protocol library was integrated into the WhatsApp adapter for real message encryption/decryption. Eight bugs were found and fixed during integration testing against real WhatsApp messages:

#### Incoming Decryption Bugs (fixed June 4–5)
1. **HKDF info string length**: `"WhisperText"` passed as 12 bytes (including C null terminator) instead of 11. Produced completely wrong keys from X3DH root key derivation.
2. **Ratchet KDF salt/IKM reversed**: `kdf_rk()` concatenated `root_key + dh_output` as IKM. Should be `HKDF(IKM=dh_output, salt=root_key, info="WhisperRatchet", 64)`.
3. **HKDF salt default**: Some calls used `nullptr` instead of explicit `zeros(32)`. Signal Protocol requires zero salt.

#### Outgoing Encryption Bugs (fixed June 5)
4. **AES-256-GCM instead of AES-256-CBC**: `ratchet_encrypt()` used GCM. Signal uses CBC with separate HMAC-SHA256 MAC.
5. **Raw ciphertext instead of wire format**: Enc node content was raw encrypted bytes. Should be `[0x33 version][protobuf SignalMessage][8-byte MAC]`.
6. **Missing `to` wrapper node**: `enc` node must be inside `to(jid=deviceJid)` child.
7. **Missing `participants` wrapper + base JID**: 1:1 messages require `message → participants → to(jid) → enc`. `message.to` uses base JID (no device), `to.jid` uses device JID.
8. **Missing random padding**: WhatsApp requires `padRandomMax16` (1-15 bytes PKCS#7-style) before encryption.

All eight bugs fixed. Bidirectional messaging confirmed working June 5, 16:11 CEST.

### Phase 063-D: Hardening
- [ ] Pre-key rotation (signed pre-key every 48-72h)
- [ ] One-time pre-key replenishment (upload when count < 5)
- [ ] LID mapping support (phone JID → LID JID)
- [ ] Multi-device session management
- [ ] Key rotation edge cases (compromised key detection)
- [ ] Pre-key upload (signed pre-key + one-time pre-keys to server)
- [ ] SQLiteSignalStore for persistent session storage

### Future: Signal Channel Provider
- [ ] `animus::signal` is already shared — a Signal channel provider would use the same library
- [ ] Only transport and envelope formats differ between WhatsApp and Signal

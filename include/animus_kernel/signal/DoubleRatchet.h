#pragma once

// DoubleRatchet.h — Signal Protocol Double Ratchet implementation
//
// Implements the Double Ratchet algorithm as specified in:
// https://signal.org/docs/specifications/doubleratchet/
//
// Uses OpenSSL 3.x for all crypto primitives:
// - X25519 for DH key agreement
// - HKDF-SHA256 for key derivation
// - AES-256-GCM for message encryption
// - HMAC-SHA256 for key chaining
//
// The ratchet state is serializable for persistence in the session store.

#include <array>
#include <vector>
#include <cstdint>
#include <optional>
#include <string>

namespace animus::signal {

// ─── Constants ───

static constexpr size_t KEY_SIZE = 32;      // 256-bit keys
static constexpr size_t NONCE_SIZE = 12;    // 96-bit GCM nonce
static constexpr size_t TAG_SIZE = 16;      // 128-bit GCM auth tag
static constexpr size_t MAC_KEY_SIZE = 32;  // 256-bit MAC key
static constexpr uint32_t MAX_SKIP = 1000;  // max skipped messages

// ─── Key types ───

using DHPublicKey = std::array<uint8_t, KEY_SIZE>;
using DHPrivateKey = std::array<uint8_t, KEY_SIZE>;
using ChainKey = std::array<uint8_t, KEY_SIZE>;
using RootKey = std::array<uint8_t, KEY_SIZE>;
using MessageKey = std::vector<uint8_t>;   // 32 bytes cipher + 12 bytes nonce + 16 bytes tag info

struct DHKeyPair {
    DHPublicKey public_key{};
    DHPrivateKey private_key{};

    static DHKeyPair generate();

    bool operator==(const DHKeyPair& o) const {
        return public_key == o.public_key && private_key == o.private_key;
    }
};

// ─── Crypto primitives ───

// X25519 Diffie-Hellman
std::array<uint8_t, KEY_SIZE> dh_compute(
    const DHPrivateKey& private_key,
    const DHPublicKey& public_key);

// HKDF-SHA256 derive
std::vector<uint8_t> hkdf_derive(
    const uint8_t* ikm, size_t ikm_len,
    const uint8_t* salt, size_t salt_len,
    const uint8_t* info, size_t info_len,
    size_t output_len);

// AES-256-GCM encrypt
struct AesGcmCiphertext {
    std::vector<uint8_t> ciphertext;  // same length as plaintext
    std::array<uint8_t, NONCE_SIZE> nonce{};
    std::array<uint8_t, TAG_SIZE> tag{};
};

AesGcmCiphertext aes_gcm_encrypt(
    const std::array<uint8_t, KEY_SIZE>& key,
    const std::vector<uint8_t>& plaintext);

std::vector<uint8_t> aes_gcm_decrypt(
    const std::array<uint8_t, KEY_SIZE>& key,
    const AesGcmCiphertext& ct);

// HMAC-SHA256
std::array<uint8_t, 32> hmac_sha256(
    const uint8_t* key, size_t key_len,
    const uint8_t* data, size_t data_len);

// ─── Ratchet key derivation ───

// KDF_RK: root key derivation (root key + DH output → new root key + chain key)
struct RootAndChain {
    RootKey root_key;
    ChainKey chain_key;
};

RootAndChain kdf_rk(const RootKey& root_key, const std::array<uint8_t, KEY_SIZE>& dh_output);

// KDF_CK: chain key derivation (chain key → new chain key + message key)
struct ChainAndMessage {
    ChainKey chain_key;
    std::array<uint8_t, KEY_SIZE> message_key;
};

ChainAndMessage kdf_ck(const ChainKey& chain_key);

// ─── Skipped message key ───

struct SkippedKey {
    DHPublicKey header_key;  // which ratchet step
    uint32_t message_number; // which message in that step
    std::array<uint8_t, KEY_SIZE> message_key;
};

// ─── Ratchet state ───

struct RatchetState {
    // Identity
    DHKeyPair self_identity;
    DHPublicKey remote_identity;

    // Ratchet keys (current)
    DHKeyPair self_ratchet;          // our current ratchet keypair
    std::optional<DHPublicKey> remote_ratchet; // their current ratchet public key

    // Root key
    RootKey root_key{};

    // Chain keys
    std::optional<ChainKey> sending_chain_key;   // our sending chain
    std::optional<ChainKey> receiving_chain_key;  // their receiving chain

    // Counters
    uint32_t sending_count = 0;
    uint32_t receiving_count = 0;
    uint32_t prev_sending_count = 0;

    // Skipped message keys (for out-of-order messages)
    std::vector<SkippedKey> skipped_keys;

    // Serialization
    std::vector<uint8_t> serialize() const;
    static std::optional<RatchetState> deserialize(const uint8_t* data, size_t len);
};

// ─── Double Ratchet operations ───

// Initialize as initiator (Alice) after X3DH
RatchetState ratchet_init_as_initiator(
    const DHKeyPair& our_identity,
    const DHPublicKey& their_identity,
    const RootKey& root_key,
    const DHPublicKey& their_ratchet_key);

// Initialize as recipient (Bob) after X3DH
RatchetState ratchet_init_as_recipient(
    const DHKeyPair& our_identity,
    const DHPublicKey& their_identity,
    const RootKey& root_key,
    const DHKeyPair& our_ratchet);

// Ratchet encrypt: advance sending chain, encrypt message
struct RatchetCiphertext {
    DHPublicKey header_key;    // sender's current ratchet public key
    uint32_t prev_count = 0;   // previous chain length
    uint32_t message_number = 0;
    std::vector<uint8_t> ciphertext; // AES-GCM encrypted
    std::array<uint8_t, NONCE_SIZE> nonce{};
    std::array<uint8_t, TAG_SIZE> tag{};
};

RatchetCiphertext ratchet_encrypt(
    RatchetState& state,
    const std::vector<uint8_t>& plaintext);

// Ratchet decrypt: handle dh ratchet step if needed, then decrypt
std::vector<uint8_t> ratchet_decrypt(
    RatchetState& state,
    const RatchetCiphertext& ct);

} // namespace animus::signal

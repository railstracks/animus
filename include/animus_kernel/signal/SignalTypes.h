#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <array>
#include <optional>
#include <variant>

namespace animus::signal {

// ─── Curve25519 keypair ───

struct SignalKeypair {
    std::array<uint8_t, 32> public_key{};
    std::array<uint8_t, 32> private_key{};

    static SignalKeypair generate();
    bool operator==(const SignalKeypair& o) const {
        return public_key == o.public_key && private_key == o.private_key;
    }
};

// ─── Address (name + device_id) ───

struct SignalAddress {
    std::string name;       // JID (user@s.whatsapp.net) or UUID for Signal
    uint32_t device_id = 0; // 0 for default device

    bool operator==(const SignalAddress& o) const {
        return name == o.name && device_id == o.device_id;
    }
    bool operator<(const SignalAddress& o) const {
        return (name < o.name) || (name == o.name && device_id < o.device_id);
    }
    std::string to_string() const;
};

// ─── Identity key ───

struct SignalIdentityKey {
    SignalKeypair keypair;
    uint32_t registration_id = 0;

    static SignalIdentityKey generate(uint32_t registration_id);
};

// ─── Pre-keys ───

struct SignalPreKey {
    uint32_t id = 0;
    SignalKeypair keypair;

    std::vector<uint8_t> serialize() const;
    static SignalPreKey deserialize(const uint8_t* data, size_t len);
};

struct SignalSignedPreKey {
    uint32_t id = 0;
    SignalKeypair keypair;
    std::vector<uint8_t> signature; // 64-byte Ed25519 signature

    std::vector<uint8_t> serialize() const;
    static SignalSignedPreKey deserialize(const uint8_t* data, size_t len);
};

// ─── Pre-key bundle (received from server) ───

struct PreKeyBundle {
    uint32_t registration_id = 0;
    uint32_t device_id = 0;
    SignalKeypair pre_key;               // one-time pre-key (may be absent)
    std::optional<uint32_t> pre_key_id;
    SignalSignedPreKey signed_pre_key;
    SignalKeypair identity_key;          // remote identity public key

    std::vector<uint8_t> serialize() const;
    static PreKeyBundle deserialize(const uint8_t* data, size_t len);
};

// ─── Encrypted message types ───

enum class SignalMessageType : uint8_t {
    PreKey = 3,    // "pkmsg" — new session (includes pre-key)
    Whisper = 2,   // "msg"   — existing session (ratchet-only)
    SenderKey = 7, // "skmsg" — group message
    UnidentifiedSender = 6 // sealed sender envelope
};

struct SignalMessageHeader {
    uint8_t version = 0;
    uint32_t counter = 0;
    uint32_t previous_counter = 0;
    SignalKeypair ratchet_key;
};

struct EncryptedMessage {
    SignalMessageType type = SignalMessageType::Whisper;
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> header;    // serialized SignalMessageHeader for Whisper/PreKey
    uint32_t counter = 0;           // convenience copy from header

    // Full serialized form for wire transport
    std::vector<uint8_t> serialize() const;
    static EncryptedMessage deserialize(const uint8_t* data, size_t len);
};

struct GroupEncryptedMessage {
    std::string group_id;
    SignalAddress sender;
    std::vector<uint8_t> ciphertext;
    uint32_t counter = 0;

    std::vector<uint8_t> serialize() const;
    static GroupEncryptedMessage deserialize(const uint8_t* data, size_t len);
};

// ─── Session record ───

struct SessionRecord {
    std::vector<uint8_t> state;   // opaque ratchet state blob
    uint32_t version = 1;
    bool is_fresh = true;

    std::vector<uint8_t> serialize() const;
    static SessionRecord deserialize(const uint8_t* data, size_t len);
};

// ─── Sender key record ───

struct SenderKeyRecord {
    std::string group_id;
    SignalAddress sender;
    uint32_t iteration = 0;
    std::vector<uint8_t> chain_key;
    SignalKeypair signing_key;
    std::vector<uint8_t> state;    // opaque

    std::vector<uint8_t> serialize() const;
    static SenderKeyRecord deserialize(const uint8_t* data, size_t len);
};

// ─── Result type ───

template<typename T>
struct Result {
    bool ok = false;
    std::string error;
    T value{};

    static Result<T> Success(T v) { return {true, {}, std::move(v)}; }
    static Result<T> Fail(std::string err) { return {false, std::move(err)}; }
    explicit operator bool() const { return ok; }
};

template<>
struct Result<void> {
    bool ok = false;
    std::string error;

    static Result<void> Success() { return {true, {}}; }
    static Result<void> Fail(std::string err) { return {false, std::move(err)}; }
    explicit operator bool() const { return ok; }
};

} // namespace animus::signal

// Hash for SignalAddress (unordered_map key)
namespace std {
template<>
struct hash<animus::signal::SignalAddress> {
    size_t operator()(const animus::signal::SignalAddress& a) const {
        size_t h = hash<string>()(a.name);
        h ^= hash<uint32_t>()(a.device_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
} // namespace std

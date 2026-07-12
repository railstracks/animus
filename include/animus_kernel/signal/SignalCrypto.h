#pragma once

#include "animus_kernel/signal/SignalTypes.h"
#include "animus_kernel/signal/SignalStore.h"

namespace animus::signal {

// ─── High-level Signal Protocol API ───
//
// This is the primary interface consumed by channel adapters.
// Phase 063-A: stub implementations (log + return mock data).
// Phase 063-B: real implementations backed by libsignal C FFI.

/// Initialize a new 1:1 session as INITIATOR (outbound) from a pre-key bundle.
/// After this call, the session store contains a new ratchet state for `remote`.
Result<void> initialize_session(
    ISessionStore& sessions,
    IIdentityKeyStore& identity,
    const SignalAddress& remote,
    const PreKeyBundle& bundle
);

/// Initialize a new 1:1 session as RECEIVER (inbound pkmsg).
/// Called when we receive a PreKey message. Uses our pre-key and signed pre-key
/// private keys to compute the X3DH shared secret.
Result<void> initialize_session_as_receiver(
    ISessionStore& sessions,
    IIdentityKeyStore& identity,
    IPreKeyStore& preKeys,
    ISignedPreKeyStore& signedPreKeys,
    const SignalAddress& remote,
    const PreKeyBundle& bundle,
    uint32_t preKeyId  // 0 if no one-time pre-key was used
);

/// Encrypt a 1:1 message using the Double Ratchet.
/// Returns the encrypted message suitable for wire transport.
Result<EncryptedMessage> encrypt_1to1(
    ISessionStore& sessions,
    IIdentityKeyStore& identity,
    const SignalAddress& remote,
    std::string_view plaintext
);

/// Decrypt a 1:1 message using the Double Ratchet.
Result<std::string> decrypt_1to1(
    ISessionStore& sessions,
    IIdentityKeyStore& identity,
    const SignalAddress& remote,
    const EncryptedMessage& message
);

/// Encrypt a group message using Sender Keys.
/// The sender key is created on first call for a given group.
Result<GroupEncryptedMessage> encrypt_group(
    ISenderKeyStore& sender_keys,
    const SignalAddress& sender,
    const std::string& group_id,
    std::string_view plaintext
);

/// Decrypt a group message using Sender Keys.
Result<std::string> decrypt_group(
    ISenderKeyStore& sender_keys,
    const SignalAddress& sender,
    const std::string& group_id,
    const GroupEncryptedMessage& message
);

/// Generate a batch of one-time pre-keys.
std::vector<SignalPreKey> generate_pre_keys(uint32_t start_id, size_t count);

/// Generate a new signed pre-key signed by the identity key.
SignalSignedPreKey generate_signed_pre_key(uint32_t id, const SignalKeypair& identity_key);

/// Process a received sender key distribution message.
/// Stores the sender key for later group decryption.
Result<void> process_sender_key_distribution(
    ISenderKeyStore& sender_keys,
    const SignalAddress& sender,
    const std::vector<uint8_t>& distribution_message
);

/// Get the sender key distribution message for a group (to send to new members).
Result<std::vector<uint8_t>> get_sender_key_distribution(
    ISenderKeyStore& sender_keys,
    const SignalAddress& sender,
    const std::string& group_id
);

} // namespace animus::signal

// ─── Ed25519 utilities (for WhatsApp pairing + Signal signed prekeys) ───
namespace animus {

/// Sign a message with an Ed25519 private key (32 bytes).
std::vector<uint8_t> ed25519_sign(const std::array<uint8_t, 32>& private_key,
                                  const std::vector<uint8_t>& message);

/// Verify an Ed25519 signature.
bool ed25519_verify(const std::vector<uint8_t>& public_key,
                    const std::vector<uint8_t>& message,
                    const std::vector<uint8_t>& signature);

/// Verify an XEdDSA signature using an X25519 public key.
/// Converts X25519→Ed25519, extracts sign bit from signature,
/// and verifies using standard Ed25519.
bool x25519_verify(const std::vector<uint8_t>& x25519_public_key,
                   const std::vector<uint8_t>& message,
                   const std::vector<uint8_t>& signature);

} // namespace animus

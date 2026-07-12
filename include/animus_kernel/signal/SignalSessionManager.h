#pragma once

#include "animus_kernel/signal/SignalTypes.h"
#include "animus_kernel/signal/SignalStore.h"
#include <memory>

namespace animus::signal {

// ─── Session lifecycle management ───
//
// Higher-level convenience wrapper that owns the stores and
// provides a simpler API for channel adapters. Channel adapters
// can use this OR use SignalCrypto directly with their own stores.

class SignalSessionManager {
public:
    SignalSessionManager(
        std::unique_ptr<ISessionStore> sessions,
        std::unique_ptr<IPreKeyStore> pre_keys,
        std::unique_ptr<ISignedPreKeyStore> signed_pre_keys,
        std::unique_ptr<IIdentityKeyStore> identity,
        std::unique_ptr<ISenderKeyStore> sender_keys
    );

    // 1:1 session management
    bool has_session(const SignalAddress& remote) const;
    Result<void> establish_session(const SignalAddress& remote, const PreKeyBundle& bundle);
    Result<EncryptedMessage> encrypt(const SignalAddress& remote, std::string_view plaintext);
    Result<std::string> decrypt(const SignalAddress& remote, const EncryptedMessage& msg);

    // WhatsApp-specific: decrypt using the WhatsApp WhisperMessage wire format
    // This is different from our custom RatchetCiphertext format.
    // wire_data is [version_byte][protobuf SignalMessage][8-byte MAC]
    // Returns the plaintext bytes (still needs unpadding).
    Result<std::vector<uint8_t>> decrypt_whisper(
        const SignalAddress& remote,
        const std::vector<uint8_t>& wireData,
        const std::vector<uint8_t>& ourIdentityPub,
        const std::vector<uint8_t>& theirIdentityPub);

    // Group messaging
    Result<GroupEncryptedMessage> group_encrypt(const SignalAddress& sender,
                                                 const std::string& group_id,
                                                 std::string_view plaintext);
    Result<std::string> group_decrypt(const SignalAddress& sender,
                                       const std::string& group_id,
                                       const GroupEncryptedMessage& msg);

    // Pre-key lifecycle
    std::vector<SignalPreKey> generate_pre_keys(size_t count);
    SignalSignedPreKey rotate_signed_pre_key();

    // Accessors for direct store access when needed
    ISessionStore& sessions() { return *m_sessions; }
    IPreKeyStore& pre_keys() { return *m_pre_keys; }
    ISignedPreKeyStore& signed_pre_keys() { return *m_signed_pre_keys; }
    IIdentityKeyStore& identity() { return *m_identity; }
    ISenderKeyStore& sender_keys() { return *m_sender_keys; }

private:
    std::unique_ptr<ISessionStore> m_sessions;
    std::unique_ptr<IPreKeyStore> m_pre_keys;
    std::unique_ptr<ISignedPreKeyStore> m_signed_pre_keys;
    std::unique_ptr<IIdentityKeyStore> m_identity;
    std::unique_ptr<ISenderKeyStore> m_sender_keys;
};

/// Factory: create a fully in-memory session manager (for testing).
/// If identity_key is provided, uses it; otherwise generates a new one.
std::unique_ptr<SignalSessionManager> create_in_memory_manager(
    std::optional<SignalIdentityKey> identity_key = std::nullopt);

} // namespace animus::signal

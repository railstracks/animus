// WhatsAppMessageHandler.cpp — Decrypt and dispatch WhatsApp messages
// Extracted from WhatsAppGatewayLoop.cpp for reuse and testability.
// ============================================================================

#include "animus_kernel/whatsapp/WhatsAppMessageHandler.h"
#include "animus_kernel/whatsapp/WABinary.h"
#include "animus_kernel/whatsapp/WAProto.h"
#include "animus_kernel/signal/SignalCrypto.h"
#include "animus_kernel/signal/SignalSessionManager.h"
#include "animus_kernel/signal/SignalStore.h"
#include "animus_kernel/signal/DoubleRatchet.h"

#include <iostream>

namespace animus::whatsapp {

// ─── Ed25519 → X25519 key conversion ────────────────────────────────────────

// ─── Key conversion: 0x05-prefixed keys → raw X25519 ─────────────────────────
// WhatsApp sends keys with a 0x05 prefix byte (33 bytes total).
// IMPORTANT: Despite the 0x05 prefix, these ARE Curve25519 (X25519) keys, not Ed25519.
// The 0x05 is just a type/format indicator used by libsignal.
// We simply strip the prefix and use the 32 bytes directly.
// Do NOT call crypto_sign_ed25519_pk_to_curve25519 — that's for actual Ed25519 keys.
static std::vector<uint8_t> ed25519PublicKeyToCurve25519(const std::vector<uint8_t>& edKey) {
    if (edKey.size() == 33) {
        // Strip 0x05 prefix — the remaining 32 bytes ARE the X25519 key
        return std::vector<uint8_t>(edKey.begin() + 1, edKey.end());
    }
    if (edKey.size() == 32) {
        // Already a 32-byte key, use as-is
        return edKey;
    }
    std::cerr << "[whatsapp] key conversion: unexpected key size "
              << edKey.size() << std::endl;
    return {};
}

// ─── Decrypt inner WhisperMessage after session is established ──────────────
// Both pkmsg and msg use this path for the inner ciphertext.
// Wire format: [version_byte][protobuf SignalMessage][8-byte MAC]
//
// After decryption, unpad with unpadRandomMax16 and decode as Message protobuf.

static std::vector<uint8_t> decryptWhisperMessageInner(
    animus::signal::SignalSessionManager* signalMgr,
    const std::string& sender,
    const std::vector<uint8_t>& wireData)
{
    // Get our identity key (Ed25519 with 0x05 prefix)
    auto ourIdKey = signalMgr->identity().get_identity_key_pair();
    std::vector<uint8_t> ourIdentityEdPub(33);
    ourIdentityEdPub[0] = 0x05;
    std::copy(ourIdKey.public_key.begin(), ourIdKey.public_key.end(), ourIdentityEdPub.begin() + 1);

    // Get their identity key (Ed25519 with 0x05 prefix)
    auto theirIdentityX = signalMgr->identity().load_identity(animus::signal::SignalAddress{sender, 0});
    std::vector<uint8_t> theirIdentityEdPub;
    if (theirIdentityX) {
        theirIdentityEdPub.resize(33);
        theirIdentityEdPub[0] = 0x05;
        std::copy(theirIdentityX->begin(), theirIdentityX->end(), theirIdentityEdPub.begin() + 1);
    } else {
        std::cerr << "[whatsapp] WhisperMessage: no stored identity key for " << sender << std::endl;
        return {};
    }

    // Delegate to SignalSessionManager::decrypt_whisper
    animus::signal::SignalAddress remote{sender, 0};
    auto result = signalMgr->decrypt_whisper(remote, wireData, ourIdentityEdPub, theirIdentityEdPub);
    if (!result) {
        std::cerr << "[whatsapp] WhisperMessage decryption failed: " << result.error << std::endl;
        return {};
    }
    return result.value;
}

// ─── Decrypt a PreKey WhisperMessage (new session, type="pkmsg") ────────────
// Wire format: [version_byte][protobuf PreKeySignalMessage][8-byte MAC]
// PreKeySignalMessage { preKeyId, baseKey, identityKey, message, registrationId, signedPreKeyId }

static std::vector<uint8_t> decryptPreKeyMessage(
    animus::signal::SignalSessionManager* signalMgr,
    animus::signal::IPreKeyStore* preKeyStore,
    animus::signal::ISignedPreKeyStore* signedPreKeyStore,
    animus::signal::IIdentityKeyStore* identityStore,
    const std::string& sender,
    const std::vector<uint8_t>& ciphertext)
{
    if (ciphertext.size() < 2) {
        std::cerr << "[whatsapp] pkmsg too short: " << ciphertext.size() << " bytes" << std::endl;
        return {};
    }

    uint8_t version = ciphertext[0];
    std::cerr << "[whatsapp] pkmsg version: 0x" << std::hex << (int)version << std::dec << std::endl;

    // Strip version byte; the rest is PreKeySignalMessage protobuf + 8-byte MAC
    if (ciphertext.size() < 9) {
        std::cerr << "[whatsapp] pkmsg too short after version byte" << std::endl;
        return {};
    }

    // Decode PreKeySignalMessage protobuf
    // The inner message is also inside this protobuf (field 4)
    auto pkMsg = proto::decodePreKeySignalMessage(
        ciphertext.data() + 1,
        ciphertext.size() - 1);

    std::cerr << "[whatsapp] pkmsg: preKeyId=" << pkMsg.pre_key_id
              << " signedPreKeyId=" << pkMsg.signed_pre_key_id
              << " baseKey=" << (pkMsg.base_key.empty() ? 0 : pkMsg.base_key.size()) << " bytes"
              << " identityKey=" << (pkMsg.identity_key.empty() ? 0 : pkMsg.identity_key.size()) << " bytes"
              << " message=" << (pkMsg.message.empty() ? 0 : pkMsg.message.size()) << " bytes"
              << std::endl;

    // Debug: hex-dump the identity key
    std::cerr << "[whatsapp] pkmsg identity_key hex: ";
    for (auto b : pkMsg.identity_key) fprintf(stderr, "%02x", b);
    std::cerr << std::endl;
    std::cerr << "[whatsapp] pkmsg base_key hex: ";
    for (auto b : pkMsg.base_key) fprintf(stderr, "%02x", b);
    std::cerr << std::endl;

    // Convert Ed25519 identity key to Curve25519 for DH
    auto theirIdentityX = ed25519PublicKeyToCurve25519(pkMsg.identity_key);
    if (theirIdentityX.empty()) {
        std::cerr << "[whatsapp] pkmsg: failed to convert identity key" << std::endl;
        return {};
    }
    std::cerr << "[whatsapp] pkmsg identity_key (curve25519): ";
    for (auto b : theirIdentityX) fprintf(stderr, "%02x", b);
    std::cerr << std::endl;

    // Convert Ed25519 base key (ephemeral) to Curve25519 for DH
    auto theirBaseX = ed25519PublicKeyToCurve25519(pkMsg.base_key);
    if (theirBaseX.empty()) {
        std::cerr << "[whatsapp] pkmsg: failed to convert base key" << std::endl;
        return {};
    }
    std::cerr << "[whatsapp] pkmsg base_key (curve25519): ";
    for (auto b : theirBaseX) fprintf(stderr, "%02x", b);
    std::cerr << std::endl;

    // Build PreKeyBundle from the extracted keys
    // In WhatsApp's protocol, the sender's base_key IS their signed pre-key (ephemeral)
    animus::signal::PreKeyBundle bundle;
    bundle.signed_pre_key.id = pkMsg.signed_pre_key_id;
    std::copy(theirBaseX.begin(), theirBaseX.begin() + 32, bundle.signed_pre_key.keypair.public_key.begin());
    std::copy(theirIdentityX.begin(), theirIdentityX.begin() + 32, bundle.identity_key.public_key.begin());

    // If a one-time pre-key is referenced, set it
    if (pkMsg.pre_key_id > 0) {
        bundle.pre_key_id = pkMsg.pre_key_id;
    }

    // Store sender's identity key (Curve25519 format for DH)
    // We already converted it to X25519 earlier
    std::array<uint8_t, 32> identityKeyArr{};
    std::copy(theirIdentityX.begin(), theirIdentityX.begin() + 32, identityKeyArr.begin());
    identityStore->save_identity(
        animus::signal::SignalAddress{sender, 0},
        identityKeyArr
    );

    // Initialize session as receiver (X3DH from our perspective)
    animus::signal::SignalAddress remote{sender, 0};
    auto initResult = animus::signal::initialize_session_as_receiver(
        signalMgr->sessions(),
        signalMgr->identity(),
        *preKeyStore,
        *signedPreKeyStore,
        remote,
        bundle,
        pkMsg.pre_key_id);

    if (!initResult) {
        std::cerr << "[whatsapp] pkmsg: session init failed: " << initResult.error << std::endl;
        return {};
    }

    std::cerr << "[whatsapp] pkmsg: session initialized for " << sender << std::endl;

    // Now decrypt the inner WhisperMessage using the new session
    // The inner message is in pkMsg.message (field 4 of the protobuf)
    // But in WhatsApp, the entire wire data after version byte is the PreKeySignalMessage,
    // and the inner "message" field contains the WhisperMessage ciphertext
    // which itself has [version][protobuf][MAC] format

    if (pkMsg.message.empty()) {
        std::cerr << "[whatsapp] pkmsg: no inner message" << std::endl;
        return {};
    }

    // The inner message is a raw WhisperMessage wire format
    auto plaintext = decryptWhisperMessageInner(signalMgr, sender, pkMsg.message);
    if (plaintext.empty()) {
        std::cerr << "[whatsapp] pkmsg: inner WhisperMessage decryption failed" << std::endl;
        return {};
    }

    // Unpad WhatsApp random padding
    auto unpadded = proto::unpadRandomMax16(std::vector<uint8_t>(plaintext.begin(), plaintext.end()));
    return unpadded;
}

// ─── Decrypt a WhisperMessage (existing session, type="msg") ─────────────────

static std::vector<uint8_t> decryptWhisperMessage(
    animus::signal::SignalSessionManager* signalMgr,
    const std::string& sender,
    const std::vector<uint8_t>& ciphertext)
{
    auto plaintext = decryptWhisperMessageInner(signalMgr, sender, ciphertext);
    if (plaintext.empty()) return {};

    // Unpad WhatsApp random padding
    return proto::unpadRandomMax16(std::vector<uint8_t>(plaintext.begin(), plaintext.end()));
}

// ─── Main message handler ───────────────────────────────────────────────────

DecryptedMessage handleEncryptedMessage(
    const WABinaryNode& messageNode,
    animus::signal::SignalSessionManager* signalMgr,
    animus::signal::IPreKeyStore* preKeyStore,
    animus::signal::ISignedPreKeyStore* signedPreKeyStore,
    animus::signal::IIdentityKeyStore* identityStore)
{
    DecryptedMessage result;
    result.from = messageNode.getAttr("from");
    result.sender = messageNode.getAttr("participant");
    result.isGroup = isJidGroup(result.from);
    if (result.sender.empty()) result.sender = result.from;
    result.messageId = messageNode.getAttr("id");

    std::string decryptedText;
    auto children = std::get_if<std::vector<WABinaryNode>>(&messageNode.content);
    if (!children) {
        result.text = "[no children]";
        return result;
    }

    for (auto& child : *children) {
        if (child.tag != "enc") continue;

        std::string encType = child.getAttr("type");
        auto* ciphertext = std::get_if<std::vector<uint8_t>>(&child.content);
        if (!ciphertext || ciphertext->empty()) continue;

        std::cerr << "[whatsapp] enc type=" << encType
                  << " size=" << ciphertext->size() << " bytes" << std::endl;

        std::vector<uint8_t> plaintext;

        if (encType == "pkmsg") {
            plaintext = decryptPreKeyMessage(signalMgr, preKeyStore, signedPreKeyStore,
                                             identityStore, result.sender, *ciphertext);
        } else if (encType == "msg") {
            plaintext = decryptWhisperMessage(signalMgr, result.sender, *ciphertext);
        } else if (encType == "skmsg") {
            std::cerr << "[whatsapp] skmsg (group) not yet supported" << std::endl;
            continue;
        } else {
            std::cerr << "[whatsapp] unknown enc type: " << encType << std::endl;
            continue;
        }

        if (!plaintext.empty()) {
            // Decode as protobuf Message
            auto msg = proto::decodeMessage(plaintext.data(), plaintext.size());
            if (msg.hasText()) {
                decryptedText = msg.text();
                break; // Use first successful decryption
            }
        }
    }

    // If decryption failed, return clear indicator
    if (decryptedText.empty()) {
        decryptedText = "[encrypted — signal decryption failed]";
    }

    result.text = decryptedText;
    return result;
}

} // namespace animus::whatsapp
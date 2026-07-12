// WhatsAppMessageHandler.h — Decrypt and dispatch WhatsApp messages
// Extracted from WhatsAppGatewayLoop.cpp for reuse and testability.
// ============================================================================

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "animus_kernel/whatsapp/WABinary.h"

namespace animus::signal {
class SignalSessionManager;
class IPreKeyStore;
class ISignedPreKeyStore;
class IIdentityKeyStore;
}

namespace animus::whatsapp {

using WABinaryNode = BinaryNode;

/// Result of decrypting a WhatsApp message (enc node).
struct DecryptedMessage {
    std::string text;           // Decrypted message text (from protobuf Message.conversation)
    std::string sender;         // JID of the message sender
    std::string from;           // JID of the chat (group or individual)
    bool isGroup = false;       // Whether this is a group message
    std::string messageId;      // Original message ID for receipts
};

/// Handle a "message" binary node — decrypt enc children and return result.
/// Returns empty text if decryption fails (no session, corrupt data, etc.).
DecryptedMessage handleEncryptedMessage(
    const WABinaryNode& messageNode,
    animus::signal::SignalSessionManager* signalMgr,
    animus::signal::IPreKeyStore* preKeyStore,
    animus::signal::ISignedPreKeyStore* signedPreKeyStore,
    animus::signal::IIdentityKeyStore* identityStore);

} // namespace animus::whatsapp
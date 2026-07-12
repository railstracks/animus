#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace animus::whatsapp {

// X25519 key pair
struct KeyPair {
    std::vector<uint8_t> pub;
    std::vector<uint8_t> priv;
};

// Noise protocol state for WhatsApp transport
// Implements Noise_XX_25519_AESGCM_SHA256 (3-way key exchange)
class NoiseState {
public:
    NoiseState();
    ~NoiseState();

    // Initialize with ephemeral key pair + WA header bytes
    void init(const KeyPair& ephemeralKey, const std::vector<uint8_t>& noiseHeader);

    // Step 1: Get ephemeral pubkey for client hello protobuf
    // The caller wraps this in a protobuf HandshakeMessage { clientHello: { ephemeral } }
    std::vector<uint8_t> getEphemeralPub() const { return ephemeral_.pub; }

    // Step 2: Process decoded server hello fields — returns (keyEnc, payloadEnc)
    struct ClientFinishData {
        std::vector<uint8_t> static_enc;   // encrypted noise static pubkey
        std::vector<uint8_t> payload_enc;  // encrypted client payload
    };
    ClientFinishData processServerHello(
        const std::vector<uint8_t>& serverEphemeral,
        const std::vector<uint8_t>& serverStatic,
        const std::vector<uint8_t>& serverPayload,
        const KeyPair& noiseKey
    );

    // Transition to transport encryption
    void finishHandshake();

    // Encrypt client payload (called after processServerHello, before finishHandshake)
    std::vector<uint8_t> encryptClientPayload(const std::vector<uint8_t>& payload);

    // Encrypt/decrypt for transport phase
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext);
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext);

    // Frame encoding: 3-byte length prefix + optional intro header on first frame
    std::vector<uint8_t> encodeFrame(const std::vector<uint8_t>& payload);
    static std::vector<uint8_t> decodeFrame(const std::vector<uint8_t>& frame);

    bool isTransportReady() const { return transport_ != nullptr; }
    void setRoutingInfo(const std::vector<uint8_t>& info) { routingInfo_ = info; }

private:
    void authenticate(const std::vector<uint8_t>& data);
    void mixIntoKey(const std::vector<uint8_t>& data);

    struct TransportKeys {
        std::vector<uint8_t> encKey;
        std::vector<uint8_t> decKey;
        uint32_t writeCounter = 0;
        uint32_t readCounter = 0;
    };

    // Handshake state
    KeyPair ephemeral_;
    KeyPair staticKey_;
    std::vector<uint8_t> hash_;
    std::vector<uint8_t> salt_;
    std::vector<uint8_t> encKey_;
    uint32_t counter_ = 0;

    // Intro/header state
    std::vector<uint8_t> noiseHeader_;
    std::vector<uint8_t> routingInfo_;
    bool sentIntro_ = false;

    // Transport state (after handshake)
    TransportKeys* transport_ = nullptr;
};

// Crypto primitives (OpenSSL-backed)
KeyPair generateKeyPair();
std::vector<uint8_t> curve25519Shared(const std::vector<uint8_t>& priv, const std::vector<uint8_t>& pub);
std::vector<uint8_t> aesEncryptGCM(const std::vector<uint8_t>& plaintext,
                                     const std::vector<uint8_t>& key,
                                     const std::vector<uint8_t>& iv,
                                     const std::vector<uint8_t>& aad = {});
std::vector<uint8_t> aesDecryptGCM(const std::vector<uint8_t>& ciphertext,
                                     const std::vector<uint8_t>& key,
                                     const std::vector<uint8_t>& iv,
                                     const std::vector<uint8_t>& aad = {});
std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);
std::vector<uint8_t> hkdfExpand(const std::vector<uint8_t>& ikm, size_t length,
                                  const std::vector<uint8_t>& salt, const std::string& info);

// Constants
constexpr uint8_t NOISE_WA_HEADER[] = {87, 65, 6, 3};  // 'W' 'A' version 3
constexpr const char* NOISE_MODE = "Noise_XX_25519_AESGCM_SHA256";
constexpr size_t IV_LENGTH = 12;
constexpr size_t KEY_LENGTH = 32;

} // namespace animus::whatsapp

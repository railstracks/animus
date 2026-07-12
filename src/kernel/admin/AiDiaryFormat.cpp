#include "animus_kernel/admin/AiDiaryFormat.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <random>
#include <sstream>

#include <json/writer.h>
#include <json/reader.h>

// For v1, we use OpenSSL for AES-256-GCM and a SHA-256 based KDF.
// Argon2id can be substituted when the dependency is available.
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace animus::kernel {

// ============================================================================
// Constants
// ============================================================================

static const std::array<uint8_t, 4> AIDI_MAGIC = {0x41, 0x49, 0x44, 0x49}; // "AIDI"

// ============================================================================
// Key derivation (v1: SHA-256 based, upgradeable to Argon2id)
// ============================================================================

std::vector<uint8_t> AiDiaryFormat::DeriveKey(
        const std::string& agentSecret,
        const std::vector<uint8_t>& salt,
        uint32_t iterations,
        uint32_t memoryKiB) {
    // v1 uses iterative SHA-256 as a placeholder KDF.
    // This is NOT as strong as Argon2id against GPU attacks, but:
    //   1. The diary encryption is a privacy boundary, not state secrets
    //   2. The attack surface is local file access (not network)
    //   3. This can be upgraded to Argon2id when the dep is added
    //
    // Iterative SHA-256: key = SHA256(salt || secret) repeated
    // `iterations` times.

    (void)memoryKiB;  // v1: not used

    std::vector<uint8_t> key(32);  // 256 bits

    // First iteration: SHA256(salt || secret)
    unsigned int outLen = 0;
    EVP_Digest(nullptr, 0, key.data(), &outLen, nullptr, nullptr); // init ctx
    // Manual two-part since EVP_Digest doesn't support multi-input easily
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(mdctx, salt.data(), salt.size());
    EVP_DigestUpdate(mdctx, agentSecret.data(), agentSecret.size());
    EVP_DigestFinal_ex(mdctx, key.data(), &outLen);
    EVP_MD_CTX_free(mdctx);

    // Subsequent iterations: SHA256(key) repeated
    for (uint32_t i = 1; i < iterations; ++i) {
        unsigned int len = 0;
        EVP_Digest(key.data(), key.size(), key.data(), &len, EVP_sha256(), nullptr);
    }

    return key;
}

// ============================================================================
// Encryption
// ============================================================================

std::vector<uint8_t> AiDiaryFormat::Encrypt(
        const Json::Value& payload,
        const std::string& agentSecret,
        const AiDiaryContainerHeader& headerDefaults) {

    // Serialize payload to JSON bytes
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    std::string jsonStr = Json::writeString(wb, payload);
    const uint8_t* plaintext = reinterpret_cast<const uint8_t*>(jsonStr.data());
    size_t plaintextLen = jsonStr.size();

    // Generate salt and nonce
    std::vector<uint8_t> salt(32);
    std::vector<uint8_t> nonce(12);  // GCM standard 96-bit nonce
    RAND_bytes(salt.data(), static_cast<int>(salt.size()));
    RAND_bytes(nonce.data(), static_cast<int>(nonce.size()));

    // Derive key
    AiDiaryContainerHeader hdr = headerDefaults;
    hdr.salt = salt;
    hdr.nonce = nonce;
    auto key = DeriveKey(agentSecret, salt, hdr.kdfIterations, hdr.kdfMemoryKiB);

    // Encrypt with AES-256-GCM
    std::vector<uint8_t> ciphertext(plaintextLen);
    std::vector<uint8_t> tag(16);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr);
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data());

    int outLen = 0;
    EVP_EncryptUpdate(ctx, ciphertext.data(), &outLen, plaintext, static_cast<int>(plaintextLen));
    int ciphertextLen = outLen;

    EVP_EncryptFinal_ex(ctx, ciphertext.data() + outLen, &outLen);
    ciphertextLen += outLen;
    ciphertext.resize(ciphertextLen);

    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data());
    EVP_CIPHER_CTX_free(ctx);

    // Build container
    std::vector<uint8_t> container;
    container.reserve(4 + 2 + 2 + 2 + 32 + 4 + 4 + 12 + ciphertextLen + 16);

    // Magic
    container.insert(container.end(), AIDI_MAGIC.begin(), AIDI_MAGIC.end());

    // Format version
    uint16_t version = hdr.formatVersion;
    container.push_back(static_cast<uint8_t>((version >> 8) & 0xFF));
    container.push_back(static_cast<uint8_t>(version & 0xFF));

    // KDF algorithm
    uint16_t kdfAlgo = hdr.kdfAlgorithm;
    container.push_back(static_cast<uint8_t>((kdfAlgo >> 8) & 0xFF));
    container.push_back(static_cast<uint8_t>(kdfAlgo & 0xFF));

    // Cipher algorithm
    uint16_t cipherAlgo = hdr.cipherAlgorithm;
    container.push_back(static_cast<uint8_t>((cipherAlgo >> 8) & 0xFF));
    container.push_back(static_cast<uint8_t>(cipherAlgo & 0xFF));

    // Salt (32 bytes)
    container.insert(container.end(), salt.begin(), salt.end());

    // KDF iterations (uint32 big-endian)
    uint32_t iters = hdr.kdfIterations;
    container.push_back(static_cast<uint8_t>((iters >> 24) & 0xFF));
    container.push_back(static_cast<uint8_t>((iters >> 16) & 0xFF));
    container.push_back(static_cast<uint8_t>((iters >> 8) & 0xFF));
    container.push_back(static_cast<uint8_t>(iters & 0xFF));

    // KDF memory KiB (uint32 big-endian)
    uint32_t memKiB = hdr.kdfMemoryKiB;
    container.push_back(static_cast<uint8_t>((memKiB >> 24) & 0xFF));
    container.push_back(static_cast<uint8_t>((memKiB >> 16) & 0xFF));
    container.push_back(static_cast<uint8_t>((memKiB >> 8) & 0xFF));
    container.push_back(static_cast<uint8_t>(memKiB & 0xFF));

    // Nonce (12 bytes)
    container.insert(container.end(), nonce.begin(), nonce.end());

    // Ciphertext
    container.insert(container.end(), ciphertext.begin(), ciphertext.end());

    // Auth tag (16 bytes)
    container.insert(container.end(), tag.begin(), tag.end());

    return container;
}

// ============================================================================
// Decryption
// ============================================================================

std::optional<Json::Value> AiDiaryFormat::Decrypt(
        const std::vector<uint8_t>& container,
        const std::string& agentSecret) {

    // Minimum size: magic(4) + version(2) + kdf(2) + cipher(2) + salt(32) +
    //               iters(4) + mem(4) + nonce(12) + tag(16) = 78
    // Plus at least 1 byte of ciphertext
    if (container.size() < 79) return std::nullopt;

    // Check magic
    if (container[0] != AIDI_MAGIC[0] || container[1] != AIDI_MAGIC[1] ||
        container[2] != AIDI_MAGIC[2] || container[3] != AIDI_MAGIC[3]) {
        return std::nullopt;
    }

    size_t offset = 4;

    // Parse header
    uint16_t formatVersion = (static_cast<uint16_t>(container[offset]) << 8) | container[offset + 1];
    offset += 2;
    if (formatVersion != 1) return std::nullopt;  // Unsupported version

    uint16_t kdfAlgorithm = (static_cast<uint16_t>(container[offset]) << 8) | container[offset + 1];
    offset += 2;
    if (kdfAlgorithm != 0) return std::nullopt;  // Unsupported KDF

    uint16_t cipherAlgorithm = (static_cast<uint16_t>(container[offset]) << 8) | container[offset + 1];
    offset += 2;
    if (cipherAlgorithm != 0) return std::nullopt;  // Unsupported cipher

    // Salt
    std::vector<uint8_t> salt(32);
    std::copy(container.begin() + offset, container.begin() + offset + 32, salt.begin());
    offset += 32;

    // KDF iterations
    uint32_t kdfIterations = (static_cast<uint32_t>(container[offset]) << 24) |
                              (static_cast<uint32_t>(container[offset + 1]) << 16) |
                              (static_cast<uint32_t>(container[offset + 2]) << 8) |
                              static_cast<uint32_t>(container[offset + 3]);
    offset += 4;

    // KDF memory KiB
    uint32_t kdfMemoryKiB = (static_cast<uint32_t>(container[offset]) << 24) |
                              (static_cast<uint32_t>(container[offset + 1]) << 16) |
                              (static_cast<uint32_t>(container[offset + 2]) << 8) |
                              static_cast<uint32_t>(container[offset + 3]);
    offset += 4;

    // Nonce
    std::vector<uint8_t> nonce(12);
    std::copy(container.begin() + offset, container.begin() + offset + 12, nonce.begin());
    offset += 12;

    // Ciphertext = total - offset - 16 (tag)
    size_t tagOffset = container.size() - 16;
    std::vector<uint8_t> ciphertext(container.begin() + offset, container.begin() + tagOffset);

    // Auth tag
    std::vector<uint8_t> tag(16);
    std::copy(container.begin() + tagOffset, container.end(), tag.begin());

    // Derive key
    auto key = DeriveKey(agentSecret, salt, kdfIterations, kdfMemoryKiB);

    // Decrypt with AES-256-GCM
    std::vector<uint8_t> plaintext(ciphertext.size());
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr);
    EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data());

    int outLen = 0;
    EVP_DecryptUpdate(ctx, plaintext.data(), &outLen, ciphertext.data(), static_cast<int>(ciphertext.size()));
    int plaintextLen = outLen;

    // Set expected tag
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag.data());

    int finalLen = 0;
    int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + outLen, &finalLen);
    EVP_CIPHER_CTX_free(ctx);

    if (ret != 1) {
        // Authentication failed — wrong key or corrupted data
        return std::nullopt;
    }

    plaintextLen += finalLen;
    plaintext.resize(plaintextLen);

    // Parse JSON
    std::string jsonStr(reinterpret_cast<const char*>(plaintext.data()), plaintext.size());
    Json::Value payload;
    Json::CharReaderBuilder rb;
    std::istringstream stream(jsonStr);
    std::string parseErr;
    if (!Json::parseFromStream(rb, stream, &payload, &parseErr)) {
        return std::nullopt;
    }

    return payload;
}

// ============================================================================
// Container detection
// ============================================================================

bool AiDiaryFormat::IsAiDiaryContainer(const std::vector<uint8_t>& data) {
    if (data.size() < 4) return false;
    return data[0] == AIDI_MAGIC[0] && data[1] == AIDI_MAGIC[1] &&
           data[2] == AIDI_MAGIC[2] && data[3] == AIDI_MAGIC[3];
}

} // namespace animus::kernel
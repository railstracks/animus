#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

namespace animus::kernel {

// ============================================================================
// .aidiary encrypted container format
//
// This format is used for export/import only. Primary storage is SQL.
// The container provides encrypted backup/transfer capability.
//
// Format:
//   [4 bytes]  Magic: "AIDI"
//   [2 bytes]  Format version (uint16, currently 1)
//   [2 bytes]  KDF algorithm (0 = Argon2id)
//   [2 bytes]  Cipher algorithm (0 = AES-256-GCM)
//   [32 bytes] Salt
//   [4 bytes]  Argon2id iterations (uint32)
//   [4 bytes]  Argon2id memory KiB (uint32)
//   [16 bytes] Nonce/IV
//   [variable] Encrypted payload (AES-256-GCM authenticated ciphertext)
//   [16 bytes]  GCM auth tag
// ============================================================================

struct AiDiaryContainerHeader {
    uint16_t formatVersion{1};
    uint16_t kdfAlgorithm{0};      // 0 = Argon2id
    uint16_t cipherAlgorithm{0};    // 0 = AES-256-GCM
    uint32_t kdfIterations{3};      // Argon2id iterations (time cost)
    uint32_t kdfMemoryKiB{65536};   // Argon2id memory cost (64 MB)
    std::vector<uint8_t> salt;       // 32 bytes
    std::vector<uint8_t> nonce;      // 12 bytes (GCM standard)
};

class AiDiaryFormat {
public:
    // Encrypt a JSON payload into .aidiary binary format
    // Returns the complete container bytes (header + ciphertext + tag)
    static std::vector<uint8_t> Encrypt(
        const Json::Value& payload,
        const std::string& agentSecret,
        const AiDiaryContainerHeader& header = {});

    // Decrypt a .aidiary binary container into JSON payload
    // Returns empty optional on failure (wrong key, corrupted data, etc.)
    static std::optional<Json::Value> Decrypt(
        const std::vector<uint8_t>& container,
        const std::string& agentSecret);

    // Check if a binary blob starts with the AIDI magic bytes
    static bool IsAiDiaryContainer(const std::vector<uint8_t>& data);

    // Derive a 256-bit key from agent secret + salt using Argon2id
    // Note: actual Argon2id implementation requires linking argon2 library.
    // For v1, this uses a SHA-256 based KDF as a placeholder that can be
    // upgraded to Argon2id when the dependency is added.
    static std::vector<uint8_t> DeriveKey(
        const std::string& agentSecret,
        const std::vector<uint8_t>& salt,
        uint32_t iterations,
        uint32_t memoryKiB);

private:
    static constexpr size_t SALT_SIZE = 32;
    static constexpr size_t NONCE_SIZE = 12;  // GCM standard nonce
    static constexpr size_t TAG_SIZE = 16;    // GCM auth tag
};

} // namespace animus::kernel
// test_hkdf.cpp - Verify HKDF against Node.js reference
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>

std::vector<uint8_t> hkdf_derive(
    const uint8_t* ikm, size_t ikm_len,
    const uint8_t* salt, size_t salt_len,
    const uint8_t* info, size_t info_len,
    size_t output_len)
{
    // RFC 5869: if no salt is provided, use HashLen zeros (32 bytes for SHA-256)
    std::vector<uint8_t> defaultSalt;
    if (salt_len == 0) {
        defaultSalt.resize(32, 0);
        salt = defaultSalt.data();
        salt_len = defaultSalt.size();
    }

    std::vector<uint8_t> output(output_len);

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!ctx) {
        fprintf(stderr, "EVP_PKEY_CTX_new_id failed\n");
        return {};
    }

    if (EVP_PKEY_derive_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_salt(ctx, salt, salt_len) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_key(ctx, ikm, ikm_len) <= 0 ||
        (info_len > 0 && EVP_PKEY_CTX_add1_hkdf_info(ctx, info, info_len) <= 0))
    {
        fprintf(stderr, "HKDF setup failed\n");
        EVP_PKEY_CTX_free(ctx);
        return {};
    }

    size_t out_len = output_len;
    if (EVP_PKEY_derive(ctx, output.data(), &out_len) <= 0) {
        fprintf(stderr, "HKDF derive failed\n");
        EVP_PKEY_CTX_free(ctx);
        return {};
    }

    EVP_PKEY_CTX_free(ctx);
    return output;
}

void print_hex(const char* label, const uint8_t* data, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) printf("%02x", data[i]);
    printf("\n");
}

int main() {
    // IKM from diagnostic
    const char* ikm_hex = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                           "17582b7e2da892a1e096938cb4b9014e571152662266bbf5ecbc48c7b93c7867"
                           "785792c560f69d40c60b53fba646ebc978bda3d2141a60c182bfdd6ae4a33419"
                           "15d201970c2271e560794a0f31ff77164be3a9b93b3a33b4c329cc9a32d5001e";

    // Parse hex IKM
    uint8_t ikm[128];
    for (int i = 0; i < 128; i++) {
        unsigned int byte;
        sscanf(ikm_hex + i*2, "%2x", &byte);
        ikm[i] = (uint8_t)byte;
    }

    // Salt: 32 zeros
    uint8_t zeros[32] = {0};

    // Info: "WhisperText"
    const uint8_t info[] = "WhisperText";
    size_t info_len = 11;  // strlen("WhisperText")

    printf("=== HKDF Test ===\n");
    print_hex("IKM", ikm, 128);
    print_hex("Salt", zeros, 32);
    printf("Info: WhisperText (11 bytes)\n\n");

    // Test 1: Explicit zeros(32) salt
    auto result1 = hkdf_derive(ikm, 128, zeros, 32, info, info_len, 64);
    print_hex("Root key (zeros salt)", result1.data(), 32);
    print_hex("Chain key (zeros salt)", result1.data() + 32, 32);

    // Test 2: No salt (nullptr, 0) - should use default zeros
    auto result2 = hkdf_derive(ikm, 128, nullptr, 0, info, info_len, 64);
    print_hex("Root key (no salt)", result2.data(), 32);
    print_hex("Chain key (no salt)", result2.data() + 32, 32);

    // Expected from Node.js:
    printf("\nExpected root key:  1824e1209ad3b8df221edb4b33c84a55f867d5bc158273d6d678cd85d6c301bc\n");
    printf("Expected chain key: 90e3aa86adff6ac68b3c91baec93307ed94cf8884e3a46f0742f7fe3cf2cf078\n");

    // Test 3: What about the HKDF info with the length byte?
    // Signal uses HKDF with info = "WhisperText" (11 bytes)
    // But some implementations append a null terminator or length prefix
    printf("\nInfo hex: ");
    for (size_t i = 0; i < info_len; i++) printf("%02x", info[i]);
    printf(" (len=%zu)\n", info_len);

    // Test with 12-byte info (including null terminator)
    auto result3 = hkdf_derive(ikm, 128, zeros, 32, info, 12, 64);
    print_hex("Root key (12-byte info)", result3.data(), 32);

    return 0;
}
// WhatsAppCryptoHelpers.cpp — Crypto and encoding helpers for WhatsApp protocol
// Extracted from WhatsAppGatewayLoop.cpp for reuse and testability.
// ============================================================================

#include "animus_kernel/whatsapp/WhatsAppCryptoHelpers.h"

#include <chrono>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <atomic>

namespace animus::whatsapp {

std::vector<uint8_t> hmacSHA256(const std::vector<uint8_t>& key,
                                const std::vector<uint8_t>& data) {
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int resultLen = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         data.data(), static_cast<int>(data.size()), result, &resultLen);
    return {result, result + resultLen};
}

std::vector<uint8_t> concatBytes(const std::vector<uint8_t>& a,
                                 const std::vector<uint8_t>& b) {
    std::vector<uint8_t> result = a;
    result.insert(result.end(), b.begin(), b.end());
    return result;
}

std::vector<uint8_t> concatBytes3(const std::vector<uint8_t>& a,
                                  const std::vector<uint8_t>& b,
                                  const std::vector<uint8_t>& c) {
    std::vector<uint8_t> result = a;
    result.insert(result.end(), b.begin(), b.end());
    result.insert(result.end(), c.begin(), c.end());
    return result;
}

std::vector<uint8_t> prefixBytes(const uint8_t* prefix, size_t prefixLen,
                                 const std::vector<uint8_t>& data) {
    std::vector<uint8_t> result(prefix, prefix + prefixLen);
    result.insert(result.end(), data.begin(), data.end());
    return result;
}

std::vector<uint8_t> base64Decode(const std::string& input) {
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> result;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[base64_chars[i]] = i;

    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

std::string base64Encode(const std::vector<uint8_t>& data) {
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = (data[i] << 16) |
                     (i + 1 < data.size() ? data[i + 1] << 8 : 0) |
                     (i + 2 < data.size() ? data[i + 2] : 0);
        result += b64[(n >> 18) & 0x3f];
        result += b64[(n >> 12) & 0x3f];
        result += (i + 1 < data.size()) ? b64[(n >> 6) & 0x3f] : '=';
        result += (i + 2 < data.size()) ? b64[n & 0x3f] : '=';
    }
    return result;
}

std::string base64UrlEncode(const std::string& data) {
    std::vector<uint8_t> bytes(data.begin(), data.end());
    auto b64 = base64Encode(bytes);
    for (auto& c : b64) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!b64.empty() && b64.back() == '=') b64.pop_back();
    return b64;
}

std::string bytesToHex(const std::vector<uint8_t>& bytes) {
    std::string s;
    for (auto b : bytes) { char buf[3]; snprintf(buf, 3, "%02x", b); s += buf; }
    return s;
}

std::string generateTag() {
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return std::to_string(now) + "." + std::to_string(counter++);
}

} // namespace animus::whatsapp
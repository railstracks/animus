// WhatsAppCryptoHelpers.h — Crypto and encoding helpers for WhatsApp protocol
// Extracted from WhatsAppGatewayLoop.cpp for reuse and testability.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace animus::whatsapp {

// ADV signature prefixes (from Baileys)
inline constexpr uint8_t ADV_ACCOUNT_SIG_PREFIX[] = {6, 0};
inline constexpr uint8_t ADV_DEVICE_SIG_PREFIX[] = {6, 1};
inline constexpr uint8_t ADV_HOSTED_ACCOUNT_SIG_PREFIX[] = {6, 5};
inline constexpr uint8_t ADV_HOSTED_DEVICE_SIG_PREFIX[] = {6, 6};
inline constexpr uint8_t KEY_BUNDLE_TYPE[] = {5};

// HMAC-SHA256
std::vector<uint8_t> hmacSHA256(const std::vector<uint8_t>& key,
                                 const std::vector<uint8_t>& data);

// Concatenate byte vectors
std::vector<uint8_t> concatBytes(const std::vector<uint8_t>& a,
                                 const std::vector<uint8_t>& b);

std::vector<uint8_t> concatBytes3(const std::vector<uint8_t>& a,
                                  const std::vector<uint8_t>& b,
                                  const std::vector<uint8_t>& c);

// Prepend a byte prefix to data
std::vector<uint8_t> prefixBytes(const uint8_t* prefix, size_t prefixLen,
                                 const std::vector<uint8_t>& data);

// Base64 encode/decode
std::vector<uint8_t> base64Decode(const std::string& input);
std::string base64Encode(const std::vector<uint8_t>& data);
std::string base64UrlEncode(const std::string& data);

// Hex encode
std::string bytesToHex(const std::vector<uint8_t>& bytes);

// Generate unique message tag
std::string generateTag();

} // namespace animus::whatsapp
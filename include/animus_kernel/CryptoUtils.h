#pragma once

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>

namespace animus::kernel::crypto {

// Compute HMAC-SHA256 and return as hex string.
inline std::string HmacSha256Hex(const std::string& key, const std::string& message) {
    unsigned char digest[32];
    unsigned int digestLen = 0;
    HMAC(EVP_sha256(),
         reinterpret_cast<const unsigned char*>(key.data()), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(message.data()), message.size(),
         digest, &digestLen);

    std::ostringstream ss;
    for (unsigned int i = 0; i < digestLen; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    return ss.str();
}

// Constant-time hex string comparison.
inline bool ConstantTimeCompare(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char result = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        result |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return result == 0;
}

// Generate cryptographically secure random hex string.
inline std::string RandomHex(size_t bytes) {
    std::vector<unsigned char> buf(bytes);
    RAND_bytes(buf.data(), static_cast<int>(bytes));
    std::ostringstream ss;
    for (size_t i = 0; i < bytes; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buf[i]);
    }
    return ss.str();
}

// SHA-256 hash, returned as hex string.
inline std::string Sha256Hex(const std::string& data) {
    unsigned char digest[32];
    unsigned int digestLen = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, digest, &digestLen);
    EVP_MD_CTX_free(ctx);

    std::ostringstream ss;
    for (unsigned int i = 0; i < digestLen; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    return ss.str();
}

// ---------------------------------------------------------------------------
// AES-256-GCM seal/open (#23 secrets vault). Envelope layout (hex-encoded):
//   [0]      version byte (0x01)
//   [1..12]  12-byte random nonce
//   [13..]   ciphertext || 16-byte GCM tag
// AAD is empty; the vault binds entries to packages by row, not by ciphertext.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// #275: version-parameterized seal/open. Version byte 0x01 = vault
// master-key era; 0x02 = federation epoch-DEK era. Old (v01-only) kernels
// fail CLASSIFIED on 0x02 rows ("unsupported envelope version"), never as
// garbage GCM auth errors — mixed-version deferral per the #182 design.
// ---------------------------------------------------------------------------
inline std::string Aes256GcmSealVersion(const std::vector<unsigned char>& key32,
                                        const std::string& plaintext,
                                        unsigned char version,
                                        std::string& error) {
    error.clear();
    if (key32.size() != 32) { error = "seal: key must be 32 bytes"; return {}; }
    unsigned char nonce[12];
    if (RAND_bytes(nonce, sizeof(nonce)) != 1) { error = "seal: RAND_bytes failed"; return {}; }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) { error = "seal: ctx alloc failed"; return {}; }
    std::string envelope;
    do {
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(nonce), nullptr) != 1 ||
            EVP_EncryptInit_ex(ctx, nullptr, nullptr, key32.data(), nonce) != 1) {
            error = "seal: encrypt init failed"; break;
        }
        std::vector<unsigned char> ct(plaintext.size() + 16);
        int len = 0, total = 0;
        if (!plaintext.empty() &&
            EVP_EncryptUpdate(ctx, ct.data(), &len,
                              reinterpret_cast<const unsigned char*>(plaintext.data()),
                              static_cast<int>(plaintext.size())) != 1) {
            error = "seal: encrypt update failed"; break;
        }
        total = len;
        if (EVP_EncryptFinal_ex(ctx, ct.data() + total, &len) != 1) {
            error = "seal: encrypt final failed"; break;
        }
        total += len;
        unsigned char tag[16];
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof(tag), tag) != 1) {
            error = "seal: get tag failed"; break;
        }
        static const char* hex = "0123456789abcdef";
        envelope.reserve(2 + 2 * (sizeof(nonce) + total + sizeof(tag)));
        envelope += hex[version >> 4];
        envelope += hex[version & 0xF];
        for (unsigned char b : nonce) { envelope += hex[b >> 4]; envelope += hex[b & 0xF]; }
        for (int i = 0; i < total; ++i) { envelope += hex[ct[i] >> 4]; envelope += hex[ct[i] & 0xF]; }
        for (unsigned char b : tag) { envelope += hex[b >> 4]; envelope += hex[b & 0xF]; }
    } while (false);
    EVP_CIPHER_CTX_free(ctx);
    return envelope;
}

inline std::string Aes256GcmSeal(const std::vector<unsigned char>& key32,
                                 const std::string& plaintext,
                                 std::string& error) {
    return Aes256GcmSealVersion(key32, plaintext, 0x01, error);
}

inline bool Aes256GcmOpenVersioned(const std::vector<unsigned char>& key32,
                                   const std::string& envelopeHex,
                                   std::string& plaintext,
                                   std::string& error,
                                   unsigned char& versionOut) {
    error.clear();
    versionOut = 0;
    plaintext.clear();
    if (key32.size() != 32) { error = "open: key must be 32 bytes"; return false; }
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (envelopeHex.size() < 2 || envelopeHex.size() % 2 != 0) {
        error = "open: malformed envelope"; return false;
    }
    // Size discipline: envelopes come from the vault DB; a hostile/garbage
    // row must not drive huge allocations. 2 MB hex = 1 MB plaintext ceiling —
    // credentials are bytes-to-KB, never more.
    if (envelopeHex.size() > 2 * 1024 * 1024) {
        error = "open: envelope exceeds size limit"; return false;
    }
    std::vector<unsigned char> raw;
    raw.reserve(envelopeHex.size() / 2);
    for (size_t i = 0; i < envelopeHex.size(); i += 2) {
        int hi = hexVal(envelopeHex[i]), lo = hexVal(envelopeHex[i + 1]);
        if (hi < 0 || lo < 0) { error = "open: malformed envelope"; return false; }
        raw.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    if ((raw[0] != 0x01 && raw[0] != 0x02) || raw.size() < 1 + 12 + 16) {
        error = "open: unsupported envelope version or truncated"; return false;
    }
    versionOut = raw[0];
    const unsigned char* nonce = raw.data() + 1;
    const unsigned char* ct = raw.data() + 13;
    const size_t ctLen = raw.size() - 13 - 16;
    if (ctLen > static_cast<size_t>(std::numeric_limits<int>::max())) {
        error = "open: envelope exceeds size limit"; return false;
    }
    const unsigned char* tag = ct + ctLen;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) { error = "open: ctx alloc failed"; return false; }
    bool ok = false;
    do {
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 ||
            EVP_DecryptInit_ex(ctx, nullptr, nullptr, key32.data(), nonce) != 1) {
            error = "open: decrypt init failed"; break;
        }
        int len = 0, total = 0;
        std::vector<unsigned char> pt(ctLen + 16);  // decrypt target; Final may add a partial block
        if (ctLen > 0 &&
            EVP_DecryptUpdate(ctx, pt.data(), &len, ct,
                              static_cast<int>(ctLen)) != 1) {
            error = "open: decrypt update failed"; break;
        }
        total = len;
        // Supply the expected tag, then Final authenticates against it.
        // (Decrypt uses SET_TAG — GET_TAG is the encrypt-side operation.)
        unsigned char expectedTag[16];
        std::memcpy(expectedTag, tag, sizeof(expectedTag));
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, sizeof(expectedTag),
                                expectedTag) != 1) {
            error = "open: set tag failed"; break;
        }
        if (EVP_DecryptFinal_ex(ctx, pt.data() + total, &len) != 1) {
            // Auth failure — wrong key or tampered ciphertext.
            error = "open: authentication failed (wrong key or tampered ciphertext)";
            break;
        }
        total += len;
        plaintext.assign(reinterpret_cast<const char*>(pt.data()), total);
        ok = true;
    } while (false);
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

// ---------------------------------------------------------------------------
// X25519 key agreement + HKDF-SHA256 (#182 federation secrets, ticket #272).
// Lifted from the in-tree stack idioms (OpenSSL EVP X25519 per
// SignalCrypto.cpp; EVP_PKEY_HKDF per WhatsAppNoise.cpp) — no new dependency.
// Fail-loud discipline throughout: peer keys that yield an all-zero shared
// secret (RFC 7748 §6.1 small-order points) error out, never resolve empty.
// ---------------------------------------------------------------------------
struct X25519KeyPair {
    std::array<unsigned char, 32> private_key{};
    std::array<unsigned char, 32> public_key{};
};

inline bool GenerateX25519KeyPair(X25519KeyPair& out, std::string& error) {
    error.clear();
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    if (!ctx) { error = "x25519: ctx alloc failed"; return false; }
    EVP_PKEY* pkey = nullptr;
    bool ok = false;
    do {
        if (EVP_PKEY_keygen_init(ctx) != 1) { error = "x25519: keygen init failed"; break; }
        if (EVP_PKEY_keygen(ctx, &pkey) != 1) { error = "x25519: keygen failed"; break; }
        size_t privLen = out.private_key.size(), pubLen = out.public_key.size();
        if (EVP_PKEY_get_raw_private_key(pkey, out.private_key.data(), &privLen) != 1 ||
            EVP_PKEY_get_raw_public_key(pkey, out.public_key.data(), &pubLen) != 1) {
            error = "x25519: raw key export failed"; break;
        }
        if (privLen != 32 || pubLen != 32) { error = "x25519: unexpected key length"; break; }
        ok = true;
    } while (false);
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);
    return ok;
}

inline bool X25519SharedSecret(const std::array<unsigned char, 32>& own_private,
                               const std::array<unsigned char, 32>& peer_public,
                               std::array<unsigned char, 32>& out,
                               std::string& error) {
    error.clear();
    out.fill(0);
    EVP_PKEY* own = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr,
                                                 own_private.data(), own_private.size());
    EVP_PKEY* peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr,
                                                 peer_public.data(), peer_public.size());
    EVP_PKEY_CTX* ctx = own ? EVP_PKEY_CTX_new(own, nullptr) : nullptr;
    bool ok = false;
    do {
        if (!own || !peer || !ctx) { error = "x25519: key load failed"; break; }
        if (EVP_PKEY_derive_init(ctx) != 1) { error = "x25519: derive init failed"; break; }
        if (EVP_PKEY_derive_set_peer(ctx, peer) != 1) { error = "x25519: set peer failed"; break; }
        size_t len = out.size();
        if (EVP_PKEY_derive(ctx, out.data(), &len) != 1) {
            error = "x25519: derive failed"; break;
        }
        if (len != 32) { error = "x25519: unexpected shared length"; break; }
        // Contributory check, explicit (RFC 7748 §6.1): an all-zero shared
        // secret means a small-order/invalid peer point — reject loudly even
        // on builds where the derive call itself would not.
        bool allZero = true;
        for (unsigned char b : out) { if (b != 0) { allZero = false; break; } }
        if (allZero) { error = "x25519: all-zero shared secret rejected"; out.fill(0); break; }
        ok = true;
    } while (false);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(peer);
    EVP_PKEY_free(own);
    return ok;
}

inline bool Aes256GcmOpen(const std::vector<unsigned char>& key32,
                          const std::string& envelopeHex,
                          std::string& plaintext,
                          std::string& error) {
    unsigned char version = 0;
    return Aes256GcmOpenVersioned(key32, envelopeHex, plaintext, error, version);
}

// HKDF-SHA256 (extract+expand, RFC 5869). Empty salt means the RFC zero
// salt; empty IKM routes through a manual HMAC path because OpenSSL's EVP
// HKDF requires at least one key byte.
inline std::vector<unsigned char> HkdfSha256(const std::vector<unsigned char>& ikm,
                                             const std::vector<unsigned char>& salt,
                                             const std::vector<unsigned char>& info,
                                             size_t length,
                                             std::string& error) {
    error.clear();
    std::vector<unsigned char> okm;
    if (length == 0) return okm;
    if (length > 255u * 32u) { error = "hkdf: length exceeds 255*SHA256"; return okm; }
    if (!ikm.empty()) {
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
        if (!ctx) { error = "hkdf: ctx alloc failed"; return okm; }
        okm.resize(length);
        bool ok = false;
        do {
            if (EVP_PKEY_derive_init(ctx) != 1) { error = "hkdf: init failed"; break; }
            if (EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) != 1 ||
                EVP_PKEY_CTX_set_hkdf_mode(ctx, EVP_PKEY_HKDEF_MODE_EXTRACT_AND_EXPAND) != 1) {
                error = "hkdf: mode set failed"; break;
            }
            if (!salt.empty() &&
                EVP_PKEY_CTX_set1_hkdf_salt(ctx, salt.data(), static_cast<int>(salt.size())) != 1) {
                error = "hkdf: salt set failed"; break;
            }
            if (!info.empty() &&
                EVP_PKEY_CTX_add1_hkdf_info(ctx, info.data(), static_cast<int>(info.size())) != 1) {
                error = "hkdf: info set failed"; break;
            }
            if (EVP_PKEY_CTX_set1_hkdf_key(ctx, ikm.data(), static_cast<int>(ikm.size())) != 1) {
                error = "hkdf: key set failed"; break;
            }
            size_t len = length;
            if (EVP_PKEY_derive(ctx, okm.data(), &len) != 1) { error = "hkdf: derive failed"; break; }
            ok = true;
        } while (false);
        EVP_PKEY_CTX_free(ctx);
        if (!ok) { okm.clear(); return okm; }
        return okm;
    }
    // Manual HMAC path for empty IKM.
    auto hmac = [](const unsigned char* key, size_t keyLen,
                   const unsigned char* msg, size_t msgLen,
                   unsigned char out32[32]) {
        unsigned int outLen = 0;
        HMAC(EVP_sha256(), key, static_cast<int>(keyLen), msg, msgLen, out32, &outLen);
        return outLen == 32;
    };
    static const unsigned char kEmpty[1] = {0};  // non-null pointer, zero length
    std::array<unsigned char, 32> prk{};
    const unsigned char* saltKey = salt.empty() ? prk.data() : salt.data();
    size_t saltLen = salt.empty() ? prk.size() : salt.size();  // absent salt = 32 zero bytes
    if (!hmac(saltKey, saltLen, kEmpty, 0, prk.data())) {
        error = "hkdf: extract failed"; return okm;
    }
    std::vector<unsigned char> t;
    okm.reserve(length);
    unsigned char block[32];
    for (unsigned char counter = 1; okm.size() < length; ++counter) {
        std::vector<unsigned char> msg(t);
        msg.insert(msg.end(), info.begin(), info.end());
        msg.push_back(counter);
        if (!hmac(prk.data(), prk.size(), msg.data(), msg.size(), block)) {
            error = "hkdf: expand failed"; okm.clear(); return okm;
        }
        size_t take = std::min<size_t>(32, length - okm.size());
        okm.insert(okm.end(), block, block + take);
        t.assign(block, block + 32);
    }
    return okm;
}

} // namespace animus::kernel::crypto

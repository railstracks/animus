// CryptoUtils primitive tests — X25519 + HKDF-SHA256 (#272, from the #182
// federation-secrets design). Vectors: RFC 7748 §5.2 / §6.1, RFC 5869 case 1.
#include "animus_kernel/CryptoUtils.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace animus::kernel::crypto;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::vector<unsigned char> Unhex(const std::string& hex) {
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<unsigned char> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        int hi = val(hex[i]), lo = val(hex[i + 1]);
        if (hi < 0 || lo < 0) break;
        out.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    return out;
}

std::string Hex(const unsigned char* data, size_t len) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < len; ++i) {
        out += hex[data[i] >> 4];
        out += hex[data[i] & 0xF];
    }
    return out;
}

std::array<unsigned char, 32> Arr(const std::string& hex) {
    auto bytes = Unhex(hex);
    std::array<unsigned char, 32> out{};
    if (bytes.size() == 32) std::copy(bytes.begin(), bytes.end(), out.begin());
    return out;
}

// RFC 7748 §5.2: one-iteration X25519 scalar-multiplication vector.
void TestX25519ScalarMult() {
    std::cerr << "[1/5] X25519 RFC 7748 5.2 scalar mult\n";
    auto k = Arr("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4");
    auto u = Arr("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c");
    std::array<unsigned char, 32> out{};
    std::string err;
    Assert(X25519SharedSecret(k, u, out, err), "scalarmult succeeds: " + err);
    Assert(Hex(out.data(), 32) ==
               "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552",
           "RFC 7748 5.2 output");
}

// RFC 7748 §6.1: Diffie-Hellman exchange — both directions agree on K.
void TestX25519Dh() {
    std::cerr << "[2/5] X25519 RFC 7748 6.1 DH exchange\n";
    auto aPriv = Arr("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    auto aPub = Arr("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    auto bPriv = Arr("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    auto bPub = Arr("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    auto expected = Arr("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    std::array<unsigned char, 32> sA{}, sB{};
    std::string err;
    Assert(X25519SharedSecret(aPriv, bPub, sA, err), "alice side: " + err);
    Assert(X25519SharedSecret(bPriv, aPub, sB, err), "bob side: " + err);
    Assert(std::memcmp(sA.data(), sB.data(), 32) == 0, "both sides agree");
    Assert(std::memcmp(sA.data(), expected.data(), 32) == 0, "RFC 7748 6.1 shared K");
}

void TestX25519Keygen() {
    std::cerr << "[3/5] X25519 keypair generation\n";
    X25519KeyPair kp, peer;
    std::string err;
    Assert(GenerateX25519KeyPair(kp, err), "generate: " + err);
    Assert(GenerateX25519KeyPair(peer, err), "peer generate: " + err);
    // Public key = X25519(private, base point 9) — validates the pair.
    auto base = Arr("09" + std::string(62, '0'));
    std::array<unsigned char, 32> derivedPub{};
    Assert(X25519SharedSecret(kp.private_key, base, derivedPub, err),
           "scalarmult-base: " + err);
    Assert(std::memcmp(derivedPub.data(), kp.public_key.data(), 32) == 0,
           "public key = X25519(priv, 9)");
    // Full ECDH between two generated pairs.
    std::array<unsigned char, 32> s1{}, s2{};
    Assert(X25519SharedSecret(kp.private_key, peer.public_key, s1, err), "ECDH A->B: " + err);
    Assert(X25519SharedSecret(peer.private_key, kp.public_key, s2, err), "ECDH B->A: " + err);
    Assert(std::memcmp(s1.data(), s2.data(), 32) == 0, "symmetric shared secret");
}

// RFC 5869 test case 1 (SHA-256).
void TestHkdfRfc5869() {
    std::cerr << "[4/5] HKDF-SHA256 RFC 5869 test case 1\n";
    std::vector<unsigned char> ikm(22, 0x0b);
    auto salt = Unhex("000102030405060708090a0b0c");
    auto info = Unhex("f0f1f2f3f4f5f6f7f8f9");
    std::string err;
    auto okm = HkdfSha256(ikm, salt, info, 42, err);
    Assert(err.empty(), "no error: " + err);
    Assert(okm.size() == 42, "42-byte OKM");
    Assert(Hex(okm.data(), okm.size()) ==
               "3cb25f25faacd57a90434f64d0362f2a"
               "2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
               "34007208d5b887185865",
           "RFC 5869 case 1 OKM");
}

void TestHkdfEdges() {
    std::cerr << "[5/5] HKDF edge paths\n";
    std::vector<unsigned char> ikm(16, 0x2a);
    std::vector<unsigned char> empty;
    std::string err;
    // Absent salt == explicit 32-zero-byte salt (RFC 5869 2.3).
    auto a = HkdfSha256(ikm, empty, empty, 32, err);
    Assert(err.empty(), "empty salt/info: " + err);
    auto b = HkdfSha256(ikm, std::vector<unsigned char>(32, 0), empty, 32, err);
    Assert(err.empty(), "zero salt: " + err);
    Assert(a == b, "absent salt == zero salt");
    // Empty IKM routes through the manual HMAC path without error.
    auto c = HkdfSha256(empty, empty, empty, 32, err);
    Assert(err.empty() && c.size() == 32, "empty ikm manual path: " + err);
    // Length cap fails loudly.
    auto d = HkdfSha256(ikm, empty, empty, 255u * 32u + 1, err);
    Assert(!err.empty() && d.empty(), "length cap errors");
}

} // namespace

int main() {
    TestX25519ScalarMult();
    TestX25519Dh();
    TestX25519Keygen();
    TestHkdfRfc5869();
    TestHkdfEdges();
    if (g_failures == 0) std::cerr << "CryptoUtilsTests: all passed\n";
    return g_failures == 0 ? 0 : 1;
}

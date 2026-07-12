// WhatsAppNoise.cpp - Noise XX protocol for WhatsApp transport
// Implements Noise_XX_25519_AESGCM_SHA256 handshake + transport encryption
// ============================================================================

#include "animus_kernel/whatsapp/WhatsAppNoise.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <stdexcept>
#include <cstring>
#include <iostream>

namespace animus::whatsapp {

// ---------------------------------------------------------------------------
// Crypto primitives
// ---------------------------------------------------------------------------

KeyPair generateKeyPair() {
    KeyPair kp;
    kp.pub.resize(32);
    kp.priv.resize(32);

    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    if (!ctx) throw std::runtime_error("failed to create X25519 context");
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_keygen(ctx, &pkey);

    size_t pubLen = 32, privLen = 32;
    EVP_PKEY_get_raw_public_key(pkey, kp.pub.data(), &pubLen);
    EVP_PKEY_get_raw_private_key(pkey, kp.priv.data(), &privLen);

    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);
    return kp;
}

std::vector<uint8_t> curve25519Shared(const std::vector<uint8_t>& priv,
                                       const std::vector<uint8_t>& pub) {
    std::vector<uint8_t> shared(32);

    EVP_PKEY* privKey = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_X25519, nullptr, priv.data(), 32);
    EVP_PKEY* pubKey = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_X25519, nullptr, pub.data(), 32);

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(privKey, nullptr);
    EVP_PKEY_derive_init(ctx);
    EVP_PKEY_derive_set_peer(ctx, pubKey);

    size_t len = 32;
    EVP_PKEY_derive(ctx, shared.data(), &len);

    EVP_PKEY_free(privKey);
    EVP_PKEY_free(pubKey);
    EVP_PKEY_CTX_free(ctx);
    return shared;
}

std::vector<uint8_t> sha256(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hash(32);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, hash.data(), nullptr);
    EVP_MD_CTX_free(ctx);
    return hash;
}

static std::vector<uint8_t> generateIV(uint32_t counter) {
    std::vector<uint8_t> iv(IV_LENGTH, 0);
    iv[8]  = (counter >> 24) & 0xff;
    iv[9]  = (counter >> 16) & 0xff;
    iv[10] = (counter >> 8) & 0xff;
    iv[11] = counter & 0xff;
    return iv;
}

std::vector<uint8_t> aesEncryptGCM(const std::vector<uint8_t>& plaintext,
                                     const std::vector<uint8_t>& key,
                                     const std::vector<uint8_t>& iv,
                                     const std::vector<uint8_t>& aad) {
    std::vector<uint8_t> out(plaintext.size() + 16);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data());
    if (!aad.empty()) {
        int aadLen = 0;  // OpenSSL 3.0 requires non-null outl even when out is null
        EVP_EncryptUpdate(ctx, nullptr, &aadLen, aad.data(), static_cast<int>(aad.size()));
    }

    int len = 0;
    EVP_EncryptUpdate(ctx, out.data(), &len, plaintext.data(), static_cast<int>(plaintext.size()));
    int ctLen = len;
    EVP_EncryptFinal_ex(ctx, out.data() + ctLen, &len);
    ctLen += len;
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, out.data() + ctLen);
    out.resize(ctLen + 16);
    EVP_CIPHER_CTX_free(ctx);
    return out;
}

std::vector<uint8_t> aesDecryptGCM(const std::vector<uint8_t>& ciphertext,
                                     const std::vector<uint8_t>& key,
                                     const std::vector<uint8_t>& iv,
                                     const std::vector<uint8_t>& aad) {
    if (ciphertext.size() < 16) throw std::runtime_error("ciphertext too short");
    size_t ctLen = ciphertext.size() - 16;
    std::vector<uint8_t> out(ctLen);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data());
    if (!aad.empty()) {
        int aadLen = 0;  // OpenSSL 3.0 requires non-null outl even when out is null
        EVP_DecryptUpdate(ctx, nullptr, &aadLen, aad.data(), static_cast<int>(aad.size()));
    }

    int len = 0;
    EVP_DecryptUpdate(ctx, out.data(), &len, ciphertext.data(), static_cast<int>(ctLen));
    int ptLen = len;
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
                         const_cast<uint8_t*>(ciphertext.data() + ctLen));
    int ret = EVP_DecryptFinal_ex(ctx, out.data() + ptLen, &len);
    EVP_CIPHER_CTX_free(ctx);
    if (ret <= 0) throw std::runtime_error("GCM decryption failed");
    out.resize(ptLen + len);
    return out;
}

std::vector<uint8_t> hkdfExpand(const std::vector<uint8_t>& ikm, size_t length,
                                  const std::vector<uint8_t>& salt,
                                  const std::string& info) {
    std::vector<uint8_t> out(length);
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    EVP_PKEY_derive_init(ctx);
    EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256());
    EVP_PKEY_CTX_set_hkdf_mode(ctx, EVP_PKEY_HKDEF_MODE_EXTRACT_AND_EXPAND);
    if (!salt.empty())
        EVP_PKEY_CTX_set1_hkdf_salt(ctx, salt.data(), static_cast<int>(salt.size()));
    EVP_PKEY_CTX_add1_hkdf_info(ctx,
        reinterpret_cast<const unsigned char*>(info.c_str()),
        static_cast<int>(info.size()));
    // Handle empty IKM — OpenSSL needs at least 1 byte. Use HMAC-based manual HKDF.
    if (ikm.empty()) {
        EVP_PKEY_CTX_free(ctx);
        // Manual HKDF Extract + Expand
        unsigned int mdLen = 32;
        uint8_t prk[32];
        HMAC(EVP_sha256(), salt.data(), salt.size(), nullptr, 0, prk, &mdLen);
        out.resize(0);
        std::vector<uint8_t> t;
        uint8_t counter = 0;
        while (out.size() < length) {
            counter++;
            std::vector<uint8_t> hmac_input;
            hmac_input.insert(hmac_input.end(), t.begin(), t.end());
            hmac_input.insert(hmac_input.end(), info.begin(), info.end());
            hmac_input.push_back(counter);
            t.resize(32);
            HMAC(EVP_sha256(), prk, 32, hmac_input.data(), hmac_input.size(), t.data(), &mdLen);
            out.insert(out.end(), t.begin(), t.end());
        }
        out.resize(length);
        return out;
    }
    EVP_PKEY_CTX_set1_hkdf_key(ctx, ikm.data(), static_cast<int>(ikm.size()));
    size_t outLen = length;
    EVP_PKEY_derive(ctx, out.data(), &outLen);
    EVP_PKEY_CTX_free(ctx);
    return out;
}

// ---------------------------------------------------------------------------
// NoiseState
// ---------------------------------------------------------------------------

NoiseState::NoiseState() = default;
NoiseState::~NoiseState() { delete transport_; }

void NoiseState::init(const KeyPair& ephemeralKey,
                       const std::vector<uint8_t>& noiseHeader) {
    ephemeral_ = ephemeralKey;
    noiseHeader_ = noiseHeader;

    // Initialize hash from NOISE_MODE
    // Baileys: const data = Buffer.from(NOISE_MODE)
    //           let hash = data.byteLength === 32 ? data : sha256(data)
    // NOISE_MODE is "Noise_XX_25519_AESGCM_SHA256\0\0\0\0" = 32 bytes exactly
    // So hash = NOISE_MODE directly, NOT SHA256(NOISE_MODE)
    std::string modeStr(NOISE_MODE);
    modeStr.append(4, '\0');
    auto modeBytes = std::vector<uint8_t>(modeStr.begin(), modeStr.end());
    if (modeBytes.size() == 32) {
        hash_ = modeBytes;  // Use directly, no hashing
    } else {
        hash_ = sha256(modeBytes);
    }
    salt_ = hash_;
    encKey_ = hash_;
    counter_ = 0;

    // Baileys: authenticate(NOISE_HEADER); authenticate(publicKey)
    // These are called in makeNoiseHandler, before any handshake
    std::cerr << "[noise] Before WA_HEADER: hash[0..8]=";
    for (int i = 0; i < 8; i++) fprintf(stderr, "%02x", hash_[i]);
    std::cerr << std::endl;
    authenticate(noiseHeader_);
    std::cerr << "[noise] After WA_HEADER: hash[0..8]=";
    for (int i = 0; i < 8; i++) fprintf(stderr, "%02x", hash_[i]);
    std::cerr << std::endl;
    authenticate(ephemeral_.pub);
    std::cerr << "[noise] After ephPub: hash[0..8]=";
    for (int i = 0; i < 8; i++) fprintf(stderr, "%02x", hash_[i]);
    std::cerr << std::endl;
    std::cerr << "[noise] Init complete. hash[0..4]=";
    for (int i = 0; i < 4; i++) fprintf(stderr, "%02x", hash_[i]);
    std::cerr << " salt[0..4]=";
    for (int i = 0; i < 4; i++) fprintf(stderr, "%02x", salt_[i]);
    std::cerr << " encKey[0..4]=";
    for (int i = 0; i < 4; i++) fprintf(stderr, "%02x", encKey_[i]);
    std::cerr << std::endl;
    // Dump full keypair for offline comparison (file avoids interleaved log output)
    {
        FILE* kf = fopen("/tmp/wa-noise-keys.txt", "w");
        if (kf) {
            fprintf(kf, "EPH_PRIV:"); for (auto b : ephemeral_.priv) fprintf(kf, "%02x", b); fprintf(kf, "\n");
            fprintf(kf, "EPH_PUB:");  for (auto b : ephemeral_.pub)  fprintf(kf, "%02x", b); fprintf(kf, "\n");
            fclose(kf);
        }
    }
    std::cerr << "[noise] EPH_PRIV: ";
    for (auto b : ephemeral_.priv) fprintf(stderr, "%02x", b);
    std::cerr << std::endl;
    std::cerr << "[noise] EPH_PUB: ";
    for (auto b : ephemeral_.pub) fprintf(stderr, "%02x", b);
    std::cerr << std::endl;
    std::cerr << "[noise] HASH: ";
    for (auto b : hash_) fprintf(stderr, "%02x", b);
    std::cerr << std::endl;
}

void NoiseState::authenticate(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> combined;
    combined.reserve(hash_.size() + data.size());
    combined.insert(combined.end(), hash_.begin(), hash_.end());
    combined.insert(combined.end(), data.begin(), data.end());
    hash_ = sha256(combined);
}

void NoiseState::mixIntoKey(const std::vector<uint8_t>& data) {
    auto derived = hkdfExpand(data, 64, salt_, "");
    salt_.assign(derived.begin(), derived.begin() + 32);
    encKey_.assign(derived.begin() + 32, derived.end());
    counter_ = 0;
    std::cerr << "[noise] mixIntoKey: salt[0..4]=";
    for (int i = 0; i < 4; i++) fprintf(stderr, "%02x", salt_[i]);
    std::cerr << " encKey[0..4]=";
    for (int i = 0; i < 4; i++) fprintf(stderr, "%02x", encKey_[i]);
    std::cerr << " counter=0" << std::endl;
}

NoiseState::ClientFinishData NoiseState::processServerHello(
    const std::vector<uint8_t>& serverEphemeral,
    const std::vector<uint8_t>& serverStatic,
    const std::vector<uint8_t>& serverPayload,
    const KeyPair& noiseKey
) {
    ClientFinishData result;

    // Step 1: Authenticate server ephemeral + DH(ephemeral.priv, serverEph)
    authenticate(serverEphemeral);
    std::cerr << "[noise] After auth(serverEph), counter=" << counter_ << std::endl;
    auto dh1 = curve25519Shared(ephemeral_.priv, serverEphemeral);
    std::cerr << "[noise] DH1 shared[0..4]: ";
    for (int i = 0; i < 4; i++) fprintf(stderr, "%02x", dh1[i]);
    std::cerr << std::endl;
    mixIntoKey(dh1);
    std::cerr << "[noise] After DH1 + mixIntoKey, counter=" << counter_ << std::endl;

    // Step 2: Decrypt server static + DH(ephemeral.priv, serverStatic)
    std::cerr << "[noise] Decrypting server static (" << serverStatic.size() << " bytes) encKey[0..4]=";
    for (int i = 0; i < 4; i++) fprintf(stderr, "%02x", encKey_[i]);
    std::cerr << " counter=" << counter_ << " hash[0..4]=";
    for (int i = 0; i < 4; i++) fprintf(stderr, "%02x", hash_[i]);
    std::cerr << std::endl;

    // DEBUG: Try decrypting with raw OpenSSL to isolate the issue
    {
        auto iv = generateIV(0);
        std::cerr << "[noise] DEBUG encKey: ";
        for (auto b : encKey_) fprintf(stderr, "%02x", b);
        std::cerr << std::endl;
        std::cerr << "[noise] DEBUG hash: ";
        for (auto b : hash_) fprintf(stderr, "%02x", b);
        std::cerr << std::endl;
        std::cerr << "[noise] DEBUG iv: ";
        for (auto b : iv) fprintf(stderr, "%02x", b);
        std::cerr << std::endl;
        std::cerr << "[noise] DEBUG ct: ";
        for (auto b : serverStatic) fprintf(stderr, "%02x", b);
        std::cerr << std::endl;
        try {
            auto result = aesDecryptGCM(serverStatic, encKey_, iv, hash_);
            std::cerr << "[noise] DEBUG raw decrypt succeeded!" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[noise] DEBUG raw decrypt failed: " << e.what() << std::endl;
        }
    }

    auto decStatic = decrypt(serverStatic);
    auto dh2 = curve25519Shared(ephemeral_.priv, decStatic);
    mixIntoKey(dh2);

    // Step 3: Decrypt server payload (cert chain)
    // Baileys: const certDecoded = decrypt(serverHello.payload)
    // We skip cert verification for now
    if (!serverPayload.empty()) {
        auto certPayload = decrypt(serverPayload);
        (void)certPayload;
    }

    // Step 4: Encrypt our noise static public key
    // Baileys: const keyEnc = encrypt(noiseKey.public)
    //          mixIntoKey(Curve.sharedKey(noiseKey.private, serverHello.ephemeral))
    result.static_enc = encrypt(noiseKey.pub);
    auto dh3 = curve25519Shared(noiseKey.priv, serverEphemeral);
    mixIntoKey(dh3);

    staticKey_ = noiseKey;
    return result;
}

// Client payload encryption (called separately, after processServerHello)
std::vector<uint8_t> NoiseState::encryptClientPayload(const std::vector<uint8_t>& payload) {
    return encrypt(payload);
}

void NoiseState::finishHandshake() {
    auto derived = hkdfExpand({}, 64, salt_, "");
    transport_ = new TransportKeys();
    transport_->encKey.assign(derived.begin(), derived.begin() + 32);
    transport_->decKey.assign(derived.begin() + 32, derived.end());
    std::cerr << "[noise] finishHandshake: encKey=";
    for (size_t i = 0; i < 8; i++) fprintf(stderr, "%02x", transport_->encKey[i]);
    std::cerr << "... decKey=";
    for (size_t i = 0; i < 8; i++) fprintf(stderr, "%02x", transport_->decKey[i]);
    std::cerr << "..." << std::endl;
}

std::vector<uint8_t> NoiseState::encrypt(const std::vector<uint8_t>& plaintext) {
    auto iv = generateIV(transport_ ? transport_->writeCounter++ : counter_++);
    const auto& key = transport_ ? transport_->encKey : encKey_;
    if (!transport_) {
        auto result = aesEncryptGCM(plaintext, key, iv, hash_);
        authenticate(result);
        return result;
    }
    return aesEncryptGCM(plaintext, key, iv);
}

std::vector<uint8_t> NoiseState::decrypt(const std::vector<uint8_t>& ciphertext) {
    uint32_t counter = transport_ ? transport_->readCounter : counter_;
    auto iv = generateIV(transport_ ? transport_->readCounter++ : counter_++);
    const auto& key = transport_ ? transport_->decKey : encKey_;
    std::cerr << "[noise] decrypt: " << ciphertext.size() << " bytes, counter=" << counter
              << " transport=" << (transport_ ? "yes" : "no") << std::endl;
    if (!transport_) {
        auto result = aesDecryptGCM(ciphertext, key, iv, hash_);
        authenticate(ciphertext);
        return result;
    }
    try {
        auto result = aesDecryptGCM(ciphertext, key, iv);
        return result;
    } catch (const std::exception& e) {
        std::cerr << "[noise] GCM decryption failed at counter " << counter << ": " << e.what() << std::endl;
        std::cerr << "[noise] Failed ciphertext hex: ";
        for (size_t i = 0; i < std::min(size_t(32), ciphertext.size()); i++) fprintf(stderr, "%02x", ciphertext[i]);
        std::cerr << "..." << std::endl;
        throw;
    }
}

std::vector<uint8_t> NoiseState::encodeFrame(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> framed_data;
    if (transport_) {
        framed_data = encrypt(payload);
    } else {
        std::cerr << "[noise] encodeFrame: transport_ is NULL! Sending " << payload.size() << " bytes unencrypted" << std::endl;
        framed_data = payload;
    }

    size_t introSize = sentIntro_ ? 0 : noiseHeader_.size();
    std::vector<uint8_t> frame(introSize + 3 + framed_data.size());

    size_t offset = 0;
    if (!sentIntro_) {
        std::memcpy(frame.data(), noiseHeader_.data(), noiseHeader_.size());
        offset = noiseHeader_.size();
        sentIntro_ = true;
    }

    frame[offset]     = (framed_data.size() >> 16) & 0xff;
    frame[offset + 1] = (framed_data.size() >> 8) & 0xff;
    frame[offset + 2] = framed_data.size() & 0xff;
    std::memcpy(frame.data() + offset + 3, framed_data.data(), framed_data.size());

    return frame;
}

std::vector<uint8_t> NoiseState::decodeFrame(const std::vector<uint8_t>& frame) {
    if (frame.size() < 3)
        throw std::runtime_error("frame too short");
    size_t len = (frame[0] << 16) | (frame[1] << 8) | frame[2];
    if (frame.size() < 3 + len)
        throw std::runtime_error("frame truncated");
    return std::vector<uint8_t>(frame.begin() + 3, frame.begin() + 3 + len);
}

} // namespace animus::whatsapp

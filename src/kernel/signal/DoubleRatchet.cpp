// DoubleRatchet.cpp — Signal Protocol Double Ratchet implementation
//
// All crypto via OpenSSL 3.x EVP API.

#include "animus_kernel/signal/DoubleRatchet.h"

#include <iostream>

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/core_names.h>
#include <openssl/params.h>

#include <cstring>
#include <stdexcept>
#include <algorithm>

namespace animus::signal {

// ============================================================================
// Error helper
// ============================================================================

static void throw_openssl(const char* msg) {
    char buf[256];
    ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
    throw std::runtime_error(std::string(msg) + ": " + buf);
}

// ============================================================================
// DH key pair generation
// ============================================================================

DHKeyPair DHKeyPair::generate() {
    DHKeyPair kp;
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "X25519", nullptr);
    if (!ctx) throw_openssl("EVP_PKEY_CTX_new_from_name");

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw_openssl("EVP_PKEY_keygen_init");
    }
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw_openssl("EVP_PKEY_keygen");
    }
    EVP_PKEY_CTX_free(ctx);

    size_t pub_len = KEY_SIZE, priv_len = KEY_SIZE;
    EVP_PKEY_get_octet_string_param(pkey, OSSL_PKEY_PARAM_PUB_KEY,
                                     kp.public_key.data(), pub_len, &pub_len);
    EVP_PKEY_get_octet_string_param(pkey, OSSL_PKEY_PARAM_PRIV_KEY,
                                     kp.private_key.data(), priv_len, &priv_len);
    EVP_PKEY_free(pkey);
    return kp;
}

// ============================================================================
// X25519 DH
// ============================================================================

std::array<uint8_t, KEY_SIZE> dh_compute(
    const DHPrivateKey& private_key,
    const DHPublicKey& public_key)
{
    std::array<uint8_t, KEY_SIZE> shared;
    // Load private key via EVP_PKEY_new_raw_private_key
    EVP_PKEY* priv = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_X25519, nullptr,
        private_key.data(), KEY_SIZE);
    if (!priv) throw_openssl("EVP_PKEY_new_raw_private_key");

    // Load public key via EVP_PKEY_new_raw_public_key
    EVP_PKEY* pub = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_X25519, nullptr,
        public_key.data(), KEY_SIZE);
    if (!pub) {
        EVP_PKEY_free(priv);
        throw_openssl("EVP_PKEY_new_raw_public_key");
    }

    // Derive shared secret
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(priv, nullptr);
    if (EVP_PKEY_derive_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx); EVP_PKEY_free(priv); EVP_PKEY_free(pub);
        throw_openssl("EVP_PKEY_derive_init");
    }
    if (EVP_PKEY_derive_set_peer(ctx, pub) <= 0) {
        EVP_PKEY_CTX_free(ctx); EVP_PKEY_free(priv); EVP_PKEY_free(pub);
        throw_openssl("EVP_PKEY_derive_set_peer");
    }

    size_t secret_len = KEY_SIZE;
    if (EVP_PKEY_derive(ctx, shared.data(), &secret_len) <= 0) {
        EVP_PKEY_CTX_free(ctx); EVP_PKEY_free(priv); EVP_PKEY_free(pub);
        throw_openssl("EVP_PKEY_derive");
    }

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(priv);
    EVP_PKEY_free(pub);
    return shared;
}

// ============================================================================
// HKDF-SHA256
// ============================================================================

std::vector<uint8_t> hkdf_derive(
    const uint8_t* ikm, size_t ikm_len,
    const uint8_t* salt, size_t salt_len,
    const uint8_t* info, size_t info_len,
    size_t output_len)
{
    // RFC 5869: if no salt is provided, use HashLen zeros (32 bytes for SHA-256)
    // OpenSSL EVP_PKEY_HKDF does NOT default to zeros when salt is not set,
    // so we must explicitly provide zeros.
    std::vector<uint8_t> defaultSalt;
    if (salt_len == 0) {
        defaultSalt.resize(32, 0);
        salt = defaultSalt.data();
        salt_len = defaultSalt.size();
    }

    std::vector<uint8_t> output(output_len);

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!ctx) throw_openssl("EVP_PKEY_CTX_new_id HKDF");

    if (EVP_PKEY_derive_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_salt(ctx, salt, salt_len) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_key(ctx, ikm, ikm_len) <= 0 ||
        (info_len > 0 && EVP_PKEY_CTX_add1_hkdf_info(ctx, info, info_len) <= 0))
    {
        EVP_PKEY_CTX_free(ctx);
        throw_openssl("HKDF setup");
    }

    size_t out_len = output_len;
    if (EVP_PKEY_derive(ctx, output.data(), &out_len) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw_openssl("HKDF derive");
    }
    EVP_PKEY_CTX_free(ctx);
    return output;
}

// ============================================================================
// AES-256-GCM
// ============================================================================

AesGcmCiphertext aes_gcm_encrypt(
    const std::array<uint8_t, KEY_SIZE>& key,
    const std::vector<uint8_t>& plaintext)
{
    AesGcmCiphertext ct;
    ct.ciphertext.resize(plaintext.size());

    // Generate random nonce
    RAND_bytes(ct.nonce.data(), NONCE_SIZE);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw_openssl("EVP_CIPHER_CTX_new");

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) <= 0 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, NONCE_SIZE, nullptr) <= 0 ||
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), ct.nonce.data()) <= 0)
    {
        EVP_CIPHER_CTX_free(ctx);
        throw_openssl("AES-GCM init");
    }

    int len = 0;
    if (EVP_EncryptUpdate(ctx, ct.ciphertext.data(), &len,
                          plaintext.data(), plaintext.size()) <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        throw_openssl("AES-GCM encrypt");
    }

    int ciphertext_len = len;
    if (EVP_EncryptFinal_ex(ctx, ct.ciphertext.data() + len, &len) <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        throw_openssl("AES-GCM final");
    }
    ciphertext_len += len;

    // Get tag
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, ct.tag.data()) <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        throw_openssl("AES-GCM get tag");
    }

    EVP_CIPHER_CTX_free(ctx);
    ct.ciphertext.resize(ciphertext_len);
    return ct;
}

std::vector<uint8_t> aes_gcm_decrypt(
    const std::array<uint8_t, KEY_SIZE>& key,
    const AesGcmCiphertext& ct)
{
    std::vector<uint8_t> plaintext(ct.ciphertext.size());

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw_openssl("EVP_CIPHER_CTX_new");

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) <= 0 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, NONCE_SIZE, nullptr) <= 0 ||
        EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(),
                           const_cast<uint8_t*>(ct.nonce.data())) <= 0)
    {
        EVP_CIPHER_CTX_free(ctx);
        throw_openssl("AES-GCM decrypt init");
    }

    int len = 0;
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                          ct.ciphertext.data(), ct.ciphertext.size()) <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        throw_openssl("AES-GCM decrypt update");
    }
    int plaintext_len = len;

    // Set expected tag
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE,
                            const_cast<uint8_t*>(ct.tag.data())) <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        throw_openssl("AES-GCM set tag");
    }

    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AES-GCM authentication failed");
    }
    plaintext_len += len;

    EVP_CIPHER_CTX_free(ctx);
    plaintext.resize(plaintext_len);
    return plaintext;
}

// ============================================================================
// HMAC-SHA256
// ============================================================================

std::array<uint8_t, 32> hmac_sha256(
    const uint8_t* key, size_t key_len,
    const uint8_t* data, size_t data_len)
{
    std::array<uint8_t, 32> out;
    size_t out_len = 32;

    EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(mac);

    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string("digest", const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_END
    };

    if (EVP_MAC_init(ctx, key, key_len, params) <= 0 ||
        EVP_MAC_update(ctx, data, data_len) <= 0 ||
        EVP_MAC_final(ctx, out.data(), &out_len, out.size()) <= 0)
    {
        EVP_MAC_CTX_free(ctx);
        EVP_MAC_free(mac);
        throw_openssl("HMAC-SHA256");
    }

    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    return out;
}

// ============================================================================
// Ratchet key derivation
// ============================================================================

static const uint8_t KDF_RK_INFO[] = "WhisperRatchet";
static const size_t KDF_RK_INFO_LEN = 14;

static const uint8_t KDF_CK_INFO = 0x02;
static const uint8_t KDF_CK_MSG = 0x01;

RootAndChain kdf_rk(const RootKey& root_key, const std::array<uint8_t, KEY_SIZE>& dh_output) {
    // Signal spec: HKDF(IKM=dh_output, salt=root_key, info="WhisperRatchet", 64)
    // NOT: HKDF(root_key+dh_output, zeros(32))
    auto derived = hkdf_derive(dh_output.data(), dh_output.size(),
                               root_key.data(), root_key.size(),
                               KDF_RK_INFO, KDF_RK_INFO_LEN,
                               KEY_SIZE * 2);

    RootAndChain result;
    std::copy(derived.begin(), derived.begin() + KEY_SIZE, result.root_key.begin());
    std::copy(derived.begin() + KEY_SIZE, derived.end(), result.chain_key.begin());
    return result;
}

ChainAndMessage kdf_ck(const ChainKey& chain_key) {
    // Message key = HMAC(ck, 0x01)
    // Next chain key = HMAC(ck, 0x02)
    ChainAndMessage result;
    result.message_key = hmac_sha256(chain_key.data(), KEY_SIZE, &KDF_CK_MSG, 1);
    ChainKey next_ck = hmac_sha256(chain_key.data(), KEY_SIZE, &KDF_CK_INFO, 1);
    result.chain_key = next_ck;
    return result;
}

// ============================================================================
// Serialization helpers
// ============================================================================

static void write_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back((v >> 24) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back(v & 0xFF);
}

static uint32_t read_u32(const uint8_t* data, size_t max_len, size_t& offset) {
    if (offset + 4 > max_len) return 0;
    uint32_t v = (uint32_t(data[offset]) << 24) | (uint32_t(data[offset+1]) << 16) |
                 (uint32_t(data[offset+2]) << 8) | uint32_t(data[offset+3]);
    offset += 4;
    return v;
}

static void write_bytes(std::vector<uint8_t>& buf, const uint8_t* data, size_t len) {
    write_u32(buf, static_cast<uint32_t>(len));
    buf.insert(buf.end(), data, data + len);
}

static bool read_bytes(const uint8_t* data, size_t max_len, size_t& offset,
                       std::vector<uint8_t>& out, size_t expected = 0) {
    if (offset + 4 > max_len) return false;
    uint32_t len = read_u32(data, max_len, offset);
    if (offset + len > max_len) return false;
    if (expected > 0 && len != expected) return false;
    out.assign(data + offset, data + offset + len);
    offset += len;
    return true;
}

static void write_fixed(std::vector<uint8_t>& buf, const uint8_t* data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

static void read_fixed(const uint8_t* data, size_t max_len, size_t& offset,
                       uint8_t* out, size_t len) {
    if (offset + len > max_len) return;
    std::memcpy(out, data + offset, len);
    offset += len;
}

// ============================================================================
// Ratchet state serialization
// ============================================================================

std::vector<uint8_t> RatchetState::serialize() const {
    std::vector<uint8_t> buf;

    // Version
    write_u32(buf, 1);

    // Self identity
    write_fixed(buf, self_identity.public_key.data(), KEY_SIZE);
    write_fixed(buf, self_identity.private_key.data(), KEY_SIZE);

    // Remote identity
    write_fixed(buf, remote_identity.data(), KEY_SIZE);

    // Self ratchet
    write_fixed(buf, self_ratchet.public_key.data(), KEY_SIZE);
    write_fixed(buf, self_ratchet.private_key.data(), KEY_SIZE);

    // Remote ratchet (optional)
    buf.push_back(remote_ratchet.has_value() ? 1 : 0);
    if (remote_ratchet) write_fixed(buf, remote_ratchet->data(), KEY_SIZE);

    // Root key
    write_fixed(buf, root_key.data(), KEY_SIZE);

    // Sending chain key (optional)
    buf.push_back(sending_chain_key.has_value() ? 1 : 0);
    if (sending_chain_key) write_fixed(buf, sending_chain_key->data(), KEY_SIZE);

    // Receiving chain key (optional)
    buf.push_back(receiving_chain_key.has_value() ? 1 : 0);
    if (receiving_chain_key) write_fixed(buf, receiving_chain_key->data(), KEY_SIZE);

    // Counters
    write_u32(buf, sending_count);
    write_u32(buf, receiving_count);
    write_u32(buf, prev_sending_count);

    // Skipped keys
    write_u32(buf, static_cast<uint32_t>(skipped_keys.size()));
    for (auto& sk : skipped_keys) {
        write_fixed(buf, sk.header_key.data(), KEY_SIZE);
        write_u32(buf, sk.message_number);
        write_fixed(buf, sk.message_key.data(), KEY_SIZE);
    }

    return buf;
}

std::optional<RatchetState> RatchetState::deserialize(const uint8_t* data, size_t len) {
    RatchetState state;
    size_t offset = 0;

    uint32_t version = read_u32(data, len, offset);
    if (version != 1) return std::nullopt;

    read_fixed(data, len, offset, state.self_identity.public_key.data(), KEY_SIZE);
    read_fixed(data, len, offset, state.self_identity.private_key.data(), KEY_SIZE);
    read_fixed(data, len, offset, state.remote_identity.data(), KEY_SIZE);
    read_fixed(data, len, offset, state.self_ratchet.public_key.data(), KEY_SIZE);
    read_fixed(data, len, offset, state.self_ratchet.private_key.data(), KEY_SIZE);

    bool has_remote_ratchet = (data[offset++] != 0);
    if (has_remote_ratchet) {
        DHPublicKey rr;
        read_fixed(data, len, offset, rr.data(), KEY_SIZE);
        state.remote_ratchet = rr;
    }

    read_fixed(data, len, offset, state.root_key.data(), KEY_SIZE);

    bool has_sending = (data[offset++] != 0);
    if (has_sending) {
        ChainKey ck;
        read_fixed(data, len, offset, ck.data(), KEY_SIZE);
        state.sending_chain_key = ck;
    }

    bool has_receiving = (data[offset++] != 0);
    if (has_receiving) {
        ChainKey ck;
        read_fixed(data, len, offset, ck.data(), KEY_SIZE);
        state.receiving_chain_key = ck;
    }

    state.sending_count = read_u32(data, len, offset);
    state.receiving_count = read_u32(data, len, offset);
    state.prev_sending_count = read_u32(data, len, offset);

    uint32_t num_skipped = read_u32(data, len, offset);
    for (uint32_t i = 0; i < num_skipped; ++i) {
        SkippedKey sk;
        read_fixed(data, len, offset, sk.header_key.data(), KEY_SIZE);
        sk.message_number = read_u32(data, len, offset);
        read_fixed(data, len, offset, sk.message_key.data(), KEY_SIZE);
        state.skipped_keys.push_back(std::move(sk));
    }

    return state;
}

// ============================================================================
// Ratchet operations
// ============================================================================

RatchetState ratchet_init_as_initiator(
    const DHKeyPair& our_identity,
    const DHPublicKey& their_identity,
    const RootKey& root_key,
    const DHPublicKey& their_ratchet_key)
{
    RatchetState state;
    state.self_identity = our_identity;
    state.remote_identity = their_identity;
    state.root_key = root_key;
    state.remote_ratchet = their_ratchet_key;

    // Generate new ratchet keypair
    state.self_ratchet = DHKeyPair::generate();

    // DH(ratchet_priv, remote_ratchet_pub)
    auto dh_out = dh_compute(state.self_ratchet.private_key, their_ratchet_key);

    // KDF_RK(root_key, dh_output) → new root_key + sending chain key
    auto [new_root, send_chain] = kdf_rk(root_key, dh_out);
    state.root_key = new_root;
    state.sending_chain_key = send_chain;
    state.receiving_chain_key = std::nullopt; // no receiving chain yet
    state.sending_count = 0;
    state.receiving_count = 0;
    state.prev_sending_count = 0;

    return state;
}

RatchetState ratchet_init_as_recipient(
    const DHKeyPair& our_identity,
    const DHPublicKey& their_identity,
    const RootKey& root_key,
    const DHKeyPair& our_ratchet)
{
    RatchetState state;
    state.self_identity = our_identity;
    state.remote_identity = their_identity;
    state.root_key = root_key;
    state.self_ratchet = our_ratchet;
    state.remote_ratchet = std::nullopt; // don't know their ratchet key yet
    state.sending_chain_key = std::nullopt;
    state.receiving_chain_key = std::nullopt;
    state.sending_count = 0;
    state.receiving_count = 0;
    state.prev_sending_count = 0;

    return state;
}

RatchetCiphertext ratchet_encrypt(
    RatchetState& state,
    const std::vector<uint8_t>& plaintext)
{
    if (!state.sending_chain_key.has_value()) {
        // Receiver (Bob) encrypting for the first time — need a DH ratchet step
        if (!state.remote_ratchet.has_value()) {
            throw std::runtime_error("no sending chain and no remote ratchet key — cannot encrypt");
        }

        // Generate new ratchet key pair
        state.self_ratchet = DHKeyPair::generate();

        // DH step: new_self_priv × remote_ratchet_pub
        auto dhOutput = dh_compute(state.self_ratchet.private_key, *state.remote_ratchet);

        // Derive new root key + sending chain key
        auto derived = hkdf_derive(
            dhOutput.data(), dhOutput.size(),
            state.root_key.data(), state.root_key.size(),
            reinterpret_cast<const uint8_t*>("WhisperRatchet"), 14,
            KEY_SIZE * 2);
        std::copy(derived.begin(), derived.begin() + KEY_SIZE, state.root_key.begin());
        ChainKey sendChain;
        std::copy(derived.begin() + KEY_SIZE, derived.end(), sendChain.begin());

        state.prev_sending_count = state.sending_count;
        state.sending_count = 0;
        state.sending_chain_key = sendChain;

        std::cerr << "[ratchet] DH step for first encrypt as receiver" << std::endl;
    }

    // Derive message key from chain key, advance chain
    auto [new_chain, msg_key] = kdf_ck(*state.sending_chain_key);
    state.sending_chain_key = new_chain;

    // Encrypt with message key
    std::array<uint8_t, KEY_SIZE> mk_array;
    std::copy(msg_key.begin(), msg_key.end(), mk_array.begin());
    auto gcm = aes_gcm_encrypt(mk_array, plaintext);

    RatchetCiphertext ct;
    ct.header_key = state.self_ratchet.public_key;
    ct.prev_count = state.prev_sending_count;
    ct.message_number = state.sending_count;
    ct.ciphertext = std::move(gcm.ciphertext);
    ct.nonce = gcm.nonce;
    ct.tag = gcm.tag;

    state.sending_count++;
    return ct;
}

// Try to skip messages and find the right key
static std::optional<std::array<uint8_t, KEY_SIZE>> try_skipped(
    RatchetState& state,
    const DHPublicKey& header_key,
    uint32_t message_number,
    const std::vector<uint8_t>& ciphertext,
    const std::array<uint8_t, NONCE_SIZE>& nonce,
    const std::array<uint8_t, TAG_SIZE>& tag)
{
    for (auto it = state.skipped_keys.begin(); it != state.skipped_keys.end(); ++it) {
        if (it->header_key == header_key && it->message_number == message_number) {
            // Found a matching skipped key — try to decrypt
            std::array<uint8_t, KEY_SIZE> mk = it->message_key;
            state.skipped_keys.erase(it);

            AesGcmCiphertext gcm;
            gcm.ciphertext = ciphertext;
            gcm.nonce = nonce;
            gcm.tag = tag;
            try {
                return mk; // return the key for caller to decrypt
            } catch (...) {
                return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

static void skip_message_keys(RatchetState& state, uint32_t until_n) {
    // Advance receiving chain until we reach 'until_n' counter
    // Storing skipped keys along the way
    if (state.receiving_chain_key.has_value()) {
        while (state.receiving_count < until_n) {
            auto [new_ck, msg_key] = kdf_ck(*state.receiving_chain_key);
            state.skipped_keys.push_back({
                *state.remote_ratchet,
                state.receiving_count,
                msg_key
            });
            state.receiving_chain_key = new_ck;
            state.receiving_count++;

            if (state.skipped_keys.size() > MAX_SKIP) {
                throw std::runtime_error("too many skipped messages");
            }
        }
    }
}

static void dh_ratchet_step(RatchetState& state, const DHPublicKey& their_new_key) {
    // Advance sending chain's prev count
    state.prev_sending_count = state.sending_count;

    // DH ratchet: derive new receiving chain
    state.remote_ratchet = their_new_key;
    auto dh_out = dh_compute(state.self_ratchet.private_key, *state.remote_ratchet);
    auto [new_root, recv_chain] = kdf_rk(state.root_key, dh_out);
    state.root_key = new_root;
    state.receiving_chain_key = recv_chain;
    state.receiving_count = 0;

    // New ratchet keyPair
    state.self_ratchet = DHKeyPair::generate();

    // Derive new sending chain
    auto dh_out2 = dh_compute(state.self_ratchet.private_key, *state.remote_ratchet);
    auto [new_root2, send_chain] = kdf_rk(state.root_key, dh_out2);
    state.root_key = new_root2;
    state.sending_chain_key = send_chain;
    state.sending_count = 0;
}

std::vector<uint8_t> ratchet_decrypt(
    RatchetState& state,
    const RatchetCiphertext& ct)
{
    // 1. Try skipped keys
    auto skipped_mk = try_skipped(state, ct.header_key, ct.message_number,
                                   ct.ciphertext, ct.nonce, ct.tag);
    if (skipped_mk) {
        AesGcmCiphertext gcm;
        gcm.ciphertext = ct.ciphertext;
        gcm.nonce = ct.nonce;
        gcm.tag = ct.tag;
        return aes_gcm_decrypt(*skipped_mk, gcm);
    }

    // 2. Check if header key is different from current remote ratchet → dh ratchet step
    bool new_ratchet = !state.remote_ratchet.has_value() ||
                       ct.header_key != *state.remote_ratchet;

    if (new_ratchet) {
        // Skip any remaining messages in the current receiving chain
        skip_message_keys(state, ct.prev_count);
        // Perform DH ratchet step
        dh_ratchet_step(state, ct.header_key);
    }

    // 3. Skip messages in the new receiving chain to reach message_number
    skip_message_keys(state, ct.message_number);

    // 4. Derive message key for this message
    if (!state.receiving_chain_key.has_value()) {
        throw std::runtime_error("no receiving chain after ratchet step");
    }

    auto [new_ck, msg_key] = kdf_ck(*state.receiving_chain_key);
    state.receiving_chain_key = new_ck;
    state.receiving_count++;

    // 5. Decrypt
    std::array<uint8_t, KEY_SIZE> mk_array;
    std::copy(msg_key.begin(), msg_key.end(), mk_array.begin());

    AesGcmCiphertext gcm;
    gcm.ciphertext = ct.ciphertext;
    gcm.nonce = ct.nonce;
    gcm.tag = ct.tag;
    return aes_gcm_decrypt(mk_array, gcm);
}

} // namespace animus::signal

#include "animus_kernel/signal/SignalCrypto.h"
#include "animus_kernel/signal/DoubleRatchet.h"
#include "animus_kernel/whatsapp/WAProto.h"

#include <sodium/crypto_hash_sha512.h>
#include <sodium/crypto_scalarmult_ed25519.h>
#include <sodium/crypto_core_ed25519.h>
#include <sodium/crypto_sign_ed25519.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <iostream>
#include <cstring>
#include <algorithm>
#include <openssl/rand.h>
#include <openssl/bn.h>

namespace animus::signal {

// Local serialization helpers for ratchet ciphertext header
namespace {

void write_u32_be(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back((v >> 24) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back(v & 0xFF);
}

uint32_t read_u32_be(const uint8_t* data) {
    return (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) |
           (uint32_t(data[2]) << 8)  | uint32_t(data[3]);
}

} // anonymous namespace

// ─── Real Signal Protocol implementation backed by DoubleRatchet ───
//
// Session records store serialized RatchetState.
// Key pairs use OpenSSL X25519.
// Encryption uses AES-256-GCM via the Double Ratchet.

static const char* TAG = "[signal::crypto]";

// Helper: load or create ratchet state from session record
static RatchetState load_ratchet(const SessionRecord& rec) {
    if (!rec.state.empty()) {
        auto opt = RatchetState::deserialize(rec.state.data(), rec.state.size());
        if (opt) return std::move(*opt);
    }
    throw std::runtime_error("cannot deserialize ratchet state");
}

static SessionRecord save_ratchet(const RatchetState& rs) {
    SessionRecord rec;
    rec.version = 1;
    rec.is_fresh = false;
    rec.state = rs.serialize();
    return rec;
}

Result<void> initialize_session(
    ISessionStore& sessions,
    IIdentityKeyStore& identity,
    const SignalAddress& remote,
    const PreKeyBundle& bundle)
{
    std::cerr << TAG << " initialize_session: remote=" << remote.to_string()
              << " device=" << bundle.device_id << std::endl;

    try {
        // Get our identity key
        auto our_kp = identity.get_identity_key_pair();
        DHKeyPair our_dh;
        std::memcpy(our_dh.public_key.data(), our_kp.public_key.data(), 32);
        std::memcpy(our_dh.private_key.data(), our_kp.private_key.data(), 32);

        // Their identity key
        DHPublicKey their_id;
        std::memcpy(their_id.data(), bundle.identity_key.public_key.data(), 32);

        // Perform X3DH-simplified key agreement:
        // For the initiator (us), we compute:
        // DH1 = DH(our_id_priv, their_id_pub)
        // DH2 = DH(our_id_priv, their_signed_prekey_pub)
        // DH3 = DH(our_ephemeral_priv, their_id_pub)
        // DH4 = DH(our_ephemeral_priv, their_signed_prekey_pub) [if prekey]
        // Then HKDF to derive root key

        auto dh1 = dh_compute(our_dh.private_key, their_id);

        DHPublicKey their_spk;
        std::memcpy(their_spk.data(), bundle.signed_pre_key.keypair.public_key.data(), 32);
        auto dh2 = dh_compute(our_dh.private_key, their_spk);

        DHKeyPair our_ephemeral = DHKeyPair::generate();
        auto dh3 = dh_compute(our_ephemeral.private_key, their_id);
        auto dh4 = dh_compute(our_ephemeral.private_key, their_spk);

        // Concatenate DH outputs as IKM
        std::vector<uint8_t> ikm;
        ikm.insert(ikm.end(), dh1.begin(), dh1.end());
        ikm.insert(ikm.end(), dh2.begin(), dh2.end());
        ikm.insert(ikm.end(), dh3.begin(), dh3.end());
        ikm.insert(ikm.end(), dh4.begin(), dh4.end());

        // Derive root key (explicit zeros(32) salt per Signal spec)
        std::array<uint8_t, 32> zeroSaltInit{};
        auto derived = hkdf_derive(ikm.data(), ikm.size(),
                                    zeroSaltInit.data(), zeroSaltInit.size(),
                                    reinterpret_cast<const uint8_t*>("WhisperText"),
                                    11,
                                    KEY_SIZE);

        RootKey root_key;
        std::copy(derived.begin(), derived.end(), root_key.begin());

        // Initialize ratchet as initiator
        DHPublicKey their_ratchet;
        std::memcpy(their_ratchet.data(), bundle.signed_pre_key.keypair.public_key.data(), 32);

        auto ratchet_state = ratchet_init_as_initiator(our_dh, their_id, root_key, their_ratchet);

        SessionRecord rec = save_ratchet(ratchet_state);
        sessions.store_session(remote, rec);

        std::cerr << TAG << " session initialized for " << remote.to_string() << std::endl;
        return Result<void>::Success();
    } catch (const std::exception& e) {
        return Result<void>::Fail(e.what());
    }
}

Result<void> initialize_session_as_receiver(
    ISessionStore& sessions,
    IIdentityKeyStore& identity,
    IPreKeyStore& preKeys,
    ISignedPreKeyStore& signedPreKeys,
    const SignalAddress& remote,
    const PreKeyBundle& bundle,
    uint32_t preKeyId)
{
    std::cerr << TAG << " initialize_session_as_receiver: remote=" << remote.to_string()
              << " preKeyId=" << preKeyId << std::endl;

    // Helper for hex dump
    auto hexDump = [](const std::string& label, const uint8_t* data, size_t len) {
        std::cerr << TAG << " [X3DH] " << label << " (" << len << " bytes): ";
        for (size_t i = 0; i < len; ++i) fprintf(stderr, "%02x", data[i]);
        std::cerr << std::endl;
    };

    try {
        // Get our identity key pair
        auto our_kp = identity.get_identity_key_pair();
        DHKeyPair our_id;
        std::memcpy(our_id.public_key.data(), our_kp.public_key.data(), 32);
        std::memcpy(our_id.private_key.data(), our_kp.private_key.data(), 32);

        // Get our signed pre-key
        auto our_spk = signedPreKeys.load_signed_pre_key(bundle.signed_pre_key.id);
        if (!our_spk) {
            return Result<void>::Fail("signed pre-key not found: " + std::to_string(bundle.signed_pre_key.id));
        }
        DHKeyPair our_spk_dh;
        std::memcpy(our_spk_dh.public_key.data(), our_spk->keypair.public_key.data(), 32);
        std::memcpy(our_spk_dh.private_key.data(), our_spk->keypair.private_key.data(), 32);

        // Get our one-time pre-key (if used)
        DHKeyPair our_opk_dh;
        bool has_opk = false;
        if (preKeyId > 0) {
            auto our_opk = preKeys.load_pre_key(preKeyId);
            if (our_opk) {
                std::memcpy(our_opk_dh.public_key.data(), our_opk->keypair.public_key.data(), 32);
                std::memcpy(our_opk_dh.private_key.data(), our_opk->keypair.private_key.data(), 32);
                has_opk = true;
            } else {
                return Result<void>::Fail("one-time pre-key not found: " + std::to_string(preKeyId)
                    + " — cannot complete X3DH without DH4");
            }
        }

        // Their keys from the PreKey message
        DHPublicKey their_id;     // bundle.identity_key
        std::memcpy(their_id.data(), bundle.identity_key.public_key.data(), 32);
        DHPublicKey their_ephemeral; // bundle signed_pre_key.keypair.public_key (sender's ephemeral/base key)
        std::memcpy(their_ephemeral.data(), bundle.signed_pre_key.keypair.public_key.data(), 32);

        // Diagnostic: log all key inputs
        hexDump("our_id.priv", our_id.private_key.data(), 32);
        hexDump("our_id.pub", our_id.public_key.data(), 32);
        hexDump("our_spk.priv", our_spk_dh.private_key.data(), 32);
        hexDump("our_spk.pub", our_spk_dh.public_key.data(), 32);
        hexDump("their_id (identity)", their_id.data(), 32);
        hexDump("their_ephemeral (base)", their_ephemeral.data(), 32);
        if (has_opk) {
            hexDump("our_opk.priv", our_opk_dh.private_key.data(), 32);
            hexDump("our_opk.pub", our_opk_dh.public_key.data(), 32);
        }

        // X3DH as RECEIVER:
        // DH1 = DH(our_identity_priv, their_signed_prekey/base_pub)
        //   = ECDH(I_B, E_A) ... but Signal DH1 = ECDH(I_A, SPK_B)
        //   We label this "dh1" but it's actually Signal DH2 (receiver perspective)
        // DH2 = DH(our_signed_prekey_priv, their_identity_pub)
        //   = ECDH(SPK_B, I_A) ... this is actually Signal DH1
        // DH3 = DH(our_signed_prekey_priv, their_ephemeral/base_pub)
        //   = ECDH(SPK_B, E_A)
        // DH4 = DH(our_opk_priv, their_ephemeral/base_pub) [if used]
        //   = ECDH(OPK_B, E_A)
        //
        // IKM layout: [0xFF*32] [DH1] [DH2] [DH3] [DH4?]
        // where DH1=ECDH(SPK_B, I_A), DH2=ECDH(I_B, E_A), DH3=ECDH(SPK_B, E_A)
        // Variable names are swapped but IKM assembly order is correct.

        auto dh1 = dh_compute(our_id.private_key, bundle.signed_pre_key.keypair.public_key);
        auto dh2 = dh_compute(our_spk_dh.private_key, their_id);
        auto dh3 = dh_compute(our_spk_dh.private_key, their_ephemeral);

        hexDump("DH1 (I_B, E_A)", dh1.data(), dh1.size());
        hexDump("DH2 (SPK_B, I_A)", dh2.data(), dh2.size());
        hexDump("DH3 (SPK_B, E_A)", dh3.data(), dh3.size());

        std::vector<uint8_t> ikm;
        // Disambiguation bytes
        ikm.resize(32, 0xFF);
        // DH1 (ECDH(SPK_B, I_A)) at offset 32
        ikm.insert(ikm.end(), dh2.begin(), dh2.end());
        // DH2 (ECDH(I_B, E_A)) at offset 64
        ikm.insert(ikm.end(), dh1.begin(), dh1.end());
        // DH3 at offset 96
        ikm.insert(ikm.end(), dh3.begin(), dh3.end());
        // DH4 (optional) at offset 128
        if (has_opk) {
            auto dh4 = dh_compute(our_opk_dh.private_key, their_ephemeral);
            hexDump("DH4 (OPK_B, E_A)", dh4.data(), dh4.size());
            ikm.insert(ikm.end(), dh4.begin(), dh4.end());
        }

        hexDump("IKM (full)", ikm.data(), ikm.size());

        // Derive root key (explicit zeros(32) salt per Signal spec)
        std::array<uint8_t, 32> zeroSaltRecv{};
        auto derived = hkdf_derive(ikm.data(), ikm.size(),
                                    zeroSaltRecv.data(), zeroSaltRecv.size(),
                                    reinterpret_cast<const uint8_t*>("WhisperText"),
                                    11,
                                    KEY_SIZE);

        RootKey root_key;
        std::copy(derived.begin(), derived.end(), root_key.begin());

        hexDump("root_key (derived)", root_key.data(), root_key.size());

        // Initialize ratchet as receiver
        // Our ratchet key = our signed pre-key pair (used as initial ratchet by receiver)

        auto ratchet_state = ratchet_init_as_recipient(our_id, their_id, root_key, our_spk_dh);

        SessionRecord rec = save_ratchet(ratchet_state);
        sessions.store_session(remote, rec);

        // Remove the used one-time pre-key
        if (has_opk && preKeyId > 0) {
            preKeys.remove_pre_key(preKeyId);
        }

        std::cerr << TAG << " receiver session initialized for " << remote.to_string() << std::endl;
        return Result<void>::Success();

    } catch (const std::exception& e) {
        return Result<void>::Fail(e.what());
    }
}

// ─── Helper functions for outgoing message serialization ───

// AES-256-CBC encryption (matches the Signal Protocol specification)
static std::vector<uint8_t> aes256CbcEncrypt_out(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& iv)
{
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    std::vector<uint8_t> ciphertext(plaintext.size() + 16);
    int len = 0, totalLen = 0;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) != 1 ||
        EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(), plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    totalLen = len;
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    totalLen += len;
    ciphertext.resize(totalLen);
    EVP_CIPHER_CTX_free(ctx);
    return ciphertext;
}

// Derive whisper message keys: [cipherKey(32), macKey(32), iv(32→16)]
// Same HKDF-Expand used in the decrypt path
static std::array<std::vector<uint8_t>, 3> deriveWhisperMessageKeysOut(const std::vector<uint8_t>& messageKey) {
    std::vector<uint8_t> salt(32, 0);
    std::string info = "WhisperMessageKeys";

    unsigned char prk[32];
    unsigned int prkLen = 0;
    HMAC(EVP_sha256(), salt.data(), 32, messageKey.data(), messageKey.size(), prk, &prkLen);

    // T1 = HMAC-SHA256(PRK, info || 0x01)
    std::vector<uint8_t> info1(info.size() + 1);
    std::copy(info.begin(), info.end(), info1.begin());
    info1[info.size()] = 0x01;
    unsigned char t1[32]; unsigned int t1Len = 0;
    HMAC(EVP_sha256(), prk, prkLen, info1.data(), info1.size(), t1, &t1Len);

    // T2 = HMAC-SHA256(PRK, T1 || info || 0x02)
    std::vector<uint8_t> info2(32 + info.size() + 1);
    std::copy(t1, t1 + 32, info2.begin());
    std::copy(info.begin(), info.end(), info2.begin() + 32);
    info2[32 + info.size()] = 0x02;
    unsigned char t2[32]; unsigned int t2Len = 0;
    HMAC(EVP_sha256(), prk, prkLen, info2.data(), info2.size(), t2, &t2Len);

    // T3 = HMAC-SHA256(PRK, T2 || info || 0x03)
    std::vector<uint8_t> info3(32 + info.size() + 1);
    std::copy(t2, t2 + 32, info3.begin());
    std::copy(info.begin(), info.end(), info3.begin() + 32);
    info3[32 + info.size()] = 0x03;
    unsigned char t3[32]; unsigned int t3Len = 0;
    HMAC(EVP_sha256(), prk, prkLen, info3.data(), info3.size(), t3, &t3Len);

    std::array<std::vector<uint8_t>, 3> keys;
    keys[0].assign(t1, t1 + 32);  // cipher key
    keys[1].assign(t2, t2 + 32);  // MAC key
    keys[2].assign(t3, t3 + 32);  // IV (first 16 bytes used)
    return keys;
}

// Convert Curve25519 public key to Ed25519 wire format (0x05 + 32 bytes)
static std::vector<uint8_t> curve25519PublicKeyToEd(const DHPublicKey& x25519) {
    std::vector<uint8_t> ed(33);
    ed[0] = 0x05;
    std::copy(x25519.begin(), x25519.end(), ed.begin() + 1);
    return ed;
}

// Compute MAC for SignalMessage:
// MAC = HMAC-SHA256(macKey, theirIdentity(33) || ourIdentity(33) || version(1) || protobuf)[:8]
static std::vector<uint8_t> computeSignalMessageMac(
    const std::vector<uint8_t>& macKey,
    const std::vector<uint8_t>& theirIdentityEd,
    const std::vector<uint8_t>& ourIdentityEd,
    uint8_t version,
    const std::vector<uint8_t>& protoData)
{
    std::vector<uint8_t> macInput;
    macInput.reserve(theirIdentityEd.size() + ourIdentityEd.size() + 1 + protoData.size());
    macInput.insert(macInput.end(), theirIdentityEd.begin(), theirIdentityEd.end());
    macInput.insert(macInput.end(), ourIdentityEd.begin(), ourIdentityEd.end());
    macInput.push_back(version);
    macInput.insert(macInput.end(), protoData.begin(), protoData.end());

    unsigned char mac[32];
    unsigned int macLen = 0;
    HMAC(EVP_sha256(), macKey.data(), macKey.size(), macInput.data(), macInput.size(), mac, &macLen);

    // Return first 8 bytes (truncated MAC)
    return std::vector<uint8_t>(mac, mac + 8);
}

Result<EncryptedMessage> encrypt_1to1(
    ISessionStore& sessions,
    IIdentityKeyStore& identity,
    const SignalAddress& remote,
    std::string_view plaintext)
{
    std::cerr << TAG << " encrypt_1to1: remote=" << remote.to_string()
              << " plaintext_len=" << plaintext.size() << std::endl;

    auto rec = sessions.load_session(remote);
    if (!rec) {
        return Result<EncryptedMessage>::Fail(
            "no session for " + remote.to_string());
    }

    try {
        auto state = load_ratchet(*rec);

        // Get our identity key pair for MAC computation
        auto ourIdentityKeyPair = identity.get_identity_key_pair();

        // ── Step 1: DH ratchet step if needed (first encrypt as receiver) ──
        if (!state.sending_chain_key.has_value()) {
            if (!state.remote_ratchet.has_value()) {
                return Result<EncryptedMessage>::Fail(
                    "no sending chain and no remote ratchet key — cannot encrypt");
            }
            std::cerr << TAG << " DH ratchet step: root_key=";
            for (size_t i = 0; i < 4; ++i) fprintf(stderr, "%02x", state.root_key[i]);
            std::cerr << " remote_ratchet=";
            for (size_t i = 0; i < 4; ++i) fprintf(stderr, "%02x", state.remote_ratchet->data()[i]);
            std::cerr << std::endl;

            state.self_ratchet = DHKeyPair::generate();
            auto dhOutput = dh_compute(state.self_ratchet.private_key, *state.remote_ratchet);
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

            std::cerr << TAG << " new root_key=";
            for (size_t i = 0; i < 4; ++i) fprintf(stderr, "%02x", state.root_key[i]);
            std::cerr << " send_chain=";
            for (size_t i = 0; i < 4; ++i) fprintf(stderr, "%02x", sendChain[i]);
            std::cerr << " new_ratchet_pub=";
            for (size_t i = 0; i < 4; ++i) fprintf(stderr, "%02x", state.self_ratchet.public_key[i]);
            std::cerr << std::endl;
            std::cerr << "[ratchet] DH step for first encrypt as receiver" << std::endl;
        }

        // ── Step 2: Derive message key from chain ──
        auto [new_chain, msg_key] = kdf_ck(*state.sending_chain_key);
        state.sending_chain_key = new_chain;
        uint32_t counter = state.sending_count;

        // ── Step 3: Derive whisper message keys (AES-256-CBC key, MAC key, IV) ──
        auto keys = deriveWhisperMessageKeysOut(
            std::vector<uint8_t>(msg_key.begin(), msg_key.end()));
        const auto& cipherKey = keys[0];
        const auto& macKey = keys[1];
        std::vector<uint8_t> iv(keys[2].begin(), keys[2].begin() + 16);

        // ── Step 4: Encrypt plaintext with AES-256-CBC ──
        std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
        auto ciphertext = aes256CbcEncrypt_out(cipherKey, pt, iv);
        if (ciphertext.empty()) {
            return Result<EncryptedMessage>::Fail("AES-256-CBC encryption failed");
        }

        // ── Step 5: Convert keys to Ed25519 wire format (0x05 prefix) ──
        auto ratchetKeyEd = curve25519PublicKeyToEd(state.self_ratchet.public_key);
        auto ourIdentityEd = curve25519PublicKeyToEd(ourIdentityKeyPair.public_key);
        auto theirIdentityEd = curve25519PublicKeyToEd(state.remote_identity);

        // ── Step 6: Build SignalMessage protobuf ──
        auto protoData = animus::whatsapp::proto::encodeSignalMessageProto(
            ratchetKeyEd,            // field 1: ratchetKey
            counter,                 // field 2: counter
            state.prev_sending_count, // field 3: previousCounter
            ciphertext               // field 4: ciphertext
        );

        // ── Step 7: Compute MAC ──
        // MAC input: senderIdentity(33) || receiverIdentity(33) || version(1) || protobuf
        // We are the sender, so our identity comes first.
        uint8_t version = 0x33;  // Signal Protocol version 3.3
        auto mac = computeSignalMessageMac(
            macKey, ourIdentityEd, theirIdentityEd, version, protoData);

        std::cerr << TAG << " wire format: version=0x" << std::hex << (int)version
                  << " proto_len=" << std::dec << protoData.size()
                  << " mac_len=" << mac.size()
                  << " mac=";
        for (auto b : mac) fprintf(stderr, "%02x", b);
        std::cerr << std::endl;

        std::cerr << TAG << " ourIdentity=";
        for (auto b : ourIdentityEd) fprintf(stderr, "%02x", b);
        std::cerr << " theirIdentity=";
        for (auto b : theirIdentityEd) fprintf(stderr, "%02x", b);
        std::cerr << std::endl;

        // ── Step 8: Assemble wire format [version][proto][MAC(8)] ──
        std::vector<uint8_t> wireData;
        wireData.reserve(1 + protoData.size() + 8);
        wireData.push_back(version);
        wireData.insert(wireData.end(), protoData.begin(), protoData.end());
        wireData.insert(wireData.end(), mac.begin(), mac.end());

        // Advance counter and save state
        state.sending_count++;
        auto new_rec = save_ratchet(state);
        sessions.store_session(remote, new_rec);

        // Package into EncryptedMessage
        EncryptedMessage msg;
        msg.type = SignalMessageType::Whisper;  // Always msg for receiver-initiated sessions
        msg.counter = counter;
        msg.ciphertext = std::move(wireData);  // Full wire format for enc node content

        std::cerr << TAG << " encrypted " << plaintext.size()
                  << " bytes for " << remote.to_string()
                  << " counter=" << counter << std::endl;
        return Result<EncryptedMessage>::Success(std::move(msg));
    } catch (const std::exception& e) {
        return Result<EncryptedMessage>::Fail(e.what());
    }
}

Result<std::string> decrypt_1to1(
    ISessionStore& sessions,
    IIdentityKeyStore& identity,
    const SignalAddress& remote,
    const EncryptedMessage& message)
{
    std::cerr << TAG << " decrypt_1to1: remote=" << remote.to_string()
              << " type=" << static_cast<int>(message.type)
              << " ciphertext_len=" << message.ciphertext.size() << std::endl;

    auto rec = sessions.load_session(remote);
    if (!rec) {
        return Result<std::string>::Fail(
            "no session for " + remote.to_string());
    }

    try {
        auto state = load_ratchet(*rec);

        // Deserialize ratchet ciphertext from header
        RatchetCiphertext ct;
        ct.ciphertext = message.ciphertext;
        const auto& hdr = message.header;
        if (hdr.size() >= KEY_SIZE + 4 + 4 + 4) {
            size_t off = 0;
            std::memcpy(ct.header_key.data(), hdr.data() + off, KEY_SIZE);
            off += KEY_SIZE;
            ct.prev_count = read_u32_be(hdr.data() + off); off += 4;
            ct.message_number = read_u32_be(hdr.data() + off); off += 4;
            uint32_t ct_len = read_u32_be(hdr.data() + off); off += 4;
            if (off + ct_len + NONCE_SIZE + TAG_SIZE <= hdr.size()) {
                ct.ciphertext.assign(hdr.begin() + off, hdr.begin() + off + ct_len);
                off += ct_len;
                std::memcpy(ct.nonce.data(), hdr.data() + off, NONCE_SIZE);
                off += NONCE_SIZE;
                std::memcpy(ct.tag.data(), hdr.data() + off, TAG_SIZE);
            }
        }

        auto plaintext = ratchet_decrypt(state, ct);

        // Save updated state
        auto new_rec = save_ratchet(state);
        sessions.store_session(remote, new_rec);

        std::cerr << TAG << " decrypted " << plaintext.size()
                  << " bytes from " << remote.to_string() << std::endl;
        return Result<std::string>::Success(
            std::string(plaintext.begin(), plaintext.end()));
    } catch (const std::exception& e) {
        return Result<std::string>::Fail(e.what());
    }
}

Result<GroupEncryptedMessage> encrypt_group(
    ISenderKeyStore& sender_keys,
    const SignalAddress& sender,
    const std::string& group_id,
    std::string_view plaintext)
{
    std::cerr << TAG << " encrypt_group: sender=" << sender.to_string()
              << " group=" << group_id
              << " plaintext_len=" << plaintext.size() << std::endl;

    // Sender Keys: for now, use AES-GCM with a symmetric key derived from
    // a stored sender key record. Full Sender Key distribution protocol
    // will be implemented when needed for group messaging.

    auto skr = sender_keys.load_sender_key(sender, group_id);
    if (!skr) {
        // Create sender key
        SenderKeyRecord record;
        record.group_id = group_id;
        record.sender = sender;
        record.iteration = 0;
        record.chain_key.resize(KEY_SIZE);
        RAND_bytes(record.chain_key.data(), KEY_SIZE);
        auto signing_kp = DHKeyPair::generate();
        record.signing_key.public_key = signing_kp.public_key;
        record.signing_key.private_key = signing_kp.private_key;
        sender_keys.store_sender_key(sender, group_id, record);
        skr = record;
        std::cerr << TAG << " created sender key for group " << group_id << std::endl;
    }

    // Derive message key from chain key
    ChainKey ck;
    std::memcpy(ck.data(), skr->chain_key.data(), std::min(skr->chain_key.size(), KEY_SIZE));
    auto [new_ck, msg_key] = kdf_ck(ck);

    // Encrypt
    std::array<uint8_t, KEY_SIZE> mk;
    std::memcpy(mk.data(), msg_key.data(), KEY_SIZE);
    std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
    auto gcm = aes_gcm_encrypt(mk, pt);

    // DON'T update the store yet — keep the chain key for decrypt
    // The store is updated only when we know the message was successfully sent
    // For now, we advance after encrypt so the next encrypt uses a new key
    SenderKeyRecord updated = *skr;
    updated.chain_key.assign(new_ck.begin(), new_ck.end());
    updated.iteration++;
    sender_keys.store_sender_key(sender, group_id, updated);

    GroupEncryptedMessage msg;
    msg.group_id = group_id;
    msg.sender = sender;
    msg.ciphertext = std::move(gcm.ciphertext);
    // Pack: msg_key (32) + nonce (12) + tag (16) + ciphertext
    // This allows decrypt to recover the key even after chain advances
    msg.ciphertext.insert(msg.ciphertext.begin(), msg_key.begin(), msg_key.end());
    msg.ciphertext.insert(msg.ciphertext.end(), gcm.nonce.begin(), gcm.nonce.end());
    msg.ciphertext.insert(msg.ciphertext.end(), gcm.tag.begin(), gcm.tag.end());
    msg.counter = updated.iteration;

    return Result<GroupEncryptedMessage>::Success(std::move(msg));
}

Result<std::string> decrypt_group(
    ISenderKeyStore& sender_keys,
    const SignalAddress& sender,
    const std::string& group_id,
    const GroupEncryptedMessage& message)
{
    std::cerr << TAG << " decrypt_group: sender=" << sender.to_string()
              << " group=" << group_id
              << " ciphertext_len=" << message.ciphertext.size() << std::endl;

    auto skr = sender_keys.load_sender_key(sender, group_id);
    if (!skr) {
        return Result<std::string>::Fail("no sender key for " + sender.to_string());
    }

    // Extract: msg_key (32) + ciphertext + nonce (12) + tag (16)
    if (message.ciphertext.size() < KEY_SIZE + NONCE_SIZE + TAG_SIZE) {
        return Result<std::string>::Fail("ciphertext too short");
    }

    size_t total = message.ciphertext.size();
    size_t ct_len = total - KEY_SIZE - NONCE_SIZE - TAG_SIZE;

    // Extract message key
    std::array<uint8_t, KEY_SIZE> mk;
    std::memcpy(mk.data(), message.ciphertext.data(), KEY_SIZE);

    // Extract ciphertext
    AesGcmCiphertext gcm;
    gcm.ciphertext.assign(message.ciphertext.begin() + KEY_SIZE,
                          message.ciphertext.begin() + KEY_SIZE + ct_len);

    // Extract nonce and tag
    std::memcpy(gcm.nonce.data(), message.ciphertext.data() + KEY_SIZE + ct_len, NONCE_SIZE);
    std::memcpy(gcm.tag.data(), message.ciphertext.data() + KEY_SIZE + ct_len + NONCE_SIZE, TAG_SIZE);

    try {
        auto plaintext = aes_gcm_decrypt(mk, gcm);
        return Result<std::string>::Success(
            std::string(plaintext.begin(), plaintext.end()));
    } catch (const std::exception& e) {
        return Result<std::string>::Fail(std::string("AES-GCM decrypt failed: ") + e.what());
    }
}

std::vector<SignalPreKey> generate_pre_keys(uint32_t start_id, size_t count) {
    std::cerr << TAG << " generate_pre_keys: start=" << start_id
              << " count=" << count << std::endl;

    std::vector<SignalPreKey> keys;
    keys.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        auto dh = DHKeyPair::generate();
        SignalPreKey pk;
        pk.id = start_id + static_cast<uint32_t>(i);
        std::memcpy(pk.keypair.public_key.data(), dh.public_key.data(), 32);
        std::memcpy(pk.keypair.private_key.data(), dh.private_key.data(), 32);
        keys.push_back(std::move(pk));
    }
    return keys;
}

SignalSignedPreKey generate_signed_pre_key(uint32_t id, const SignalKeypair& identity_key) {
    std::cerr << TAG << " generate_signed_pre_key: id=" << id << std::endl;

    auto dh = DHKeyPair::generate();
    SignalSignedPreKey spk;
    spk.id = id;
    std::memcpy(spk.keypair.public_key.data(), dh.public_key.data(), 32);
    std::memcpy(spk.keypair.private_key.data(), dh.private_key.data(), 32);

    // Sign [0x05 + pubkey] with identity key using Ed25519
    // Baileys: Curve.sign(identityKeyPair.private, generateSignalPubKey(preKey.public))
    // generateSignalPubKey prepends KEY_BUNDLE_TYPE (0x05) to 32-byte pubkey
    std::vector<uint8_t> signalPubKey(33);
    signalPubKey[0] = 0x05;  // KEY_BUNDLE_TYPE
    std::memcpy(signalPubKey.data() + 1, dh.public_key.data(), 32);

    auto sig = ed25519_sign(identity_key.private_key, signalPubKey);
    spk.signature = sig;

    return spk;
}

Result<void> process_sender_key_distribution(
    ISenderKeyStore& sender_keys,
    const SignalAddress& sender,
    const std::vector<uint8_t>& distribution_message)
{
    std::cerr << TAG << " process_sender_key_distribution: from="
              << sender.to_string() << std::endl;
    // TODO: parse distribution message, store sender key
    return Result<void>::Success();
}

Result<std::vector<uint8_t>> get_sender_key_distribution(
    ISenderKeyStore& sender_keys,
    const SignalAddress& sender,
    const std::string& group_id)
{
    std::cerr << TAG << " get_sender_key_distribution: sender="
              << sender.to_string() << " group=" << group_id << std::endl;

    auto skr = sender_keys.load_sender_key(sender, group_id);
    if (!skr) {
        return Result<std::vector<uint8_t>>::Fail("no sender key");
    }
    return Result<std::vector<uint8_t>>::Success(skr->state);
}

} // namespace animus::signal

// ─── Ed25519 utilities (outside namespace for use by WhatsApp adapter) ───

namespace animus {

// Curve25519 signing, matching curve25519-js/libsignal behavior.
// This is NOT standard Ed25519 (RFC 8032) which does SHA-512(seed) to derive the scalar.
// Instead, the X25519 private key is used as the Ed25519 scalar directly (after clamping),
// matching the curve25519-js `sign()` function used by libsignal.
std::vector<uint8_t> ed25519_sign(const std::array<uint8_t, 32>& x25519_private,
                                  const std::vector<uint8_t>& message) {
    // Step 1: Clamp the X25519 scalar (same as curve25519-js)
    uint8_t scalar[32];
    std::memcpy(scalar, x25519_private.data(), 32);
    scalar[0] &= 248;
    scalar[31] &= 127;
    scalar[31] |= 64;

    // Step 2: Derive Ed25519 public key A = scalar * B (no clamp, already clamped)
    uint8_t A[32];
    crypto_scalarmult_ed25519_base_noclamp(A, scalar);

    // Step 3: r = SHA-512(scalar || message), reduce mod L
    std::vector<uint8_t> hashInput(scalar, scalar + 32);
    hashInput.insert(hashInput.end(), message.begin(), message.end());
    uint8_t rHash[64];
    crypto_hash_sha512(rHash, hashInput.data(), hashInput.size());
    crypto_core_ed25519_scalar_reduce(rHash, rHash);  // r is now in rHash[0..31]

    // Step 4: R = r * B (noclamp — r is already reduced mod L, must not be clamped)
    uint8_t R[32];
    crypto_scalarmult_ed25519_base_noclamp(R, rHash);

    // Step 5: k = SHA-512(R || A || message), reduce mod L
    std::vector<uint8_t> kInput(R, R + 32);
    kInput.insert(kInput.end(), A, A + 32);
    kInput.insert(kInput.end(), message.begin(), message.end());
    uint8_t kHash[64];
    crypto_hash_sha512(kHash, kInput.data(), kInput.size());
    crypto_core_ed25519_scalar_reduce(kHash, kHash);  // k is now in kHash[0..31]

    // Step 6: S = (r + k * scalar) mod L
    uint8_t ks[32];
    crypto_core_ed25519_scalar_mul(ks, kHash, scalar);
    uint8_t S[32];
    crypto_core_ed25519_scalar_add(S, rHash, ks);

    // Step 7: Signature = R || S (with sign bit from Ed25519 public key)
    std::vector<uint8_t> sig(64);
    std::memcpy(sig.data(), R, 32);
    std::memcpy(sig.data() + 32, S, 32);
    // curve25519-js copies sign bit from Ed25519 public key into signature
    sig[63] |= (A[31] & 0x80);
    return sig;
}

bool ed25519_verify(const std::vector<uint8_t>& public_key,
                    const std::vector<uint8_t>& message,
                    const std::vector<uint8_t>& signature) {
    EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519, nullptr, public_key.data(), public_key.size());
    if (!pkey) return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey);
    int ok = EVP_DigestVerify(ctx, signature.data(), signature.size(),
                              message.data(), message.size());
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    return ok == 1;
}

// XEdDSA verification: verify a curve25519-js signature using an X25519 public key
// This implements the same verification as libsignal's curve25519_verify:
// 1. Convert X25519 (Montgomery u) to Ed25519 (Edwards y): y = (u-1)/(u+1) mod p
// 2. Extract sign bit from signature[63] and place it in Ed25519 pubkey[31]
// 3. Clear sign bit in signature copy
// 4. Verify using standard Ed25519
//
// The conversion formula from RFC 7748 / curve25519-js / libsignal-protocol-c:
// Given Montgomery u-coordinate, recover Edwards y-coordinate:
//   y = (u - 1) / (u + 1) mod p
// where p = 2^255 - 19 (the Curve25519 field prime)
bool x25519_verify(const std::vector<uint8_t>& x25519_public_key,
                   const std::vector<uint8_t>& message,
                   const std::vector<uint8_t>& signature) {
    if (x25519_public_key.size() != 32 || signature.size() != 64) return false;

    // Convert X25519 Montgomery u-coordinate to Ed25519 Edwards y-coordinate
    // Using OpenSSL BIGNUM: y = (u - 1) * mod_inverse(u + 1, p) mod p
    // X25519 keys are little-endian; BN_bin2bn expects big-endian.
    BN_CTX* ctx = BN_CTX_new();
    std::vector<uint8_t> u_be(32);
    std::reverse_copy(x25519_public_key.begin(), x25519_public_key.end(), u_be.begin());
    BIGNUM* u = BN_bin2bn(u_be.data(), 32, nullptr);
    BIGNUM* p = BN_new();
    BIGNUM* one = BN_new();
    BN_set_word(one, 1);
    // p = 2^255 - 19
    BN_set_bit(p, 255);
    BN_sub_word(p, 19);

    // y = (u - 1) / (u + 1) mod p
    BIGNUM* numerator = BN_new();
    BN_mod_sub(numerator, u, one, p, ctx);
    BIGNUM* denominator = BN_new();
    BN_mod_add(denominator, u, one, p, ctx);
    BIGNUM* denom_inv = BN_mod_inverse(nullptr, denominator, p, ctx);
    BIGNUM* y = BN_new();
    BN_mod_mul(y, numerator, denom_inv, p, ctx);

    // Encode y as Ed25519 public key (little-endian, 32 bytes)
    uint8_t ed25519_pk[32];
    BN_bn2lebinpad(y, ed25519_pk, 32);

    BN_free(u); BN_free(p); BN_free(one); BN_free(numerator);
    BN_free(denominator); BN_free(denom_inv); BN_free(y); BN_CTX_free(ctx);

    // Clear any high bits in the Ed25519 public key
    ed25519_pk[31] &= 0x7F;

    // Copy sign bit from signature into Ed25519 public key
    // (XEdDSA stores the sign bit in signature[63])
    ed25519_pk[31] |= (signature[63] & 0x80);

    // Create a copy of the signature with sign bit cleared
    std::vector<uint8_t> sig_copy = signature;
    sig_copy[63] &= 0x7F;

    // Verify using standard Ed25519
    return ed25519_verify(
        std::vector<uint8_t>(ed25519_pk, ed25519_pk + 32),
        message,
        sig_copy);
}

} // namespace animus

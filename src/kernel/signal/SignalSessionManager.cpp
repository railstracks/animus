#include "animus_kernel/signal/SignalSessionManager.h"
#include "animus_kernel/signal/SignalCrypto.h"
#include "animus_kernel/signal/DoubleRatchet.h"
#include "animus_kernel/whatsapp/WAProto.h"

#include <cstdio>
#include <iostream>
#include <memory>
#include <optional>
#include <openssl/hmac.h>
#include <openssl/evp.h>

namespace {

// Ed25519 public key to Curve25519 (X25519) public key conversion
// IMPORTANT: WhatsApp's 0x05-prefixed keys ARE Curve25519 keys, not Ed25519.
// The 0x05 is a libsignal format indicator. Strip prefix and use bytes directly.
std::vector<uint8_t> ed25519_to_curve25519(const std::vector<uint8_t>& edKey) {
    if (edKey.size() == 33) {
        // Strip 0x05 prefix
        return std::vector<uint8_t>(edKey.begin() + 1, edKey.end());
    }
    if (edKey.size() == 32) {
        return edKey;
    }
    return {};
}

// Derive 3 keys from a message key: [cipherKey(32), macKey(32), iv(32)]
// Matches libsignal's deriveSecrets(key, zeros(32), "WhisperMessageKeys", 3)
std::array<std::vector<uint8_t>, 3> deriveWhisperMessageKeys(const std::vector<uint8_t>& messageKey) {
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
    keys[1].assign(t2, t2 + 32);  // mac key
    keys[2].assign(t3, t3 + 32);  // IV (first 16 bytes)
    return keys;
}

// AES-256-CBC decryption
std::vector<uint8_t> aes256CbcDecrypt(const std::vector<uint8_t>& key,
                                    const std::vector<uint8_t>& ciphertext,
                                    const std::vector<uint8_t>& iv) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};
    std::vector<uint8_t> plaintext(ciphertext.size() + 16);
    int len = 0, totalLen = 0;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) != 1 ||
        EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), ciphertext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    totalLen = len;
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + totalLen, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    totalLen += len;
    EVP_CIPHER_CTX_free(ctx);
    plaintext.resize(totalLen);
    return plaintext;
}

} // anonymous namespace

namespace animus::signal {

SignalSessionManager::SignalSessionManager(
    std::unique_ptr<ISessionStore> sessions,
    std::unique_ptr<IPreKeyStore> pre_keys,
    std::unique_ptr<ISignedPreKeyStore> signed_pre_keys,
    std::unique_ptr<IIdentityKeyStore> identity,
    std::unique_ptr<ISenderKeyStore> sender_keys)
    : m_sessions(std::move(sessions))
    , m_pre_keys(std::move(pre_keys))
    , m_signed_pre_keys(std::move(signed_pre_keys))
    , m_identity(std::move(identity))
    , m_sender_keys(std::move(sender_keys))
{}

bool SignalSessionManager::has_session(const SignalAddress& remote) const {
    return const_cast<ISessionStore&>(*m_sessions).contains_session(
        const_cast<SignalAddress&>(remote));
}

Result<void> SignalSessionManager::establish_session(
    const SignalAddress& remote, const PreKeyBundle& bundle)
{
    return initialize_session(*m_sessions, *m_identity, remote, bundle);
}

Result<EncryptedMessage> SignalSessionManager::encrypt(
    const SignalAddress& remote, std::string_view plaintext)
{
    return encrypt_1to1(*m_sessions, *m_identity, remote, plaintext);
}

Result<std::string> SignalSessionManager::decrypt(
    const SignalAddress& remote, const EncryptedMessage& msg)
{
    return decrypt_1to1(*m_sessions, *m_identity, remote, msg);
}

Result<std::vector<uint8_t>> SignalSessionManager::decrypt_whisper(
    const SignalAddress& remote,
    const std::vector<uint8_t>& wireData,
    const std::vector<uint8_t>& ourIdentityPub,
    const std::vector<uint8_t>& theirIdentityPub)
{
    // Wire format: [version_byte][protobuf SignalMessage][8-byte MAC]
    if (wireData.size() < 10) {
        return Result<std::vector<uint8_t>>::Fail("whisper message too short");
    }

    uint8_t version = wireData[0];

    // Load session
    auto sessOpt = m_sessions->load_session(remote);
    if (!sessOpt) {
        return Result<std::vector<uint8_t>>::Fail("no session for " + remote.to_string());
    }
    auto ratchetData = RatchetState::deserialize(sessOpt->state.data(), sessOpt->state.size());
    if (!ratchetData) {
        return Result<std::vector<uint8_t>>::Fail("failed to deserialize ratchet state");
    }
    auto state = std::move(*ratchetData);

    // Decode SignalMessage protobuf
    size_t protoLen = wireData.size() - 1 - 8; // skip version byte, strip MAC
    std::vector<uint8_t> messageProto(wireData.begin() + 1, wireData.begin() + 1 + protoLen);
    std::vector<uint8_t> receivedMac(wireData.end() - 8, wireData.end());

    auto msg = animus::whatsapp::proto::decodeSignalMessageInner(messageProto.data(), messageProto.size());
    if (msg.ratchet_key.empty() || msg.ciphertext.empty()) {
        return Result<std::vector<uint8_t>>::Fail("missing ratchet_key or ciphertext in WhisperMessage");
    }

    // Convert Ed25519 ratchet key to Curve25519
    std::vector<uint8_t> ratchetX = ed25519_to_curve25519(msg.ratchet_key);
    if (ratchetX.empty()) {
        return Result<std::vector<uint8_t>>::Fail("failed to convert ratchet key to Curve25519");
    }
    DHPublicKey theirRatchetKey;
    std::copy(ratchetX.begin(), ratchetX.begin() + 32, theirRatchetKey.begin());

    // Hex dump helper for diagnostics
    auto hexDump = [](const std::string& label, const uint8_t* data, size_t len) {
        std::cerr << "[SignalDecrypt] " << label << " (" << len << " bytes): ";
        for (size_t i = 0; i < len; ++i) fprintf(stderr, "%02x", data[i]);
        std::cerr << std::endl;
    };

    std::cerr << "[SignalDecrypt] remote=" << remote.to_string()
              << " version=" << (int)version
              << " newRatchet=" << (state.remote_ratchet.has_value() ? "no" : "yes")
              << " wireData.size()=" << wireData.size()
              << " counter=" << msg.counter << std::endl;
    hexDump("root_key (before ratchet)", state.root_key.data(), 32);
    hexDump("self_ratchet.priv", state.self_ratchet.private_key.data(), 32);
    hexDump("self_ratchet.pub", state.self_ratchet.public_key.data(), 32);
    hexDump("theirRatchetKey (curve)", theirRatchetKey.data(), 32);
    hexDump("msg.ratchet_key (ed25519)", msg.ratchet_key.data(), msg.ratchet_key.size());

    // Maybe step ratchet: if the ratchet key is new
    bool newRatchet = !state.remote_ratchet.has_value() ||
                      theirRatchetKey != *state.remote_ratchet;
    if (newRatchet) {
        // Perform DH ratchet step
        auto dhOutput = dh_compute(state.self_ratchet.private_key, theirRatchetKey);
        hexDump("ratchet DH output", dhOutput.data(), dhOutput.size());

        RootKey newRoot;
        auto derived = hkdf_derive(dhOutput.data(), dhOutput.size(),
                                    state.root_key.data(), state.root_key.size(),
                                    reinterpret_cast<const uint8_t*>("WhisperRatchet"), 14,
                                    KEY_SIZE * 2);
        std::copy(derived.begin(), derived.begin() + KEY_SIZE, newRoot.begin());
        ChainKey newChain;
        std::copy(derived.begin() + KEY_SIZE, derived.end(), newChain.begin());

        hexDump("new root_key (after ratchet)", newRoot.data(), 32);
        hexDump("new chain_key", newChain.data(), 32);

        state.root_key = newRoot;
        state.receiving_chain_key = newChain;
        state.remote_ratchet = theirRatchetKey;
        state.receiving_count = 0;

        // Generate new self ratchet key
        state.self_ratchet = DHKeyPair::generate();
    }

    // Advance chain to derive message key for msg.counter
    if (!state.receiving_chain_key.has_value()) {
        return Result<std::vector<uint8_t>>::Fail("no receiving chain after ratchet step");
    }

    ChainKey currentChain = *state.receiving_chain_key;
    std::array<uint8_t, KEY_SIZE> messageKey;
    hexDump("chain_key (start)", currentChain.data(), 32);
    for (uint32_t i = state.receiving_count; i <= msg.counter; i++) {
        auto [newChain, mk] = kdf_ck(currentChain);
        currentChain = newChain;
        messageKey = mk;
        std::cerr << "[SignalDecrypt] chain step i=" << i
                  << " counter=" << msg.counter << std::endl;
    }
    state.receiving_chain_key = currentChain;
    state.receiving_count = msg.counter + 1;

    hexDump("messageKey", messageKey.data(), 32);

    // Derive whisper message keys: [cipherKey(32), macKey(32), iv(16)]
    auto keys = deriveWhisperMessageKeys(std::vector<uint8_t>(messageKey.begin(), messageKey.end()));
    hexDump("cipherKey", keys[0].data(), keys[0].size());
    hexDump("macKey", keys[1].data(), keys[1].size());
    hexDump("iv", keys[2].data(), 16);

    // Verify MAC
    // MAC input: [theirIdentityKey(33) | ourIdentityKey(33) | versionByte(1) | messageProto]
    std::vector<uint8_t> macInput;
    macInput.reserve(theirIdentityPub.size() + ourIdentityPub.size() + 1 + messageProto.size());
    macInput.insert(macInput.end(), theirIdentityPub.begin(), theirIdentityPub.end());
    macInput.insert(macInput.end(), ourIdentityPub.begin(), ourIdentityPub.end());
    macInput.push_back(version);
    macInput.insert(macInput.end(), messageProto.begin(), messageProto.end());

    std::cerr << "[SignalDecrypt] MAC input: theirIdentity=" << theirIdentityPub.size()
              << " ourIdentity=" << ourIdentityPub.size()
              << " version=" << (int)version
              << " protoLen=" << messageProto.size()
              << " macInputTotal=" << macInput.size() << std::endl;
    hexDump("theirIdentityPub", theirIdentityPub.data(), theirIdentityPub.size());
    hexDump("ourIdentityPub", ourIdentityPub.data(), ourIdentityPub.size());
    hexDump("receivedMac", receivedMac.data(), receivedMac.size());

    unsigned char calculatedMac[32];
    unsigned int macLen = 0;
    HMAC(EVP_sha256(), keys[1].data(), keys[1].size(),
         macInput.data(), macInput.size(), calculatedMac, &macLen);
    hexDump("calculatedMac (full 32)", calculatedMac, 32);
    std::cerr << "[SignalDecrypt] MAC check: received=" << receivedMac.size()
              << " calculated8=";
    for (int i = 0; i < 8; ++i) fprintf(stderr, "%02x", calculatedMac[i]);
    std::cerr << std::endl;

    if (receivedMac.size() != 8 || CRYPTO_memcmp(calculatedMac, receivedMac.data(), 8) != 0) {
        return Result<std::vector<uint8_t>>::Fail("MAC verification failed");
    }

    // Decrypt with AES-256-CBC
    std::vector<uint8_t> iv(keys[2].begin(), keys[2].begin() + 16);
    auto plaintext = aes256CbcDecrypt(keys[0], msg.ciphertext, iv);
    if (plaintext.empty()) {
        return Result<std::vector<uint8_t>>::Fail("AES-256-CBC decryption failed");
    }

    // Save updated session
    SessionRecord updated;
    updated.state = state.serialize();
    updated.version = 1;
    updated.is_fresh = false;
    m_sessions->store_session(remote, updated);

    return Result<std::vector<uint8_t>>::Success(std::move(plaintext));
}

Result<GroupEncryptedMessage> SignalSessionManager::group_encrypt(
    const SignalAddress& sender, const std::string& group_id,
    std::string_view plaintext)
{
    return encrypt_group(*m_sender_keys, sender, group_id, plaintext);
}

Result<std::string> SignalSessionManager::group_decrypt(
    const SignalAddress& sender, const std::string& group_id,
    const GroupEncryptedMessage& msg)
{
    return decrypt_group(*m_sender_keys, sender, group_id, msg);
}

std::vector<SignalPreKey> SignalSessionManager::generate_pre_keys(size_t count) {
    uint32_t start = m_pre_keys->next_pre_key_id();
    auto keys = animus::signal::generate_pre_keys(start, count);
    for (auto& k : keys) {
        m_pre_keys->store_pre_key(k);
    }
    return keys;
}

SignalSignedPreKey SignalSessionManager::rotate_signed_pre_key() {
    uint32_t id = m_signed_pre_keys->current_signed_pre_key_id() + 1;
    auto spk = animus::signal::generate_signed_pre_key(id, m_identity->get_identity_key_pair());
    m_signed_pre_keys->store_signed_pre_key(spk);
    return spk;
}

// ─── Factory ───

std::unique_ptr<SignalSessionManager> create_in_memory_manager(
    std::optional<SignalIdentityKey> identity_key) {
    auto idKey = identity_key.value_or(SignalIdentityKey::generate(1));
    return std::make_unique<SignalSessionManager>(
        std::make_unique<InMemorySessionStore>(),
        std::make_unique<InMemoryPreKeyStore>(),
        std::make_unique<InMemorySignedPreKeyStore>(),
        std::make_unique<InMemoryIdentityKeyStore>(std::move(idKey)),
        std::make_unique<InMemorySenderKeyStore>()
    );
}

} // namespace animus::signal

// WhatsAppAuthState.h — Auth state persistence for WhatsApp Linked Devices
// Extracted from WhatsAppGatewayLoop.cpp for reuse and testability.
// ============================================================================

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <json/json.h>
#include <fstream>

#include "animus_kernel/whatsapp/WhatsAppNoise.h" // KeyPair

namespace animus::whatsapp {

struct AuthState {
    KeyPair noiseKey;
    KeyPair identityKey;

    std::vector<uint8_t> identityEdPubKey;
    std::vector<uint8_t> signedPreKeyPub;
    std::vector<uint8_t> signedPreKeyPriv;
    std::vector<uint8_t> signedPreKeySig;
    uint32_t signedPreKeyId = 1;
    uint32_t registrationId = 0;
    std::string advSecretKey;

    // One-time pre-keys (needed for X3DH receiver session init)
    // Each entry: {id, public_key(32), private_key(32)}
    struct PreKeyEntry {
        uint32_t id = 0;
        std::vector<uint8_t> pub;
        std::vector<uint8_t> priv;
    };
    std::vector<PreKeyEntry> preKeys;
    std::string jid;
    std::string phone;
    std::string name;
    std::string routingInfo;
    std::string platform;

    // Pair-success fields (persisted for session resume)
    std::vector<uint8_t> accountDetails;       // ADVSignedDeviceIdentity.details
    std::vector<uint8_t> accountSignatureKey;  // phone's public key
    std::vector<uint8_t> deviceIdentityRaw;    // ADVDeviceIdentity (raw protobuf)
    std::vector<uint8_t> deviceSignature;      // our device signature
    bool paired = false;

    // Per-peer last-processed message timestamp (Unix seconds)
    // Used to avoid replaying old offline messages on reconnect
    std::map<std::string, int64_t> lastMessageTs;

    void save(const std::string& path) const {
        Json::Value root;
        auto toHex = [](const std::vector<uint8_t>& v) -> std::string {
            std::string s;
            for (auto b : v) { char buf[3]; snprintf(buf, 3, "%02x", b); s += buf; }
            return s;
        };
        root["noise_priv"] = toHex(noiseKey.priv);
        root["noise_pub"] = toHex(noiseKey.pub);
        root["identity_priv"] = toHex(identityKey.priv);
        root["identity_pub"] = toHex(identityKey.pub);
        if (!identityEdPubKey.empty())
            root["identity_ed_pub"] = toHex(identityEdPubKey);
        root["signed_prekey_priv"] = toHex(signedPreKeyPriv);
        root["signed_prekey_pub"] = toHex(signedPreKeyPub);
        root["signed_prekey_sig"] = toHex(signedPreKeySig);
        root["signed_prekey_id"] = signedPreKeyId;
        root["registration_id"] = registrationId;
        root["adv_secret_key"] = advSecretKey;
        root["jid"] = jid;
        root["phone"] = phone;
        root["name"] = name;
        root["paired"] = paired;
        root["routing_info"] = routingInfo;
        root["platform"] = platform;
        if (!accountDetails.empty())
            root["account_details"] = toHex(accountDetails);
        if (!accountSignatureKey.empty())
            root["account_signature_key"] = toHex(accountSignatureKey);
        if (!deviceIdentityRaw.empty())
            root["device_identity_raw"] = toHex(deviceIdentityRaw);
        if (!deviceSignature.empty())
            root["device_signature"] = toHex(deviceSignature);

        // One-time pre-keys
        Json::Value pkArray(Json::arrayValue);
        for (const auto& pk : preKeys) {
            Json::Value entry;
            entry["id"] = pk.id;
            entry["pub"] = toHex(pk.pub);
            entry["priv"] = toHex(pk.priv);
            pkArray.append(entry);
        }
        root["pre_keys"] = pkArray;

        // Per-peer last message timestamps
        Json::Value tsObj(Json::objectValue);
        for (const auto& [peer, ts] : lastMessageTs) {
            tsObj[peer] = static_cast<Json::Int64>(ts);
        }
        root["last_message_ts"] = tsObj;

        Json::StreamWriterBuilder builder;
        std::ofstream ofs(path);
        ofs << Json::writeString(builder, root);
    }

    static AuthState load(const std::string& path) {
        AuthState st;
        std::ifstream ifs(path);
        if (!ifs.is_open()) return st;
        Json::Value root;
        ifs >> root;

        auto fromHex = [](const std::string& s) -> std::vector<uint8_t> {
            std::vector<uint8_t> v;
            for (size_t i = 0; i + 1 < s.size(); i += 2)
                v.push_back(static_cast<uint8_t>(std::stoul(s.substr(i, 2), nullptr, 16)));
            return v;
        };

        auto getStr = [&](const std::string& key) -> std::string {
            return root.isMember(key) ? root[key].asString() : "";
        };

        st.noiseKey.priv = fromHex(getStr("noise_priv"));
        st.noiseKey.pub = fromHex(getStr("noise_pub"));
        st.identityKey.priv = fromHex(getStr("identity_priv"));
        st.identityKey.pub = fromHex(getStr("identity_pub"));
        st.identityEdPubKey = fromHex(getStr("identity_ed_pub"));
        st.signedPreKeyPriv = fromHex(getStr("signed_prekey_priv"));
        st.signedPreKeyPub = fromHex(getStr("signed_prekey_pub"));
        st.signedPreKeySig = fromHex(getStr("signed_prekey_sig"));
        st.signedPreKeyId = root.get("signed_prekey_id", 1).asUInt();
        st.registrationId = root.get("registration_id", 0).asUInt();
        st.advSecretKey = getStr("adv_secret_key");
        st.jid = getStr("jid");
        st.phone = getStr("phone");
        st.name = getStr("name");
        st.paired = root.get("paired", false).asBool();
        st.routingInfo = getStr("routing_info");
        st.platform = getStr("platform");
        st.accountDetails = fromHex(getStr("account_details"));
        st.accountSignatureKey = fromHex(getStr("account_signature_key"));
        st.deviceIdentityRaw = fromHex(getStr("device_identity_raw"));
        st.deviceSignature = fromHex(getStr("device_signature"));

        // One-time pre-keys
        if (root.isMember("pre_keys") && root["pre_keys"].isArray()) {
            for (const auto& entry : root["pre_keys"]) {
                PreKeyEntry pk;
                pk.id = entry.get("id", 0).asUInt();
                pk.pub = fromHex(entry.get("pub", "").asString());
                pk.priv = fromHex(entry.get("priv", "").asString());
                if (pk.id > 0 && !pk.pub.empty() && !pk.priv.empty())
                    st.preKeys.push_back(pk);
            }
        }

        // Per-peer last message timestamps
        if (root.isMember("last_message_ts") && root["last_message_ts"].isObject()) {
            for (const auto& key : root["last_message_ts"].getMemberNames()) {
                st.lastMessageTs[key] = root["last_message_ts"][key].asInt64();
            }
        }

        return st;
    }
};

} // namespace animus::whatsapp
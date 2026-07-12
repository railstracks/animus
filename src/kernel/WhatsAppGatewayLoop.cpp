// WhatsAppGatewayLoop.cpp — WhatsApp Web (Linked Devices) gateway loop
//
// Full connection lifecycle:
//   1. Load/create auth state (noise key pair, identity, etc.)
//   2. Raw TCP+TLS+WebSocket connect to web.whatsapp.com/ws/chat
//   3. Noise XX handshake (client hello → server hello → client finish)
//   4. Post-handshake: ClientInit binary node (device info, connect reason)
//   5. QR pairing or session resume (based on auth state)
//   6. Keep-alive ping every 25s
//   7. Dispatch inbound messages to agent sessions (Signal decrypt → proto decode → dispatch)
//   8. Send outbound messages (proto encode → Signal encrypt → binary node → send)
//
// Transport: WhatsAppTransport (raw TCP + OpenSSL + manual WS framing)
// Previously used Drogon's WebSocketClient but it caused server rejection (1011)
// after the Noise client finish. Issue isolated to Drogon's WS send path.
// ============================================================================

#include "animus_kernel/ChannelManager.h"
#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/whatsapp/WABinary.h"
#include "animus_kernel/whatsapp/WhatsAppAuthState.h"
#include "animus_kernel/whatsapp/WhatsAppCryptoHelpers.h"
#include "animus_kernel/whatsapp/WhatsAppMessageHandler.h"
#include "animus_kernel/whatsapp/WhatsAppNoise.h"
#include "animus_kernel/whatsapp/WhatsAppTransport.h"
#include "animus_kernel/whatsapp/WAProto.h"
#include "animus_kernel/signal/SignalCrypto.h"
#include "animus_kernel/signal/SignalSessionManager.h"
#include "animus_kernel/signal/SignalStore.h"

#include <json/json.h>
#include <json/writer.h>

#include <openssl/evp.h>
#include <openssl/rand.h>

using namespace animus::kernel;
using WABinaryNode = animus::whatsapp::BinaryNode;
using animus::whatsapp::AuthState;
using animus::whatsapp::NoiseState;
using animus::whatsapp::WhatsAppTransport;
using animus::whatsapp::generateKeyPair;
using animus::whatsapp::encodeBinaryNode;
using animus::whatsapp::decodeBinaryNode;
using animus::whatsapp::getBinaryNodeChild;
using animus::whatsapp::isJidGroup;
using animus::whatsapp::hmacSHA256;
using animus::whatsapp::concatBytes;
using animus::whatsapp::concatBytes3;
using animus::whatsapp::prefixBytes;
using animus::whatsapp::base64Decode;
using animus::whatsapp::base64Encode;
using animus::whatsapp::base64UrlEncode;
using animus::whatsapp::bytesToHex;
using animus::whatsapp::generateTag;
using animus::whatsapp::ADV_ACCOUNT_SIG_PREFIX;
using animus::whatsapp::ADV_DEVICE_SIG_PREFIX;
using animus::whatsapp::ADV_HOSTED_ACCOUNT_SIG_PREFIX;
using animus::whatsapp::ADV_HOSTED_DEVICE_SIG_PREFIX;
using animus::whatsapp::KEY_BUNDLE_TYPE;
using animus::whatsapp::handleEncryptedMessage;
using animus::signal::create_in_memory_manager;
using animus::signal::SignalAddress;
using animus::signal::SignalMessageType;
using animus::signal::EncryptedMessage;
using animus::signal::SignalSessionManager;
using animus::signal::PreKeyBundle;
using animus::ed25519_sign;
using animus::x25519_verify;

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <sodium.h>
#include <vector>

// WA version constants
#define WA_VERSION "2.3000.1035194821"
#define WA_BROWSER "Mac OS"
#define WA_DEVICE "Desktop"
#define WA_PLATFORM 0  // CompanionWebClientType



// Post-login sequence state machine
enum class PostLoginStep {
    Idle,           // Not in post-login
    WaitCount,      // Sent count query, waiting for response
    WaitPassive,    // Sent passive IQ, waiting for response
    WaitDigest,     // Sent digest, waiting for response
    Done            // All post-login steps complete
};

static std::string postLoginExpectedId; // Track which IQ ID we're waiting for

// OutboundMessage is defined in ChannelManager.h

std::string GetString(const Json::Value& v, const std::string& key,
                      const std::string& def = "") {
    return v.isMember(key) ? v[key].asString() : def;
}

std::string getAttr(const WABinaryNode& node, const std::string& key) {
    return node.getAttr(key);
}

// Forward declaration — DispatchToSession is a ChannelManager member
// (included via ChannelManager.h)

void ChannelManager::WhatsAppGatewayLoop(PollerState* state) {
    while (state->active) {
        try {
            WhatsAppGatewayLoopInner(state);
        } catch (const std::exception& e) {
            std::cerr << "[whatsapp] FATAL: Unhandled exception in gateway loop: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[whatsapp] FATAL: Unknown exception in gateway loop" << std::endl;
        }
        if (!state->active) break;
        std::cerr << "[whatsapp] Reconnecting in 3s..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::cerr << "[whatsapp] Reconnect sleep done, re-entering inner loop" << std::endl;
    }
}

void ChannelManager::WhatsAppGatewayLoopInner(PollerState* state) {
    std::cerr << "[whatsapp] Gateway loop starting for " << state->channel_name << std::endl;

    // Clear any stale QR URL
    {
        std::lock_guard<std::mutex> qrLock(state->whatsapp_qr_mutex);
        state->whatsapp_qr_url.clear();
    }

    // --- Auth state ---
    std::string authDir = GetString(state->config, "auth_dir", "/tmp/wa-auth");
    std::filesystem::create_directories(authDir);
    std::string authFile = authDir + "/auth.json";
    PostLoginStep postLoginStep = PostLoginStep::Idle;

    AuthState auth;
    if (std::filesystem::exists(authFile)) {
        auth = AuthState::load(authFile);
        std::cerr << "[whatsapp] Loaded auth: paired=" << auth.paired
                  << " jid=" << auth.jid << std::endl;
    } else {
        auth.noiseKey = animus::whatsapp::generateKeyPair();
        auth.identityKey = animus::whatsapp::generateKeyPair();
        std::random_device rd;
        std::mt19937 gen(rd());
        auth.registrationId = gen() & 0x3FFFFFFF;

        std::vector<uint8_t> advBytes(32);
        for (int i = 0; i < 32; i++) advBytes[i] = gen() & 0xff;
        {
            static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string b64str;
            b64str.reserve(44);
            for (int i = 0; i < 32; i += 3) {
                uint32_t n = (advBytes[i] << 16) | (i+1 < 32 ? advBytes[i+1] << 8 : 0) | (i+2 < 32 ? advBytes[i+2] : 0);
                b64str += b64[(n >> 18) & 0x3f];
                b64str += b64[(n >> 12) & 0x3f];
                b64str += (i+1 < 32) ? b64[(n >> 6) & 0x3f] : '=';
                b64str += (i+2 < 32) ? b64[n & 0x3f] : '=';
            }
            auth.advSecretKey = b64str;
        }

        {
            auto spk = animus::whatsapp::generateKeyPair();
            auth.signedPreKeyPub = spk.pub;
            auth.signedPreKeyPriv = spk.priv;

            EVP_PKEY* edKey = EVP_PKEY_new_raw_private_key(
                EVP_PKEY_ED25519, nullptr, auth.identityKey.priv.data(), 32);
            if (edKey) {
                size_t edPubLen = 32;
                uint8_t edPub[32];
                EVP_PKEY_get_raw_public_key(edKey, edPub, &edPubLen);
                auth.identityEdPubKey.assign(edPub, edPub + 32);
                EVP_PKEY_free(edKey);
            }

            std::vector<uint8_t> signalPub(33);
            signalPub[0] = 0x05;
            std::memcpy(signalPub.data() + 1, spk.pub.data(), 32);
            std::array<uint8_t, 32> idPriv;
            std::memcpy(idPriv.data(), auth.identityKey.priv.data(), 32);
            auth.signedPreKeySig = animus::ed25519_sign(idPriv, signalPub);
        }

        std::cerr << "[whatsapp] Generated new auth state: regId=" << auth.registrationId << std::endl;
        auth.save(authFile);
    }

    // --- Noise state ---
    animus::whatsapp::KeyPair ephemeralKey = animus::whatsapp::generateKeyPair();
    std::vector<uint8_t> waHeader(std::begin(animus::whatsapp::NOISE_WA_HEADER),
                                   std::end(animus::whatsapp::NOISE_WA_HEADER));
    NoiseState noise;
    noise.init(ephemeralKey, waHeader);

    animus::whatsapp::proto::ClientHello chello;
    chello.ephemeral = noise.getEphemeralPub();
    auto clientHelloProto = animus::whatsapp::proto::encodeClientHello(chello);
    std::cerr << "[whatsapp] Client hello protobuf: " << clientHelloProto.size() << " bytes" << std::endl;

    // --- Signal Protocol ---
    // Create Signal manager with our identity key from auth state
    animus::signal::SignalIdentityKey authIdKey;
    authIdKey.registration_id = auth.registrationId;
    if (!auth.identityKey.pub.empty() && !auth.identityKey.priv.empty()) {
        std::memcpy(authIdKey.keypair.public_key.data(), auth.identityKey.pub.data(), 32);
        std::memcpy(authIdKey.keypair.private_key.data(), auth.identityKey.priv.data(), 32);
    } else {
        authIdKey = animus::signal::SignalIdentityKey::generate(1);
    }
    auto signalMgr = animus::signal::create_in_memory_manager(std::move(authIdKey));
    animus::signal::SignalSessionManager* rawSignalMgr = signalMgr.get();

    // Populate Signal stores from auth state so we can decrypt inbound messages.
    // The pre-keys were generated during auth state creation and uploaded to
    // WhatsApp, but we need them in our local stores for decryption.
    if (!auth.signedPreKeyPub.empty() && !auth.signedPreKeyPriv.empty()) {
        animus::signal::SignalSignedPreKey spk;
        spk.id = auth.signedPreKeyId;
        std::memcpy(spk.keypair.public_key.data(), auth.signedPreKeyPub.data(), 32);
        std::memcpy(spk.keypair.private_key.data(), auth.signedPreKeyPriv.data(), 32);
        spk.signature = auth.signedPreKeySig;
        rawSignalMgr->signed_pre_keys().store_signed_pre_key(spk);
    }

    // Populate one-time pre-keys from auth state
    for (const auto& pk : auth.preKeys) {
        animus::signal::SignalPreKey opk;
        opk.id = pk.id;
        std::memcpy(opk.keypair.public_key.data(), pk.pub.data(), 32);
        std::memcpy(opk.keypair.private_key.data(), pk.priv.data(), 32);
        rawSignalMgr->pre_keys().store_pre_key(opk);
    }
    std::cerr << "[whatsapp] Populated " << auth.preKeys.size()
              << " one-time pre-keys from auth state" << std::endl;

    // --- Connection state ---
    bool handshakeComplete = false;
    std::mutex connMutex;
    std::condition_variable connCV;

    // --- Outbound queue ---
    std::vector<OutboundMessage> outbox;
    std::mutex outboxMutex;

    // Map from base JID → last-seen device JID for outgoing encryption
    // e.g., "199930794737855@lid" → "199930794737855:26@lid"
    std::unordered_map<std::string, std::string> lastDeviceJid;
    std::mutex lastDeviceMutex;

    state->config["_wa_outbox_mutex"] = reinterpret_cast<Json::UInt64>(&outboxMutex);
    state->config["_wa_outbox"] = reinterpret_cast<Json::UInt64>(&outbox);
    state->config["_wa_signal_mgr"] = reinterpret_cast<Json::UInt64>(signalMgr.get());

    // --- Helper: send binary node via noise transport ---
    // (takes lock internally for thread safety)
    animus::whatsapp::WhatsAppTransport* transportPtr = nullptr;

    auto sendNode = [&](const WABinaryNode& node) {
        auto encoded = animus::whatsapp::encodeBinaryNode(node);
        // Debug: hex dump the encoded node
        std::cerr << "[wa-encode] Sending node tag='" << node.tag << "' encoded=" << encoded.size() << " bytes:";
        for (size_t i = 0; i < encoded.size() && i < 20; i++) fprintf(stderr, " %02x", encoded[i]);
        std::cerr << std::endl;
        auto frame = noise.encodeFrame(encoded);
        if (transportPtr) {
            transportPtr->send(frame);
        }
    };

    auto sendQueuedMessage = [&](const OutboundMessage& msg) {
        // Resolve device JID for encryption: msg.baseJid → device JID from mapping
        std::string encryptJid = msg.jid;
        std::string baseJid = msg.baseJid.empty() ? msg.jid : msg.baseJid;
        {
            std::lock_guard<std::mutex> lk(lastDeviceMutex);
            auto it = lastDeviceJid.find(baseJid);
            if (it != lastDeviceJid.end()) {
                encryptJid = it->second;
            }
        }

        std::cerr << "[whatsapp] Sending to " << baseJid
                  << " (encrypt_jid=" << encryptJid << ")"
                  << ": " << msg.text.substr(0, 80) << std::endl;

        auto protoPayload = animus::whatsapp::proto::encodeTextMessage(msg.text);
        animus::signal::SignalAddress remote{encryptJid, 0};

        if (!rawSignalMgr->has_session(remote)) {
            animus::signal::PreKeyBundle bundle;
            bundle.device_id = 0;
            auto init = rawSignalMgr->establish_session(remote, bundle);
            if (!init) {
                std::cerr << "[whatsapp] Failed to establish signal session for "
                          << msg.jid << ": " << init.error << std::endl;
                return;
            }
        }

        auto encrypted = rawSignalMgr->encrypt(remote,
            std::string_view(reinterpret_cast<const char*>(protoPayload.data()),
                             protoPayload.size()));
        if (!encrypted) {
            std::cerr << "[whatsapp] Encryption failed for " << msg.jid
                      << ": " << encrypted.error << std::endl;
            return;
        }

        WABinaryNode encNode;
        encNode.tag = "enc";
        encNode.setAttr("v", "2");
        encNode.setAttr("type", (encrypted.value.type == animus::signal::SignalMessageType::PreKey)
                                ? "pkmsg" : "msg");
        encNode.content = encrypted.value.ciphertext;

        // Diagnostic: hex dump of enc node content
        std::cerr << "[whatsapp] enc node type=" << (encrypted.value.type == animus::signal::SignalMessageType::PreKey ? "pkmsg" : "msg")
                  << " content_len=" << encrypted.value.ciphertext.size() << " hex=";
        for (size_t i = 0; i < std::min(encrypted.value.ciphertext.size(), size_t(40)); ++i)
            fprintf(stderr, "%02x", encrypted.value.ciphertext[i]);
        std::cerr << std::endl;

        WABinaryNode toNode;
        toNode.tag = "to";
        toNode.setAttr("jid", encryptJid);  // Device-specific JID for routing to correct device

        std::vector<WABinaryNode> toChildren;
        toChildren.push_back(std::move(encNode));
        toNode.content = std::move(toChildren);

        // Wrap in 'participants' node (required for 1:1 messages)
        WABinaryNode participantsNode;
        participantsNode.tag = "participants";
        std::vector<WABinaryNode> participantsChildren;
        participantsChildren.push_back(std::move(toNode));
        participantsNode.content = std::move(participantsChildren);

        WABinaryNode msgNode;
        msgNode.tag = "message";
        msgNode.setAttr("to", baseJid);  // Base JID (no device) for message routing
        msgNode.setAttr("id", generateTag());
        msgNode.setAttr("type", "text");

        std::vector<WABinaryNode> msgChildren;
        msgChildren.push_back(std::move(participantsNode));
        msgNode.content = std::move(msgChildren);

        sendNode(msgNode);
        std::cerr << "[whatsapp] Message sent to " << msg.jid << std::endl;
    };

    // --- Create raw transport ---
    animus::whatsapp::WhatsAppTransport transport;
    transportPtr = &transport;

    animus::whatsapp::WhatsAppTransportConfig tcfg;
    tcfg.host = "web.whatsapp.com";
    tcfg.port = 443;
    tcfg.path = "/ws/chat";
    tcfg.origin = "https://web.whatsapp.com";
    if (!auth.routingInfo.empty()) {
        tcfg.query = "ED=" + base64UrlEncode(auth.routingInfo);
    }
    transport.setConfig(tcfg);

    // --- Pending offline messages (collected during post-login, flushed after) ---
    struct PendingMessage {
        std::string routingKey;
        std::string displayText;
    };
    std::vector<PendingMessage> pendingOffline;
    std::mutex pendingOfflineMutex;

    // --- Frame callback (runs on transport's background thread) ---
    transport.onFrame([this, state, &noise, &auth, &authFile, &handshakeComplete,
                       &connMutex, &connCV, &transport, rawSignalMgr,
                       &outbox, &outboxMutex, &sendNode, &postLoginStep,
                       &pendingOffline, &pendingOfflineMutex,
                       &lastDeviceJid, &lastDeviceMutex]
                      (const std::vector<uint8_t>& frameData) {

        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        if (!handshakeComplete) {
            // --- Handshake phase ---
            if (frameData.size() < 3) {
                std::cerr << "[whatsapp] Response too short" << std::endl;
                return;
            }
            size_t frameLen = (frameData[0] << 16) | (frameData[1] << 8) | frameData[2];
            std::cerr << "[whatsapp] Frame length prefix: " << frameLen
                      << " (total " << frameData.size() << " bytes)" << std::endl;

            if (frameData.size() < 3 + frameLen) {
                std::cerr << "[whatsapp] Incomplete frame" << std::endl;
                return;
            }

            std::vector<uint8_t> protoData(frameData.begin() + 3,
                                            frameData.begin() + 3 + frameLen);

            std::cerr << "[whatsapp] Server hello protobuf: " << protoData.size() << " bytes" << std::endl;
            std::cerr << "[whatsapp] Hex: ";
            for (size_t i = 0; i < std::min(protoData.size(), (size_t)64); i++)
                fprintf(stderr, "%02x", protoData[i]);
            std::cerr << std::endl;

            try {
                auto serverHello = animus::whatsapp::proto::decodeServerHello(
                    protoData.data(), protoData.size());

                std::cerr << "[whatsapp] Server hello: eph=" << serverHello.ephemeral.size()
                          << " static=" << serverHello.static_key.size()
                          << " payload=" << serverHello.payload.size() << std::endl;

                if (serverHello.ephemeral.size() != 32)
                    throw std::runtime_error("server ephemeral wrong size");

                auto finishData = noise.processServerHello(
                    serverHello.ephemeral,
                    serverHello.static_key,
                    serverHello.payload,
                    auth.noiseKey);

                bool isNewLogin = !auth.paired;
                auto clientPayload = animus::whatsapp::proto::encodeClientPayload(
                    isNewLogin, auth.jid,
                    auth.registrationId,
                    auth.identityKey.pub,
                    auth.signedPreKeyPub,
                    auth.signedPreKeySig,
                    auth.signedPreKeyId);
                std::cerr << "[whatsapp] Client payload: " << clientPayload.size() << " bytes"
                          << " (isNewLogin=" << isNewLogin
                          << ", jid=" << auth.jid << ")" << std::endl;
                std::cerr << "[whatsapp] Payload hex: ";
                for (size_t i = 0; i < clientPayload.size(); i++) fprintf(stderr, "%02x", clientPayload[i]);
                std::cerr << std::endl;

                auto encPayload = noise.encryptClientPayload(clientPayload);

                animus::whatsapp::proto::ClientFinish cfin;
                cfin.static_key = finishData.static_enc;
                cfin.payload = encPayload;
                auto clientFinishProto = animus::whatsapp::proto::encodeClientFinish(cfin);

                // Build frame: 3-byte length prefix + protobuf (no noise encrypt)
                auto& proto = clientFinishProto;
                std::vector<uint8_t> finishFrame(3 + proto.size());
                finishFrame[0] = (proto.size() >> 16) & 0xff;
                finishFrame[1] = (proto.size() >> 8) & 0xff;
                finishFrame[2] = proto.size() & 0xff;
                std::memcpy(finishFrame.data() + 3, proto.data(), proto.size());

                std::cerr << "[whatsapp] Client finish: " << finishFrame.size()
                          << " bytes (proto=" << proto.size() << ")" << std::endl;
                // Debug: dump full frame hex for comparison with Baileys
                std::cerr << "[whatsapp] clientFinish hex: ";
                for (auto b : finishFrame) fprintf(stderr, "%02x", b);
                std::cerr << std::endl;
                std::cerr << "[whatsapp] encPayload hex: ";
                for (auto b : encPayload) fprintf(stderr, "%02x", b);
                std::cerr << std::endl;
                std::cerr << "[whatsapp] static_enc hex: ";
                for (auto b : finishData.static_enc) fprintf(stderr, "%02x", b);
                std::cerr << std::endl;

                // Send via raw transport
                transport.send(finishFrame);

                // Transition to transport mode
                noise.finishHandshake();
                handshakeComplete = true;
                std::cerr << "[whatsapp] Noise handshake complete, transport mode active" << std::endl;

                if (auth.paired) {
                    // Returning device — no additional IQ needed.
                    // Baileys just waits for the server's 'success' node.
                    // The ClientPayload in the handshake IS the login.
                    std::cerr << "[whatsapp] Login mode — waiting for server success..." << std::endl;
                } else {
                    std::cerr << "[whatsapp] New login — waiting for pair-device IQ" << std::endl;
                }

                // Notify main thread that handshake is done
                connCV.notify_all();

            } catch (const std::exception& e) {
                std::cerr << "[whatsapp] Handshake failed: " << e.what() << std::endl;
            }
            return;
        }

        // --- Transport phase: decrypt and process binary nodes ---
        std::vector<uint8_t> decryptedData;
        try {
            // A single WS frame can contain multiple Noise frames.
            // Each Noise frame has a 3-byte big-endian length prefix.
            size_t offset = 0;
            while (offset < frameData.size()) {
                if (frameData.size() - offset < 3) {
                    std::cerr << "[whatsapp] Trailing bytes in WS frame: " << (frameData.size() - offset) << std::endl;
                    break;
                }
                size_t encLen = (frameData[offset] << 16) | (frameData[offset + 1] << 8) | frameData[offset + 2];
                if (3 + encLen > frameData.size() - offset) {
                    std::cerr << "[whatsapp] Frame truncated at offset " << offset << ": encLen=" << encLen
                              << " but only " << (frameData.size() - offset - 3) << " bytes available" << std::endl;
                    break;
                }
                if (encLen == 0) {
                    std::cerr << "[whatsapp] Zero-length Noise frame at offset " << offset << std::endl;
                    break;
                }
                std::vector<uint8_t> encData(frameData.begin() + offset + 3,
                                              frameData.begin() + offset + 3 + encLen);
                auto decrypted = noise.decrypt(encData);
                decryptedData = decrypted;
                offset += 3 + encLen;

                // Process this Noise frame's binary node
                auto node = animus::whatsapp::decodeBinaryNode(decryptedData);

            std::cerr << "[whatsapp] Node: " << node.tag;
            for (auto& [k, v] : node.attrs)
                std::cerr << " " << k << "=" << v;
            std::cerr << std::endl;

            // Log children
            auto* children = std::get_if<std::vector<WABinaryNode>>(&node.content);
            if (children) {
                for (auto& child : *children) {
                    std::cerr << "  child: " << child.tag;
                    for (auto& [ck, cv] : child.attrs)
                        std::cerr << " " << ck << "=" << cv;
                    std::cerr << std::endl;
                    auto* grandchildren = std::get_if<std::vector<WABinaryNode>>(&child.content);
                    if (grandchildren) {
                        for (auto& gc : *grandchildren) {
                            // Skip verbose 'prop' nodes (config sync spam)
                            if (gc.tag == "prop") continue;
                            std::cerr << "    gc: " << gc.tag;
                            for (auto& [gck, gcv] : gc.attrs)
                                std::cerr << " " << gck << "=" << gcv;
                            std::cerr << std::endl;
                        }
                    }
                }
            }

            const std::string& tag = node.tag;

            if (tag == "failure") {
                std::string reason = getAttr(node, "reason");
                std::string location = getAttr(node, "location");
                std::cerr << "[whatsapp] Connection failure: reason=" << reason
                          << " location=" << location << std::endl;
                if (reason == "401") {
                    // 401 = session no longer valid, need to re-pair
                    std::cerr << "[whatsapp] Session invalidated (401). Clearing auth for re-pairing." << std::endl;
                    auth.paired = false;
                    auth.jid = "";
                    auth.phone = "";
                    auth.platform = "";
                    auth.accountDetails.clear();
                    auth.accountSignatureKey.clear();
                    auth.deviceIdentityRaw.clear();
                    auth.deviceSignature.clear();
                    auth.save(authFile);
                }
                transport.close();
            } else if (tag == "stream:error") {
                // Stream error from server — 515 (restart) or 401 (unauthorized) etc.
                std::string code = getAttr(node, "code");
                std::cerr << "[whatsapp] Stream error: code=" << code << std::endl;
                if (code == "515") {
                    // 515 = restart required (normal after pairing or server-side rotation)
                    std::cerr << "[whatsapp] 515 restart — will reconnect with saved credentials" << std::endl;
                } else if (code == "401" || code == "403") {
                    std::cerr << "[whatsapp] Auth error (" << code << "). Clearing auth for re-pairing." << std::endl;
                    auth.paired = false;
                    auth.jid = "";
                    auth.phone = "";
                    auth.platform = "";
                    auth.accountDetails.clear();
                    auth.accountSignatureKey.clear();
                    auth.deviceIdentityRaw.clear();
                    auth.deviceSignature.clear();
                    auth.save(authFile);
                }
                transport.close();
            } else if (tag == "iq") {
                std::string iqType = getAttr(node, "type");
                std::cerr << "[whatsapp] iqType='" << iqType << "' attrs:";
                for (auto& [k, v] : node.attrs) std::cerr << " ['" << k << "'='" << v << "']";
                std::cerr << std::endl;

                // pair-device IQ
                if (iqType == "set") {
                    // Debug: log all children tags
                    auto* iqChildren = std::get_if<std::vector<WABinaryNode>>(&node.content);
                    if (iqChildren) {
                        for (auto& c : *iqChildren) {
                            std::cerr << "[whatsapp] IQ child tag='" << c.tag << "' len=" << c.tag.size();
                            for (size_t ci = 0; ci < c.tag.size() && ci < 4; ci++) fprintf(stderr, " %02x", (unsigned char)c.tag[ci]);
                            std::cerr << std::endl;
                        }
                    }
                    auto* pairDeviceNode = animus::whatsapp::getBinaryNodeChild(node, "pair-device");
                    std::cerr << "[whatsapp] getBinaryNodeChild result: " << (pairDeviceNode ? "FOUND" : "NULL") << std::endl;
                    if (pairDeviceNode) {
                        std::cerr << "[whatsapp] pair-device node found, content index=" << pairDeviceNode->content.index() << std::endl;
                        // Ack
                        {
                            WABinaryNode ack;
                            ack.tag = "iq";
                            ack.setAttr("to", "s.whatsapp.net");
                            ack.setAttr("type", "result");
                            ack.setAttr("id", getAttr(node, "id"));
                            sendNode(ack);
                        }

                        std::vector<std::string> refStrings;
                        auto* pdChildren = std::get_if<std::vector<WABinaryNode>>(&pairDeviceNode->content);
                        if (pdChildren) {
                            std::cerr << "[whatsapp] pair-device has " << pdChildren->size() << " children" << std::endl;
                            for (auto& child : *pdChildren) {
                                std::cerr << "  ref child: tag=" << child.tag << " content_index=" << child.content.index() << std::endl;
                                if (child.tag == "ref") {
                                    // Try string content first
                                    auto* strContent = std::get_if<std::string>(&child.content);
                                    if (strContent) {
                                        refStrings.push_back(*strContent);
                                        std::cerr << "    string: " << *strContent << std::endl;
                                    } else {
                                        auto* binContent = std::get_if<std::vector<uint8_t>>(&child.content);
                                        if (binContent) {
                                            refStrings.push_back(std::string(binContent->begin(), binContent->end()));
                                            std::cerr << "    binary: " << std::string(binContent->begin(), binContent->end()) << std::endl;
                                        }
                                    }
                                }
                            }
                        }

                        if (!refStrings.empty()) {
                            std::string noiseKeyB64 = base64Encode(auth.noiseKey.pub);
                            std::string identityKeyB64 = base64Encode(auth.identityKey.pub);
                            std::string qrData = "https://wa.me/settings/linked_devices#"
                                + refStrings[0] + ","
                                + noiseKeyB64 + ","
                                + identityKeyB64 + ","
                                + auth.advSecretKey + ",1";

                            std::cerr << "\n[whatsapp] ========== QR CODE ==========" << std::endl;
                            std::cerr << "[whatsapp] " << qrData << std::endl;
                            std::cerr << "[whatsapp] Scan with WhatsApp > Linked Devices" << std::endl;
                            std::cerr << "[whatsapp] ==============================" << std::endl;

                            // Store QR URL for admin API
                            {
                                std::lock_guard<std::mutex> qrLock(state->whatsapp_qr_mutex);
                                state->whatsapp_qr_url = qrData;
                            }
                        }
                    }
                }

                // pair-success
                if (iqType == "set") {
                    auto* pairSuccessNode = getBinaryNodeChild(node, "pair-success");
                    if (pairSuccessNode) {
                        std::cerr << "[whatsapp] Received pair-success, processing..." << std::endl;

                        bool pairOk = false;
                        do { // do-once for break-to-continue semantics

                        // Extract device info
                        auto* deviceNode = getBinaryNodeChild(*pairSuccessNode, "device");
                        auto* platformNode = getBinaryNodeChild(*pairSuccessNode, "platform");
                        auto* deviceIdentityNode = getBinaryNodeChild(*pairSuccessNode, "device-identity");

                        if (!deviceNode || !deviceIdentityNode) {
                            std::cerr << "[whatsapp] ERROR: Missing device or device-identity in pair-success" << std::endl;
                            break;
                        }

                        std::string pairJid = getAttr(*deviceNode, "jid");
                        std::string pairLid = getAttr(*deviceNode, "lid");
                        std::string platformName = platformNode ? getAttr(*platformNode, "name") : "";

                        // Get device-identity content (protobuf bytes)
                        auto* diContent = std::get_if<std::vector<uint8_t>>(&deviceIdentityNode->content);
                        if (!diContent || diContent->empty()) {
                            std::cerr << "[whatsapp] ERROR: device-identity has no binary content" << std::endl;
                            break;
                        }

                        // Step 1: Decode ADVSignedDeviceIdentityHMAC
                        auto hmacMsg = animus::whatsapp::proto::decodeADVSignedDeviceIdentityHMAC(*diContent);
                        std::cerr << "[whatsapp] device-identity content size: " << diContent->size() << std::endl;
                        std::cerr << "[whatsapp] HMAC msg: details=" << hmacMsg.details.size()
                                  << " bytes, hmac=" << hmacMsg.hmac.size()
                                  << " bytes, accountType=" << hmacMsg.accountType << std::endl;

                        // Debug: first bytes of device-identity content
                        std::cerr << "[whatsapp] device-identity first 16 bytes: ";
                        for (size_t i = 0; i < std::min(diContent->size(), (size_t)16); i++) fprintf(stderr, "%02x", (*diContent)[i]);
                        std::cerr << std::endl;

                        // Step 2: Verify HMAC using advSecretKey
                        auto advKeyBytes = base64Decode(auth.advSecretKey);
                        std::cerr << "[whatsapp] advSecretKey base64: " << auth.advSecretKey.substr(0, 20) << "..." << std::endl;
                        std::cerr << "[whatsapp] advKeyBytes (" << advKeyBytes.size() << " bytes): ";
                        for (size_t i = 0; i < std::min(advKeyBytes.size(), (size_t)8); i++) fprintf(stderr, "%02x", advKeyBytes[i]);
                        std::cerr << "..." << std::endl;
                        std::cerr << "[whatsapp] HMAC msg details (" << hmacMsg.details.size() << " bytes): ";
                        for (size_t i = 0; i < std::min(hmacMsg.details.size(), (size_t)8); i++) fprintf(stderr, "%02x", hmacMsg.details[i]);
                        std::cerr << "..." << std::endl;
                        std::cerr << "[whatsapp] HMAC msg hmac (" << hmacMsg.hmac.size() << " bytes): ";
                        for (size_t i = 0; i < std::min(hmacMsg.hmac.size(), (size_t)16); i++) fprintf(stderr, "%02x", hmacMsg.hmac[i]);
                        std::cerr << std::endl;
                        std::cerr << "[whatsapp] HMAC msg accountType: " << hmacMsg.accountType << std::endl;
                        std::vector<uint8_t> hmacInput;
                        if (hmacMsg.accountType == 1) { // HOSTED
                            hmacInput = prefixBytes(ADV_HOSTED_ACCOUNT_SIG_PREFIX,
                                sizeof(ADV_HOSTED_ACCOUNT_SIG_PREFIX), hmacMsg.details);
                        } else {
                            // E2EE: HMAC is computed on details alone, NO prefix
                            hmacInput = hmacMsg.details;
                        }
                        // Debug: dump full details hex for offline verification
                        std::cerr << "[whatsapp] details full hex: ";
                        for (auto b : hmacMsg.details) fprintf(stderr, "%02x", b);
                        std::cerr << std::endl;
                        std::cerr << "[whatsapp] hmac full hex: ";
                        for (auto b : hmacMsg.hmac) fprintf(stderr, "%02x", b);
                        std::cerr << std::endl;
                        std::cerr << "[whatsapp] advKeyBytes full hex: ";
                        for (auto b : advKeyBytes) fprintf(stderr, "%02x", b);
                        std::cerr << std::endl;
                        auto computedHmac = hmacSHA256(advKeyBytes, hmacInput);

                        if (computedHmac.size() != hmacMsg.hmac.size() ||
                            !std::equal(computedHmac.begin(), computedHmac.end(), hmacMsg.hmac.begin())) {
                            std::cerr << "[whatsapp] ERROR: HMAC verification failed!" << std::endl;
                            std::cerr << "[whatsapp] Expected: " << bytesToHex(computedHmac).substr(0, 16) << "..." << std::endl;
                            std::cerr << "[whatsapp] Got:      " << bytesToHex(hmacMsg.hmac).substr(0, 16) << "..." << std::endl;
                            break;
                        }
                        std::cerr << "[whatsapp] HMAC verified OK" << std::endl;

                        // Step 3: Decode ADVSignedDeviceIdentity
                        auto account = animus::whatsapp::proto::decodeADVSignedDeviceIdentity(hmacMsg.details);

                        // Step 4: Decode inner ADVDeviceIdentity
                        auto deviceIdentity = animus::whatsapp::proto::decodeADVDeviceIdentity(account.details);

                        std::cerr << "[whatsapp] Device identity: rawId=" << deviceIdentity.rawId
                                  << " keyIndex=" << deviceIdentity.keyIndex
                                  << " accountType=" << deviceIdentity.accountType
                                  << " deviceType=" << deviceIdentity.deviceType << std::endl;

                        // Step 5: Verify account signature
                        // XEdDSA verification: the accountSignatureKey is a 32-byte X25519 key.
                        // The signature was made using curve25519-js (XEdDSA), which stores
                        // the sign bit in signature[63]. We use x25519_verify which handles
                        // the X25519→Ed25519 conversion and sign bit extraction.

                        // Build accountMsg = [prefix] + deviceDetails + signedIdentityKey.public
                        std::vector<uint8_t> accountMsg;
                        if (deviceIdentity.deviceType == 1) { // HOSTED
                            accountMsg = prefixBytes(ADV_HOSTED_ACCOUNT_SIG_PREFIX,
                                sizeof(ADV_HOSTED_ACCOUNT_SIG_PREFIX), account.details);
                        } else {
                            accountMsg = prefixBytes(ADV_ACCOUNT_SIG_PREFIX,
                                sizeof(ADV_ACCOUNT_SIG_PREFIX), account.details);
                        }
                        // Append our identity key (32 bytes, X25519, without prefix)
                        accountMsg.insert(accountMsg.end(), auth.identityKey.pub.begin(), auth.identityKey.pub.end());

                        std::cerr << "[whatsapp] Verifying account signature (XEdDSA)..." << std::endl;
                        std::cerr << "[whatsapp] accountSignatureKey (32): ";
                        for (size_t i = 0; i < 8; i++) fprintf(stderr, "%02x", account.accountSignatureKey[i]);
                        std::cerr << "..." << std::endl;
                        std::cerr << "[whatsapp] accountMsg (first 24): ";
                        for (size_t i = 0; i < std::min(accountMsg.size(), (size_t)24); i++) fprintf(stderr, "%02x", accountMsg[i]);
                        std::cerr << std::endl;
                        std::cerr << "[whatsapp] accountSignature (64): ";
                        for (size_t i = 0; i < 8; i++) fprintf(stderr, "%02x", account.accountSignature[i]);
                        std::cerr << "..." << std::endl;

                        if (!x25519_verify(account.accountSignatureKey, accountMsg, account.accountSignature)) {
                            std::cerr << "[whatsapp] ERROR: Account signature verification failed!" << std::endl;
                            break;
                        }

                        // Step 6: Sign device identity
                        // deviceMsg = [DEVICE_SIG_PREFIX] + deviceDetails + signedIdentityKey.public + accountSignatureKey
                        std::vector<uint8_t> deviceMsg =
                            prefixBytes(ADV_DEVICE_SIG_PREFIX, sizeof(ADV_DEVICE_SIG_PREFIX),
                                        account.details);
                        deviceMsg.insert(deviceMsg.end(), auth.identityKey.pub.begin(), auth.identityKey.pub.end());
                        deviceMsg.insert(deviceMsg.end(), account.accountSignatureKey.begin(),
                                         account.accountSignatureKey.end());

                        std::array<uint8_t, 32> identityPriv;
                        std::copy(auth.identityKey.priv.begin(), auth.identityKey.priv.end(),
                                  identityPriv.begin());
                        auto deviceSignature = ed25519_sign(identityPriv, deviceMsg);

                        // Step 7: Build reply — pair-device-sign with device-identity
                        // Encode the signed device identity (without accountSignatureKey)
                        animus::whatsapp::proto::ADVSignedDeviceIdentity signedIdentity;
                        signedIdentity.details = account.details;
                        signedIdentity.accountSignature = account.accountSignature;
                        signedIdentity.deviceSignature = deviceSignature;
                        // accountSignatureKey is omitted per encodeSignedDeviceIdentity(account, false)

                        auto encodedIdentity = animus::whatsapp::proto::encodeADVSignedDeviceIdentity(signedIdentity);

                        // Debug: hex dump the encoded identity
                        std::cerr << "[whatsapp] Encoded device-identity (" << encodedIdentity.size() << " bytes):";
                        for (size_t i = 0; i < encodedIdentity.size(); i++) {
                            if (i % 32 == 0) std::cerr << std::endl;
                            fprintf(stderr, "%02x", encodedIdentity[i]);
                        }
                        std::cerr << std::endl;
                        std::cerr << "[whatsapp] details (" << signedIdentity.details.size() << " bytes), accountSignature (" << signedIdentity.accountSignature.size() << " bytes), deviceSignature (" << signedIdentity.deviceSignature.size() << " bytes)" << std::endl;

                        WABinaryNode deviceIdNode;
                        deviceIdNode.tag = "device-identity";
                        deviceIdNode.setAttr("key-index", std::to_string(deviceIdentity.keyIndex));
                        deviceIdNode.content = encodedIdentity;

                        WABinaryNode pairSignNode;
                        pairSignNode.tag = "pair-device-sign";
                        pairSignNode.content = std::vector<WABinaryNode>{deviceIdNode};

                        WABinaryNode reply;
                        reply.tag = "iq";
                        reply.setAttr("to", "s.whatsapp.net");
                        reply.setAttr("type", "result");
                        reply.setAttr("id", getAttr(node, "id"));
                        reply.content = std::vector<WABinaryNode>{pairSignNode};

                        // Update auth state
                        auth.jid = pairJid;
                        auth.phone = pairLid;
                        auth.platform = platformName;
                        auth.paired = true;
                        auth.accountDetails = account.details;
                        auth.accountSignatureKey = account.accountSignatureKey;
                        auth.deviceIdentityRaw = account.details; // store for future reference
                        auth.deviceSignature = deviceSignature;
                        auth.save(authFile);

                        std::cerr << "[whatsapp] PAIRED SUCCESSFULLY! jid=" << auth.jid
                                  << " platform=" << platformName
                                  << " keyIndex=" << deviceIdentity.keyIndex << std::endl;

                        sendNode(reply);

                        // After sending pair-device-sign, server will send 515 (restart required)
                        // This is normal — WhatsApp expects a reconnection with saved credentials.
                        // The outer ChannelManager will handle reconnection.
                        std::cerr << "[whatsapp] Sent pair-device-sign. Expecting 515 restart (normal after pairing)." << std::endl;
                        pairOk = true;
                        } while(false); // end do-once
                        if (!pairOk) {
                            std::cerr << "[whatsapp] Pair-success processing failed" << std::endl;
                        }
                    }
                }

                if (iqType == "result") {
                    // Registration QR code (pair-device response)
                    auto* child = animus::whatsapp::getBinaryNodeChild(node, "registration");
                    if (child) {
                        std::string ref = getAttr(*child, "ref");
                        if (!ref.empty()) {
                            std::string qrData = "https://wa.me/settings/linked_devices#"
                                + ref + ","
                                + base64Encode(auth.noiseKey.pub) + ","
                                + base64Encode(auth.identityKey.pub) + ","
                                + auth.advSecretKey + ",1";
                            std::cerr << "\n[whatsapp] ========== QR CODE ==========" << std::endl;
                            std::cerr << "[whatsapp] " << qrData << std::endl;
                            std::cerr << "[whatsapp] ==============================" << std::endl;
                        }
                    }

                    auto* connChild = animus::whatsapp::getBinaryNodeChild(node, "Conn");
                    if (connChild) {
                        std::string connJid = getAttr(*connChild, "jid");
                        if (!connJid.empty()) {
                            auth.jid = connJid;
                            auth.save(authFile);
                        }
                    }

                    // Post-login state machine: advance through sequential steps
                    // Only advance if the IQ response matches our expected ID
                    std::string iqId = getAttr(node, "id");
                    if (postLoginStep == PostLoginStep::WaitCount && iqId == postLoginExpectedId) {
                        // Got count response → check pre-key count and upload if needed
                        // The count response child should have a "count" node with the server's
                        // stored pre-key count
                        int serverPreKeyCount = 0;
                        auto* countChildren = std::get_if<std::vector<WABinaryNode>>(&node.content);
                        if (countChildren) {
                            for (const auto& child : *countChildren) {
                                if (child.tag == "count") {
                                    std::string countStr = child.getAttr("value");
                                    if (countStr.empty()) {
                                        // Try getting from text content
                                        auto* txt = std::get_if<std::vector<uint8_t>>(&child.content);
                                        if (txt && !txt->empty()) {
                                            countStr = std::string(txt->begin(), txt->end());
                                        }
                                    }
                                    if (!countStr.empty()) serverPreKeyCount = std::stoi(countStr);
                                }
                            }
                        }
                        std::cerr << "[whatsapp] Server has " << serverPreKeyCount
                                  << " one-time pre-keys stored" << std::endl;

                        // Generate and upload pre-keys if server has fewer than 30
                        const int PREKEY_TARGET = 30;
                        if (serverPreKeyCount < PREKEY_TARGET) {
                            int toGenerate = PREKEY_TARGET - serverPreKeyCount;
                            std::cerr << "[whatsapp] Generating " << toGenerate
                                      << " one-time pre-keys to upload" << std::endl;

                            // Start pre-key IDs after existing ones
                            uint32_t startId = static_cast<uint32_t>(auth.preKeys.size()) + 1;

                            // Build pre-key upload node
                            WABinaryNode uploadNode;
                            uploadNode.tag = "iq";
                            uploadNode.setAttr("to", "s.whatsapp.net");
                            uploadNode.setAttr("type", "set");
                            uploadNode.setAttr("xmlns", "encrypt");
                            uploadNode.setAttr("id", generateTag());

                            std::vector<WABinaryNode> preKeyNodes;
                            for (int i = 0; i < toGenerate; i++) {
                                auto keyPair = animus::whatsapp::generateKeyPair();
                                uint32_t pkId = startId + i;

                                // Store locally
                                AuthState::PreKeyEntry entry;
                                entry.id = pkId;
                                entry.pub = keyPair.pub;
                                entry.priv = keyPair.priv;
                                auth.preKeys.push_back(entry);

                                // Also store in Signal manager
                                animus::signal::SignalPreKey opk;
                                opk.id = pkId;
                                std::memcpy(opk.keypair.public_key.data(), keyPair.pub.data(), 32);
                                std::memcpy(opk.keypair.private_key.data(), keyPair.priv.data(), 32);
                                rawSignalMgr->pre_keys().store_pre_key(opk);

                                // Build pre-key node for upload
                                WABinaryNode pkNode;
                                pkNode.tag = "prekey";
                                pkNode.setAttr("id", std::to_string(pkId));
                                // Content is [0x05 prefix + public key(32 bytes)]
                                std::vector<uint8_t> pubWithPrefix;
                                pubWithPrefix.push_back(0x05);
                                pubWithPrefix.insert(pubWithPrefix.end(), keyPair.pub.begin(), keyPair.pub.end());
                                pkNode.content = pubWithPrefix;
                                preKeyNodes.push_back(pkNode);
                            }

                            WABinaryNode keyBundle;
                            keyBundle.tag = "keys";
                            keyBundle.content = preKeyNodes;
                            uploadNode.content = std::vector<WABinaryNode>{keyBundle};
                            sendNode(uploadNode);

                            // Save auth state with new pre-keys
                            auth.save(authFile);
                            std::cerr << "[whatsapp] Uploaded " << toGenerate
                                      << " one-time pre-keys (saved to auth)" << std::endl;
                        }

                        // Bare presence
                        postLoginStep = PostLoginStep::WaitPassive;

                        // Bare presence
                        {
                            WABinaryNode presence;
                            presence.tag = "presence";
                            sendNode(presence);
                            std::cerr << "[whatsapp] [post-login step 2a] Sent bare presence" << std::endl;
                        }

                        // Passive IQ
                        {
                            WABinaryNode passive;
                            passive.tag = "iq";
                            passive.setAttr("to", "s.whatsapp.net");
                            passive.setAttr("type", "set");
                            passive.setAttr("xmlns", "passive");
                            std::string passiveId = generateTag();
                            postLoginExpectedId = passiveId;
                            passive.setAttr("id", passiveId);
                            WABinaryNode activeNode;
                            activeNode.tag = "active";
                            passive.content = std::vector<WABinaryNode>{activeNode};
                            sendNode(passive);
                            std::cerr << "[whatsapp] [post-login step 2b] Sent passive IQ" << std::endl;
                        }
                    } else if (postLoginStep == PostLoginStep::WaitPassive && iqId == postLoginExpectedId) {
                        // Got passive response → send digest query
                        postLoginStep = PostLoginStep::WaitDigest;
                        {
                            WABinaryNode digestNode;
                            digestNode.tag = "iq";
                            digestNode.setAttr("to", "s.whatsapp.net");
                            digestNode.setAttr("type", "get");
                            digestNode.setAttr("xmlns", "encrypt");
                            std::string digestId = generateTag();
                            postLoginExpectedId = digestId;
                            digestNode.setAttr("id", digestId);
                            WABinaryNode digestChild;
                            digestChild.tag = "digest";
                            digestNode.content = std::vector<WABinaryNode>{digestChild};
                            sendNode(digestNode);
                            std::cerr << "[whatsapp] [post-login step 3] Sent digest query" << std::endl;
                        }
                    } else if (postLoginStep == PostLoginStep::WaitDigest && iqId == postLoginExpectedId) {
                        // Got digest response → post-login sequence complete
                        postLoginStep = PostLoginStep::Done;

                        // Flush any pending offline messages as a batch
                        {
                            std::lock_guard<std::mutex> lock(pendingOfflineMutex);
                            if (!pendingOffline.empty()) {
                                std::cerr << "[whatsapp] Flushing " << pendingOffline.size()
                                          << " pending offline messages" << std::endl;
                                // Group by routing key and dispatch each group as one prompt
                                std::map<std::string, std::vector<std::string>> bySession;
                                for (const auto& pm : pendingOffline) {
                                    bySession[pm.routingKey].push_back(pm.displayText);
                                }
                                for (const auto& [rk, messages] : bySession) {
                                    std::string batched;
                                    for (size_t i = 0; i < messages.size(); i++) {
                                        if (i > 0) batched += "\n";
                                        batched += messages[i];
                                    }
                                    DispatchToSession(state, rk, batched, "chat");
                                }
                                pendingOffline.clear();
                            }
                        }

                        // Send remaining Baileys post-login queries
                        // abt props query
                        {
                            WABinaryNode abtNode;
                            abtNode.tag = "iq";
                            abtNode.setAttr("to", "s.whatsapp.net");
                            abtNode.setAttr("type", "get");
                            abtNode.setAttr("xmlns", "abt");
                            abtNode.setAttr("id", generateTag());
                            WABinaryNode propsNode;
                            propsNode.tag = "props";
                            propsNode.setAttr("protocol", "1");
                            abtNode.content = std::vector<WABinaryNode>{propsNode};
                            sendNode(abtNode);
                            std::cerr << "[whatsapp] Sent abt props query" << std::endl;
                        }

                        // blocklist query
                        {
                            WABinaryNode blockNode;
                            blockNode.tag = "iq";
                            blockNode.setAttr("xmlns", "blocklist");
                            blockNode.setAttr("to", "s.whatsapp.net");
                            blockNode.setAttr("type", "get");
                            blockNode.setAttr("id", generateTag());
                            sendNode(blockNode);
                            std::cerr << "[whatsapp] Sent blocklist query" << std::endl;
                        }

                        // privacy query
                        {
                            WABinaryNode privNode;
                            privNode.tag = "iq";
                            privNode.setAttr("xmlns", "privacy");
                            privNode.setAttr("to", "s.whatsapp.net");
                            privNode.setAttr("type", "get");
                            privNode.setAttr("id", generateTag());
                            WABinaryNode privChild;
                            privChild.tag = "privacy";
                            privNode.content = std::vector<WABinaryNode>{privChild};
                            sendNode(privNode);
                            std::cerr << "[whatsapp] Sent privacy query" << std::endl;
                        }

                        // unified_session + presence available (Baileys' sendPresenceUpdate)
                        {
                            // unified_session 1
                            WABinaryNode ibNode;
                            ibNode.tag = "ib";
                            WABinaryNode usNode;
                            usNode.tag = "unified_session";
                            {
                                auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch()).count();
                                int64_t threeDaysMs = 3LL * 24 * 60 * 60 * 1000;
                                int64_t oneWeekMs = 7LL * 24 * 60 * 60 * 1000;
                                int64_t sessionId = (nowMs + threeDaysMs) % oneWeekMs;
                                usNode.setAttr("id", std::to_string(sessionId));
                            }
                            ibNode.content = std::vector<WABinaryNode>{usNode};
                            sendNode(ibNode);

                            // presence available
                            WABinaryNode presence;
                            presence.tag = "presence";
                            std::string pushName = auth.name;
                            if (pushName.empty()) pushName = "Kestrel";
                            pushName.erase(std::remove(pushName.begin(), pushName.end(), '@'), pushName.end());
                            presence.setAttr("name", pushName);
                            presence.setAttr("type", "available");
                            sendNode(presence);

                            // unified_session 2
                            WABinaryNode ibNode2;
                            ibNode2.tag = "ib";
                            WABinaryNode usNode2;
                            usNode2.tag = "unified_session";
                            {
                                auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch()).count();
                                int64_t threeDaysMs = 3LL * 24 * 60 * 60 * 1000;
                                int64_t oneWeekMs = 7LL * 24 * 60 * 60 * 1000;
                                int64_t sessionId = (nowMs + threeDaysMs) % oneWeekMs + 1;
                                usNode2.setAttr("id", std::to_string(sessionId));
                            }
                            ibNode2.content = std::vector<WABinaryNode>{usNode2};
                            sendNode(ibNode2);

                            std::cerr << "[whatsapp] Sent unified_session + presence available" << std::endl;
                        }

                        std::cerr << "[whatsapp] ========================================" << std::endl;
                        std::cerr << "[whatsapp] CONNECTION FULLY ESTABLISHED!" << std::endl;
                        std::cerr << "[whatsapp] jid=" << auth.jid << " phone=" << auth.phone << std::endl;
                        std::cerr << "[whatsapp] ========================================" << std::endl;
                    }
                } else if (iqType == "error") {
                    // Server ping (urn:xmpp:ping) — must respond to keep connection alive
                    std::string xmlns = getAttr(node, "xmlns");
                    WABinaryNode pong;
                    pong.tag = "iq";
                    pong.setAttr("to", getAttr(node, "from"));
                    pong.setAttr("type", "result");
                    pong.setAttr("id", getAttr(node, "id"));
                    sendNode(pong);
                    std::cerr << "[whatsapp] Responded to IQ get (" << xmlns << ")" << std::endl;
                }
            } else if (tag == "message") {
                auto decrypted = handleEncryptedMessage(
                    node,
                    rawSignalMgr,
                    &rawSignalMgr->pre_keys(),
                    &rawSignalMgr->signed_pre_keys(),
                    &rawSignalMgr->identity());

                std::string displayText = "[WA] " + decrypted.sender + ": " + decrypted.text;

                // Extract message timestamp from the node
                int64_t msgTs = 0;
                std::string tsAttr = getAttr(node, "t");
                if (!tsAttr.empty()) {
                    try { msgTs = std::stoll(tsAttr); } catch (...) {}
                }

                // Determine routing key
                // Strip device number from 1:1 JIDs so desktop (:26@lid) and mobile (@lid)
                // route to the same session
                std::string baseFrom = decrypted.from;
                if (!decrypted.isGroup) {
                    auto colonPos = baseFrom.find(':');
                    if (colonPos != std::string::npos) {
                        auto atPos = baseFrom.find('@');
                        if (atPos != std::string::npos && atPos > colonPos) {
                            // Store device JID → base JID mapping for outgoing replies
                            {
                                std::lock_guard<std::mutex> lk(lastDeviceMutex);
                                lastDeviceJid[baseFrom.substr(0, colonPos) + baseFrom.substr(atPos)] = decrypted.from;
                            }
                            baseFrom = baseFrom.substr(0, colonPos) + baseFrom.substr(atPos);
                        }
                    }
                }
                std::string routingKey = decrypted.isGroup ? ("post:" + decrypted.from) : ("peer:" + baseFrom);

                // Check if this is an old (already-seen) message
                auto lastTsIt = auth.lastMessageTs.find(decrypted.from);
                bool isOld = (lastTsIt != auth.lastMessageTs.end() && msgTs <= lastTsIt->second);

                if (isOld) {
                    std::cerr << "[whatsapp] Old message (ts=" << msgTs << ", last=" << lastTsIt->second
                              << "): " << displayText << std::endl;
                } else if (postLoginStep != PostLoginStep::Done) {
                    // New message but post-login not yet complete — buffer it
                    std::cerr << "[whatsapp] New offline message (ts=" << msgTs << "): " << displayText << std::endl;
                    std::lock_guard<std::mutex> lock(pendingOfflineMutex);
                    pendingOffline.push_back({routingKey, displayText});
                    // Update timestamp so we don't replay it next restart either
                    if (msgTs > 0) {
                        auth.lastMessageTs[decrypted.from] = msgTs;
                        auth.save(authFile);
                    }
                } else {
                    // Live message — dispatch immediately
                    std::cerr << "[whatsapp] Message: " << displayText << std::endl;
                    DispatchToSession(state, routingKey, displayText, "chat");
                    // Update timestamp
                    if (msgTs > 0) {
                        auth.lastMessageTs[decrypted.from] = msgTs;
                        auth.save(authFile);
                    }
                }
                state->last_ws_event = std::chrono::steady_clock::now();

                // Delivery receipt
                {
                    WABinaryNode receiptNode;
                    receiptNode.tag = "receipt";
                    receiptNode.setAttr("to", decrypted.from);
                    receiptNode.setAttr("id", decrypted.messageId);
                    receiptNode.setAttr("type", "read");
                    receiptNode.setAttr("t", std::to_string(
                        std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count()));
                    if (decrypted.isGroup) receiptNode.setAttr("participant", decrypted.sender);
                    sendNode(receiptNode);
                }
            } else if (tag == "receipt") {
                std::string from = getAttr(node, "from");
                std::string receiptType = getAttr(node, "type");
                std::cerr << "[whatsapp] Receipt: type=" << receiptType
                          << " from=" << from << std::endl;
            } else if (tag == "ib" || tag == "ack") {
                // Handle ib (info buffer) nodes — dirty nodes require sync responses
                auto* ibChildren = std::get_if<std::vector<WABinaryNode>>(&node.content);
                if (ibChildren) {
                    for (auto& child : *ibChildren) {
                        if (child.tag == "offline") {
                            std::string count = getAttr(child, "count");
                            std::cerr << "[whatsapp] Offline notifications: count=" << count << std::endl;
                            // Send offline_batch to tell server we're ready for messages
                            WABinaryNode batch;
                            batch.tag = "ib";
                            WABinaryNode obNode;
                            obNode.tag = "offline_batch";
                            obNode.setAttr("count", "100");
                            batch.content = std::vector<WABinaryNode>{obNode};
                            sendNode(batch);
                            std::cerr << "[whatsapp] Sent offline_batch request" << std::endl;
                        } else if (child.tag == "offline_preview") {
                            std::cerr << "[whatsapp] Offline preview received" << std::endl;
                            // Tell server we can handle 100 offline messages
                            WABinaryNode batch;
                            batch.tag = "ib";
                            WABinaryNode obNode;
                            obNode.tag = "offline_batch";
                            obNode.setAttr("count", "100");
                            batch.content = std::vector<WABinaryNode>{obNode};
                            sendNode(batch);
                            std::cerr << "[whatsapp] Sent offline_batch request" << std::endl;
                        } else if (child.tag == "dirty") {
                            std::string dirtyType = getAttr(child, "type");
                            std::string dirtyTs = getAttr(child, "timestamp");
                            std::cerr << "[whatsapp] Dirty: type=" << dirtyType
                                      << " timestamp=" << dirtyTs << std::endl;

                            // Dirty node received — log it
                            // Sync response is deferred until appStateSyncKeyShare is received
                            // (sending it proactively causes 400 bad-request)
                        } else if (child.tag == "edge_routing") {
                            std::cerr << "[whatsapp] Edge routing update" << std::endl;
                            // Extract routing_info binary content and save to auth
                            auto* riNode = getBinaryNodeChild(child, "routing_info");
                            if (riNode) {
                                auto* riContent = std::get_if<std::vector<uint8_t>>(&riNode->content);
                                if (riContent && !riContent->empty()) {
                                    auth.routingInfo = std::string(riContent->begin(), riContent->end());
                                    auth.save(authFile);
                                    std::cerr << "[whatsapp] Updated routing info (" << riContent->size() << " bytes)" << std::endl;
                                } else {
                                    auto* strContent = std::get_if<std::string>(&riNode->content);
                                    if (strContent && !strContent->empty()) {
                                        auth.routingInfo = *strContent;
                                        auth.save(authFile);
                                        std::cerr << "[whatsapp] Updated routing info (string, " << strContent->size() << " bytes)" << std::endl;
                                    }
                                }
                            }
                        } else {
                            std::cerr << "[whatsapp] ib child: " << child.tag << std::endl;
                        }
                    }
                } else {
                    std::cerr << "[whatsapp] " << tag << " received" << std::endl;
                }
            } else if (tag == "notification") {
                std::string nType = getAttr(node, "type");
                std::cerr << "[whatsapp] Notification: type=" << nType << std::endl;

                if (nType == "account_sync" || nType == "encrypt") {
                    auto* pairNode = animus::whatsapp::getBinaryNodeChild(node, "pair-success");
                    if (pairNode) {
                        auth.jid = getAttr(*pairNode, "jid");
                        auth.phone = getAttr(*pairNode, "phone");
                        auth.paired = true;
                        auth.save(authFile);
                        std::cerr << "[whatsapp] Pair-success via notification! jid=" << auth.jid << std::endl;
                    }
                }
                {
                    std::string notifId = getAttr(node, "id");
                    std::string notifFrom = getAttr(node, "from");
                    if (!notifId.empty()) {
                        WABinaryNode ackNode;
                        ackNode.tag = "ack";  // Baileys uses 'ack', not 'receipt'
                        ackNode.setAttr("to", notifFrom);
                        ackNode.setAttr("id", notifId);
                        ackNode.setAttr("class", "notification");
                        ackNode.setAttr("type", nType);  // Match notification type
                        sendNode(ackNode);
                        std::cerr << "[whatsapp] ACKed notification type=" << nType << std::endl;
                    }
                }
            } else if (tag == "success") {
                // Login success — connection is now fully established
                std::string lid = getAttr(node, "lid");
                std::string abprops = getAttr(node, "abprops");
                std::string creation = getAttr(node, "creation");
                std::string companionEncStatic = getAttr(node, "companion_enc_static");
                std::cerr << "[whatsapp] Login SUCCESS" << std::endl;
                if (!lid.empty()) {
                    std::cerr << "[whatsapp] LID: " << lid << std::endl;
                    if (auth.phone.empty() || auth.phone != lid) {
                        auth.phone = lid;
                        auth.save(authFile);
                    }
                }

                // Post-login sequence — sequential state machine matching Baileys
                // Baileys sends one IQ, waits for response, then sends next.
                // This sequential flow triggers offline_preview from the server.
                //
                // Step 1: Send count query → wait for count response
                // Step 2: (on count response) Send bare presence + passive IQ
                // Step 3: (on passive response) Send digest query
                // Step 4: (on digest response) Sequence complete, wait for offline_preview
                {
                    postLoginStep = PostLoginStep::WaitCount;
                    
                    WABinaryNode countNode;
                    countNode.tag = "iq";
                    countNode.setAttr("to", "s.whatsapp.net");
                    countNode.setAttr("type", "get");
                    countNode.setAttr("xmlns", "encrypt");
                    std::string countId = generateTag();
                    postLoginExpectedId = countId;
                    countNode.setAttr("id", countId);
                    WABinaryNode countChild;
                    countChild.tag = "count";
                    countNode.content = std::vector<WABinaryNode>{countChild};
                    sendNode(countNode);
                    std::cerr << "[whatsapp] [post-login step 1] Sent pre-key count query" << std::endl;
                }

                // App state sync — DEFERRED until after offline_preview
            } else if (tag == "Conn" || tag == "Conn_Push") {
                std::string connJid = getAttr(node, "jid");
                std::string connPhone = getAttr(node, "phone");
                std::string connName = getAttr(node, "name");
                std::string routingInfo = getAttr(node, "routing_info");

                if (!connJid.empty()) auth.jid = connJid;
                if (!connPhone.empty()) auth.phone = connPhone;
                if (!connName.empty()) auth.name = connName;
                if (!routingInfo.empty()) auth.routingInfo = routingInfo;
                auth.save(authFile);

                std::cerr << "[whatsapp] Connected as " << auth.jid
                          << " (" << auth.phone << ")" << std::endl;
                state->last_ws_event = std::chrono::steady_clock::now();
            } else {
                // Unknown tag — log it for debugging
                std::cerr << "[whatsapp] UNKNOWN NODE TAG: " << tag;
                for (auto& [k, v] : node.attrs)
                    std::cerr << " " << k << "=" << v;
                std::cerr << std::endl;
            }
            } // while(offset < frameData.size())
        } catch (const std::exception& e) {
            std::cerr << "[whatsapp] Decode error: " << e.what() << std::endl;
            // Hex dump first 128 bytes of decrypted data for debugging
            std::cerr << "[whatsapp] Decrypted hex (" << decryptedData.size() << " bytes): ";
            for (size_t i = 0; i < std::min(decryptedData.size(), (size_t)128); i++)
                fprintf(stderr, "%02x", decryptedData[i]);
            std::cerr << std::endl;
        }
    });

    // --- Close callback ---
    transport.onClose([state, &handshakeComplete](int code, const std::string& reason) {
        std::cerr << "[whatsapp] Connection closed: code=" << code
                  << " reason='" << reason << "'" << std::endl;
        state->ws_connected = false;
        handshakeComplete = false;
    });

    // --- Connect callback ---
    transport.onConnect([this, state, &noise, &clientHelloProto, &transport]
                        (bool success, const std::string& error) {
        if (!success) {
            state->consecutive_errors++;
            std::cerr << "[whatsapp] Connection failed: " << error << std::endl;
            return;
        }

        std::cerr << "[whatsapp] Connected! Sending client hello" << std::endl;
        state->ws_connected = true;
        state->consecutive_errors = 0;

        // Send client hello via noise encodeFrame (adds WA header + length prefix)
        auto helloFrame = noise.encodeFrame(clientHelloProto);
        transport.send(helloFrame);
        std::cerr << "[whatsapp] Sent client hello (" << helloFrame.size()
                  << " bytes: protobuf=" << clientHelloProto.size() << ")" << std::endl;
    });

    // --- Start connection ---
    transport.connect();

    // --- Main loop: keepalive, outbound flush, shutdown check ---
    state->last_ws_event = std::chrono::steady_clock::now();

    while (state->active) {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        if (!state->active) break;

        // Outbound flush
        if (transport.isConnected() && handshakeComplete) {
            std::vector<OutboundMessage> toSend;
            {
                std::lock_guard<std::mutex> lock(outboxMutex);
                toSend.swap(outbox);
            }
            for (auto& msg : toSend) {
                try { sendQueuedMessage(msg); }
                catch (const std::exception& e) {
                    std::cerr << "[whatsapp] Send error: " << e.what() << std::endl;
                }
            }
        }

        // Keepalive ping (every ~25s) — must send IQ ping, NOT empty frame
        if (transport.isConnected() && handshakeComplete) {
            static int keepaliveCounter = 0;
            if (++keepaliveCounter >= 12) {
                keepaliveCounter = 0;
                try {
                    // Send proper IQ ping like Baileys does
                    static int pingId = 0;
                    WABinaryNode ping;
                    ping.tag = "iq";
                    ping.setAttr("id", std::to_string(++pingId));
                    ping.setAttr("to", "s.whatsapp.net");
                    ping.setAttr("type", "get");
                    ping.setAttr("xmlns", "w:p");
                    ping.content = std::vector<WABinaryNode>{WABinaryNode{"ping", {}, std::monostate{}}};
                    sendNode(ping);
                } catch (const std::exception& e) {
                    std::cerr << "[whatsapp] Keepalive ping error: " << e.what() << std::endl;
                }
            }
        }

        // Stale check
        if (transport.isConnected()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - state->last_ws_event).count();
            if (elapsed > 300) {
                std::cerr << "[whatsapp] No activity for " << elapsed << "s — reconnecting" << std::endl;
                transport.close();
                std::cerr << "[whatsapp] Stale reconnect: transport closed, breaking" << std::endl;
                break;  // Exit loop; outer ChannelManager will restart
            }
        }

        // Reconnect if disconnected
        if (!transport.isConnected() && state->active) {
            std::cerr << "[whatsapp] Disconnected — exiting for reconnect" << std::endl;
            break;
        }
    }

    transport.close();
    std::cerr << "[whatsapp] Cleanup: transport closed" << std::endl;

    // Invalidate dangling pointers to stack-local objects so SendReply
    // doesn't dereference them between loop iterations.
    state->config.removeMember("_wa_outbox_mutex");
    state->config.removeMember("_wa_outbox");
    state->config.removeMember("_wa_signal_mgr");
    transportPtr = nullptr;
    std::cerr << "[whatsapp] Cleanup: pointers invalidated" << std::endl;

    std::cerr << "[whatsapp] Gateway loop stopped for " << state->channel_name << std::endl;
}

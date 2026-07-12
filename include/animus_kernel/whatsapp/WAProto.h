#pragma once

// Minimal protobuf encoding/decoding for WhatsApp message types.
// Instead of pulling in the full WAProto (175KB), we hand-roll the
// wire format for just the message types needed for text send/receive.
//
// Wire format reference: https://protobuf.dev/programming-guides/encoding/
// - varint (wire type 0): field tag + value
// - length-delimited (wire type 2): field tag + length + bytes
// - Each field = (field_number << 3 | wire_type)

#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <optional>

namespace animus::whatsapp::proto {

// ============================================================================
// Low-level wire format primitives
// ============================================================================

struct Writer {
    std::vector<uint8_t> buf;

    void writeVarint(uint64_t v) {
        while (v > 0x7F) {
            buf.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
            v >>= 7;
        }
        buf.push_back(static_cast<uint8_t>(v));
    }

    void writeTag(uint32_t field, uint32_t wire_type) {
        writeVarint((static_cast<uint64_t>(field) << 3) | wire_type);
    }

    // Wire type 0: varint
    void writeVarintField(uint32_t field, uint64_t value) {
        writeTag(field, 0);
        writeVarint(value);
    }

    // Wire type 2: length-delimited bytes
    void writeBytesField(uint32_t field, const std::vector<uint8_t>& data) {
        writeTag(field, 2);
        writeVarint(data.size());
        buf.insert(buf.end(), data.begin(), data.end());
    }

    // Wire type 2: length-delimited string
    void writeStringField(uint32_t field, const std::string& value) {
        writeTag(field, 2);
        writeVarint(value.size());
        buf.insert(buf.end(), value.begin(), value.end());
    }

    // Wire type 2: embedded message
    void writeMessageField(uint32_t field, const std::vector<uint8_t>& msg) {
        writeTag(field, 2);
        writeVarint(msg.size());
        buf.insert(buf.end(), msg.begin(), msg.end());
    }
};

struct Reader {
    const uint8_t* data;
    size_t len;
    size_t offset = 0;

    bool readVarint(uint64_t& out) {
        out = 0;
        int shift = 0;
        while (offset < len) {
            uint8_t b = data[offset++];
            out |= static_cast<uint64_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) return true;
            shift += 7;
            if (shift >= 64) return false;
        }
        return false;
    }

    bool readTag(uint32_t& field, uint32_t& wire_type) {
        uint64_t tag;
        if (!readVarint(tag)) return false;
        field = static_cast<uint32_t>(tag >> 3);
        wire_type = static_cast<uint32_t>(tag & 0x7);
        return true;
    }

    bool readBytes(std::vector<uint8_t>& out) {
        uint64_t length;
        if (!readVarint(length)) return false;
        if (offset + length > len) return false;
        out.assign(data + offset, data + offset + length);
        offset += static_cast<size_t>(length);
        return true;
    }

    bool readString(std::string& out) {
        std::vector<uint8_t> bytes;
        if (!readBytes(bytes)) return false;
        out = std::string(bytes.begin(), bytes.end());
        return true;
    }

    bool skipField(uint32_t wire_type) {
        switch (wire_type) {
            case 0: { uint64_t v; return readVarint(v); }
            case 1: { if (offset + 8 > len) return false; offset += 8; return true; }
            case 2: { uint64_t l; if (!readVarint(l)) return false; if (offset + l > len) return false; offset += static_cast<size_t>(l); return true; }
            case 5: { if (offset + 4 > len) return false; offset += 4; return true; }
            default: return false;
        }
    }
};

// ============================================================================
// Message types
// ============================================================================

// Conversation layer message (proto.Message)
struct Message {
    // field 1: string conversation (plain text)
    std::string conversation;

    // field 2: ExtendedTextMessage (for richer text)
    std::string ext_text;           // field 1
    std::string ext_matched_text;   // field 16 (optional matched pattern)
    std::string ext_canonical_url;  // field 17

    // field 3: ImageMessage
    // field 4: ContactMessage
    // ... etc — we only need text for Phase 2

    bool hasText() const { return !conversation.empty() || !ext_text.empty(); }
    std::string text() const {
        if (!conversation.empty()) return conversation;
        return ext_text;
    }
};

// Encode a text message
inline std::vector<uint8_t> encodeMessage(const Message& msg) {
    Writer w;
    if (!msg.conversation.empty()) {
        w.writeStringField(1, msg.conversation);
    }
    if (!msg.ext_text.empty() || !msg.ext_canonical_url.empty()) {
        Writer ext;
        if (!msg.ext_text.empty()) ext.writeStringField(1, msg.ext_text);
        if (!msg.ext_canonical_url.empty()) ext.writeStringField(17, msg.ext_canonical_url);
        w.writeMessageField(2, ext.buf);
    }
    return w.buf;
}

// Decode a message
inline Message decodeMessage(const uint8_t* data, size_t len) {
    Reader r{data, len};
    Message msg;
    while (r.offset < r.len) {
        uint32_t field, wt;
        if (!r.readTag(field, wt)) break;
        switch (field) {
            case 1: // conversation (string)
                if (wt == 2) r.readString(msg.conversation);
                else r.skipField(wt);
                break;
            case 2: { // ExtendedTextMessage (embedded)
                if (wt == 2) {
                    std::vector<uint8_t> extBytes;
                    if (r.readBytes(extBytes)) {
                        Reader er{extBytes.data(), extBytes.size()};
                        while (er.offset < er.len) {
                            uint32_t ef, ewt;
                            if (!er.readTag(ef, ewt)) break;
                            if (ef == 1 && ewt == 2) er.readString(msg.ext_text);
                            else er.skipField(ewt);
                        }
                    }
                } else r.skipField(wt);
                break;
            }
            default:
                r.skipField(wt);
                break;
        }
    }
    return msg;
}

// ============================================================================
// MessageKey (used in WebMessageInfo)
// ============================================================================

struct MessageKey {
    std::string remote_jid;   // field 1: string
    std::string from_me;      // field 2: bool (varint)
    std::string id;           // field 3: string
    std::string participant;  // field 4: string (for groups)
    // field 5: DataMessage (ignored for text-only)
};

// ============================================================================
// WebMessageInfo (wrapper around Message with metadata)
// ============================================================================

struct WebMessageInfo {
    MessageKey key;            // field 1
    Message message;           // field 2
    uint64_t message_timestamp = 0; // field 3
    // ... many more fields we don't need
};

// Decode a WebMessageInfo
inline WebMessageInfo decodeWebMessageInfo(const uint8_t* data, size_t len) {
    Reader r{data, len};
    WebMessageInfo info;
    while (r.offset < r.len) {
        uint32_t field, wt;
        if (!r.readTag(field, wt)) break;
        switch (field) {
            case 1: { // MessageKey (embedded)
                if (wt == 2) {
                    std::vector<uint8_t> keyBytes;
                    if (r.readBytes(keyBytes)) {
                        Reader kr{keyBytes.data(), keyBytes.size()};
                        while (kr.offset < kr.len) {
                            uint32_t kf, kwt;
                            if (!kr.readTag(kf, kwt)) break;
                            switch (kf) {
                                case 1: kr.readString(info.key.remote_jid); break;
                                case 3: kr.readString(info.key.id); break;
                                case 4: kr.readString(info.key.participant); break;
                                default: kr.skipField(kwt); break;
                            }
                        }
                    }
                } else r.skipField(wt);
                break;
            }
            case 2: { // Message (embedded)
                if (wt == 2) {
                    std::vector<uint8_t> msgBytes;
                    if (r.readBytes(msgBytes)) {
                        info.message = decodeMessage(msgBytes.data(), msgBytes.size());
                    }
                } else r.skipField(wt);
                break;
            }
            case 3: { // timestamp
                if (wt == 0) { uint64_t v; r.readVarint(v); info.message_timestamp = v; }
                else r.skipField(wt);
                break;
            }
            default:
                r.skipField(wt);
                break;
        }
    }
    return info;
}

// ============================================================================
// HandshakeMessage (Noise handshake envelope)
// Proto structure from WAProto.proto:
//   message HandshakeMessage {
//     optional ClientHello clientHello = 2;
//     optional ServerHello serverHello = 3;
//     optional ClientFinish clientFinish = 4;
//   }
//   message ClientHello { optional bytes ephemeral = 1; }
//   message ServerHello {
//     optional bytes ephemeral = 1;
//     optional bytes static = 2;
//     optional bytes payload = 3;
//   }
//   message ClientFinish {
//     optional bytes static = 1;
//     optional bytes payload = 2;
//   }
// ============================================================================

struct ClientHello {
    std::vector<uint8_t> ephemeral;  // field 1
};

struct ServerHello {
    std::vector<uint8_t> ephemeral;  // field 1
    std::vector<uint8_t> static_key; // field 2 ("static" in proto)
    std::vector<uint8_t> payload;    // field 3
};

struct ClientFinish {
    std::vector<uint8_t> static_key; // field 1 ("static" in proto)
    std::vector<uint8_t> payload;    // field 2
};

inline std::vector<uint8_t> encodeClientHello(const ClientHello& hello) {
    Writer inner;
    if (!hello.ephemeral.empty())
        inner.writeBytesField(1, hello.ephemeral);
    Writer outer;
    outer.writeMessageField(2, inner.buf);  // HandshakeMessage.clientHello = field 2
    return outer.buf;
}

inline std::vector<uint8_t> encodeClientFinish(const ClientFinish& finish) {
    Writer inner;
    if (!finish.static_key.empty())
        inner.writeBytesField(1, finish.static_key);
    if (!finish.payload.empty())
        inner.writeBytesField(2, finish.payload);
    Writer outer;
    outer.writeMessageField(4, inner.buf);  // HandshakeMessage.clientFinish = field 4
    return outer.buf;
}

// Decode server hello from raw protobuf (HandshakeMessage.serverHello = field 3)
inline ServerHello decodeServerHello(const uint8_t* data, size_t len) {
    Reader r{data, len};
    ServerHello hello;
    while (r.offset < r.len) {
        uint32_t field, wt;
        if (!r.readTag(field, wt)) break;
        if (field == 3 && wt == 2) {
            // ServerHello embedded message
            std::vector<uint8_t> shBytes;
            if (!r.readBytes(shBytes)) break;
            Reader sr{shBytes.data(), shBytes.size()};
            while (sr.offset < sr.len) {
                uint32_t sf, swt;
                if (!sr.readTag(sf, swt)) break;
                switch (sf) {
                    case 1: if (swt == 2) sr.readBytes(hello.ephemeral); else sr.skipField(swt); break;
                    case 2: if (swt == 2) sr.readBytes(hello.static_key); else sr.skipField(swt); break;
                    case 3: if (swt == 2) sr.readBytes(hello.payload); else sr.skipField(swt); break;
                    default: sr.skipField(swt); break;
                }
            }
        } else {
            r.skipField(wt);
        }
    }
    return hello;
}

// Encode ClientPayload for handshake — registration (new device) or login (existing)
// Baileys: generateRegistrationNode / generateLoginNode
inline std::vector<uint8_t> encodeClientPayload(
    bool isNewLogin,
    const std::string& jid = "",
    uint32_t registrationId = 0,
    const std::vector<uint8_t>& identityPubKey = {},
    const std::vector<uint8_t>& signedPreKeyPub = {},
    const std::vector<uint8_t>& signedPreKeySig = {},
    uint32_t signedPreKeyId = 0
) {
    Writer w;
    // === Field ordering must match Baileys exactly ===
    // Baileys: passive, userAgent, webInfo, pushName, connectType, connectReason, devicePairingData, pull

    // passive (field 3) = false for new login, true for returning
    w.writeVarintField(3, isNewLogin ? 0 : 1);

    // UserAgent (field 5)
    {
        Writer ua;
        // Fields in ascending proto field number order
        // platform (field 1) = WEB = 14
        ua.writeVarintField(1, 14);
        // appVersion (field 2) = {primary=2, secondary=3000, tertiary=1035194821}
        {
            Writer av;
            av.writeVarintField(1, 2);
            av.writeVarintField(2, 3000);
            av.writeVarintField(3, 1035194821);
            ua.writeMessageField(2, av.buf);
        }
        // mcc (field 3)
        ua.writeStringField(3, "000");
        // mnc (field 4)
        ua.writeStringField(4, "000");
        // osVersion (field 5)
        ua.writeStringField(5, "0.1");
        // manufacturer (field 6) = "" (empty)
        ua.writeStringField(6, "");
        // device (field 7)
        ua.writeStringField(7, "Desktop");
        // osBuildNumber (field 8)
        ua.writeStringField(8, "0.1");
        // phoneId (field 9)
        ua.writeStringField(9, "000000000000000");
        // releaseChannel (field 10) = RELEASE = 0 — OMITTED (proto3 default)
        // ua.writeVarintField(10, 0);
        // localeLanguageIso6391 (field 11)
        ua.writeStringField(11, "en");
        // localeCountryIso31661Alpha2 (field 12)
        ua.writeStringField(12, "US");
        w.writeMessageField(5, ua.buf);
    }
    // WebInfo (field 6)
    {
        Writer wi;
        // webSubPlatform (field 4) = DARWIN = 3 (matches Baileys with syncFullHistory=true)
        wi.writeVarintField(4, 3);
        w.writeMessageField(6, wi.buf);
    }
    // pushName (field 7)
    // Baileys omits pushName for registration, includes it for login
    if (!isNewLogin) {
        w.writeStringField(7, "Kestrel");
    }

    // connectType (field 12) = WIFI_UNKNOWN = 1
    w.writeVarintField(12, 1);
    // connectReason (field 13) = USER_ACTIVATED = 1
    w.writeVarintField(13, 1);

    if (isNewLogin && registrationId != 0) {
        // devicePairingData (field 19)
        {
            Writer dpd;
            // eRegid (field 1) = registrationId as 4-byte big-endian (0 if not set)
            {
                std::vector<uint8_t> regBytes(4);
                regBytes[0] = (registrationId >> 24) & 0xff;
                regBytes[1] = (registrationId >> 16) & 0xff;
                regBytes[2] = (registrationId >> 8) & 0xff;
                regBytes[3] = registrationId & 0xff;
                dpd.writeBytesField(1, regBytes);
            }
            // eKeytype (field 2) = [0x05] (KEY_BUNDLE_TYPE)
            dpd.writeBytesField(2, {0x05});
            // eIdent (field 3) = identity public key (32 bytes, NO 0x05 prefix)
            // Baileys strips the 0x05 prefix from libsignal keys
            if (!identityPubKey.empty()) {
                dpd.writeBytesField(3, identityPubKey);
            }
            // eSkeyId (field 4) = signed prekey id as 3-byte big-endian
            {
                std::vector<uint8_t> spkIdBytes(3);
                spkIdBytes[0] = (signedPreKeyId >> 16) & 0xff;
                spkIdBytes[1] = (signedPreKeyId >> 8) & 0xff;
                spkIdBytes[2] = signedPreKeyId & 0xff;
                dpd.writeBytesField(4, spkIdBytes);
            }
            // eSkeyVal (field 5) = signed prekey public key (32 bytes, NO 0x05 prefix)
            if (!signedPreKeyPub.empty()) {
                dpd.writeBytesField(5, signedPreKeyPub);
            }
            // eSkeySig (field 6) = signed prekey signature
            if (!signedPreKeySig.empty()) {
                dpd.writeBytesField(6, signedPreKeySig);
            }
            // buildHash (field 7) = MD5 of "2.3000.1035194821"
            // Computed: a2c9471eff910a9e1ae659890a28872d
            dpd.writeBytesField(7, {0xa2, 0xc9, 0x47, 0x1e, 0xff, 0x91, 0x0a, 0x9e,
                                   0x1a, 0xe6, 0x59, 0x89, 0x0a, 0x28, 0x87, 0x2d});
            // deviceProps (field 8) = DeviceProps protobuf
            {
                Writer dp;
                // os (field 1) = "Mac OS"
                dp.writeStringField(1, "Mac OS");
                // version (field 2) = AppVersion {primary=10, secondary=15, tertiary=7}
                {
                    Writer av;
                    av.writeVarintField(1, 10);
                    av.writeVarintField(2, 15);
                    av.writeVarintField(3, 7);
                    dp.writeMessageField(2, av.buf);
                }
                // platformType (field 3) = DESKTOP = 7
                // getPlatformType('Desktop') → DESKTOP
                dp.writeVarintField(3, 7);
                // requireFullSync (field 4) = true (Baileys sends true by default)
                dp.writeVarintField(4, 1);
                // historySyncConfig (field 5) — proto field numbers from WAProto.proto
                {
                    Writer hsc;
                    // storageQuotaMb (field 3) = 10240
                    hsc.writeVarintField(3, 10240);
                    // inlineInitialPayloadInE2EeMsg (field 4) = true
                    hsc.writeVarintField(4, 1);
                    // recentSyncDaysLimit (field 5) — omitted (Baileys sets undefined)
                    // supportCallLogHistory (field 6) = false (explicitly included by Baileys)
                    hsc.writeVarintField(6, 0);
                    // supportBotUserAgentChatHistory (field 7) = true
                    hsc.writeVarintField(7, 1);
                    // supportCagReactionsAndPolls (field 8) = true
                    hsc.writeVarintField(8, 1);
                    // supportBizHostedMsg (field 9) = true
                    hsc.writeVarintField(9, 1);
                    // supportRecentSyncChunkMessageCountTuning (field 10) = true
                    hsc.writeVarintField(10, 1);
                    // supportHostedGroupMsg (field 11) = true
                    hsc.writeVarintField(11, 1);
                    // supportFbidBotChatHistory (field 12) = true
                    hsc.writeVarintField(12, 1);
                    // supportMessageAssociation (field 14) = true (field 13 omitted)
                    hsc.writeVarintField(14, 1);
                    // supportGroupHistory (field 15) = false
                    hsc.writeVarintField(15, 0);
                    dp.writeMessageField(5, hsc.buf);
                }
                dpd.writeBytesField(8, dp.buf);
            }
            w.writeMessageField(19, dpd.buf);
        }
        // pull (field 33) = false for new login
        w.writeVarintField(33, 0);
    } else {
        // Login: pull=true
        // pull (field 33) = true
        w.writeVarintField(33, 1);
        // username (field 1)
        if (!jid.empty()) {
            // Extract user and device from JID ("12345:18@s.whatsapp.net" → user=12345, device=18)
            auto atPos = jid.find('@');
            auto colonPos = jid.find(':');
            if (atPos != std::string::npos && colonPos != std::string::npos) {
                try {
                    uint64_t user = std::stoull(jid.substr(0, colonPos));
                    w.writeVarintField(1, user);
                    // device (field 18) — extracted from JID after colon
                    uint64_t deviceId = std::stoull(jid.substr(colonPos + 1, atPos - colonPos - 1));
                    w.writeVarintField(18, deviceId);
                } catch (...) {}
            }
        }
        // lidDbMigrated (field 41) = false
        w.writeVarintField(41, 0);
    }
    return w.buf;
}

// ============================================================================
// Convenience: extract text content from a <message> binary node's
// encrypted payload. After decryption, the plaintext is a proto.Message.
// ============================================================================

inline std::string extractTextFromDecryptedPayload(const std::vector<uint8_t>& plaintext) {
    auto msg = decodeMessage(plaintext.data(), plaintext.size());
    return msg.text();
}

// Remove PKCS#7-style random padding that WhatsApp adds before the plaintext protobuf.
// The last byte indicates how many padding bytes to strip (value 1-16).
inline std::vector<uint8_t> unpadRandomMax16(const std::vector<uint8_t>& data) {
    if (data.empty()) return data;
    uint8_t padLen = data.back();
    if (padLen < 1 || padLen > 16 || padLen > data.size()) return data;
    // Verify all padding bytes match
    for (size_t i = data.size() - padLen; i < data.size(); i++) {
        if (data[i] != padLen) return data; // not valid padding, return as-is
    }
    return std::vector<uint8_t>(data.begin(), data.end() - padLen);
}

// Add PKCS#7-style random padding that WhatsApp expects on outgoing messages.
// Same algorithm as Baileys' writeRandomPadMax16:
// Generate 1 random byte, mask to 0-15, if 0 then use 15.
// Append that many bytes all set to the pad length value.
inline std::vector<uint8_t> padRandomMax16(const std::vector<uint8_t>& data) {
    if (data.empty()) return data;
    // Simple deterministic padding using data length for variety
    uint8_t padByte = static_cast<uint8_t>((data.size() + 1) & 0x0f);
    if (padByte == 0) padByte = 0x0f;
    std::vector<uint8_t> padded(data.size() + padByte);
    std::copy(data.begin(), data.end(), padded.begin());
    for (size_t i = data.size(); i < padded.size(); i++) {
        padded[i] = padByte;
    }
    return padded;
}

// Encode a plain text message for sending
inline std::vector<uint8_t> encodeTextMessage(const std::string& text) {
    Message msg;
    msg.conversation = text;
    return padRandomMax16(encodeMessage(msg));
}

// ============================================================================
// ADV (Advanced Device Identity) protobuf types for pair-success
// ============================================================================

struct ADVDeviceIdentity {
    uint32_t rawId = 0;       // field 1
    uint64_t timestamp = 0;   // field 2
    uint32_t keyIndex = 0;    // field 3
    uint32_t accountType = 0; // field 4 (ADVEncryptionType: 0=E2EE, 1=HOSTED)
    uint32_t deviceType = 0;  // field 5
};

struct ADVSignedDeviceIdentity {
    std::vector<uint8_t> details;             // field 1 (ADVDeviceIdentity encoded)
    std::vector<uint8_t> accountSignatureKey;  // field 2
    std::vector<uint8_t> accountSignature;     // field 3
    std::vector<uint8_t> deviceSignature;     // field 4
};

struct ADVSignedDeviceIdentityHMAC {
    std::vector<uint8_t> details;  // field 1 (ADVSignedDeviceIdentity encoded)
    std::vector<uint8_t> hmac;      // field 2
    uint32_t accountType = 0;        // field 3 (ADVEncryptionType)
};

// Decode ADVSignedDeviceIdentityHMAC from binary node content
inline ADVSignedDeviceIdentityHMAC decodeADVSignedDeviceIdentityHMAC(const std::vector<uint8_t>& data) {
    ADVSignedDeviceIdentityHMAC result;
    Reader r(data.data(), data.size());
    uint32_t field;
    uint32_t wt;
    while (r.readTag(field, wt)) {
        if (field == 1 && wt == 2) {
            r.readBytes(result.details);
        } else if (field == 2 && wt == 2) {
            r.readBytes(result.hmac);
        } else if (field == 3 && wt == 0) {
            uint64_t v; r.readVarint(v); result.accountType = static_cast<uint32_t>(v);
        } else {
            r.skipField(wt);
        }
    }
    return result;
}

// Decode ADVSignedDeviceIdentity
inline ADVSignedDeviceIdentity decodeADVSignedDeviceIdentity(const std::vector<uint8_t>& data) {
    ADVSignedDeviceIdentity result;
    Reader r(data.data(), data.size());
    uint32_t field;
    uint32_t wt;
    while (r.readTag(field, wt)) {
        if (field == 1 && wt == 2) {
            r.readBytes(result.details);
        } else if (field == 2 && wt == 2) {
            r.readBytes(result.accountSignatureKey);
        } else if (field == 3 && wt == 2) {
            r.readBytes(result.accountSignature);
        } else if (field == 4 && wt == 2) {
            r.readBytes(result.deviceSignature);
        } else {
            r.skipField(wt);
        }
    }
    return result;
}

// Decode ADVDeviceIdentity
inline ADVDeviceIdentity decodeADVDeviceIdentity(const std::vector<uint8_t>& data) {
    ADVDeviceIdentity result;
    Reader r(data.data(), data.size());
    uint32_t field;
    uint32_t wt;
    while (r.readTag(field, wt)) {
        if (field == 1 && wt == 0) {
            uint64_t v; r.readVarint(v); result.rawId = static_cast<uint32_t>(v);
        } else if (field == 2 && wt == 0) {
            uint64_t v; r.readVarint(v); result.timestamp = v;
        } else if (field == 3 && wt == 0) {
            uint64_t v; r.readVarint(v); result.keyIndex = static_cast<uint32_t>(v);
        } else if (field == 4 && wt == 0) {
            uint64_t v; r.readVarint(v); result.accountType = static_cast<uint32_t>(v);
        } else if (field == 5 && wt == 0) {
            uint64_t v; r.readVarint(v); result.deviceType = static_cast<uint32_t>(v);
        } else {
            r.skipField(wt);
        }
    }
    return result;
}

// Encode ADVSignedDeviceIdentity (for reply)
inline std::vector<uint8_t> encodeADVSignedDeviceIdentity(const ADVSignedDeviceIdentity& id) {
    Writer w;
    if (!id.details.empty()) w.writeBytesField(1, id.details);
    // accountSignatureKey is null (not included) per encodeSignedDeviceIdentity(account, false)
    if (!id.accountSignature.empty()) w.writeBytesField(3, id.accountSignature);
    if (!id.deviceSignature.empty()) w.writeBytesField(4, id.deviceSignature);
    return w.buf;
}

// ─── Signal Protocol message types (for pkmsg/msg decryption) ───
//
// PreKeySignalMessage wire format:
//   byte 0: version (e.g. 0x33 = v3.3)
//   bytes 1+: protobuf PreKeySignalMessage
//
// Protobuf fields (from WAProto.proto):
//   1: uint32 preKeyId
//   2: bytes  baseKey (sender ephemeral, 33 bytes with 0x05 prefix)
//   3: bytes  identityKey (sender identity, 33 bytes with 0x05 prefix)
//   4: bytes  message (inner SignalMessage protobuf)
//   5: uint32 registrationId
//   6: uint32 signedPreKeyId
//
// SignalMessage (inner) protobuf fields:
//   1: bytes  ratchetKey (33 bytes with 0x05 prefix)
//   2: uint32 counter
//   3: uint32 previousCounter
//   4: bytes  ciphertext

struct PreKeySignalMessage {
    uint32_t pre_key_id = 0;         // field 1
    std::vector<uint8_t> base_key;   // field 2: sender ephemeral key (33 bytes)
    std::vector<uint8_t> identity_key; // field 3: sender identity key (33 bytes)
    std::vector<uint8_t> message;     // field 4: inner SignalMessage protobuf
    uint32_t registration_id = 0;    // field 5
    uint32_t signed_pre_key_id = 0;  // field 6
};

struct SignalMessageInner {
    std::vector<uint8_t> ratchet_key;  // field 1 (33 bytes)
    uint32_t counter = 0;              // field 2
    uint32_t previous_counter = 0;     // field 3
    std::vector<uint8_t> ciphertext;   // field 4
};

inline PreKeySignalMessage decodePreKeySignalMessage(const uint8_t* data, size_t len) {
    PreKeySignalMessage msg;
    Reader r{data, len};
    while (r.offset < r.len) {
        uint32_t field, wt;
        if (!r.readTag(field, wt)) break;
        switch (field) {
            case 1: if (wt == 0) { uint64_t v; r.readVarint(v); msg.pre_key_id = static_cast<uint32_t>(v); } else r.skipField(wt); break;
            case 2: if (wt == 2) r.readBytes(msg.base_key); else r.skipField(wt); break;
            case 3: if (wt == 2) r.readBytes(msg.identity_key); else r.skipField(wt); break;
            case 4: if (wt == 2) r.readBytes(msg.message); else r.skipField(wt); break;
            case 5: if (wt == 0) { uint64_t v; r.readVarint(v); msg.registration_id = static_cast<uint32_t>(v); } else r.skipField(wt); break;
            case 6: if (wt == 0) { uint64_t v; r.readVarint(v); msg.signed_pre_key_id = static_cast<uint32_t>(v); } else r.skipField(wt); break;
            default: r.skipField(wt); break;
        }
    }
    return msg;
}

inline SignalMessageInner decodeSignalMessageInner(const uint8_t* data, size_t len) {
    SignalMessageInner msg;
    Reader r{data, len};
    while (r.offset < r.len) {
        uint32_t field, wt;
        if (!r.readTag(field, wt)) break;
        switch (field) {
            case 1: if (wt == 2) r.readBytes(msg.ratchet_key); else r.skipField(wt); break;
            case 2: if (wt == 0) { uint64_t v; r.readVarint(v); msg.counter = static_cast<uint32_t>(v); } else r.skipField(wt); break;
            case 3: if (wt == 0) { uint64_t v; r.readVarint(v); msg.previous_counter = static_cast<uint32_t>(v); } else r.skipField(wt); break;
            case 4: if (wt == 2) r.readBytes(msg.ciphertext); else r.skipField(wt); break;
            default: r.skipField(wt); break;
        }
    }
    return msg;
}



// ─── Signal Protocol message serialization (outgoing) ───

// Encode a SignalMessage protobuf (inner message, without version byte or MAC)
// Fields: ratchetKey (1, bytes), counter (2, varint), previousCounter (3, varint), ciphertext (4, bytes)
inline std::vector<uint8_t> encodeSignalMessageProto(
    const std::vector<uint8_t>& ratchetKey,  // 33 bytes (0x05 + X25519)
    uint32_t counter,
    uint32_t previousCounter,
    const std::vector<uint8_t>& ciphertext)
{
    std::vector<uint8_t> buf;
    Writer w;
    w.writeBytesField(1, ratchetKey);
    w.writeVarintField(2, counter);
    w.writeVarintField(3, previousCounter);
    w.writeBytesField(4, ciphertext);
    return std::move(w).buf;
}

// Encode a PreKeySignalMessage protobuf
// Fields: preKeyId (1, varint), baseKey (2, bytes), identityKey (3, bytes),
//         message (4, bytes), registrationId (5, varint), signedPreKeyId (6, varint)
inline std::vector<uint8_t> encodePreKeySignalMessageProto(
    uint32_t preKeyId,
    const std::vector<uint8_t>& baseKey,       // 33 bytes (0x05 + X25519)
    const std::vector<uint8_t>& identityKey,    // 33 bytes (0x05 + X25519)
    const std::vector<uint8_t>& innerMessage,   // serialized SignalMessage (version + proto + MAC)
    uint32_t registrationId,
    uint32_t signedPreKeyId)
{
    Writer w;
    if (preKeyId > 0) {
        w.writeVarintField(1, preKeyId);
    }
    w.writeBytesField(2, baseKey);
    w.writeBytesField(3, identityKey);
    w.writeBytesField(4, innerMessage);
    w.writeVarintField(5, registrationId);
    w.writeVarintField(6, signedPreKeyId);
    return std::move(w).buf;
}

} // namespace animus::whatsapp::proto

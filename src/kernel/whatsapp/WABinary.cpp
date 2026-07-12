// WABinary.cpp - Binary node encoder/decoder for WhatsApp protocol
// Ported from Baileys v7.0.0-rc13
// ============================================================================

#include "animus_kernel/whatsapp/WABinary.h"

#include <cstring>
#include <stdexcept>
#include <sstream>
#include <cmath>
#include <zlib.h>

namespace animus::whatsapp {

// Single-byte tokens
static const char* SINGLE_BYTE_TOKENS[] = {
    "", "xmlstreamstart", "xmlstreamend", "s.whatsapp.net",
    "type", "participant", "from", "receipt",
    "id", "notification", "disappearing_mode", "status",
    "jid", "broadcast", "user", "devices",
    "device_hash", "to", "offline", "message",
    "result", "class", "xmlns", "duration",
    "notify", "iq", "t", "ack",
    "g.us", "enc", "urn:xmpp:whatsapp:push", "presence",
    "config_value", "picture", "verified_name", "config_code",
    "key-index-list", "contact", "mediatype", "routing_info",
    "edge_routing", "get", "read", "urn:xmpp:ping",
    "fallback_hostname", "0", "chatstate", "business_hours_config",
    "unavailable", "download_buckets", "skmsg", "verified_level",
    "composing", "handshake", "device-list", "media",
    "text", "fallback_ip4", "media_conn", "device",
    "creation", "location", "config", "item",
    "fallback_ip6", "count", "w:profile:picture", "image",
    "business", "2", "hostname", "call-creator",
    "display_name", "relaylatency", "platform", "abprops",
    "success", "msg", "offline_preview", "prop",
    "key-index", "v", "day_of_week", "pkmsg",
    "version", "1", "ping", "w:p",
    "download", "video", "set", "specific_hours",
    "props", "primary", "unknown", "hash",
    "commerce_experience", "last", "subscribe", "max_buckets",
    "call", "profile", "member_since_text", "close_time",
    "call-id", "sticker", "mode", "participants",
    "value", "query", "profile_options", "open_time",
    "code", "list", "host", "ts",
    "contacts", "upload", "lid", "preview",
    "update", "usync", "w:stats", "delivery",
    "auth_ttl", "context", "fail", "cart_enabled",
    "appdata", "category", "atn", "direct_connection",
    "decrypt-fail", "relay_id", "mmg-fallback.whatsapp.net", "target",
    "available", "name", "last_id", "mmg.whatsapp.net",
    "categories", "401", "is_new", "index",
    "tctoken", "ip4", "token_id", "latency",
    "recipient", "edit", "ip6", "add",
    "thumbnail-document", "26", "paused", "true",
    "identity", "stream:error", "key", "sidelist",
    "background", "audio", "3", "thumbnail-image",
    "biz-cover-photo", "cat", "gcm", "thumbnail-video",
    "error", "auth", "deny", "serial",
    "in", "registration", "thumbnail-link", "remove",
    "00", "gif", "thumbnail-gif", "tag",
    "capability", "multicast", "item-not-found", "description",
    "business_hours", "config_expo_key", "md-app-state", "expiration",
    "fallback", "ttl", "300", "md-msg-hist",
    "device_orientation", "out", "w:m", "open_24h",
    "side_list", "token", "inactive", "01",
    "document", "te2", "played", "encrypt",
    "msgr", "hide", "direct_path", "12",
    "state", "not-authorized", "url", "terminate",
    "signature", "status-revoke-delay", "02", "te",
    "linked_accounts", "trusted_contact", "timezone", "ptt",
    "kyc-id", "privacy_token", "readreceipts", "appointment_only",
    "address", "expected_ts", "privacy", "7",
    "android", "interactive", "device-identity", "enabled",
    "attribute_padding", "1080", "03", "screen_height"
};
static constexpr size_t SINGLE_BYTE_TOKEN_COUNT = sizeof(SINGLE_BYTE_TOKENS) / sizeof(SINGLE_BYTE_TOKENS[0]);

#include "animus_kernel/whatsapp/WATokenMap.h"

// ---------------------------------------------------------------------------
// JID utilities
// ---------------------------------------------------------------------------

std::string jidEncode(const std::string& user, const std::string& server, int device) {
    std::string result = user;
    if (device > 0) result += ":" + std::to_string(device);
    result += "@" + server;
    return result;
}

std::optional<FullJid> jidDecode(const std::string& jid) {
    auto sepIdx = jid.find('@');
    if (sepIdx == std::string::npos) return std::nullopt;
    FullJid result;
    result.server = jid.substr(sepIdx + 1);
    std::string userCombined = jid.substr(0, sepIdx);
    auto colonIdx = userCombined.find(':');
    std::string userPart;
    if (colonIdx != std::string::npos) {
        userPart = userCombined.substr(0, colonIdx);
        result.device = std::stoi(userCombined.substr(colonIdx + 1));
    } else {
        userPart = userCombined;
    }
    result.user = userPart;
    if (result.server == "lid") result.domainType = 1;
    else if (result.server == "hosted") result.domainType = 128;
    else if (result.server == "hosted.lid") result.domainType = 129;
    return result;
}

bool isJidGroup(const std::string& jid) {
    return jid.size() > 5 && jid.substr(jid.size() - 5) == "@g.us";
}

bool isLidUser(const std::string& jid) {
    return jid.size() > 4 && jid.substr(jid.size() - 4) == "@lid";
}

// ---------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------

class BinaryEncoder {
public:
    std::vector<uint8_t> buffer;

    void pushByte(uint8_t v) { buffer.push_back(v); }
    void pushBytes(const uint8_t* d, size_t n) { buffer.insert(buffer.end(), d, d + n); }
    void pushBytes(const std::vector<uint8_t>& d) { buffer.insert(buffer.end(), d.begin(), d.end()); }

    void pushInt16(uint16_t v) { pushByte(v >> 8); pushByte(v & 0xff); }
    void pushInt20(uint32_t v) { pushByte((v >> 16) & 0x0f); pushByte((v >> 8) & 0xff); pushByte(v & 0xff); }
    void pushInt32(uint32_t v) { pushByte(v >> 24); pushByte((v >> 16) & 0xff); pushByte((v >> 8) & 0xff); pushByte(v & 0xff); }

    void writeByteLength(size_t len) {
        if (len >= (1u << 20)) { pushByte(Tags::BINARY_32); pushInt32(len); }
        else if (len >= 256) { pushByte(Tags::BINARY_20); pushInt20(len); }
        else { pushByte(Tags::BINARY_8); pushByte(len); }
    }

    void writeStringRaw(const std::string& s) {
        writeByteLength(s.size());
        pushBytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    static uint8_t packNibble(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c == '-') return 10;
        if (c == '.') return 11;
        throw std::runtime_error("invalid nibble");
    }
    static uint8_t packHex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        throw std::runtime_error("invalid hex");
    }
    static bool isNibble(const std::string& s) {
        if (s.empty() || s.size() > Tags::PACKED_MAX) return false;
        for (char c : s) if (!((c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
        return true;
    }
    static bool isHex(const std::string& s) {
        if (s.empty() || s.size() > Tags::PACKED_MAX) return false;
        for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
        return true;
    }

    void writePackedBytes(const std::string& s, bool nibble) {
        pushByte(nibble ? Tags::NIBBLE_8 : Tags::HEX_8);
        auto fn = nibble ? packNibble : packHex;
        uint8_t rounded = static_cast<uint8_t>(std::ceil(s.size() / 2.0));
        if (s.size() % 2 != 0) rounded |= 128;
        pushByte(rounded);
        size_t half = s.size() / 2;
        for (size_t i = 0; i < half; i++) pushByte((fn(s[2*i]) << 4) | fn(s[2*i+1]));
        if (s.size() % 2 != 0) pushByte((fn(s.back()) << 4) | 0x0f);
    }

    void writeJid(const FullJid& j) {
        if (j.device > 0) {
            pushByte(Tags::AD_JID);
            pushByte(j.domainType);
            pushByte(j.device);
            writeString(j.user);
        } else {
            pushByte(Tags::JID_PAIR);
            j.user.empty() ? pushByte(Tags::LIST_EMPTY) : writeString(j.user);
            writeString(j.server);
        }
    }

    void writeString(const std::string& s) {
        if (s.empty()) { writeStringRaw(s); return; }
        auto it = TOKEN_MAP.find(s);
        if (it != TOKEN_MAP.end()) {
            if (it->second.dict >= 0) pushByte(Tags::DICTIONARY_0 + it->second.dict);
            pushByte(it->second.index);
            return;
        }
        if (isNibble(s)) writePackedBytes(s, true);
        else if (isHex(s)) writePackedBytes(s, false);
        else {
            auto jid = jidDecode(s);
            if (jid) writeJid(*jid); else writeStringRaw(s);
        }
    }

    void writeListStart(size_t n) {
        if (n == 0) pushByte(Tags::LIST_EMPTY);
        else if (n < 256) { pushByte(Tags::LIST_8); pushByte(n); }
        else { pushByte(Tags::LIST_16); pushInt16(n); }
    }

    void encodeNode(const BinaryNode& node) {
        size_t validAttrs = 0;
        for (auto& [k, v] : node.attrs) if (!v.empty()) validAttrs++;
        bool hasContent = !std::holds_alternative<std::monostate>(node.content);
        writeListStart(2 * validAttrs + 1 + (hasContent ? 1 : 0));
        writeString(node.tag);
        for (auto& [k, v] : node.attrs) if (!v.empty()) { writeString(k); writeString(v); }

        if (std::holds_alternative<std::string>(node.content))
            writeString(std::get<std::string>(node.content));
        else if (std::holds_alternative<std::vector<uint8_t>>(node.content)) {
            auto& bytes = std::get<std::vector<uint8_t>>(node.content);
            writeByteLength(bytes.size()); pushBytes(bytes);
        } else if (std::holds_alternative<std::vector<BinaryNode>>(node.content)) {
            auto& children = std::get<std::vector<BinaryNode>>(node.content);
            writeListStart(children.size());
            for (auto& child : children) encodeNode(child);
        }
    }
};

std::vector<uint8_t> encodeBinaryNode(const BinaryNode& node) {
    BinaryEncoder enc;
    enc.buffer.push_back(0);
    enc.encodeNode(node);
    return enc.buffer;
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

class BinaryDecoder {
    const uint8_t* data;
    size_t len;
    size_t pos = 0;
public:
    BinaryDecoder(const uint8_t* d, size_t l) : data(d), len(l) {}
private:
    bool checkEOS(size_t n) { return pos + n <= len; }
    uint8_t readByte() { if (!checkEOS(1)) throw std::runtime_error("EOS"); return data[pos++]; }
    std::vector<uint8_t> readBytes(size_t n) {
        if (!checkEOS(n)) throw std::runtime_error("EOS");
        std::vector<uint8_t> r(data + pos, data + pos + n); pos += n; return r;
    }
    uint16_t readInt16() { auto b = readBytes(2); return (b[0] << 8) | b[1]; }
    uint32_t readInt20() { auto b = readBytes(3); return ((b[0] & 0x0f) << 16) | (b[1] << 8) | b[2]; }
    uint32_t readInt32() { auto b = readBytes(4); return (b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3]; }
    std::string readStr(size_t n) { auto b = readBytes(n); return std::string(b.begin(), b.end()); }

    static char unpackNibble(uint8_t v) {
        if (v <= 9) return '0' + v;
        if (v == 10) return '-'; if (v == 11) return '.'; if (v == 15) return '\0';
        throw std::runtime_error("bad nibble: " + std::to_string(v));
    }
    static char unpackHex(uint8_t v) {
        if (v < 10) return '0' + v; if (v < 16) return 'A' + v - 10; throw std::runtime_error("bad hex");
    }

    std::string readPacked8(uint8_t tag) {
        uint8_t start = readByte(); std::string val;
        for (uint8_t i = 0; i < (start & 0x7f); i++) {
            uint8_t b = readByte();
            val += (tag == Tags::NIBBLE_8) ? unpackNibble((b >> 4)) : unpackHex((b >> 4));
            val += (tag == Tags::NIBBLE_8) ? unpackNibble(b & 0x0f) : unpackHex(b & 0x0f);
        }
        if (start >> 7) val.pop_back();
        return val;
    }

    size_t readListSize(uint8_t tag) {
        if (tag == Tags::LIST_EMPTY) return 0;
        if (tag == Tags::LIST_8) return readByte();
        if (tag == Tags::LIST_16) return readInt16();
        throw std::runtime_error("bad list tag");
    }
    static bool isListTag(uint8_t t) { return t == Tags::LIST_EMPTY || t == Tags::LIST_8 || t == Tags::LIST_16; }

    std::string readJidPair() {
        std::string i = readString(readByte()), j = readString(readByte());
        return j.empty() ? throw std::runtime_error("bad jid") : i + "@" + j;
    }
    std::string readAdJid() {
        uint8_t dt = readByte(), dev = readByte();
        std::string user = readString(readByte());
        std::string srv = "s.whatsapp.net";
        if (dt == 1) srv = "lid"; else if (dt == 128) srv = "hosted"; else if (dt == 129) srv = "hosted.lid";
        return jidEncode(user, srv, dev);
    }

    std::string getTokenDouble(uint8_t dict, uint8_t idx) {
        auto d = DOUBLE_BYTE_TOKENS.find(dict);
        if (d == DOUBLE_BYTE_TOKENS.end()) throw std::runtime_error("bad dict");
        auto t = d->second.find(idx);
        if (t == d->second.end()) throw std::runtime_error("bad token");
        return t->second;
    }

    std::string readString(uint8_t tag) {
        if (tag >= 1 && tag < SINGLE_BYTE_TOKEN_COUNT) return SINGLE_BYTE_TOKENS[tag] ? SINGLE_BYTE_TOKENS[tag] : "";
        switch (tag) {
            case Tags::DICTIONARY_0: case Tags::DICTIONARY_1: case Tags::DICTIONARY_2: case Tags::DICTIONARY_3:
                return getTokenDouble(tag - Tags::DICTIONARY_0, readByte());
            case Tags::LIST_EMPTY: return "";
            case Tags::BINARY_8: return readStr(readByte());
            case Tags::BINARY_20: return readStr(readInt20());
            case Tags::BINARY_32: return readStr(readInt32());
            case Tags::JID_PAIR: return readJidPair();
            case Tags::AD_JID: return readAdJid();
            case Tags::HEX_8: case Tags::NIBBLE_8: return readPacked8(tag);
            default: throw std::runtime_error("bad string tag: " + std::to_string(tag));
        }
    }

    std::vector<BinaryNode> readList(uint8_t tag) {
        std::vector<BinaryNode> items; size_t n = readListSize(tag);
        for (size_t i = 0; i < n; i++) items.push_back(decodeNode());
        return items;
    }

    BinaryNode decodeNode() {
        size_t ls = readListSize(readByte());
        std::string hdr = readString(readByte());
        BinaryNode node; node.tag = hdr;
        size_t al = (ls - 1) / 2;
        for (size_t i = 0; i < al; i++) {
            std::string key = readString(readByte());
            std::string val = readString(readByte());
            node.attrs.emplace_back(key, val);
        }
        if (ls % 2 == 0) {
            uint8_t t = readByte();
            if (isListTag(t)) node.content = readList(t);
            else if (t == Tags::BINARY_8) node.content = readBytes(readByte());
            else if (t == Tags::BINARY_20) node.content = readBytes(readInt20());
            else if (t == Tags::BINARY_32) node.content = readBytes(readInt32());
            else node.content = readString(t);
        }
        return node;
    }

public:
    static BinaryNode decode(const uint8_t* data_, size_t len_) {
        BinaryDecoder d(data_, len_);
        return d.decodeNode();
    }
};

BinaryNode decodeBinaryNode(const std::vector<uint8_t>& raw) {
    if (raw.empty()) throw std::runtime_error("empty buffer");
    bool compressed = (raw[0] & 0x02) != 0;
    std::vector<uint8_t> decompressed;
    if (compressed) {
        std::vector<uint8_t> src(raw.begin() + 1, raw.end());
        decompressed.resize(src.size() * 8);
        z_stream strm = {}; inflateInit(&strm);
        strm.next_in = src.data(); strm.avail_in = src.size();
        int ret;
        do {
            strm.next_out = decompressed.data() + strm.total_out;
            strm.avail_out = decompressed.size() - strm.total_out;
            ret = inflate(&strm, Z_NO_FLUSH);
            if (ret == Z_BUF_ERROR || strm.total_out >= decompressed.size()) {
                decompressed.resize(decompressed.size() * 2);
                strm.next_out = decompressed.data() + strm.total_out;
                strm.avail_out = decompressed.size() - strm.total_out;
                ret = inflate(&strm, Z_NO_FLUSH);
            }
        } while (ret == Z_OK);
        if (ret != Z_STREAM_END) { inflateEnd(&strm); throw std::runtime_error("zlib fail"); }
        decompressed.resize(strm.total_out); inflateEnd(&strm);
    } else {
        decompressed.assign(raw.begin() + 1, raw.end());
    }
    return BinaryDecoder::decode(decompressed.data(), decompressed.size());
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

const BinaryNode* getBinaryNodeChild(const BinaryNode& node, const std::string& tag) {
    if (!std::holds_alternative<std::vector<BinaryNode>>(node.content)) return nullptr;
    for (auto& c : std::get<std::vector<BinaryNode>>(node.content))
        if (c.tag == tag) return &c;
    return nullptr;
}

std::vector<const BinaryNode*> getBinaryNodeChildren(const BinaryNode& node, const std::string& tag) {
    std::vector<const BinaryNode*> r;
    if (!std::holds_alternative<std::vector<BinaryNode>>(node.content)) return r;
    for (auto& c : std::get<std::vector<BinaryNode>>(node.content))
        if (c.tag == tag) r.push_back(&c);
    return r;
}

static void nodeStr(const BinaryNode& n, std::ostringstream& ss, int ind) {
    std::string p(ind, ' ');
    ss << p << "<" << n.tag;
    for (auto& [k,v] : n.attrs) ss << " " << k << "=\"" << v << "\"";
    if (std::holds_alternative<std::monostate>(n.content)) ss << "/>";
    else if (std::holds_alternative<std::string>(n.content)) ss << ">" << std::get<std::string>(n.content) << "</" << n.tag << ">";
    else if (std::holds_alternative<std::vector<uint8_t>>(n.content)) ss << ">[" << std::get<std::vector<uint8_t>>(n.content).size() << " bytes]</" << n.tag << ">";
    else if (std::holds_alternative<std::vector<BinaryNode>>(n.content)) {
        ss << ">\n";
        for (auto& c : std::get<std::vector<BinaryNode>>(n.content)) { nodeStr(c, ss, ind+2); ss << "\n"; }
        ss << p << "</" << n.tag << ">";
    }
}

std::string binaryNodeToString(const BinaryNode& node) {
    std::ostringstream ss; nodeStr(node, ss, 0); return ss.str();
}

} // namespace animus::whatsapp

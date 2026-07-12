#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <optional>
#include <variant>

namespace animus::whatsapp {

namespace Tags {
    constexpr uint8_t LIST_EMPTY    = 0;
    constexpr uint8_t DICTIONARY_0  = 236;
    constexpr uint8_t DICTIONARY_1  = 237;
    constexpr uint8_t DICTIONARY_2  = 238;
    constexpr uint8_t DICTIONARY_3  = 239;
    constexpr uint8_t INTEROP_JID   = 245;
    constexpr uint8_t FB_JID        = 246;
    constexpr uint8_t AD_JID        = 247;
    constexpr uint8_t LIST_8        = 248;
    constexpr uint8_t LIST_16       = 249;
    constexpr uint8_t JID_PAIR      = 250;
    constexpr uint8_t HEX_8         = 251;
    constexpr uint8_t BINARY_8      = 252;
    constexpr uint8_t BINARY_20     = 253;
    constexpr uint8_t BINARY_32     = 254;
    constexpr uint8_t NIBBLE_8      = 255;
    constexpr uint8_t PACKED_MAX    = 127;
}

struct BinaryNode;

using BinaryNodeContent = std::variant<
    std::monostate,
    std::string,
    std::vector<uint8_t>,
    std::vector<BinaryNode>
>;

struct BinaryNode {
    std::string tag;
    std::vector<std::pair<std::string, std::string>> attrs;  // ordered, NOT sorted
    BinaryNodeContent content;

    // Set attr (updates existing or appends)
    void setAttr(const std::string& key, const std::string& value) {
        for (auto& [k, v] : attrs) {
            if (k == key) { v = value; return; }
        }
        attrs.emplace_back(key, value);
    }

    // Get attr
    std::string getAttr(const std::string& key, const std::string& def = "") const {
        for (auto& [k, v] : attrs) {
            if (k == key) return v;
        }
        return def;
    }
};

struct FullJid {
    std::string user;
    std::string server;
    int device = 0;
    int domainType = 0;
};

std::string jidEncode(const std::string& user, const std::string& server, int device = 0);
std::optional<FullJid> jidDecode(const std::string& jid);
bool isJidGroup(const std::string& jid);
bool isLidUser(const std::string& jid);

std::vector<uint8_t> encodeBinaryNode(const BinaryNode& node);
BinaryNode decodeBinaryNode(const std::vector<uint8_t>& data);

const BinaryNode* getBinaryNodeChild(const BinaryNode& node, const std::string& childTag);
std::vector<const BinaryNode*> getBinaryNodeChildren(const BinaryNode& node, const std::string& childTag);
std::string binaryNodeToString(const BinaryNode& node);

} // namespace animus::whatsapp

#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <json/json.h>
#include <json/reader.h>
#include <json/writer.h>

namespace animus::kernel {

namespace channel_detail {

inline Json::Value ParseJson(const std::string& json) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(json);
    std::string errors;
    Json::parseFromStream(builder, stream, &root, &errors);
    return root;
}

inline std::string GetString(const Json::Value& v, const std::string& key,
                             const std::string& def = "") {
    if (v.isMember(key) && v[key].isString()) return v[key].asString();
    return def;
}

inline int64_t GetInt(const Json::Value& v, const std::string& key, int64_t def = 0) {
    if (v.isMember(key) && v[key].isInt64()) return v[key].asInt64();
    if (v.isMember(key) && v[key].isInt()) return v[key].asInt();
    return def;
}

inline std::string UrlEncode(const std::string& input) {
    std::string result;
    for (char c : input) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
            result += buf;
        }
    }
    return result;
}

inline std::string Base64EncodeStr(const std::string& input) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((input.size() + 2) / 3) * 4);
    for (size_t i = 0; i < input.size(); i += 3) {
        uint32_t n = static_cast<uint8_t>(input[i]) << 16;
        if (i + 1 < input.size()) n |= static_cast<uint8_t>(input[i + 1]) << 8;
        if (i + 2 < input.size()) n |= static_cast<uint8_t>(input[i + 2]);
        result += table[(n >> 18) & 0x3F];
        result += table[(n >> 12) & 0x3F];
        result += (i + 1 < input.size()) ? table[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < input.size()) ? table[n & 0x3F] : '=';
    }
    return result;
}

inline std::string JsonCompact(const Json::Value& v) {
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, v);
}

inline std::string StripHtmlSimple(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    bool inTag = false;
    bool lastWasSpace = true;
    for (char c : html) {
        if (inTag) { if (c == '>') inTag = false; continue; }
        if (c == '<') { inTag = true; continue; }
        if (c == '\n') { if (!lastWasSpace) { out += '\n'; lastWasSpace = true; } continue; }
        if (c == ' ' || c == '\t' || c == '\r') { if (!lastWasSpace) { out += ' '; lastWasSpace = true; } continue; }
        out += c; lastWasSpace = false;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) out.pop_back();
    return out;
}

} // namespace channel_detail

} // namespace animus::kernel

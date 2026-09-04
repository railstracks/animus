#include "animus_kernel/api/ManifestV1.h"

#include <openssl/evp.h>

#include <algorithm>
#include <sstream>

namespace animus::kernel::manifest_v1 {

namespace {

// Reject floats anywhere in the document — the registry lints the same way;
// the daemon re-checks so canonicalization can never silently diverge.
bool NoFloats(const Json::Value& v, const std::string& path, std::string& err) {
    if (v.isNull() || v.isBool() || v.isString() || v.isIntegral()) return true;
    if (v.isNumeric()) {  // real
        err = path + ": floats are not allowed in manifests (canonicalization portability)";
        return false;
    }
    if (v.isObject()) {
        for (const std::string& k : v.getMemberNames()) {
            if (!NoFloats(v[k], path + "." + k, err)) return false;
        }
        return true;
    }
    if (v.isArray()) {
        for (Json::ArrayIndex i = 0; i < v.size(); ++i) {
            if (!NoFloats(v[i], path + "[" + std::to_string(i) + "]", err)) return false;
        }
        return true;
    }
    return true;
}

// Recursive key sort in place.
void SortKeys(Json::Value& v) {
    if (v.isObject()) {
        std::vector<std::string> keys = v.getMemberNames();
        std::sort(keys.begin(), keys.end());
        Json::Value sorted(Json::objectValue);
        for (const std::string& k : keys) {
            Json::Value child = v[k];
            SortKeys(child);
            sorted[k] = child;
        }
        v = sorted;
    } else if (v.isArray()) {
        for (Json::ArrayIndex i = 0; i < v.size(); ++i) {
            SortKeys(v[i]);
        }
    }
}

Json::StreamWriterBuilder CanonicalWriter() {
    Json::StreamWriterBuilder w;
    w["indentation"] = "";       // compact — no whitespace
    w["sortKeys"] = true;        // byte-order key sort (belt; SortKeys is braces)
    w["emitUTF8"] = true;        // raw UTF-8 like Ruby JSON.generate
    w["commentStyle"] = "None";
    return w;
}

}  // namespace

std::string Canonicalize(const Json::Value& value) {
    Json::Value normalized = value;
    SortKeys(normalized);
    Json::StreamWriterBuilder w = CanonicalWriter();
    w["sortKeys"] = true;
    return Json::writeString(w, normalized);
}

std::string Sha256Hex(const std::string& data) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (EVP_Digest(data.data(), data.size(), md, &len, EVP_sha256(), nullptr) != 1 || len != 32) {
        return {};
    }
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (unsigned i = 0; i < len; ++i) {
        out.push_back(hex[md[i] >> 4]);
        out.push_back(hex[md[i] & 0xF]);
    }
    return out;
}

bool Process(const std::string& raw, Processed& out, std::string& err) {
    Json::CharReaderBuilder rb;
    rb["collectComments"] = false;
    std::string parseErr;
    std::istringstream ss(raw);
    if (!Json::parseFromStream(rb, ss, &out.manifest, &parseErr)) {
        err = "invalid JSON: " + parseErr;
        return false;
    }
    if (!out.manifest.isObject()) {
        err = "manifest must be a JSON object";
        return false;
    }
    if (!NoFloats(out.manifest, "$", err)) return false;

    out.canonical = Canonicalize(out.manifest);
    out.content_hash = Sha256Hex(out.canonical);
    if (out.content_hash.empty()) {
        err = "sha256 failed";
        return false;
    }
    return true;
}

}  // namespace animus::kernel::manifest_v1

// FileSignalStore.cpp — file-backed Signal stores (#53)
//
// Root cause fixed here: WhatsAppGatewayLoop used create_in_memory_manager,
// so every daemon restart wiped peer sessions — the peer keeps sending
// msg-type ciphertext against a session we no longer have, and decryption
// fails ("no stored identity key"). These stores persist the ratchet state
// as opaque blobs so restarts are transparent to peers.

#include "animus_kernel/signal/FileSignalStore.h"
#include "animus_kernel/signal/SignalSessionManager.h"

#include <json/json.h>
#include <fstream>
#include <cstdio>
#include <filesystem>

namespace animus::signal {
namespace {

// Compact base64 — self-contained so the signal module stays
// whatsapp-independent.
const char* kB64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string B64Encode(const std::vector<uint8_t>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    uint32_t buf = 0;
    int bits = 0;
    for (uint8_t c : data) {
        buf = (buf << 8) | c;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out += kB64Chars[(buf >> bits) & 0x3F];
        }
    }
    if (bits > 0) out += kB64Chars[(buf << (6 - bits)) & 0x3F];
    while (out.size() % 4 != 0) out += '=';
    return out;
}

int B64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::vector<uint8_t> B64Decode(const std::string& s) {
    std::vector<uint8_t> out;
    uint32_t buf = 0;
    int bits = 0;
    for (char c : s) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        int v = B64Val(c);
        if (v < 0) return {};
        buf = (buf << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((buf >> bits) & 0xFF));
        }
    }
    return out;
}

// Atomic write: serialize -> path.tmp -> rename.
bool WriteJsonAtomic(const std::string& path, const Json::Value& root) {
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string data = Json::writeString(wb, root);
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return false;
        f.write(data.data(), (std::streamsize)data.size());
        if (!f.good()) return false;
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

Json::Value ReadJsonOrEmpty(const std::string& path) {
    Json::Value root;
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return root;
    std::string errs;
    Json::CharReaderBuilder rb;
    if (!Json::parseFromStream(rb, f, &root, &errs)) return Json::Value();
    return root;
}

std::string AddrKey(const SignalAddress& a) {
    return a.name + ":" + std::to_string(a.device_id);
}

} // namespace

// ─── FileSessionStore ───

FileSessionStore::FileSessionStore(const std::string& file) : file_(file) {
    auto root = ReadJsonOrEmpty(file);
    for (auto it = root.begin(); it != root.end(); ++it)
        sessions_[it.name()] = it->asString();
}

std::optional<SessionRecord> FileSessionStore::load_session(const SignalAddress& addr) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(AddrKey(addr));
    if (it == sessions_.end()) return std::nullopt;
    auto bytes = B64Decode(it->second);
    if (bytes.empty()) return std::nullopt;
    return SessionRecord::deserialize(bytes.data(), bytes.size());
}

void FileSessionStore::store_session(const SignalAddress& addr, const SessionRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[AddrKey(addr)] = B64Encode(record.serialize());
    PersistLocked();
}

bool FileSessionStore::contains_session(const SignalAddress& addr) {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.count(AddrKey(addr)) > 0;
}

void FileSessionStore::delete_session(const SignalAddress& addr) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(AddrKey(addr));
    PersistLocked();
}

void FileSessionStore::delete_all_sessions(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string prefix = name + ":";
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->first.rfind(prefix, 0) == 0) it = sessions_.erase(it);
        else ++it;
    }
    PersistLocked();
}

std::vector<uint32_t> FileSessionStore::get_sub_device_sessions(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint32_t> devices;
    const std::string prefix = name + ":";
    for (const auto& [k, v] : sessions_) {
        if (k.rfind(prefix, 0) != 0) continue;
        auto pos = k.find_last_of(':');
        if (pos == std::string::npos || pos + 1 >= k.size()) continue;
        try { devices.push_back((uint32_t)std::stoul(k.substr(pos + 1))); } catch (...) {}
    }
    return devices;
}

void FileSessionStore::PersistLocked() {
    Json::Value root(Json::objectValue);
    for (const auto& [k, v] : sessions_) root[k] = v;
    WriteJsonAtomic(file_, root);
}

// ─── FileIdentityKeyStore ───

FileIdentityKeyStore::FileIdentityKeyStore(const std::string& file, SignalIdentityKey own)
    : file_(file), own_(std::move(own)) {
    auto root = ReadJsonOrEmpty(file);
    for (auto it = root.begin(); it != root.end(); ++it)
        identities_[it.name()] = it->asString();
}

SignalKeypair FileIdentityKeyStore::get_identity_key_pair() { return own_.keypair; }

uint32_t FileIdentityKeyStore::get_local_registration_id() { return own_.registration_id; }

bool FileIdentityKeyStore::save_identity(const SignalAddress& addr,
                                          const std::array<uint8_t, 32>& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool was_new = (identities_.find(AddrKey(addr)) == identities_.end());
    identities_[AddrKey(addr)] = B64Encode(std::vector<uint8_t>(key.begin(), key.end()));
    PersistLocked();
    return was_new;
}

bool FileIdentityKeyStore::is_trusted_identity(const SignalAddress& addr,
                                                const std::array<uint8_t, 32>& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = identities_.find(AddrKey(addr));
    if (it == identities_.end()) return true; // TOFU — first sight
    auto stored = B64Decode(it->second);
    if (stored.size() != 32) return false;
    return std::equal(stored.begin(), stored.end(), key.begin());
}

std::optional<std::array<uint8_t, 32>> FileIdentityKeyStore::load_identity(const SignalAddress& addr) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = identities_.find(AddrKey(addr));
    if (it == identities_.end()) return std::nullopt;
    auto stored = B64Decode(it->second);
    if (stored.size() != 32) return std::nullopt;
    std::array<uint8_t, 32> key{};
    std::copy(stored.begin(), stored.end(), key.begin());
    return key;
}

void FileIdentityKeyStore::PersistLocked() {
    Json::Value root(Json::objectValue);
    for (const auto& [k, v] : identities_) root[k] = v;
    WriteJsonAtomic(file_, root);
}

// ─── FileSenderKeyStore ───

FileSenderKeyStore::FileSenderKeyStore(const std::string& file) : file_(file) {
    auto root = ReadJsonOrEmpty(file);
    for (auto it = root.begin(); it != root.end(); ++it)
        keys_[it.name()] = it->asString();
}

std::optional<SenderKeyRecord> FileSenderKeyStore::load_sender_key(
        const SignalAddress& addr, const std::string& group_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = keys_.find(AddrKey(addr) + "|" + group_id);
    if (it == keys_.end()) return std::nullopt;
    auto bytes = B64Decode(it->second);
    if (bytes.empty()) return std::nullopt;
    return SenderKeyRecord::deserialize(bytes.data(), bytes.size());
}

void FileSenderKeyStore::store_sender_key(const SignalAddress& addr, const std::string& group_id,
                                           const SenderKeyRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    keys_[AddrKey(addr) + "|" + group_id] = B64Encode(record.serialize());
    PersistLocked();
}

bool FileSenderKeyStore::contains_sender_key(const SignalAddress& addr, const std::string& group_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return keys_.count(AddrKey(addr) + "|" + group_id) > 0;
}

void FileSenderKeyStore::PersistLocked() {
    Json::Value root(Json::objectValue);
    for (const auto& [k, v] : keys_) root[k] = v;
    WriteJsonAtomic(file_, root);
}

// ─── Factory ───

std::unique_ptr<SignalSessionManager> create_file_manager(
        const std::string& baseDir, std::optional<SignalIdentityKey> identity_key) {
    std::error_code ec;
    std::filesystem::create_directories(baseDir, ec); // ok if exists/fails
    auto idKey = identity_key.value_or(SignalIdentityKey::generate(1));
    return std::make_unique<SignalSessionManager>(
        std::make_unique<FileSessionStore>(baseDir + "/sessions.json"),
        std::make_unique<InMemoryPreKeyStore>(),        // ours — re-populated from auth each boot
        std::make_unique<InMemorySignedPreKeyStore>(),  // ours — re-populated from auth each boot
        std::make_unique<FileIdentityKeyStore>(baseDir + "/identities.json", std::move(idKey)),
        std::make_unique<FileSenderKeyStore>(baseDir + "/sender-keys.json"));
}

} // namespace animus::signal

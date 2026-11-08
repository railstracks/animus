#include "animus_kernel/NodeIdentity.h"

#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/Log.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace animus::kernel {

namespace {

// X25519 base point (RFC 7748): u = 9, little-endian.
constexpr std::array<unsigned char, 32> kBasePoint = {
    0x09, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

} // namespace

std::string BytesToHex(const unsigned char* data, size_t len) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(2 * len);
    for (size_t i = 0; i < len; ++i) {
        out += hex[data[i] >> 4];
        out += hex[data[i] & 0xF];
    }
    return out;
}

bool HexToBytes32(const std::string& hex, std::array<unsigned char, 32>& out) {
    if (hex.size() != 64) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < 32; ++i) {
        int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}

bool LoadOrCreateNodeIdentity(const std::string& keyPath, NodeIdentity& out,
                              std::string& error) {
    // Load path — same discipline as SecretsVault::LoadOrCreateKey: no
    // symlink following, regular files only, read from the verified
    // descriptor, full validation before anything commits.
    int keyFd = ::open(keyPath.c_str(), O_RDONLY | O_NOFOLLOW);
    if (keyFd < 0 && errno != ENOENT) {
        error = "cannot open node identity file '" + keyPath + "' (" +
                std::strerror(errno) + ")";
        return false;
    }
    if (keyFd >= 0) {
        struct stat st {};
        if (::fstat(keyFd, &st) != 0 || !S_ISREG(st.st_mode)) {
            ::close(keyFd);
            error = "node identity file '" + keyPath + "' is not a regular file";
            return false;
        }
        if ((st.st_mode & 077) != 0) {
            ALOG_WARNING("federation", "[identity] key file " << keyPath << " is group/other "
                         << "accessible (mode " << std::oct << (st.st_mode & 0777) << std::dec
                         << ") — expected 0600; refusing nothing, but tighten it");
        }
        std::string hex;
        char rbuf[4096];
        ssize_t n = 0;
        while ((n = ::read(keyFd, rbuf, sizeof rbuf)) > 0)
            hex.append(rbuf, static_cast<size_t>(n));
        ::close(keyFd);
        if (n < 0) {
            error = "read error on node identity file '" + keyPath + "'";
            return false;
        }
        // Trim exactly one trailing newline (CRLF tolerated); anything else
        // is a malformed file and gets rejected below — not silently eaten.
        if (!hex.empty() && (hex.back() == '\n' || hex.back() == '\r')) {
            hex.pop_back();
            if (!hex.empty() && (hex.back() == '\n' || hex.back() == '\r')) hex.pop_back();
        }
        NodeIdentity parsed;
        if (!HexToBytes32(hex, parsed.private_key)) {
            error = "node identity file '" + keyPath +
                    "' must hold 64 hex chars (32 bytes), found " + std::to_string(hex.size());
            return false;
        }
        // Derive the public key; a scalar that fails derivation means a
        // corrupt file — loud failure, never a silent regeneration.
        std::string deriveErr;
        if (!crypto::X25519SharedSecret(parsed.private_key, kBasePoint,
                                        parsed.public_key, deriveErr)) {
            error = "node identity private key fails public-key derivation: " + deriveErr;
            return false;
        }
        out = parsed;
        ALOG_INFO("federation", "[identity] node identity loaded from " << keyPath
                  << " (public key " << BytesToHex(out.public_key.data(), 8) << "...)");
        return true;
    }

    // First use — generate. Exclusive, no-follow create with tight mode from
    // the first syscall: no symlink pre-placement, no umask window.
    crypto::X25519KeyPair kp;
    std::string genErr;
    if (!crypto::GenerateX25519KeyPair(kp, genErr)) {
        error = "keypair generation failed: " + genErr;
        return false;
    }
    const std::string keyHex = BytesToHex(kp.private_key.data(), kp.private_key.size());
    {
        const int fd = ::open(keyPath.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (fd < 0) {
            if (errno == EEXIST) {
                // Lost a creation race (or a pre-existing file appeared
                // between the load attempt and now) — reload, don't clobber.
                return LoadOrCreateNodeIdentity(keyPath, out, error);
            }
            error = "cannot create node identity file '" + keyPath + "' (" +
                    std::strerror(errno) + ")";
            return false;
        }
        const std::string blob = keyHex + "\n";
        size_t off = 0;
        while (off < blob.size()) {
            const ssize_t w = ::write(fd, blob.data() + off, blob.size() - off);
            if (w <= 0) {
                ::close(fd);
                ::unlink(keyPath.c_str());  // never leave a partial key
                error = "short write creating node identity file '" + keyPath + "'";
                return false;
            }
            off += static_cast<size_t>(w);
        }
        ::fsync(fd);  // losing this file orphans every envelope sealed to us
        ::close(fd);
    }
    out.private_key = kp.private_key;
    out.public_key = kp.public_key;
    ALOG_INFO("federation", "[identity] NEW node identity generated at " << keyPath
              << " (mode 0600). Losing this file orphans every envelope sealed to "
              << "this node's public key — back it up alongside api-vault.key.");
    return true;
}

std::string PeerNodeKeyRow(const std::string& nodeId) {
    return "federation.node_keys." + nodeId;
}

bool StorePeerNodeKey(AgentConfigStore& store, const std::string& nodeId,
                      const std::array<unsigned char, 32>& publicKey,
                      std::string& error) {
    if (nodeId.empty()) {
        error = "node id must not be empty";
        return false;
    }
    store.Set("", PeerNodeKeyRow(nodeId), BytesToHex(publicKey.data(), publicKey.size()));
    return true;
}

bool GetPeerNodeKey(AgentConfigStore& store, const std::string& nodeId,
                    std::array<unsigned char, 32>& out, std::string& error) {
    const std::string hex = store.Get("", PeerNodeKeyRow(nodeId));
    if (hex.empty()) {
        error = "no public key stored for node '" + nodeId + "'";
        return false;
    }
    if (!HexToBytes32(hex, out)) {
        error = "stored public key for node '" + nodeId + "' is malformed";
        return false;
    }
    return true;
}

} // namespace animus::kernel

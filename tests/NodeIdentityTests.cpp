// NodeIdentity tests (#273 — federation secrets 2/5): file discipline
// (0600 hex key file, create/reload, derive pub = X25519(priv, 9)),
// malformed-file rejection, and the peer public-key registry.
#include "animus_kernel/NodeIdentity.h"
#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/CryptoUtils.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <iterator>

using namespace animus::kernel;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string MakeTempPath(const char* tag) {
    char tmp[] = "/tmp/animus_node_identity_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + tag;
}

// [1/5] First boot creates the key file; reload returns the same identity.
void TestCreateAndReload() {
    std::cerr << "[1/5] identity create + reload\n";
    namespace fs = std::filesystem;
    const std::string dir = MakeTempPath(".d");
    fs::create_directories(dir);
    const std::string keyPath = dir + "/node-identity.key";

    NodeIdentity a, b;
    std::string err;
    Assert(LoadOrCreateNodeIdentity(keyPath, a, err), "create: " + err);
    Assert(fs::exists(keyPath), "key file exists");

    struct stat st {};
    Assert(::stat(keyPath.c_str(), &st) == 0, "stat");
    Assert((st.st_mode & 0777) == 0600, "mode 0600");
    Assert(st.st_size == 65, "64 hex chars + newline = 65 bytes");

    std::ifstream f(keyPath);
    std::string hex((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Assert(hex.size() == 65 && hex.back() == '\n', "trailing newline");
    Assert(HexToBytes32(hex.substr(0, 64), b.private_key), "file holds 64 hex chars");
    Assert(std::memcmp(b.private_key.data(), a.private_key.data(), 32) == 0,
           "file holds the same private key");

    Assert(LoadOrCreateNodeIdentity(keyPath, b, err), "reload: " + err);
    Assert(std::memcmp(a.private_key.data(), b.private_key.data(), 32) == 0,
           "same private key on reload");
    Assert(std::memcmp(a.public_key.data(), b.public_key.data(), 32) == 0,
           "same public key on reload");
    fs::remove_all(dir);
}

// [2/5] Public key is derived, never stored: pub == X25519(priv, 9).
void TestPublicKeyDerivation() {
    std::cerr << "[2/5] public key derivation identity\n";
    namespace fs = std::filesystem;
    const std::string dir = MakeTempPath(".d");
    fs::create_directories(dir);
    const std::string keyPath = dir + "/node-identity.key";
    NodeIdentity id;
    std::string err;
    Assert(LoadOrCreateNodeIdentity(keyPath, id, err), "create: " + err);
    std::array<unsigned char, 32> base{};
    base[0] = 9;
    std::array<unsigned char, 32> derived{};
    Assert(crypto::X25519SharedSecret(id.private_key, base, derived, err),
           "scalarmult-base: " + err);
    Assert(std::memcmp(derived.data(), id.public_key.data(), 32) == 0,
           "pub == X25519(priv, 9)");
    fs::remove_all(dir);
}

// [3/5] Malformed key files fail loudly and are never clobbered.
void TestMalformedFiles() {
    std::cerr << "[3/5] malformed key files rejected\n";
    namespace fs = std::filesystem;
    const std::string dir = MakeTempPath(".d");
    fs::create_directories(dir);

    {   // Wrong length (63 hex chars).
        const std::string p = dir + "/short.key";
        std::ofstream(p) << std::string(63, 'a') << "\n";
        NodeIdentity id;
        std::string err;
        Assert(!LoadOrCreateNodeIdentity(p, id, err), "63-char file rejected");
        Assert(!err.empty(), "error message present");
        Assert(fs::exists(p), "file not clobbered on rejection");
    }
    {   // Non-hex characters.
        const std::string p = dir + "/nonhex.key";
        std::ofstream(p) << std::string(64, 'z') << "\n";
        NodeIdentity id;
        std::string err;
        Assert(!LoadOrCreateNodeIdentity(p, id, err), "non-hex file rejected");
    }
    fs::remove_all(dir);
}

// [4/5] Peer public-key registry: store/get roundtrip via AgentConfigStore,
// malformed stored values and empty node ids fail loudly.
void TestPeerKeyRegistry() {
    std::cerr << "[4/5] peer public-key registry\n";
    const std::string dbPath = MakeTempPath(".db");
    SqliteDataStore dataStore(dbPath);
    AgentConfigStore store(&dataStore);

    std::array<unsigned char, 32> pub{};
    pub[0] = 0xde;
    pub[31] = 0xad;
    std::string err;
    Assert(PeerNodeKeyRow("42") == "federation.node_keys.42", "row name");
    Assert(StorePeerNodeKey(store, "42", pub, err), "store: " + err);

    std::array<unsigned char, 32> got{};
    Assert(GetPeerNodeKey(store, "42", got, err), "get: " + err);
    Assert(std::memcmp(got.data(), pub.data(), 32) == 0, "roundtrip");

    Assert(!GetPeerNodeKey(store, "7", got, err), "missing node errors");
    Assert(!err.empty(), "missing-node error message");

    Assert(!StorePeerNodeKey(store, "", pub, err), "empty node id rejected");

    // Malformed stored value (simulated corrupt row).
    store.Set("", PeerNodeKeyRow("9"), "not-hex");
    Assert(!GetPeerNodeKey(store, "9", got, err), "malformed row errors");
    ::unlink(dbPath.c_str());
}

// [5/5] Hex helpers roundtrip.
void TestHexHelpers() {
    std::cerr << "[5/5] hex helpers\n";
    std::array<unsigned char, 32> in{};
    for (size_t i = 0; i < 32; ++i) in[i] = static_cast<unsigned char>(i * 8);
    const std::string hex = BytesToHex(in.data(), 32);
    Assert(hex.size() == 64, "64 chars");
    std::array<unsigned char, 32> out{};
    Assert(HexToBytes32(hex, out), "parse");
    Assert(std::memcmp(in.data(), out.data(), 32) == 0, "roundtrip");
    Assert(!HexToBytes32("zz", out), "garbage rejected");
    Assert(!HexToBytes32(std::string(63, 'a'), out), "short rejected");
}

} // namespace

int main() {
    TestCreateAndReload();
    TestPublicKeyDerivation();
    TestMalformedFiles();
    TestPeerKeyRegistry();
    TestHexHelpers();
    if (g_failures == 0) std::cerr << "NodeIdentityTests: all passed\n";
    return g_failures == 0 ? 0 : 1;
}

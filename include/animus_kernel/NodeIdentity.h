#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "animus_kernel/CryptoUtils.h"

namespace animus::kernel {

class AgentConfigStore;

// ============================================================================
// NodeIdentity — X25519 identity keypair for federation secrets
// (#182 design, ticket #273).
//
// Private key at rest: <dataDir>/node-identity.key — 64 hex chars + one
// trailing newline, mode 0600, exactly the api-vault.key discipline
// (O_EXCL|O_NOFOLLOW create with fsync, no symlink following on load,
// regular files only, full validation before commit, never a partial
// file). The public key is NEVER stored — it is derived at load as
// X25519(private, base point 9).
// ============================================================================

struct NodeIdentity {
    std::array<unsigned char, 32> private_key{};
    std::array<unsigned char, 32> public_key{};
};

// Load the identity from keyPath, creating it on first use. Returns false
// + error on any file-format/security violation. Callers must fail closed
// (federation ceremonies unavailable), never silently regenerate: a fresh
// private key orphans every envelope ever sealed to the old public key.
bool LoadOrCreateNodeIdentity(const std::string& keyPath, NodeIdentity& out,
                              std::string& error);

// --- Peer public-key registry (admin exchange surface) ---------------------
// Peer node public keys live in the agent config store as PLAIN rows
// (public keys are not secrets): federation.node_keys.<node_id>. They ride
// the #93 replication set, so every peer learns every peer's key.
std::string PeerNodeKeyRow(const std::string& nodeId);
bool StorePeerNodeKey(AgentConfigStore& store, const std::string& nodeId,
                      const std::array<unsigned char, 32>& publicKey,
                      std::string& error);
bool GetPeerNodeKey(AgentConfigStore& store, const std::string& nodeId,
                    std::array<unsigned char, 32>& out, std::string& error);

// Hex helpers for 32-byte keys (admin routes + tests).
std::string BytesToHex(const unsigned char* data, size_t len);
bool HexToBytes32(const std::string& hex, std::array<unsigned char, 32>& out);

} // namespace animus::kernel

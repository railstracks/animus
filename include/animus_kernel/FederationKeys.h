#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "animus_kernel/IDataStore.h"
#include "animus_kernel/NodeIdentity.h"

namespace animus::kernel {

class AgentConfigStore;
class SecretsVault;

// ============================================================================
// FederationKeys — epoch DEK envelopes (#274, fed-secrets 3/5; #182 design).
//
// One random 32-byte DEK per network per epoch. The DEK is sealed to each
// approved member's X25519 public key — ephemeral X25519 → HKDF-SHA256 with
// the full row context as `info` → AES-256-GCM — and the envelope rows
// themselves sync: a node can only unwrap the row addressed to its own key.
//
// Row identity: TEXT primary key "<epoch>:<target_node>" (deterministic,
// rides the plain NEW.id sync-trigger path; no composite registration).
// The context binds {protocol, epoch, target, source, ephemeral pub}, so
// an edited row fails its own GCM authentication — fail-closed, never a
// silent empty key (the vault's missing-key hard-error discipline applied
// at the envelope layer).
// ============================================================================

class FederationKeys {
public:
    explicit FederationKeys(IDataStore* store) : m_store(store) {}

    void EnsureSchema();

    struct EnvelopeRow {
        int64_t epoch{0};
        std::string targetNode;
        std::string sourceNode;
        std::string ephemeralPubHex;   // 64 hex chars
        std::string ciphertextHex;     // Aes256GcmSeal envelope hex
    };

    // Seal `dek` for `targetNode` under `targetPub`, upserting the
    // (epoch, target) row — re-sealing replaces the envelope (re-key,
    // approve-node). Returns false + error on any primitive failure.
    bool SealEpochDek(int64_t epoch,
                      const std::string& targetNode,
                      const std::string& sourceNode,
                      const std::array<unsigned char, 32>& targetPub,
                      const std::array<unsigned char, 32>& dek,
                      std::string& error);

    bool GetEnvelope(int64_t epoch, const std::string& targetNode,
                     EnvelopeRow& out, std::string& error) const;

    // Unwrap the NEWEST envelope addressed to this node. False + error when
    // none exists (never an empty key) or unwrap fails (wrong/edited row).
    bool OpenEpochDek(const NodeIdentity& self,
                      const std::string& selfNodeId,
                      int64_t& epochOut,
                      std::array<unsigned char, 32>& dekOut,
                      std::string& error) const;

    bool ListEnvelopes(std::vector<EnvelopeRow>& out, std::string& error) const;

    // #276: drop every envelope addressed to a node (revocation) — its
    // remaining copies on the revoked member's own DB are useless against
    // the post-rotation epoch's ciphertext.
    bool DeleteEnvelopesFor(const std::string& targetNode, std::string& error);

    // --- #276: owner-triggered ceremonies (v1, per the design) -------------

    struct CeremonyContext {
        FederationKeys* keys{nullptr};
        SecretsVault* vault{nullptr};          // member holding the DEK
        AgentConfigStore* registry{nullptr};   // federation.node_keys.<id> rows
        NodeIdentity* self{nullptr};
        std::string selfNodeId;
    };

    // Approve-node: register the new member's public key; if no epoch exists
    // yet (first ceremony anywhere) generate the epoch-1 DEK and rotate the
    // vault to it (re-sealing every master-key-era row), then seal the
    // current-epoch DEK to self + every member. Returns the epoch, or -1.
    int64_t RunApproveNodeCeremony(const CeremonyContext& ctx,
                                   const std::string& newNodeId,
                                   const std::array<unsigned char, 32>& newPub,
                                   std::string& error);

    // Revoke-node: delete the member's registry row + envelopes, rotate to
    // epoch N+1 with a fresh DEK (re-sealing every vault row), seal only to
    // self + remaining members. The revoked node's stored envelopes and old
    // ciphertexts both fail against epoch N+1. Honest boundary: rotation
    // restores exclusivity going forward — it does not un-read what the
    // revoked node already read. Returns the new epoch, or -1.
    int64_t RunRevokeNodeCeremony(const CeremonyContext& ctx,
                                  const std::string& revokedNodeId,
                                  std::string& error);

    // Max epoch across ALL targets — the lockout probe: peers hold an epoch
    // this node has no envelope for. False when no rows exist at all.
    bool MaxEpoch(int64_t* epochOut = nullptr) const;

    // --- primitives (testable without a store) ---------------------------

    // Context bound into HKDF info — every field comes from the row, so
    // editing any column changes the derived key and GCM auth fails.
    static std::string ContextFor(int64_t epoch, const std::string& targetNode,
                                  const std::string& sourceNode,
                                  const std::string& ephemeralPubHex);

    static bool WrapWithEphemeral(const std::array<unsigned char, 32>& ephPriv,
                                  const std::array<unsigned char, 32>& ephPub,
                                  const std::array<unsigned char, 32>& targetPub,
                                  const std::array<unsigned char, 32>& dek,
                                  const std::string& context,
                                  std::string& envelopeHexOut,
                                  std::string& error);

    static bool UnwrapDek(const std::array<unsigned char, 32>& selfPriv,
                          const std::array<unsigned char, 32>& ephemeralPub,
                          const std::string& context,
                          const std::string& envelopeHex,
                          std::array<unsigned char, 32>& dekOut,
                          std::string& error);

private:
    IDataStore* m_store;
};

} // namespace animus::kernel

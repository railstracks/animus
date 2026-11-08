#include "animus_kernel/FederationKeys.h"

#include "animus_kernel/CryptoUtils.h"
#include "animus_kernel/Log.h"
#include "animus_kernel/SchemaHelpers.h"

#include <chrono>
#include <cstring>

namespace animus::kernel {

namespace {

int64_t NowUnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

void FederationKeys::EnsureSchema() {
    if (!m_store) return;
    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS federation_key_envelopes (
            id TEXT PRIMARY KEY,
            epoch INTEGER NOT NULL,
            target_node TEXT NOT NULL,
            source_node TEXT NOT NULL,
            ephemeral_pub TEXT NOT NULL,
            ciphertext TEXT NOT NULL,
            created_at_unix_ms INTEGER NOT NULL,
            updated_at_unix_ms INTEGER NOT NULL,
            UNIQUE (epoch, target_node)
        );
    )");
}

std::string FederationKeys::ContextFor(int64_t epoch, const std::string& targetNode,
                                       const std::string& sourceNode,
                                       const std::string& ephemeralPubHex) {
    return "animus.federation.dek.v1 epoch=" + std::to_string(epoch) +
           " target=" + targetNode + " source=" + sourceNode +
           " eph=" + ephemeralPubHex;
}

bool FederationKeys::WrapWithEphemeral(const std::array<unsigned char, 32>& ephPriv,
                                       const std::array<unsigned char, 32>& ephPub,
                                       const std::array<unsigned char, 32>& targetPub,
                                       const std::array<unsigned char, 32>& dek,
                                       const std::string& context,
                                       std::string& envelopeHexOut,
                                       std::string& error) {
    std::array<unsigned char, 32> shared{};
    std::string err;
    if (!crypto::X25519SharedSecret(ephPriv, targetPub, shared, err)) {
        error = "DH with target public key failed: " + err;
        return false;
    }
    // HKDF-SHA256: ikm = shared secret, absent salt = RFC zero salt,
    // info = full row context — binding epoch/target/source/ephemeral
    // into the KEK so an edited row fails its own GCM authentication.
    const std::vector<unsigned char> ikm(shared.begin(), shared.end());
    const std::vector<unsigned char> info(context.begin(), context.end());
    std::vector<unsigned char> kek = crypto::HkdfSha256(ikm, {}, info, 32, err);
    if (kek.size() != 32) {
        error = "hkdf failed: " + err;
        return false;
    }
    const std::string dekBytes(reinterpret_cast<const char*>(dek.data()), 32);
    envelopeHexOut = crypto::Aes256GcmSeal(kek, dekBytes, err);
    if (envelopeHexOut.empty()) {
        error = "seal failed: " + err;
        return false;
    }
    (void)ephPub;  // bound via the context string, not the DH itself
    return true;
}

bool FederationKeys::UnwrapDek(const std::array<unsigned char, 32>& selfPriv,
                               const std::array<unsigned char, 32>& ephemeralPub,
                               const std::string& context,
                               const std::string& envelopeHex,
                               std::array<unsigned char, 32>& dekOut,
                               std::string& error) {
    std::array<unsigned char, 32> shared{};
    std::string err;
    if (!crypto::X25519SharedSecret(selfPriv, ephemeralPub, shared, err)) {
        error = "DH with ephemeral public key failed: " + err;
        return false;
    }
    const std::vector<unsigned char> ikm(shared.begin(), shared.end());
    const std::vector<unsigned char> info(context.begin(), context.end());
    std::vector<unsigned char> kek = crypto::HkdfSha256(ikm, {}, info, 32, err);
    if (kek.size() != 32) {
        error = "hkdf failed: " + err;
        return false;
    }
    std::string dekBytes;
    if (!crypto::Aes256GcmOpen(kek, envelopeHex, dekBytes, err)) {
        error = "envelope open failed: " + err;
        return false;
    }
    if (dekBytes.size() != 32) {
        error = "unwrapped DEK is not 32 bytes";
        return false;
    }
    std::memcpy(dekOut.data(), dekBytes.data(), 32);
    return true;
}

bool FederationKeys::SealEpochDek(int64_t epoch,
                                  const std::string& targetNode,
                                  const std::string& sourceNode,
                                  const std::array<unsigned char, 32>& targetPub,
                                  const std::array<unsigned char, 32>& dek,
                                  std::string& error) {
    if (!m_store) { error = "federation keys have no store"; return false; }
    if (targetNode.empty() || sourceNode.empty()) {
        error = "seal requires target and source node ids";
        return false;
    }
    // Ephemeral first — its pub hex is part of the context.
    crypto::X25519KeyPair eph;
    std::string err;
    if (!crypto::GenerateX25519KeyPair(eph, err)) {
        error = "ephemeral keygen failed: " + err;
        return false;
    }
    const std::string ephHex = BytesToHex(eph.public_key.data(), eph.public_key.size());
    const std::string context = ContextFor(epoch, targetNode, sourceNode, ephHex);
    std::string envelopeHex;
    if (!WrapWithEphemeral(eph.private_key, eph.public_key, targetPub, dek,
                           context, envelopeHex, error)) {
        return false;
    }

    const int64_t now = NowUnixMs();
    const std::string id = std::to_string(epoch) + ":" + targetNode;

    // Upsert by (epoch, target): re-sealing replaces the envelope.
    EnvelopeRow existing;
    std::string getErr;
    if (GetEnvelope(epoch, targetNode, existing, getErr)) {
        auto stmt = m_store->Prepare(
            "UPDATE federation_key_envelopes SET source_node = ?, ephemeral_pub = ?, "
            "ciphertext = ?, updated_at_unix_ms = ? WHERE epoch = ? AND target_node = ?");
        if (!stmt) { error = "prepare failed: " + m_store->ErrMsg(); return false; }
        stmt->BindText(1, sourceNode);
        stmt->BindText(2, ephHex);
        stmt->BindText(3, envelopeHex);
        stmt->BindInt64(4, now);
        stmt->BindInt64(5, epoch);
        stmt->BindText(6, targetNode);
        if (!stmt->ExecDML()) { error = "update failed: " + m_store->ErrMsg(); return false; }
        return true;
    }
    auto stmt = m_store->Prepare(
        "INSERT INTO federation_key_envelopes (id, epoch, target_node, source_node, "
        "ephemeral_pub, ciphertext, created_at_unix_ms, updated_at_unix_ms) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    if (!stmt) { error = "prepare failed: " + m_store->ErrMsg(); return false; }
    stmt->BindText(1, id);
    stmt->BindInt64(2, epoch);
    stmt->BindText(3, targetNode);
    stmt->BindText(4, sourceNode);
    stmt->BindText(5, ephHex);
    stmt->BindText(6, envelopeHex);
    stmt->BindInt64(7, now);
    stmt->BindInt64(8, now);
    if (!stmt->ExecDML()) {
        // Lost a race on the (epoch, target) row — fall back to update.
        auto upd = m_store->Prepare(
            "UPDATE federation_key_envelopes SET source_node = ?, ephemeral_pub = ?, "
            "ciphertext = ?, updated_at_unix_ms = ? WHERE epoch = ? AND target_node = ?");
        if (!upd) { error = "prepare failed: " + m_store->ErrMsg(); return false; }
        upd->BindText(1, sourceNode);
        upd->BindText(2, ephHex);
        upd->BindText(3, envelopeHex);
        upd->BindInt64(4, now);
        upd->BindInt64(5, epoch);
        upd->BindText(6, targetNode);
        if (!upd->ExecDML()) { error = "upsert failed: " + m_store->ErrMsg(); return false; }
    }
    return true;
}

bool FederationKeys::GetEnvelope(int64_t epoch, const std::string& targetNode,
                                 EnvelopeRow& out, std::string& error) const {
    if (!m_store) { error = "federation keys have no store"; return false; }
    auto stmt = m_store->Prepare(
        "SELECT source_node, ephemeral_pub, ciphertext FROM federation_key_envelopes "
        "WHERE epoch = ? AND target_node = ?");
    if (!stmt) { error = "prepare failed: " + m_store->ErrMsg(); return false; }
    stmt->BindInt64(1, epoch);
    stmt->BindText(2, targetNode);
    if (!stmt->Step()) { error = "no envelope for epoch/target"; return false; }
    out = EnvelopeRow{};
    out.epoch = epoch;
    out.targetNode = targetNode;
    out.sourceNode = stmt->ColumnText(0);
    out.ephemeralPubHex = stmt->ColumnText(1);
    out.ciphertextHex = stmt->ColumnText(2);
    stmt->Finalize();
    return true;
}

bool FederationKeys::OpenEpochDek(const NodeIdentity& self,
                                  const std::string& selfNodeId,
                                  int64_t& epochOut,
                                  std::array<unsigned char, 32>& dekOut,
                                  std::string& error) const {
    if (!m_store) { error = "federation keys have no store"; return false; }
    auto stmt = m_store->Prepare(
        "SELECT epoch, source_node, ephemeral_pub, ciphertext "
        "FROM federation_key_envelopes WHERE target_node = ? "
        "ORDER BY epoch DESC LIMIT 1");
    if (!stmt) { error = "prepare failed: " + m_store->ErrMsg(); return false; }
    stmt->BindText(1, selfNodeId);
    if (!stmt->Step()) {
        error = "no key envelope for node '" + selfNodeId + "'";
        return false;
    }
    const int64_t epoch = stmt->ColumnInt64(0);
    const std::string sourceNode = stmt->ColumnText(1);
    const std::string ephHex = stmt->ColumnText(2);
    const std::string ciphertext = stmt->ColumnText(3);
    stmt->Finalize();

    std::array<unsigned char, 32> ephPub{};
    if (!HexToBytes32(ephHex, ephPub)) {
        error = "envelope holds a malformed ephemeral public key";
        return false;
    }
    const std::string context = ContextFor(epoch, selfNodeId, sourceNode, ephHex);
    if (!UnwrapDek(self.private_key, ephPub, context, ciphertext, dekOut, error)) {
        return false;
    }
    epochOut = epoch;
    return true;
}

bool FederationKeys::ListEnvelopes(std::vector<EnvelopeRow>& out,
                                   std::string& error) const {
    if (!m_store) { error = "federation keys have no store"; return false; }
    auto stmt = m_store->Prepare(
        "SELECT epoch, target_node, source_node, ephemeral_pub, ciphertext "
        "FROM federation_key_envelopes ORDER BY epoch, target_node");
    if (!stmt) { error = "prepare failed: " + m_store->ErrMsg(); return false; }
    out.clear();
    while (stmt->Step()) {
        EnvelopeRow r;
        r.epoch = stmt->ColumnInt64(0);
        r.targetNode = stmt->ColumnText(1);
        r.sourceNode = stmt->ColumnText(2);
        r.ephemeralPubHex = stmt->ColumnText(3);
        r.ciphertextHex = stmt->ColumnText(4);
        out.push_back(r);
    }
    stmt->Finalize();
    return true;
}

bool FederationKeys::MaxEpoch(int64_t* epochOut) const {
    if (!m_store) return false;
    auto stmt = m_store->Prepare("SELECT MAX(epoch) FROM federation_key_envelopes");
    if (!stmt) return false;
    if (!stmt->Step()) return false;
    const bool has = !stmt->IsColumnNull(0);
    if (has && epochOut) *epochOut = stmt->ColumnInt64(0);
    stmt->Finalize();
    return has;
}

} // namespace animus::kernel

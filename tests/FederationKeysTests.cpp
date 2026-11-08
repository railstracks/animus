// FederationKeys tests (#274 — fed-secrets 3/5): epoch DEK envelope
// wrap/unwrap, context binding (tamper = auth fail), store roundtrip via a
// node identity, re-seal upsert, loud lockout when no envelope addresses
// this node, and sync-outbox capture (the table rides replication).
#include "animus_kernel/FederationKeys.h"
#include "animus_kernel/NodeIdentity.h"
#include "animus_kernel/CryptoUtils.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/SyncStore.h"
#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/api/SecretsVault.h"

#include <array>
#include <cstring>
#include <iostream>
#include <string>

using namespace animus::kernel;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

std::string MakeTempDbPath() {
    char tmp[] = "/tmp/animus_fed_keys_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

NodeIdentity MakeIdentity() {
    crypto::X25519KeyPair kp;
    std::string err;
    if (!crypto::GenerateX25519KeyPair(kp, err)) {
        std::cerr << "keygen failed: " << err << "\n";
        std::exit(1);
    }
    NodeIdentity id;
    id.private_key = kp.private_key;
    id.public_key = kp.public_key;
    return id;
}

// [1/11] Wrap/unwrap roundtrip + context binding: any edited context field
// changes the derived KEK and GCM auth fails.
void TestWrapUnwrapAndContextBinding() {
    std::cerr << "[1/11] wrap/unwrap + context binding\n";
    NodeIdentity target = MakeIdentity();
    crypto::X25519KeyPair eph;
    std::string err;
    Assert(crypto::GenerateX25519KeyPair(eph, err), "ephemeral keygen: " + err);
    const std::string ephHex = BytesToHex(eph.public_key.data(), 32);
    const std::string ctx = FederationKeys::ContextFor(7, "node-b", "node-a", ephHex);

    std::array<unsigned char, 32> dek{};
    dek[0] = 0xab; dek[31] = 0xcd;
    std::string envelopeHex;
    Assert(FederationKeys::WrapWithEphemeral(eph.private_key, eph.public_key,
                                             target.public_key, dek, ctx,
                                             envelopeHex, err),
           "wrap: " + err);

    std::array<unsigned char, 32> out{};
    Assert(FederationKeys::UnwrapDek(target.private_key, eph.public_key, ctx,
                                     envelopeHex, out, err),
           "unwrap: " + err);
    Assert(std::memcmp(out.data(), dek.data(), 32) == 0, "DEK roundtrip");

    // Tampered epoch -> different context -> auth failure, never garbage.
    const std::string ctx8 = FederationKeys::ContextFor(8, "node-b", "node-a", ephHex);
    Assert(!FederationKeys::UnwrapDek(target.private_key, eph.public_key, ctx8,
                                      envelopeHex, out, err),
           "tampered epoch rejected");
    Assert(!err.empty(), "tamper error message present");

    // Tampered source node likewise.
    const std::string ctxSrc = FederationKeys::ContextFor(7, "node-b", "node-Z", ephHex);
    Assert(!FederationKeys::UnwrapDek(target.private_key, eph.public_key, ctxSrc,
                                      envelopeHex, out, err),
           "tampered source rejected");

    // Wrong private key (another node) -> auth failure.
    NodeIdentity other = MakeIdentity();
    Assert(!FederationKeys::UnwrapDek(other.private_key, eph.public_key, ctx,
                                      envelopeHex, out, err),
           "wrong node key rejected");
}

// [2/11] Store roundtrip: seal DEK for node B's identity; B's boot path
// (OpenEpochDek) recovers it. MaxEpoch reports.
void TestStoreRoundtrip() {
    std::cerr << "[2/11] store roundtrip via identity\n";
    const std::string dbPath = MakeTempDbPath();
    SqliteDataStore db(dbPath);
    FederationKeys keys(&db);
    keys.EnsureSchema();

    NodeIdentity nodeB = MakeIdentity();
    std::array<unsigned char, 32> dek{};
    dek[5] = 0x42;
    std::string err;
    Assert(keys.SealEpochDek(3, "node-b", "node-a", nodeB.public_key, dek, err),
           "seal: " + err);

    int64_t epoch = 0;
    std::array<unsigned char, 32> out{};
    Assert(keys.OpenEpochDek(nodeB, "node-b", epoch, out, err), "open: " + err);
    Assert(epoch == 3, "epoch reported");
    Assert(std::memcmp(out.data(), dek.data(), 32) == 0, "DEK recovered");

    int64_t maxEpoch = -1;
    Assert(keys.MaxEpoch(&maxEpoch) && maxEpoch == 3, "MaxEpoch = 3");
    ::unlink(dbPath.c_str());
}

// [3/11] Re-seal upsert: sealing again replaces the envelope; the new
// envelope opens, and there is exactly one row for (epoch, target).
void TestResealUpsert() {
    std::cerr << "[3/11] re-seal upsert\n";
    const std::string dbPath = MakeTempDbPath();
    SqliteDataStore db(dbPath);
    FederationKeys keys(&db);
    keys.EnsureSchema();

    NodeIdentity nodeB = MakeIdentity();
    std::array<unsigned char, 32> dek1{}, dek2{};
    dek1[0] = 1; dek2[0] = 2;
    std::string err;
    Assert(keys.SealEpochDek(1, "node-b", "node-a", nodeB.public_key, dek1, err),
           "first seal: " + err);
    Assert(keys.SealEpochDek(1, "node-b", "node-a", nodeB.public_key, dek2, err),
           "re-seal: " + err);

    int64_t epoch = 0;
    std::array<unsigned char, 32> out{};
    Assert(keys.OpenEpochDek(nodeB, "node-b", epoch, out, err), "open after re-seal: " + err);
    Assert(std::memcmp(out.data(), dek2.data(), 32) == 0, "latest DEK wins");

    std::vector<FederationKeys::EnvelopeRow> rows;
    Assert(keys.ListEnvelopes(rows, err), "list: " + err);
    Assert(rows.size() == 1, "exactly one row for (epoch, target)");
    ::unlink(dbPath.c_str());
}

// [4/11] Lockout: envelopes exist for OTHER targets only -> OpenEpochDek
// fails loudly (boot logs LOCKED OUT), MaxEpoch still reports the epoch.
void TestLockoutWhenNoOwnEnvelope() {
    std::cerr << "[4/11] lockout — envelopes sealed to others\n";
    const std::string dbPath = MakeTempDbPath();
    SqliteDataStore db(dbPath);
    FederationKeys keys(&db);
    keys.EnsureSchema();

    NodeIdentity nodeB = MakeIdentity(), nodeC = MakeIdentity();
    std::array<unsigned char, 32> dek{};
    std::string err;
    Assert(keys.SealEpochDek(5, "node-b", "node-a", nodeB.public_key, dek, err),
           "seal for B: " + err);
    Assert(keys.SealEpochDek(5, "node-c", "node-a", nodeC.public_key, dek, err),
           "seal for C: " + err);

    NodeIdentity nodeD = MakeIdentity();
    int64_t epoch = 0;
    std::array<unsigned char, 32> out{};
    Assert(!keys.OpenEpochDek(nodeD, "node-d", epoch, out, err),
           "node D locked out");
    Assert(!err.empty(), "lockout error message present");

    int64_t maxEpoch = -1;
    Assert(keys.MaxEpoch(&maxEpoch) && maxEpoch == 5,
           "MaxEpoch sees the epoch D is locked out of");
    ::unlink(dbPath.c_str());
}

// [5/11] Empty table: no envelopes anywhere -> OpenEpochDek errors (loud,
// classified), MaxEpoch returns false (the quiet single-node case).
void TestEmptyTable() {
    std::cerr << "[5/11] empty table\n";
    const std::string dbPath = MakeTempDbPath();
    SqliteDataStore db(dbPath);
    FederationKeys keys(&db);
    keys.EnsureSchema();

    NodeIdentity self = MakeIdentity();
    int64_t epoch = 0;
    std::array<unsigned char, 32> out{};
    std::string err;
    Assert(!keys.OpenEpochDek(self, "node-a", epoch, out, err),
           "no envelope errors");
    Assert(!err.empty(), "error message present");
    Assert(!keys.MaxEpoch(), "MaxEpoch false on empty table");
    ::unlink(dbPath.c_str());
}

// [6/11] Sync capture: the table is in the agent-global set — a seal lands
// in sync_outbox via the installed trigger (federation replication claim).
void TestSyncOutboxCapture() {
    std::cerr << "[6/11] sync outbox capture\n";
    const std::string dbPath = MakeTempDbPath();
    SqliteDataStore db(dbPath);
    FederationKeys keys(&db);
    keys.EnsureSchema();   // table BEFORE SyncStore::EnsureSchema (triggers)

    SyncStore sync(&db, 1);
    std::string syncErr;
    Assert(sync.EnsureSchema(&syncErr), "sync schema: " + syncErr);

    NodeIdentity nodeB = MakeIdentity();
    std::array<unsigned char, 32> dek{};
    std::string err;
    Assert(keys.SealEpochDek(2, "node-b", "node-a", nodeB.public_key, dek, err),
           "seal: " + err);

    auto q = db.Prepare(
        "SELECT row_id, op FROM sync_outbox WHERE table_name = "
        "'federation_key_envelopes' ORDER BY id DESC LIMIT 1");
    Assert(q && q->Step(), "outbox row captured");
    if (q) {
        Assert(q->ColumnText(0) == "2:node-b", "row id = '<epoch>:<target>'");
        Assert(q->ColumnText(1) == "upsert", "op = upsert (unified insert/update trigger)");
    }
    ::unlink(dbPath.c_str());
}

std::string RawVaultCipher(SqliteDataStore& db, const char* name) {
    auto q = db.Prepare(
        "SELECT ciphertext FROM api_package_secrets WHERE name = ?");
    if (!q) return "";
    q->BindText(1, name);
    return q->Step() ? q->ColumnText(0) : "";
}

// --- #276 ceremony fixture: vault + registry + keys on one DB ---------
struct CeremonyFx {
    std::string dbPath, keyPath;
    SqliteDataStore db;
    AgentConfigStore registry;
    SecretsVault vault;
    FederationKeys keys;
    NodeIdentity self;

    CeremonyFx()
        : dbPath(MakeTempDbPath()), keyPath(dbPath + ".vault.key"),
          db(dbPath), registry(&db), vault(&db, keyPath), keys(&db) {
        vault.EnsureSchema();
        keys.EnsureSchema();
        std::string err;
        crypto::X25519KeyPair kp;
        if (!crypto::GenerateX25519KeyPair(kp, err)) std::exit(1);
        self.private_key = kp.private_key;
        self.public_key = kp.public_key;
    }
    ~CeremonyFx() { ::unlink(dbPath.c_str()); ::unlink(keyPath.c_str()); }
};

FederationKeys::CeremonyContext CtxOf(CeremonyFx& fx, const char* selfId) {
    FederationKeys::CeremonyContext ctx;
    ctx.keys = &fx.keys;
    ctx.vault = &fx.vault;
    ctx.registry = &fx.registry;
    ctx.self = &fx.self;
    ctx.selfNodeId = selfId;
    return ctx;
}

// [7/11] First ceremony: approve establishes epoch 1, re-seals the vault's
// master-key-era rows, seals the DEK to self + the new member — who
// unwraps it with its own identity.
void TestApproveFirstCeremony() {
    std::cerr << "[7/11] approve ceremony — first: epoch 1 + vault re-seal\n";
    CeremonyFx fx;
    std::string err;
    Assert(fx.vault.Set("pkg", "tok", "secret-1", err), "master-key era store");
    Assert(RawVaultCipher(fx.db, "tok").compare(0, 2, "01") == 0,
           "pre-ceremony row is v01");

    NodeIdentity nodeB = MakeIdentity();
    auto ctx = CtxOf(fx, "1");
    const int64_t epoch =
        fx.keys.RunApproveNodeCeremony(ctx, "2", nodeB.public_key, err);
    Assert(epoch == 1, "first ceremony establishes epoch 1: " + err);
    Assert(fx.vault.HasFederationDek() && fx.vault.FederationEpoch() == 1,
           "vault armed at epoch 1");
    Assert(RawVaultCipher(fx.db, "tok").compare(0, 2, "02") == 0,
           "vault row re-sealed to v02");
    Assert(fx.vault.Get("pkg", "tok").value_or("") == "secret-1",
           "value intact after rotation");

    std::vector<FederationKeys::EnvelopeRow> rows;
    Assert(fx.keys.ListEnvelopes(rows, err), "list envelopes");
    Assert(rows.size() == 2, "self + new member sealed (got " +
           std::to_string(rows.size()) + ")");

    int64_t eB = 0;
    std::array<unsigned char, 32> dekB{};
    Assert(fx.keys.OpenEpochDek(nodeB, "2", eB, dekB, err),
           "new member unwraps: " + err);
    Assert(eB == 1, "member sees epoch 1");
    Assert(std::memcmp(dekB.data(), fx.vault.FederationDekRef().data(), 32) == 0,
           "member recovered the network DEK");
}

// [8/11] Established-member join: a third member joins at the CURRENT
// epoch — no rotation, envelopes for all members, joiner unwraps.
void TestApproveEstablishedJoin() {
    std::cerr << "[8/11] approve ceremony — established member join\n";
    CeremonyFx fx;
    std::string err;
    NodeIdentity b = MakeIdentity(), c = MakeIdentity();
    auto ctx = CtxOf(fx, "1");
    Assert(fx.keys.RunApproveNodeCeremony(ctx, "2", b.public_key, err) == 1,
           "member 2 approved at epoch 1");
    Assert(fx.keys.RunApproveNodeCeremony(ctx, "3", c.public_key, err) == 1,
           "member 3 joins at the same epoch");
    Assert(fx.vault.FederationEpoch() == 1, "join did not rotate");

    std::vector<FederationKeys::EnvelopeRow> rows;
    fx.keys.ListEnvelopes(rows, err);
    Assert(rows.size() == 3, "three member envelopes");

    int64_t eC = 0;
    std::array<unsigned char, 32> dekC{};
    Assert(fx.keys.OpenEpochDek(c, "3", eC, dekC, err), "joiner unwraps: " + err);
    Assert(std::memcmp(dekC.data(), fx.vault.FederationDekRef().data(), 32) == 0,
           "joiner holds the network DEK");
}

// [9/11] Locked-out guard: envelopes exist but this node holds no DEK —
// approve must refuse to mint a fresh epoch-1 DEK (regression guard).
void TestLockedOutApproveGuard() {
    std::cerr << "[9/11] locked-out node cannot mint a new epoch\n";
    CeremonyFx fx;
    std::string err;
    NodeIdentity b = MakeIdentity(), c = MakeIdentity();
    // Envelope for epoch 1 exists (sealed directly, no vault arming).
    std::array<unsigned char, 32> alienDek{};
    Assert(fx.keys.SealEpochDek(1, "2", "1", b.public_key, alienDek, err),
           "seed an epoch-1 envelope");
    Assert(!fx.vault.HasFederationDek(), "vault not armed");

    auto ctx = CtxOf(fx, "1");
    const int64_t r =
        fx.keys.RunApproveNodeCeremony(ctx, "3", c.public_key, err);
    Assert(r == -1, "locked-out approve refused");
    Assert(err.find("locked out") != std::string::npos,
           "guard message names the state: " + err);
    Assert(!fx.vault.HasFederationDek(), "no DEK minted");
}

// [10/11] Revoke ceremony: member dropped, epoch rotates, remaining
// members stay green, the revoked member's stale DEK fails on the new
// ciphertext — exclusivity going forward.
void TestRevokeCeremony() {
    std::cerr << "[10/11] revoke ceremony — rotation cuts the member off\n";
    CeremonyFx fx;
    std::string err;
    NodeIdentity b = MakeIdentity(), c = MakeIdentity();
    auto ctx = CtxOf(fx, "1");
    Assert(fx.vault.Set("pkg", "pre", "pre-secret", err), "pre-era secret");
    Assert(fx.keys.RunApproveNodeCeremony(ctx, "2", b.public_key, err) == 1,
           "member 2 approved");
    Assert(fx.keys.RunApproveNodeCeremony(ctx, "3", c.public_key, err) == 1,
           "member 3 approved");
    Assert(fx.vault.Set("pkg", "post", "post-secret", err), "epoch-1-era secret");

    // Member 3 holds the epoch-1 DEK (its pre-revoke state).
    int64_t eC = 0;
    std::array<unsigned char, 32> staleDek{};
    Assert(fx.keys.OpenEpochDek(c, "3", eC, staleDek, err), "C unwraps epoch 1");

    const int64_t newEpoch = fx.keys.RunRevokeNodeCeremony(ctx, "3", err);
    Assert(newEpoch == 2, "epoch rotated to 2: " + err);
    Assert(fx.registry.Get("", PeerNodeKeyRow("3")).empty(),
           "registry row dropped");

    std::vector<FederationKeys::EnvelopeRow> rows;
    fx.keys.ListEnvelopes(rows, err);
    bool sawC = false, sawB2 = false, sawSelf2 = false;
    for (const auto& r : rows) {
        if (r.targetNode == "3") sawC = true;
        if (r.targetNode == "2" && r.epoch == 2) sawB2 = true;
        if (r.targetNode == "1" && r.epoch == 2) sawSelf2 = true;
    }
    Assert(!sawC, "no envelopes for the revoked member");
    Assert(sawB2 && sawSelf2, "remaining members sealed at epoch 2");

    // Remaining member stays green.
    int64_t eB = 0;
    std::array<unsigned char, 32> dekB{};
    Assert(fx.keys.OpenEpochDek(b, "2", eB, dekB, err) && eB == 2,
           "member 2 unwraps epoch 2");
    // All rows re-sealed + readable under epoch 2.
    Assert(fx.vault.Get("pkg", "pre").value_or("") == "pre-secret" &&
           fx.vault.Get("pkg", "post").value_or("") == "post-secret",
           "values intact after rotation");
    Assert(RawVaultCipher(fx.db, "pre").compare(0, 2, "02") == 0 &&
           RawVaultCipher(fx.db, "post").compare(0, 2, "02") == 0,
           "all rows are epoch-era");

    // The revoked member's stale epoch-1 DEK fails on epoch-2 ciphertext.
    std::string out, openErr;
    const std::vector<unsigned char> staleDekVec(staleDek.begin(), staleDek.end());
    Assert(!crypto::Aes256GcmOpen(staleDekVec, RawVaultCipher(fx.db, "pre"),
                                  out, openErr),
           "stale DEK fails on epoch-2 ciphertext");
}

// [11/11] Malformed registry row: the ceremony fails loudly naming the
// node; fixing the registry and re-running succeeds (idempotent re-run
// path — the interrupted pass already armed the DEK).
void TestMalformedRegistryGuard() {
    std::cerr << "[11/11] malformed registry row — loud fail, re-run green\n";
    CeremonyFx fx;
    std::string err;
    fx.registry.Set("", PeerNodeKeyRow("9"), "not-hex");
    NodeIdentity b = MakeIdentity();
    auto ctx = CtxOf(fx, "1");
    Assert(fx.keys.RunApproveNodeCeremony(ctx, "2", b.public_key, err) == -1,
           "malformed registry fails the ceremony");
    Assert(err.find("malformed") != std::string::npos,
           "error names the bad row: " + err);

    // Fix the registry; the re-run seals the already-armed DEK.
    std::array<unsigned char, 32> pub9{};
    pub9[0] = 9;
    Assert(StorePeerNodeKey(fx.registry, "9", pub9, err), "registry fixed");
    const int64_t epoch =
        fx.keys.RunApproveNodeCeremony(ctx, "2", b.public_key, err);
    Assert(epoch == 1, "re-run succeeds at epoch 1: " + err);
    std::vector<FederationKeys::EnvelopeRow> rows;
    fx.keys.ListEnvelopes(rows, err);
    Assert(rows.size() == 3, "self + member 2 + repaired member 9");
}

} // namespace

int main() {
    TestWrapUnwrapAndContextBinding();
    TestStoreRoundtrip();
    TestResealUpsert();
    TestLockoutWhenNoOwnEnvelope();
    TestEmptyTable();
    TestSyncOutboxCapture();
    TestApproveFirstCeremony();
    TestApproveEstablishedJoin();
    TestLockedOutApproveGuard();
    TestRevokeCeremony();
    TestMalformedRegistryGuard();
    if (g_failures == 0) std::cerr << "FederationKeysTests: all passed\n";
    return g_failures == 0 ? 0 : 1;
}

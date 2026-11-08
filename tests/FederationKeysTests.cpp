// FederationKeys tests (#274 — fed-secrets 3/5): epoch DEK envelope
// wrap/unwrap, context binding (tamper = auth fail), store roundtrip via a
// node identity, re-seal upsert, loud lockout when no envelope addresses
// this node, and sync-outbox capture (the table rides replication).
#include "animus_kernel/FederationKeys.h"
#include "animus_kernel/NodeIdentity.h"
#include "animus_kernel/CryptoUtils.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/SyncStore.h"

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

// [1/6] Wrap/unwrap roundtrip + context binding: any edited context field
// changes the derived KEK and GCM auth fails.
void TestWrapUnwrapAndContextBinding() {
    std::cerr << "[1/6] wrap/unwrap + context binding\n";
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

// [2/6] Store roundtrip: seal DEK for node B's identity; B's boot path
// (OpenEpochDek) recovers it. MaxEpoch reports.
void TestStoreRoundtrip() {
    std::cerr << "[2/6] store roundtrip via identity\n";
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

// [3/6] Re-seal upsert: sealing again replaces the envelope; the new
// envelope opens, and there is exactly one row for (epoch, target).
void TestResealUpsert() {
    std::cerr << "[3/6] re-seal upsert\n";
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

// [4/6] Lockout: envelopes exist for OTHER targets only -> OpenEpochDek
// fails loudly (boot logs LOCKED OUT), MaxEpoch still reports the epoch.
void TestLockoutWhenNoOwnEnvelope() {
    std::cerr << "[4/6] lockout — envelopes sealed to others\n";
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

// [5/6] Empty table: no envelopes anywhere -> OpenEpochDek errors (loud,
// classified), MaxEpoch returns false (the quiet single-node case).
void TestEmptyTable() {
    std::cerr << "[5/6] empty table\n";
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

// [6/6] Sync capture: the table is in the agent-global set — a seal lands
// in sync_outbox via the installed trigger (federation replication claim).
void TestSyncOutboxCapture() {
    std::cerr << "[6/6] sync outbox capture\n";
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

} // namespace

int main() {
    TestWrapUnwrapAndContextBinding();
    TestStoreRoundtrip();
    TestResealUpsert();
    TestLockoutWhenNoOwnEnvelope();
    TestEmptyTable();
    TestSyncOutboxCapture();
    if (g_failures == 0) std::cerr << "FederationKeysTests: all passed\n";
    return g_failures == 0 ? 0 : 1;
}

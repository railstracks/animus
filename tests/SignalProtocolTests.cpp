#include "animus_kernel/signal/SignalTypes.h"
#include "animus_kernel/signal/SignalStore.h"
#include "animus_kernel/signal/SignalCrypto.h"
#include "animus_kernel/signal/SignalSessionManager.h"

#include <iostream>
#include <string>
#include <cassert>

using namespace animus::signal;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

// ============================================================================
// SignalTypes tests
// ============================================================================

void TestSignalAddress() {
    SignalAddress a1{"user@s.whatsapp.net", 0};
    SignalAddress a2{"user@s.whatsapp.net", 1};
    SignalAddress a3{"user@s.whatsapp.net", 0};

    Assert(a1 == a3, "same address should be equal");
    Assert(!(a1 == a2), "different device_id should not be equal");
    Assert(a1 < a2, "device_id=0 < device_id=1");
    Assert(a1.to_string() == "user@s.whatsapp.net", "default device to_string");
    Assert(a2.to_string() == "user@s.whatsapp.net.1", "device_id=1 to_string");
}

void TestSignalKeypair() {
    auto kp = SignalKeypair::generate();
    // Real X25519 keypair — just verify non-zero
    bool all_zero_pub = true, all_zero_priv = true;
    for (int i = 0; i < 32; ++i) {
        if (kp.public_key[i] != 0) all_zero_pub = false;
        if (kp.private_key[i] != 0) all_zero_priv = false;
    }
    Assert(!all_zero_pub, "public key not all zeros");
    Assert(!all_zero_priv, "private key not all zeros");
    Assert(kp == kp, "keypair self-equality");
}

void TestSignalPreKeySerialization() {
    SignalPreKey pk;
    pk.id = 42;
    pk.keypair = SignalKeypair::generate();

    auto data = pk.serialize();
    auto pk2 = SignalPreKey::deserialize(data.data(), data.size());

    Assert(pk2.id == 42, "pre-key id round-trip");
    Assert(pk2.keypair.public_key == pk.keypair.public_key, "pre-key pubkey round-trip");
    Assert(pk2.keypair.private_key == pk.keypair.private_key, "pre-key privkey round-trip");
}

void TestSignalSignedPreKeySerialization() {
    SignalSignedPreKey spk;
    spk.id = 7;
    spk.keypair = SignalKeypair::generate();
    spk.signature = {0xAA, 0xBB, 0xCC, 0xDD};

    auto data = spk.serialize();
    auto spk2 = SignalSignedPreKey::deserialize(data.data(), data.size());

    Assert(spk2.id == 7, "signed pre-key id round-trip");
    Assert(spk2.signature == spk.signature, "signature round-trip");
}

void TestSessionRecordSerialization() {
    SessionRecord r;
    r.version = 2;
    r.is_fresh = false;
    r.state = {0x01, 0x02, 0x03, 0x04, 0x05};

    auto data = r.serialize();
    auto r2 = SessionRecord::deserialize(data.data(), data.size());

    Assert(r2.version == 2, "session record version round-trip");
    Assert(!r2.is_fresh, "is_fresh round-trip");
    Assert(r2.state == r.state, "session state round-trip");
}

void TestEncryptedMessageSerialization() {
    EncryptedMessage msg;
    msg.type = SignalMessageType::PreKey;
    msg.ciphertext = {0xDE, 0xAD, 0xBE, 0xEF};
    msg.header = {0x01, 0x02};
    msg.counter = 5;

    auto data = msg.serialize();
    auto msg2 = EncryptedMessage::deserialize(data.data(), data.size());

    Assert(msg2.type == SignalMessageType::PreKey, "msg type round-trip");
    Assert(msg2.ciphertext == msg.ciphertext, "ciphertext round-trip");
    Assert(msg2.header == msg.header, "header round-trip");
    Assert(msg2.counter == 5, "counter round-trip");
}

void TestGroupEncryptedMessageSerialization() {
    GroupEncryptedMessage msg;
    msg.group_id = "123456@g.us";
    msg.sender = SignalAddress{"user@s.whatsapp.net", 0};
    msg.ciphertext = {0xCA, 0xFE};
    msg.counter = 3;

    auto data = msg.serialize();
    auto msg2 = GroupEncryptedMessage::deserialize(data.data(), data.size());

    Assert(msg2.group_id == "123456@g.us", "group_id round-trip");
    Assert(msg2.sender.name == "user@s.whatsapp.net", "sender name round-trip");
    Assert(msg2.ciphertext == msg.ciphertext, "group ciphertext round-trip");
    Assert(msg2.counter == 3, "group counter round-trip");
}

// ============================================================================
// InMemoryStore tests
// ============================================================================

void TestInMemorySessionStore() {
    InMemorySessionStore store;
    SignalAddress addr{"alice@s.whatsapp.net", 1};

    Assert(!store.contains_session(addr), "empty store has no session");

    SessionRecord rec;
    rec.version = 1;
    rec.is_fresh = true;
    rec.state = {0x01, 0x02};
    store.store_session(addr, rec);

    Assert(store.contains_session(addr), "session exists after store");

    auto loaded = store.load_session(addr);
    Assert(loaded.has_value(), "load returns value");
    Assert(loaded->version == 1, "loaded version matches");
    Assert(loaded->state == rec.state, "loaded state matches");

    // Sub-device sessions
    SignalAddress addr2{"alice@s.whatsapp.net", 2};
    store.store_session(addr2, rec);
    auto devices = store.get_sub_device_sessions("alice@s.whatsapp.net");
    Assert(devices.size() == 2, "two sub-device sessions");
    Assert(devices[0] == 1 && devices[1] == 2, "correct device IDs");

    store.delete_session(addr);
    Assert(!store.contains_session(addr), "session gone after delete");
    Assert(store.contains_session(addr2), "other session still exists");

    store.delete_all_sessions("alice@s.whatsapp.net");
    Assert(!store.contains_session(addr2), "all sessions deleted");
}

void TestInMemoryPreKeyStore() {
    InMemoryPreKeyStore store;

    Assert(store.pre_key_count() == 0, "empty store has 0 keys");

    SignalPreKey pk1;
    pk1.id = store.next_pre_key_id();
    pk1.keypair = SignalKeypair::generate();
    store.store_pre_key(pk1);

    SignalPreKey pk2;
    pk2.id = store.next_pre_key_id();
    pk2.keypair = SignalKeypair::generate();
    store.store_pre_key(pk2);

    Assert(store.pre_key_count() == 2, "two keys stored");
    Assert(store.contains_pre_key(1), "key 1 exists");
    Assert(store.contains_pre_key(2), "key 2 exists");

    auto loaded = store.load_pre_key(1);
    Assert(loaded.has_value(), "load key 1");
    Assert(loaded->id == 1, "loaded key id");

    store.remove_pre_key(1);
    Assert(!store.contains_pre_key(1), "key 1 removed");
    Assert(store.pre_key_count() == 1, "one key remaining");
}

void TestInMemorySignedPreKeyStore() {
    InMemorySignedPreKeyStore store;

    SignalSignedPreKey spk;
    spk.id = 1;
    spk.keypair = SignalKeypair::generate();
    spk.signature = {0xFF};
    store.store_signed_pre_key(spk);

    Assert(store.contains_signed_pre_key(1), "signed pre-key exists");
    Assert(store.current_signed_pre_key_id() == 1, "current id = 1");

    auto loaded = store.load_signed_pre_key(1);
    Assert(loaded.has_value(), "load signed pre-key");
    Assert(loaded->signature == spk.signature, "signature matches");
}

void TestInMemoryIdentityKeyStore() {
    auto identity = SignalIdentityKey::generate(42);
    InMemoryIdentityKeyStore store(identity);

    Assert(store.get_local_registration_id() == 42, "registration id");
    auto kp = store.get_identity_key_pair();
    Assert(kp == identity.keypair, "identity keypair matches");

    SignalAddress remote{"bob@s.whatsapp.net", 0};
    // First save is new (any key)
    auto key1 = SignalKeypair::generate().public_key;
    bool was_new = store.save_identity(remote, key1);
    Assert(was_new, "first save is new");
    Assert(store.is_trusted_identity(remote, key1), "saved identity is trusted");

    auto loaded = store.load_identity(remote);
    Assert(loaded.has_value(), "load identity");
    Assert(*loaded == key1, "loaded key matches");

    // A different key should not be trusted
    auto key2 = SignalKeypair::generate().public_key;
    Assert(!store.is_trusted_identity(remote, key2), "different key not trusted");
}

void TestInMemorySenderKeyStore() {
    InMemorySenderKeyStore store;
    SignalAddress sender{"alice@s.whatsapp.net", 0};
    std::string group = "123456@g.us";

    Assert(!store.contains_sender_key(sender, group), "no sender key initially");

    SenderKeyRecord rec;
    rec.group_id = group;
    rec.sender = sender;
    rec.iteration = 1;
    rec.chain_key = {0xAB};
    store.store_sender_key(sender, group, rec);

    Assert(store.contains_sender_key(sender, group), "sender key exists after store");

    auto loaded = store.load_sender_key(sender, group);
    Assert(loaded.has_value(), "load sender key");
    Assert(loaded->iteration == 1, "iteration matches");
    Assert(loaded->chain_key == rec.chain_key, "chain key matches");
}

// ============================================================================
// SignalCrypto (stub) tests
// ============================================================================

void TestStubEncryptDecrypt1to1() {
    // Simulate Alice (initiator) and Bob (recipient) with separate identity stores
    auto aliceIdentity = SignalIdentityKey::generate(1);
    auto bobIdentity = SignalIdentityKey::generate(2);

    InMemorySessionStore aliceSessions, bobSessions;
    InMemoryIdentityKeyStore aliceIdStore(aliceIdentity);
    InMemoryIdentityKeyStore bobIdStore(bobIdentity);

    SignalAddress bobAddr{"bob@s.whatsapp.net", 0};
    SignalAddress aliceAddr{"alice@s.whatsapp.net", 0};

    // Alice initializes session with Bob's pre-key bundle
    PreKeyBundle bobBundle;
    bobBundle.device_id = 0;
    bobBundle.signed_pre_key.id = 1;
    bobBundle.signed_pre_key.keypair = bobIdentity.keypair;
    bobBundle.identity_key.public_key = bobIdentity.keypair.public_key;

    auto init = initialize_session(aliceSessions, aliceIdStore, bobAddr, bobBundle);
    Assert(init.ok, "session init success: " + (init.ok ? "" : init.error));
    Assert(aliceSessions.contains_session(bobAddr), "session stored");

    // Alice encrypts a message to Bob
    std::string plaintext = "Hello, World!";
    auto encrypted = encrypt_1to1(aliceSessions, aliceIdStore, bobAddr, plaintext);
    Assert(encrypted.ok, "encrypt success");
    Assert(encrypted.value.type == SignalMessageType::Whisper, "type is Whisper");
    Assert(!encrypted.value.ciphertext.empty(), "ciphertext not empty");

    // For now, just verify encrypt works — full decrypt requires Bob's session
    // which needs the X3DH shared secret from Bob's side.
    // The round-trip will be tested when we have full X3DH with both sides.
    std::cerr << "[test] Alice→Bob encrypt OK (" << plaintext.size() << " bytes)" << std::endl;
}

void TestStubEncryptDecryptGroup() {
    InMemorySenderKeyStore sender_keys;
    SignalAddress sender{"alice@s.whatsapp.net", 0};
    std::string group = "group123@g.us";

    std::string plaintext = "Group message!";

    auto encrypted = encrypt_group(sender_keys, sender, group, plaintext);
    Assert(encrypted.ok, "group encrypt success");
    Assert(encrypted.value.group_id == group, "group_id in encrypted msg");
    Assert(sender_keys.contains_sender_key(sender, group), "sender key created");

    auto decrypted = decrypt_group(sender_keys, sender, group, encrypted.value);
    Assert(decrypted.ok, "group decrypt success: " + (decrypted.ok ? "" : decrypted.error));
    Assert(decrypted.value == plaintext, "group plaintext round-trip matches");
}

void TestStubPreKeyGeneration() {
    auto keys = generate_pre_keys(100, 5);
    Assert(keys.size() == 5, "5 pre-keys generated");
    Assert(keys[0].id == 100, "first key id = 100");
    Assert(keys[4].id == 104, "last key id = 104");
}

void TestStubSignedPreKeyGeneration() {
    auto identity = SignalKeypair::generate();
    auto spk = generate_signed_pre_key(7, identity);
    Assert(spk.id == 7, "signed pre-key id");
    Assert(spk.signature.size() == 32, "signature is 32 bytes (HMAC-SHA256)");
}

// ============================================================================
// SignalSessionManager tests
// ============================================================================

void TestSessionManager1to1() {
    auto mgr = create_in_memory_manager();

    SignalAddress remote{"charlie@s.whatsapp.net", 0};
    PreKeyBundle bundle;
    bundle.device_id = 0;
    bundle.signed_pre_key.id = 1;
    bundle.signed_pre_key.keypair = SignalKeypair::generate();
    bundle.identity_key = SignalKeypair::generate();

    Assert(!mgr->has_session(remote), "no session initially");

    auto init = mgr->establish_session(remote, bundle);
    Assert(init.ok, "establish session");
    Assert(mgr->has_session(remote), "session exists after establish");

    // Encrypt (sender side)
    std::string plaintext = "Test via manager";
    auto encrypted = mgr->encrypt(remote, plaintext);
    Assert(encrypted.ok, "manager encrypt");

    // Can't decrypt our own messages in Double Ratchet — verified encrypt works
    std::cerr << "[test] Manager encrypt OK (" << plaintext.size() << " bytes)" << std::endl;
}

void TestSessionManagerGroup() {
    auto mgr = create_in_memory_manager();

    SignalAddress sender{"alice@s.whatsapp.net", 0};
    std::string group = "team@g.us";

    auto encrypted = mgr->group_encrypt(sender, group, "Hello group");
    Assert(encrypted.ok, "manager group encrypt");

    auto decrypted = mgr->group_decrypt(sender, group, encrypted.value);
    Assert(decrypted.ok, "manager group decrypt");
    Assert(decrypted.value == "Hello group", "manager group round-trip");
}

void TestSessionManagerPreKeyLifecycle() {
    auto mgr = create_in_memory_manager();

    auto keys = mgr->generate_pre_keys(10);
    Assert(keys.size() == 10, "generated 10 pre-keys");
    Assert(mgr->pre_keys().pre_key_count() == 10, "stored in pre-key store");

    auto spk = mgr->rotate_signed_pre_key();
    Assert(spk.id > 0, "signed pre-key has id");
    Assert(mgr->signed_pre_keys().contains_signed_pre_key(spk.id), "stored in signed pre-key store");
}

// ============================================================================
// Main
// ============================================================================

} // anonymous namespace

int main() {
    std::cerr << "=== Signal Protocol Tests ===\n";

    // Types
    TestSignalAddress();
    TestSignalKeypair();
    TestSignalPreKeySerialization();
    TestSignalSignedPreKeySerialization();
    TestSessionRecordSerialization();
    TestEncryptedMessageSerialization();
    TestGroupEncryptedMessageSerialization();
    std::cerr << "[types] passed\n";

    // Stores
    TestInMemorySessionStore();
    TestInMemoryPreKeyStore();
    TestInMemorySignedPreKeyStore();
    TestInMemoryIdentityKeyStore();
    TestInMemorySenderKeyStore();
    std::cerr << "[stores] passed\n";

    // Crypto stubs
    TestStubEncryptDecrypt1to1();
    TestStubEncryptDecryptGroup();
    TestStubPreKeyGeneration();
    TestStubSignedPreKeyGeneration();
    std::cerr << "[crypto] passed\n";

    // SessionManager
    TestSessionManager1to1();
    TestSessionManagerGroup();
    TestSessionManagerPreKeyLifecycle();
    std::cerr << "[manager] passed\n";

    if (g_failures > 0) {
        std::cerr << "\nFAILED: " << g_failures << " assertions\n";
        return 1;
    }

    std::cerr << "\nAll tests passed!\n";
    return 0;
}

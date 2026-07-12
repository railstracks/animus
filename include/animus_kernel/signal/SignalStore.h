#pragma once

#include "animus_kernel/signal/SignalTypes.h"
#include <unordered_map>
#include <mutex>
#include <functional>

namespace animus::signal {

// ─── Abstract store interfaces ───
// Each store is a separate concern following libsignal's architecture.
// Concrete implementations: InMemorySignalStore (testing), SQLiteSignalStore (production).

// --- Session Store ---

class ISessionStore {
public:
    virtual ~ISessionStore() = default;

    virtual std::optional<SessionRecord> load_session(const SignalAddress& addr) = 0;
    virtual void store_session(const SignalAddress& addr, const SessionRecord& record) = 0;
    virtual bool contains_session(const SignalAddress& addr) = 0;
    virtual void delete_session(const SignalAddress& addr) = 0;
    virtual void delete_all_sessions(const std::string& name) = 0;
    virtual std::vector<uint32_t> get_sub_device_sessions(const std::string& name) = 0;
};

// --- Pre-key Store ---

class IPreKeyStore {
public:
    virtual ~IPreKeyStore() = default;

    virtual std::optional<SignalPreKey> load_pre_key(uint32_t id) = 0;
    virtual void store_pre_key(const SignalPreKey& key) = 0;
    virtual bool contains_pre_key(uint32_t id) = 0;
    virtual void remove_pre_key(uint32_t id) = 0;
    virtual uint32_t next_pre_key_id() = 0;
    virtual size_t pre_key_count() = 0;
};

// --- Signed Pre-key Store ---

class ISignedPreKeyStore {
public:
    virtual ~ISignedPreKeyStore() = default;

    virtual std::optional<SignalSignedPreKey> load_signed_pre_key(uint32_t id) = 0;
    virtual void store_signed_pre_key(const SignalSignedPreKey& key) = 0;
    virtual bool contains_signed_pre_key(uint32_t id) = 0;
    virtual uint32_t current_signed_pre_key_id() = 0;
};

// --- Identity Key Store ---

class IIdentityKeyStore {
public:
    virtual ~IIdentityKeyStore() = default;

    virtual SignalKeypair get_identity_key_pair() = 0;
    virtual uint32_t get_local_registration_id() = 0;
    virtual bool save_identity(const SignalAddress& addr, const std::array<uint8_t, 32>& key) = 0;
    virtual bool is_trusted_identity(const SignalAddress& addr, const std::array<uint8_t, 32>& key) = 0;
    virtual std::optional<std::array<uint8_t, 32>> load_identity(const SignalAddress& addr) = 0;
};

// --- Sender Key Store ---

class ISenderKeyStore {
public:
    virtual ~ISenderKeyStore() = default;

    virtual std::optional<SenderKeyRecord> load_sender_key(const SignalAddress& addr, const std::string& group_id) = 0;
    virtual void store_sender_key(const SignalAddress& addr, const std::string& group_id, const SenderKeyRecord& record) = 0;
    virtual bool contains_sender_key(const SignalAddress& addr, const std::string& group_id) = 0;
};

// ─── In-memory implementation (testing / development) ───

class InMemorySessionStore : public ISessionStore {
public:
    std::optional<SessionRecord> load_session(const SignalAddress& addr) override;
    void store_session(const SignalAddress& addr, const SessionRecord& record) override;
    bool contains_session(const SignalAddress& addr) override;
    void delete_session(const SignalAddress& addr) override;
    void delete_all_sessions(const std::string& name) override;
    std::vector<uint32_t> get_sub_device_sessions(const std::string& name) override;
private:
    std::unordered_map<SignalAddress, SessionRecord> sessions_;
    std::mutex mutex_;
};

class InMemoryPreKeyStore : public IPreKeyStore {
public:
    std::optional<SignalPreKey> load_pre_key(uint32_t id) override;
    void store_pre_key(const SignalPreKey& key) override;
    bool contains_pre_key(uint32_t id) override;
    void remove_pre_key(uint32_t id) override;
    uint32_t next_pre_key_id() override;
    size_t pre_key_count() override;
private:
    std::unordered_map<uint32_t, SignalPreKey> keys_;
    uint32_t next_id_ = 1;
    std::mutex mutex_;
};

class InMemorySignedPreKeyStore : public ISignedPreKeyStore {
public:
    std::optional<SignalSignedPreKey> load_signed_pre_key(uint32_t id) override;
    void store_signed_pre_key(const SignalSignedPreKey& key) override;
    bool contains_signed_pre_key(uint32_t id) override;
    uint32_t current_signed_pre_key_id() override;
private:
    std::unordered_map<uint32_t, SignalSignedPreKey> keys_;
    uint32_t current_id_ = 0;
    std::mutex mutex_;
};

class InMemoryIdentityKeyStore : public IIdentityKeyStore {
public:
    explicit InMemoryIdentityKeyStore(SignalIdentityKey identity);
    SignalKeypair get_identity_key_pair() override;
    uint32_t get_local_registration_id() override;
    bool save_identity(const SignalAddress& addr, const std::array<uint8_t, 32>& key) override;
    bool is_trusted_identity(const SignalAddress& addr, const std::array<uint8_t, 32>& key) override;
    std::optional<std::array<uint8_t, 32>> load_identity(const SignalAddress& addr) override;
private:
    SignalIdentityKey identity_;
    std::unordered_map<SignalAddress, std::array<uint8_t, 32>> trusted_;
    std::mutex mutex_;
};

class InMemorySenderKeyStore : public ISenderKeyStore {
public:
    using Key = std::pair<SignalAddress, std::string>;
    struct KeyHash {
        size_t operator()(const Key& k) const {
            size_t h = std::hash<SignalAddress>()(k.first);
            h ^= std::hash<std::string>()(k.second) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    std::optional<SenderKeyRecord> load_sender_key(const SignalAddress& addr, const std::string& group_id) override;
    void store_sender_key(const SignalAddress& addr, const std::string& group_id, const SenderKeyRecord& record) override;
    bool contains_sender_key(const SignalAddress& addr, const std::string& group_id) override;
private:
    std::unordered_map<Key, SenderKeyRecord, KeyHash> keys_;
    std::mutex mutex_;
};

} // namespace animus::signal

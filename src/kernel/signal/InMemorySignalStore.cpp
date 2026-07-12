#include "animus_kernel/signal/SignalStore.h"

#include <algorithm>

namespace animus::signal {

// ─── InMemorySessionStore ───

std::optional<SessionRecord> InMemorySessionStore::load_session(const SignalAddress& addr) {
    std::lock_guard lock(mutex_);
    auto it = sessions_.find(addr);
    if (it == sessions_.end()) return std::nullopt;
    return it->second;
}

void InMemorySessionStore::store_session(const SignalAddress& addr, const SessionRecord& record) {
    std::lock_guard lock(mutex_);
    sessions_[addr] = record;
}

bool InMemorySessionStore::contains_session(const SignalAddress& addr) {
    std::lock_guard lock(mutex_);
    return sessions_.find(addr) != sessions_.end();
}

void InMemorySessionStore::delete_session(const SignalAddress& addr) {
    std::lock_guard lock(mutex_);
    sessions_.erase(addr);
}

void InMemorySessionStore::delete_all_sessions(const std::string& name) {
    std::lock_guard lock(mutex_);
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        if (it->first.name == name) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<uint32_t> InMemorySessionStore::get_sub_device_sessions(const std::string& name) {
    std::lock_guard lock(mutex_);
    std::vector<uint32_t> devices;
    for (auto& [addr, _] : sessions_) {
        if (addr.name == name && addr.device_id != 0) {
            devices.push_back(addr.device_id);
        }
    }
    std::sort(devices.begin(), devices.end());
    return devices;
}

// ─── InMemoryPreKeyStore ───

std::optional<SignalPreKey> InMemoryPreKeyStore::load_pre_key(uint32_t id) {
    std::lock_guard lock(mutex_);
    auto it = keys_.find(id);
    if (it == keys_.end()) return std::nullopt;
    return it->second;
}

void InMemoryPreKeyStore::store_pre_key(const SignalPreKey& key) {
    std::lock_guard lock(mutex_);
    keys_[key.id] = key;
}

bool InMemoryPreKeyStore::contains_pre_key(uint32_t id) {
    std::lock_guard lock(mutex_);
    return keys_.find(id) != keys_.end();
}

void InMemoryPreKeyStore::remove_pre_key(uint32_t id) {
    std::lock_guard lock(mutex_);
    keys_.erase(id);
}

uint32_t InMemoryPreKeyStore::next_pre_key_id() {
    std::lock_guard lock(mutex_);
    return next_id_++;
}

size_t InMemoryPreKeyStore::pre_key_count() {
    std::lock_guard lock(mutex_);
    return keys_.size();
}

// ─── InMemorySignedPreKeyStore ───

std::optional<SignalSignedPreKey> InMemorySignedPreKeyStore::load_signed_pre_key(uint32_t id) {
    std::lock_guard lock(mutex_);
    auto it = keys_.find(id);
    if (it == keys_.end()) return std::nullopt;
    return it->second;
}

void InMemorySignedPreKeyStore::store_signed_pre_key(const SignalSignedPreKey& key) {
    std::lock_guard lock(mutex_);
    keys_[key.id] = key;
    if (key.id > current_id_) current_id_ = key.id;
}

bool InMemorySignedPreKeyStore::contains_signed_pre_key(uint32_t id) {
    std::lock_guard lock(mutex_);
    return keys_.find(id) != keys_.end();
}

uint32_t InMemorySignedPreKeyStore::current_signed_pre_key_id() {
    std::lock_guard lock(mutex_);
    return current_id_;
}

// ─── InMemoryIdentityKeyStore ───

InMemoryIdentityKeyStore::InMemoryIdentityKeyStore(SignalIdentityKey identity)
    : identity_(std::move(identity)) {}

SignalKeypair InMemoryIdentityKeyStore::get_identity_key_pair() {
    return identity_.keypair;
}

uint32_t InMemoryIdentityKeyStore::get_local_registration_id() {
    return identity_.registration_id;
}

bool InMemoryIdentityKeyStore::save_identity(const SignalAddress& addr,
                                              const std::array<uint8_t, 32>& key) {
    std::lock_guard lock(mutex_);
    bool was_new = (trusted_.find(addr) == trusted_.end());
    trusted_[addr] = key;
    return was_new;
}

bool InMemoryIdentityKeyStore::is_trusted_identity(const SignalAddress& addr,
                                                     const std::array<uint8_t, 32>& key) {
    std::lock_guard lock(mutex_);
    auto it = trusted_.find(addr);
    // First time seeing this identity — trust it
    if (it == trusted_.end()) return true;
    // Must match what we have stored
    return it->second == key;
}

std::optional<std::array<uint8_t, 32>> InMemoryIdentityKeyStore::load_identity(const SignalAddress& addr) {
    std::lock_guard lock(mutex_);
    auto it = trusted_.find(addr);
    if (it == trusted_.end()) return std::nullopt;
    return it->second;
}

// ─── InMemorySenderKeyStore ───

std::optional<SenderKeyRecord> InMemorySenderKeyStore::load_sender_key(
    const SignalAddress& addr, const std::string& group_id) {
    std::lock_guard lock(mutex_);
    auto it = keys_.find({addr, group_id});
    if (it == keys_.end()) return std::nullopt;
    return it->second;
}

void InMemorySenderKeyStore::store_sender_key(
    const SignalAddress& addr, const std::string& group_id, const SenderKeyRecord& record) {
    std::lock_guard lock(mutex_);
    keys_[{addr, group_id}] = record;
}

bool InMemorySenderKeyStore::contains_sender_key(
    const SignalAddress& addr, const std::string& group_id) {
    std::lock_guard lock(mutex_);
    return keys_.find({addr, group_id}) != keys_.end();
}

} // namespace animus::signal

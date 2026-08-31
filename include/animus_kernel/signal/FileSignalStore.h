#pragma once

#include "animus_kernel/signal/SignalStore.h"
#include <memory>
#include <string>

namespace animus::signal {

class SignalSessionManager;

// ─── File-backed Signal stores (#53: WhatsApp production path) ───
//
// Sessions, trusted identities, and sender keys survive daemon restarts.
// Records are opaque serialized blobs (base64) in a single JSON map per
// store. Writes are atomic (tmp + rename). Our own pre-keys are NOT stored
// here — they are re-populated from the WhatsApp auth state on boot, which
// already persists them (auth.json).

class FileSessionStore : public ISessionStore {
public:
    explicit FileSessionStore(const std::string& file);

    std::optional<SessionRecord> load_session(const SignalAddress& addr) override;
    void store_session(const SignalAddress& addr, const SessionRecord& record) override;
    bool contains_session(const SignalAddress& addr) override;
    void delete_session(const SignalAddress& addr) override;
    void delete_all_sessions(const std::string& name) override;
    std::vector<uint32_t> get_sub_device_sessions(const std::string& name) override;

private:
    void PersistLocked();

    std::string file_;
    std::unordered_map<std::string, std::string> sessions_; // addrKey -> b64 blob
    std::mutex mutex_;
};

class FileIdentityKeyStore : public IIdentityKeyStore {
public:
    FileIdentityKeyStore(const std::string& file, SignalIdentityKey own);

    SignalKeypair get_identity_key_pair() override;
    uint32_t get_local_registration_id() override;
    bool save_identity(const SignalAddress& addr, const std::array<uint8_t, 32>& key) override;
    bool is_trusted_identity(const SignalAddress& addr, const std::array<uint8_t, 32>& key) override;
    std::optional<std::array<uint8_t, 32>> load_identity(const SignalAddress& addr) override;

private:
    void PersistLocked();

    std::string file_;
    SignalIdentityKey own_;
    std::unordered_map<std::string, std::string> identities_; // addrKey -> b64(32B key)
    std::mutex mutex_;
};

class FileSenderKeyStore : public ISenderKeyStore {
public:
    explicit FileSenderKeyStore(const std::string& file);

    std::optional<SenderKeyRecord> load_sender_key(const SignalAddress& addr, const std::string& group_id) override;
    void store_sender_key(const SignalAddress& addr, const std::string& group_id, const SenderKeyRecord& record) override;
    bool contains_sender_key(const SignalAddress& addr, const std::string& group_id) override;

private:
    void PersistLocked();

    std::string file_;
    std::unordered_map<std::string, std::string> keys_; // "addrKey|groupId" -> b64 blob
    std::mutex mutex_;
};

/// Factory: Signal manager with file-backed session/identity/sender-key
/// stores (pre-key stores stay in-memory; re-populated from auth state).
/// Creates baseDir if missing. identity_key comes from the WhatsApp auth
/// state — pass it so the identity matches the registered device.
std::unique_ptr<SignalSessionManager> create_file_manager(
    const std::string& baseDir,
    std::optional<SignalIdentityKey> identity_key = std::nullopt);

} // namespace animus::signal

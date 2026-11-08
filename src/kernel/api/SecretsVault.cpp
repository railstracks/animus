#include "animus_kernel/api/SecretsVault.h"

#include "animus_kernel/ApiPackageStore.h"
#include "animus_kernel/CryptoUtils.h"
#include "animus_kernel/Log.h"
#include "animus_kernel/SchemaHelpers.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace animus::kernel {

namespace {

int64_t NowUnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string GenerateId() {
    return crypto::RandomHex(16);
}

}  // namespace

SecretsVault::SecretsVault(IDataStore* store, const std::string& keyPath)
    : m_store(store), m_keyPath(keyPath) {
    if (!m_keyPath.empty()) {
        std::string err;
        if (!LoadOrCreateKey(err)) {
            ALOG_ERROR("api", "[vault] key load failed: " << err << " — vault disabled; "
                       << "secret operations will fail closed until fixed");
            m_key.clear();
        }
    }
}

bool SecretsVault::IsValidSecretName(const std::string& name) {
    if (name.empty() || name.size() > 63) return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

bool SecretsVault::LoadOrCreateKey(std::string& error) {
    // Secure load: no symlink following, regular files only, read from the
    // verified descriptor. A symlinked or non-regular "key file" is rejected
    // rather than read — the load path deserves the same discipline the
    // creation path already has (O_EXCL|O_NOFOLLOW).
    int keyFd = ::open(m_keyPath.c_str(), O_RDONLY | O_NOFOLLOW);
    if (keyFd < 0 && errno != ENOENT) {
        error = "cannot open key file '" + m_keyPath + "' (" + std::strerror(errno) + ")";
        return false;
    }
    if (keyFd >= 0) {
        struct stat st {};
        if (::fstat(keyFd, &st) != 0 || !S_ISREG(st.st_mode)) {
            ::close(keyFd);
            error = "key file '" + m_keyPath + "' is not a regular file";
            return false;
        }
        if ((st.st_mode & 077) != 0) {
            ALOG_WARNING("api", "[vault] key file " << m_keyPath << " is group/other "
                      "accessible (mode " << std::oct << (st.st_mode & 0777) << std::dec
                      << ") — expected 0600; refusing nothing, but tighten it");
        }
        std::string hex;
        char rbuf[4096];
        ssize_t n = 0;
        while ((n = ::read(keyFd, rbuf, sizeof rbuf)) > 0)
            hex.append(rbuf, static_cast<size_t>(n));
        ::close(keyFd);
        if (n < 0) {
            error = "read error on key file '" + m_keyPath + "'";
            return false;
        }
        // Trim exactly one trailing newline (CRLF tolerated); anything else is
        // a malformed file and gets rejected below — not silently eaten.
        if (!hex.empty() && (hex.back() == '\n' || hex.back() == '\r')) {
            hex.pop_back();
            if (!hex.empty() && (hex.back() == '\n' || hex.back() == '\r')) hex.pop_back();
        }
        if (hex.size() != 64) {
            error = "key file '" + m_keyPath + "' must hold 64 hex chars (32 bytes), found " +
                    std::to_string(hex.size());
            return false;
        }
        // Parse into a temporary; commit to m_key only after full validation.
        std::vector<unsigned char> parsed;
        parsed.reserve(32);
        for (size_t i = 0; i < 64; i += 2) {
            auto nib = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = nib(hex[i]), lo = nib(hex[i + 1]);
            if (hi < 0 || lo < 0) {
                error = "key file '" + m_keyPath + "' contains non-hex characters";
                return false;
            }
            parsed.push_back(static_cast<unsigned char>((hi << 4) | lo));
        }
        m_key = std::move(parsed);
        ALOG_INFO("api", "[vault] master key loaded from " << m_keyPath);
        return true;
    }

    // First use — generate. Exclusive, no-follow create with tight mode from
    // the first syscall: no symlink pre-placement, no umask window.
    const std::string keyHex = crypto::RandomHex(32);  // 32 bytes -> 64 hex chars
    {
        const int fd = ::open(m_keyPath.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (fd < 0) {
            if (errno == EEXIST) {
                // Lost a creation race (or a pre-existing file appeared between
                // the load attempt and now) — reload instead of clobber.
                return LoadOrCreateKey(error);
            } else {
                error = "cannot create key file '" + m_keyPath + "' (" + std::strerror(errno) + ")";
            }
            return false;
        }
        const std::string blob = keyHex + "\n";
        size_t off = 0;
        while (off < blob.size()) {
            const ssize_t n = ::write(fd, blob.data() + off, blob.size() - off);
            if (n <= 0) {
                ::close(fd);
                ::unlink(m_keyPath.c_str());  // never leave a partial key
                error = "short write creating key file '" + m_keyPath + "'";
                return false;
            }
            off += static_cast<size_t>(n);
        }
        ::fsync(fd);  // losing this file loses every vault secret — make it durable
        ::close(fd);
    }
    m_key.reserve(32);
    for (size_t i = 0; i < 64; i += 2) {
        m_key.push_back(static_cast<unsigned char>(
            (std::stoi(keyHex.substr(i, 2), nullptr, 16) & 0xFF)));
    }
    ALOG_INFO("api", "[vault] NEW master key generated at " << m_keyPath
              << " (mode 0600). Losing this file loses all vault secrets — "
              << "back it up separately from the database.");
    return true;
}

void SecretsVault::EnsureSchema() {
    if (!m_store) return;
    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS api_package_secrets (
            id TEXT PRIMARY KEY,
            package_id TEXT NOT NULL,
            name TEXT NOT NULL,
            ciphertext TEXT NOT NULL,
            created_at_unix_ms INTEGER NOT NULL,
            updated_at_unix_ms INTEGER NOT NULL,
            UNIQUE (package_id, name)
        );
    )");
}

// ── #93 P3a: agent-scoped secrets ──────────────────────────────────────

bool SecretsVault::IsRefValue(const std::string& raw, std::string& name) {
    if (raw.size() < 17 || raw.front() != '{') return false;
    Json::CharReaderBuilder rb;
    std::string errs;
    Json::Value v;
    std::istringstream ss(raw);
    if (!Json::parseFromStream(rb, ss, &v, &errs)) return false;
    return IsSecretRef(v, name);
}

bool SecretsVault::VaultAgentValue(const std::string& agentId, const std::string& name,
                                   const std::string& value, std::string& error) {
    if (!enabled()) {
        error = "vault disabled (no master key) — refusing to store plaintext";
        return false;
    }
    if (!IsValidSecretName(name)) {
        error = "invalid secret name '" + name + "'";
        return false;
    }
    std::string setErr;
    if (!Set(AgentScope(agentId), name, value, setErr)) {
        error = "vault write failed: " + setErr;
        return false;
    }
    return true;
}

std::string SecretsVault::ResolveAgentValue(const std::string& agentId,
                                            const std::string& rawValue) const {
    std::string refName;
    if (!IsRefValue(rawValue, refName)) return rawValue;
    auto opened = Get(AgentScope(agentId), refName);
    return opened ? *opened : std::string{};
}

int SecretsVault::MigrateAgentConfigSecrets(std::string& error) {
    // Credential-shaped agent_config values (plaintext bot tokens, api keys)
    // -> vault entries under the agent scope, values replaced by
    // {"secret_ref":"<name>"} object strings. Idempotent: values already
    // carrying refs are skipped (IsRefValue), non-credential keys untouched.
    static const std::vector<std::string> credentialSuffixes = {
        "api_key", "access_token", "bot_token", "app_token", "app_password",
        "client_secret", "refresh_token", "server_password", "access_jwt",
        "refresh_jwt", "api_secret", "secret", "password",
    };
    int migrated = 0;
    auto stmt = m_store->Prepare(
        "SELECT agent_id, key, value FROM agent_config ORDER BY agent_id, key");
    if (!stmt) {
        error = "agent_config read failed";
        return -1;
    }
    struct Row { std::string agentId, key, value; };
    std::vector<Row> rows;
    while (stmt->Step()) rows.push_back({stmt->ColumnText(0), stmt->ColumnText(1),
                                         stmt->ColumnText(2)});
    for (const auto& r : rows) {
        if (r.value.empty()) continue;
        std::string lower;
        std::transform(r.key.begin(), r.key.end(), std::back_inserter(lower),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool credentialShaped = false;
        for (const auto& suffix : credentialSuffixes) {
            std::string::size_type pos = lower.rfind(suffix);
            if (pos != std::string::npos && pos + suffix.size() == lower.size()) {
                credentialShaped = true;
                break;
            }
        }
        if (!credentialShaped) continue;
        std::string probeName;
        if (IsRefValue(r.value, probeName)) continue;  // already vaulted
        // #108 audit F1: the old '{'-prefix exemption let arbitrary JSON
        // objects ({"token":"plaintext"} under bot_token etc.) stay in the
        // row unvaulted — and JSON credentials are a LEGITIMATE shape
        // (service-account keys, provider auth blobs). Rule now: anything
        // credential-shaped that is not already a valid ref gets vaulted.
        if (!enabled()) {
            error = "vault disabled — refusing to leave credential plaintext in place";
            return -1;
        }
        const std::string name = DeriveAgentSecretName(r.key);
        std::string vaultErr;
        if (!VaultAgentValue(r.agentId, name, r.value, vaultErr)) {
            error = "migration of '" + r.key + "' failed: " + vaultErr;
            return -1;
        }
        Json::Value ref(Json::objectValue);
        ref["secret_ref"] = name;
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        const std::string refStr = Json::writeString(wb, ref);
        auto upd = m_store->Prepare(
            "UPDATE agent_config SET value = ? WHERE agent_id = ? AND key = ?");
        if (!upd) {
            error = "agent_config update prepare failed";
            return -1;
        }
        upd->BindText(1, refStr);
        upd->BindText(2, r.agentId);
        upd->BindText(3, r.key);
        if (!upd->ExecDML()) {
            error = "agent_config update failed";
            return -1;
        }
        ALOG_INFO("api", "[vault] migrated agent credential '" << r.key
                   << "' (agent '" << r.agentId << "') into the vault");
        ++migrated;
    }
    return migrated;
}

bool SecretsVault::Seal(const std::string& plaintext, std::string& envelopeHex,
                        std::string& error) const {
    if (m_key.empty()) { error = "vault disabled (no master key)"; return false; }
    // #275: armed epoch DEK → new writes seal under it (version 02);
    // otherwise the master-key era continues (01). Reads dispatch on the
    // version prefix.
    if (!m_fedDek.empty()) {
        envelopeHex = crypto::Aes256GcmSealVersion(m_fedDek, plaintext, 0x02, error);
    } else {
        envelopeHex = crypto::Aes256GcmSeal(m_key, plaintext, error);
    }
    return envelopeHex.empty() == false;
}

bool SecretsVault::Open(const std::string& envelopeHex, std::string& plaintext,
                        std::string& error) const {
    if (m_key.empty()) { error = "vault disabled (no master key)"; return false; }
    // #275: version dispatch — 01 = master-key era, 02 = federation
    // epoch-DEK era. A 02 row without an armed DEK fails CLASSIFIED
    // (awaiting key ceremony), never as a garbage GCM auth error and
    // never as an empty string.
    if (envelopeHex.size() >= 2 && envelopeHex.compare(0, 2, "02") == 0) {
        if (m_fedDek.empty()) {
            error = "epoch-DEK ciphertext but no federation DEK armed "
                    "(awaiting key ceremony)";
            return false;
        }
        return crypto::Aes256GcmOpen(m_fedDek, envelopeHex, plaintext, error);
    }
    return crypto::Aes256GcmOpen(m_key, envelopeHex, plaintext, error);
}

void SecretsVault::SetFederationDek(int64_t epoch, const unsigned char* dek32, size_t len) {
    // #274: boot-path activation. #275's versioned-ciphertext migration
    // consumes this; the master-key path stays authoritative until then.
    if (!dek32 || len != 32) {
        ALOG_ERROR("api", "[vault] refusing federation DEK — expected 32 bytes, got "
                   << len);
        return;
    }
    m_fedEpoch = epoch;
    m_fedDek.assign(dek32, dek32 + 32);
    ALOG_INFO("api", "[vault] federation epoch " << epoch << " DEK armed");
}

int SecretsVault::MigrateToEpochDek(std::string& error) {
    error.clear();
    if (!m_store) { error = "vault has no store"; return -1; }
    if (m_key.empty()) { error = "vault disabled (no master key)"; return -1; }
    if (m_fedDek.empty()) { error = "no federation DEK armed"; return -1; }

    struct Row { std::string id, packageId, name, ciphertext; };
    std::vector<Row> rows;
    {
        auto stmt = m_store->Prepare(
            "SELECT id, package_id, name, ciphertext FROM api_package_secrets");
        if (!stmt) { error = "vault prepare failed: " + m_store->ErrMsg(); return -1; }
        while (stmt->Step()) {
            Row r;
            r.id = stmt->ColumnText(0);
            r.packageId = stmt->ColumnText(1);
            r.name = stmt->ColumnText(2);
            r.ciphertext = stmt->ColumnText(3);
            rows.push_back(std::move(r));
        }
    }

    int migrated = 0;
    for (const auto& row : rows) {
        if (row.ciphertext.size() >= 2 && row.ciphertext.compare(0, 2, "02") == 0)
            continue;   // already epoch-DEK era
        if (row.ciphertext.size() < 2 || row.ciphertext.compare(0, 2, "01") != 0) {
            ALOG_WARNING("api", "[vault] re-seal sweep: skipping unrecognized "
                         << "ciphertext format for '" << row.name << "' of "
                         << row.packageId);
            continue;
        }
        std::string plaintext, openErr;
        if (!crypto::Aes256GcmOpen(m_key, row.ciphertext, plaintext, openErr)) {
            error = "re-seal sweep: cannot open '" + row.name + "' of " +
                    row.packageId + " with the master key: " + openErr;
            return -1;   // loud stop — a re-run skips what already moved
        }
        std::string sealErr;
        const std::string resealed =
            crypto::Aes256GcmSealVersion(m_fedDek, plaintext, 0x02, sealErr);
        if (resealed.empty()) {
            error = "re-seal sweep: seal failed for '" + row.name + "': " + sealErr;
            return -1;
        }
        auto upd = m_store->Prepare(
            "UPDATE api_package_secrets SET ciphertext = ?, updated_at_unix_ms = ? "
            "WHERE id = ?");
        if (!upd) { error = "vault prepare failed: " + m_store->ErrMsg(); return -1; }
        upd->BindText(1, resealed);
        upd->BindInt64(2, NowUnixMs());
        upd->BindText(3, row.id);
        if (!upd->ExecDML()) {
            error = "vault update failed: " + m_store->ErrMsg();
            return -1;
        }
        ++migrated;
        ALOG_INFO("api", "[vault] re-sealed secret '" << row.name << "' of "
                  << row.packageId << " under federation epoch " << m_fedEpoch);
    }
    return migrated;
}

bool SecretsVault::Set(const std::string& packageId, const std::string& name,
                       const std::string& value, std::string& error) {
    if (!m_store) { error = "vault has no store"; return false; }
    if (packageId.empty() || name.empty()) {
        error = "vault set requires package id and secret name";
        return false;
    }
    std::string envelope;
    if (!Seal(value, envelope, error)) return false;

    const int64_t now = NowUnixMs();
    if (Has(packageId, name)) {
        auto stmt = m_store->Prepare(
            "UPDATE api_package_secrets SET ciphertext = ?, updated_at_unix_ms = ? "
            "WHERE package_id = ? AND name = ?");
        if (!stmt) { error = "vault prepare failed: " + m_store->ErrMsg(); return false; }
        stmt->BindText(1, envelope);
        stmt->BindInt64(2, now);
        stmt->BindText(3, packageId);
        stmt->BindText(4, name);
        if (!stmt->ExecDML()) { error = "vault update failed: " + m_store->ErrMsg(); return false; }
        return true;
    }
    auto stmt = m_store->Prepare(
        "INSERT INTO api_package_secrets (id, package_id, name, ciphertext, "
        "created_at_unix_ms, updated_at_unix_ms) VALUES (?, ?, ?, ?, ?, ?)");
    if (!stmt) { error = "vault prepare failed: " + m_store->ErrMsg(); return false; }
    stmt->BindText(1, GenerateId());
    stmt->BindText(2, packageId);
    stmt->BindText(3, name);
    stmt->BindText(4, envelope);
    stmt->BindInt64(5, now);
    stmt->BindInt64(6, now);
    if (!stmt->ExecDML()) {
        // Lost a race with a concurrent insert of the same name: fall back
        // to update so Set() stays idempotent.
        auto upd = m_store->Prepare(
            "UPDATE api_package_secrets SET ciphertext = ?, updated_at_unix_ms = ? "
            "WHERE package_id = ? AND name = ?");
        if (!upd) { error = "vault prepare failed: " + m_store->ErrMsg(); return false; }
        upd->BindText(1, envelope);
        upd->BindInt64(2, now);
        upd->BindText(3, packageId);
        upd->BindText(4, name);
        if (!upd->ExecDML()) { error = "vault upsert failed: " + m_store->ErrMsg(); return false; }
    }
    return true;
}

std::optional<std::string> SecretsVault::Get(const std::string& packageId,
                                             const std::string& name) const {
    if (!m_store || m_key.empty()) return std::nullopt;
    auto stmt = m_store->Prepare(
        "SELECT ciphertext FROM api_package_secrets WHERE package_id = ? AND name = ?");
    if (!stmt) return std::nullopt;
    stmt->BindText(1, packageId);
    stmt->BindText(2, name);
    if (!stmt->Step()) return std::nullopt;
    const std::string envelope = stmt->ColumnText(0);
    stmt->Finalize();
    std::string plaintext, err;
    if (!Open(envelope, plaintext, err)) {
        ALOG_ERROR("api", "[vault] open failed for secret '" << name << "' of package "
                   << packageId << ": " << err << " (treating as unset — set it again)");
        return std::nullopt;
    }
    return plaintext;
}

bool SecretsVault::Has(const std::string& packageId, const std::string& name) const {
    if (!m_store) return false;
    auto stmt = m_store->Prepare(
        "SELECT 1 FROM api_package_secrets WHERE package_id = ? AND name = ?");
    if (!stmt) return false;
    stmt->BindText(1, packageId);
    stmt->BindText(2, name);
    const bool has = stmt->Step();
    stmt->Finalize();
    return has;
}

bool SecretsVault::Delete(const std::string& packageId, const std::string& name) {
    if (!m_store) return false;
    if (!Has(packageId, name)) return false;  // miss, not silent success
    auto stmt = m_store->Prepare(
        "DELETE FROM api_package_secrets WHERE package_id = ? AND name = ?");
    if (!stmt) return false;
    stmt->BindText(1, packageId);
    stmt->BindText(2, name);
    return stmt->ExecDML();
}

void SecretsVault::DeleteForPackage(const std::string& packageId) {
    if (!m_store) return;
    auto stmt = m_store->Prepare("DELETE FROM api_package_secrets WHERE package_id = ?");
    if (!stmt) return;
    stmt->BindText(1, packageId);
    stmt->ExecDML();
}

std::vector<SecretsVault::EntryInfo> SecretsVault::ListForPackage(
    const std::string& packageId) const {
    std::vector<EntryInfo> out;
    if (!m_store) return out;
    auto stmt = m_store->Prepare(
        "SELECT name, updated_at_unix_ms FROM api_package_secrets "
        "WHERE package_id = ? ORDER BY name");
    if (!stmt) return out;
    stmt->BindText(1, packageId);
    while (stmt->Step()) {
        EntryInfo e;
        e.name = stmt->ColumnText(0);
        e.updated_at_unix_ms = stmt->ColumnInt64(1);
        out.push_back(std::move(e));
    }
    stmt->Finalize();
    return out;
}

// ---------------------------------------------------------------------------
// State <-> vault helpers
// ---------------------------------------------------------------------------

bool SecretsVault::IsSecretRef(const Json::Value& v, std::string& name) {
    if (!v.isObject() || v.size() != 1 || !v.isMember("secret_ref")) return false;
    const Json::Value& ref = v["secret_ref"];
    if (!ref.isString() || ref.asString().empty()) return false;
    // #108 audit (minor): a ref with a name the vault could never have
    // stored is not a ref — reject consistently at the parse boundary so
    // write interception never "passes through" a malformed object.
    if (!IsValidSecretName(ref.asString())) return false;
    name = ref.asString();
    return true;
}

// #108 audit F2 — see header. FNV-1a 64-bit: dependency-free, stable.
std::string SecretsVault::DeriveAgentSecretName(const std::string& key) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    static const char* hex = "0123456789abcdef";
    std::string digest;
    for (int shift = 60; shift >= 0; shift -= 4) digest += hex[(h >> shift) & 0xF];
    std::string name;
    for (char c : key) {
        if (IsValidSecretName(std::string(1, c))) name += c;
        else if (!name.empty() && name.back() != '_') name += '_';
        // collapse runs; also a plain '.' or ':' in key position 0
    }
    while (!name.empty() && name.back() == '_') name.pop_back();
    if (name.size() > 45) name.resize(45);
    if (name.empty()) name = "cred";
    return name + "_" + digest;
}

void SecretsVault::ResolveState(const std::string& packageId,
                                const Json::Value& stateSchema,
                                Json::Value& state) const {
    if (!state.isObject() || !stateSchema.isObject()) return;
    for (const std::string& k : stateSchema.getMemberNames()) {
        const Json::Value& def = stateSchema[k];
        if (!def.isObject() || !def.get("secret", false).asBool()) continue;

        // Explicit indirection: {"secret_ref": "<other-name>"} wins.
        std::string refName;
        if (state.isMember(k) && IsSecretRef(state[k], refName)) {
            auto value = Get(packageId, refName);
            if (value) {
                state[k] = *value;
            } else {
                ALOG_ERROR("api", "[vault] secret_ref '" << refName << "' (state key '" << k
                           << "', package " << packageId << ") is not set in the vault — "
                           << "key resolves empty; set it via admin");
                state.removeMember(k);
            }
            continue;
        }
        // Default: vault entry named exactly like the state key.
        if (Has(packageId, k)) {
            auto value = Get(packageId, k);
            if (value) state[k] = *value;
            else state.removeMember(k);
        } else if (state.isMember(k)) {
            // Pre-migration literal in stored state — treat as valid in-memory
            // (MigrateLegacyStateSecrets will move it to the vault at boot).
        }
    }
}

int SecretsVault::SplitStateSecrets(const std::string& packageId,
                                    const Json::Value& stateSchema,
                                    Json::Value& state,
                                    std::string& error) const {
    if (!state.isObject() || !stateSchema.isObject()) return 0;
    Json::Value next = state;
    int migrated = 0;
    for (const std::string& k : stateSchema.getMemberNames()) {
        const Json::Value& def = stateSchema[k];
        if (!def.isObject() || !def.get("secret", false).asBool()) continue;
        if (!next.isMember(k)) continue;
        Json::Value v = next[k];
        std::string refName;
        // Split path accepts the admin write-through form {"secret_ref": name,
        // "value": literal} — strict IsSecretRef (resolution) rejects extra
        // members, so detect the ref shape directly here.
        if (v.isObject() && v.isMember("secret_ref")) {
            // Strict shape: {"secret_ref"} or {"secret_ref", "value"} — nothing
            // else. Malformed objects are an error, never silently normalized.
            if (!v["secret_ref"].isString() || v.size() > 2 ||
                (v.size() == 2 && !v.isMember("value"))) {
                error = "state key '" + k + "': malformed secret_ref object "
                        "(expected {secret_ref: name} or {secret_ref: name, value: literal})";
                return -1;
            }
            refName = v["secret_ref"].asString();
            if (!IsValidSecretName(refName)) {
                error = "state key '" + k + "': secret_ref name '" + refName +
                        "' is not a valid vault entry name ([A-Za-z0-9_-], 1-63)";
                return -1;
            }
            // Admin write-through form: {"secret_ref": name, "value": literal}.
            // If a value member is present it must be a non-empty, non-masked
            // string — malformed input is an error, never silently normalized
            // to a bare ref (that would discard the intended write).
            if (v.isMember("value")) {
                if (!v["value"].isString()) {
                    error = "state key '" + k + "': secret_ref 'value' must be a string";
                    return -1;
                }
                const std::string& wv = v["value"].asString();
                if (wv.empty() || wv == "***") {
                    error = "state key '" + k + "': secret_ref 'value' is empty/masked — "
                            "refusing to vault a placeholder";
                    return -1;
                }
            }
            if (v.isMember("value") && v["value"].isString()) {
                const std::string literal = v["value"].asString();
                if (!literal.empty() && literal != "***") {
                    if (!const_cast<SecretsVault*>(this)->Set(packageId, refName, literal, error))
                        return -1;
                    migrated++;
                }
            }
            next[k] = Json::Value(Json::objectValue);
            next[k]["secret_ref"] = refName;
            continue;
        }
        if (v.isString()) {
            const std::string literal = v.asString();
            if (literal == "***") { next.removeMember(k); continue; }  // masked echo = keep
            if (!const_cast<SecretsVault*>(this)->Set(packageId, k, literal, error)) return -1;
            migrated++;
            next.removeMember(k);
        }
    }
    state = next;
    return migrated;
}

int SecretsVault::MigrateLegacyStateSecrets(ApiPackageStore& packages, std::string& error) {
    if (!m_store) { error = "vault has no store"; return -1; }
    if (m_key.empty()) {
        ALOG_WARNING("api", "[vault] migration skipped: vault disabled (no master key) — "
                     "legacy secrets remain in state");
        return 0;
    }
    int migrated = 0;
    for (const ApiPackage& pkg : packages.ListPackages()) {
        Json::CharReaderBuilder rb;
        Json::Value schema, state;
        std::string errs;
        std::stringstream ss1(pkg.state_schema.empty() ? "{}" : pkg.state_schema);
        Json::parseFromStream(rb, ss1, &schema, &errs);
        std::stringstream ss2(pkg.state.empty() ? "{}" : pkg.state);
        Json::parseFromStream(rb, ss2, &state, &errs);
        if (!state.isObject()) continue;

        Json::Value next = state;
        bool changed = false;
        if (schema.isObject()) {
            for (const std::string& k : schema.getMemberNames()) {
                const Json::Value& def = schema[k];
                if (!def.isObject() || !def.get("secret", false).asBool()) continue;
                if (!next.isMember(k)) continue;
                if (!next[k].isString()) continue;
                const std::string literal = next[k].asString();
                std::string setErr;
                if (!Set(pkg.id, k, literal, setErr)) {
                    error = "package '" + pkg.name + "' key '" + k + "': " + setErr;
                    return -1;
                }
                next.removeMember(k);
                changed = true;
                migrated++;
                ALOG_INFO("api", "[vault] migrated secret '" << k << "' of package '"
                          << pkg.name << "' into the vault (removed from state JSON)");
            }
        }
        if (changed) {
            Json::StreamWriterBuilder wb;
            wb["indentation"] = "";
            wb["commentStyle"] = "None";
            if (!packages.SetPackageState(pkg.id, Json::writeString(wb, next))) {
                // DOCUMENTED TEMPORARY INVARIANT VIOLATION: this package's
                // secrets remain as literals in its state JSON while the vault
                // already holds encrypted copies (identical values). Not
                // transactional across the two stores by design — the vault
                // write is idempotent and the sweep retries next boot, so the
                // violation window is [failed rewrite, next successful boot].
                // This is no worse than pre-#23 state and strictly better than
                // losing the secret by deleting-first. Availability over strict
                // atomicity, chosen deliberately (audit exchange 2026-09-19).
                // Consistent, retried next boot; continue with the other
                // packages — one bad row must not stall the whole sweep.
                ALOG_ERROR("api", "[vault] migration: state rewrite failed for package '"
                           << pkg.name << "' — secrets already vaulted, state keeps literals; "
                           << "will retry next boot");
                continue;
            }
        }
    }
    if (migrated > 0) {
        ALOG_INFO("api", "[vault] migration complete: " << migrated
                  << " secret(s) moved from package state into the encrypted vault");
    }
    return migrated;
}

}  // namespace animus::kernel

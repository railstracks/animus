#pragma once
// ============================================================================
// SecretsVault (#23) — encrypted secret storage for API packages.
//
// Model:
//   - Secret-typed state keys NEVER hold values in package state JSON.
//     The vault is the only home for secret bytes; state stays pure config.
//   - Entries are namespaced per package. Resolution order for a secret-typed
//     schema key `k` of package `p`:
//       1. state[p][k] == {"secret_ref": "<name>"}  -> vault entry <name>
//          (explicit indirection: sharing / renaming; rare by design)
//       2. vault entry named exactly `k`
//       3. unset -> key stays absent; the existing missing-key hard error
//          at the use site names it. No silent empty strings.
//   - Values are sealed with AES-256-GCM under a master key kept OUTSIDE the
//     database (key file next to the data dir; auto-generated, 0600). This
//     protects against DB-only exfiltration (backups, dumps, state exports);
//     a full-host compromise reads the key file too — honest boundary.
//
// Egress discipline: values are never logged, never returned by list APIs,
// never included in exports or registry payloads. Admin set/get surfaces
// acknowledge by name only.
// ============================================================================

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json/json.h>

#include "animus_kernel/IDataStore.h"

namespace animus::kernel {

class ApiPackageStore;

class SecretsVault {
public:
    // keyPath: master key file (64-char hex = 32 bytes). Created with random
    // bytes on first use if missing — loudly logged. Empty keyPath = vault
    // disabled (tests / legacy embed); Set/Get fail closed with an error.
    SecretsVault(IDataStore* store, const std::string& keyPath);

    void EnsureSchema();

    // ── #93 P3a: agent-scoped secrets (channel/provider credentials) ────
    // The vault is namespaced by a free-form "package_id" string; nothing
    // inside requires it to be a package UUID. Agent secrets live in the
    // same table under the reserved namespace "agent:<agent_id>" (":" is
    // impossible in package ids, so the namespaces can't collide).
    // Channel credentials stored as {"secret_ref": "<name>"} in
    // agent_config resolve against namespace "agent:<agent_id>" here.
    static std::string AgentScope(const std::string& agentId) {
        return "agent:" + agentId;
    }

    bool Set(const std::string& packageId, const std::string& name,
             const std::string& value, std::string& error);
    // Opens the sealed value. Never logged by callers.
    std::optional<std::string> Get(const std::string& packageId,
                                   const std::string& name) const;
    bool Has(const std::string& packageId, const std::string& name) const;
    bool Delete(const std::string& packageId, const std::string& name);
    void DeleteForPackage(const std::string& packageId);

    struct EntryInfo {
        std::string name;
        int64_t updated_at_unix_ms{0};
    };
    // Names + timestamps only — never values.
    std::vector<EntryInfo> ListForPackage(const std::string& packageId) const;

    bool enabled() const { return !m_key.empty(); }

    // --- state <-> vault helpers (shared by runtime, admin, connections) ----

    // True when v is {"secret_ref": "<name>"} (single-member object).
    static bool IsSecretRef(const Json::Value& v, std::string& name);

    // Vault entry names: [A-Za-z0-9_-], 1-63 chars — shared by runtime writes,
    // split, and the admin routes so indirection targets can never smuggle
    // path separators, whitespace, or unbounded strings into storage keys.
    static bool IsValidSecretName(const std::string& name);

    // In-place resolution of a live state object: for every secret-typed
    // schema key, replace {"secret_ref":...} or vault-backed absence with the
    // opened value. Unset secret -> key REMOVED from `state` (missing-key
    // errors then fire naturally at use sites). Exception: a stale literal in
    // stored state (pre-migration) stays valid in-memory until the boot sweep
    // moves it to the vault. Never writes back to storage.
    // Returns an error string (no values) only on vault misuse (ref to an
    // unset entry) — caller decides severity.
    void ResolveState(const std::string& packageId,
                      const Json::Value& stateSchema,
                      Json::Value& state) const;

    // Split-write: moves literal values on secret-typed schema keys into the
    // vault (dropping them from `state`) and rewrites existing {"secret_ref"}
    // objects only when they carry a new literal under "value" (admin UI
    // write-through form). Returns the number of secrets vaulted; on failure
    // returns -1 with `error` set and `state` left unmodified.
    int SplitStateSecrets(const std::string& packageId,
                          const Json::Value& stateSchema,
                          Json::Value& state,
                          std::string& error) const;

    // One-time, idempotent migration sweep over all installed packages:
    // literal secret values in stored state -> vault entries (named by key),
    // state rewritten without them. Loud log line per migrated secret.
    // Returns migrated count or -1 on error.
    int MigrateLegacyStateSecrets(ApiPackageStore& packages, std::string& error);

    // ── #93 P3a: agent_config secret_ref integration ─────────────────────
    // agent_config values may be {"secret_ref": "<name>"} — resolved from
    // the agent scope at read time, transparent to every config.get caller.
    // Never resolves into a cached plaintext value: the AgentConfigStore
    // consults the vault at Get() when the raw value carries a ref.

    // True when a raw agent_config VALUE string is a serialized secret_ref
    // object ({"secret_ref":"name"}) — the config store stores refs as
    // strings; this parses and validates them.
    static bool IsRefValue(const std::string& raw, std::string& name);

    // #108 audit F2: injective key->name derivation for agent-scope vault
    // entries. Sanitization alone is NOT injective ('channels.a:b' and
    // 'channels.a.b' both -> 'channels_a_b'), so two distinct config keys
    // would share one vault entry and silently resolve to the wrong
    // credential. Derivation: sanitized prefix (<=45 chars, [a-zA-Z0-9_-])
    // + '_' + 16-hex FNV-1a-64 of the ORIGINAL key. Deterministic across
    // boots and nodes; collision odds at this scale are negligible.
    static std::string DeriveAgentSecretName(const std::string& key);

    // Convenience for callers holding config JSON: writes the secret into
    // the vault under the agent scope and replaces the value with a ref.
    // Returns false + error on vault misuse (disabled, bad name).
    bool VaultAgentValue(const std::string& agentId, const std::string& name,
                         const std::string& value, std::string& error);

    // Resolves a raw agent_config value: if it is a secret_ref object
    // string, opens the vault entry (agent scope). Returns the raw value
    // when it is not a ref. Unset ref entry -> empty string (missing-secret
    // hard errors fire at use sites; config.get stays non-throwing).
    std::string ResolveAgentValue(const std::string& agentId,
                                  const std::string& rawValue) const;

    // One-time, idempotent: migrate plaintext channel-credential values in
    // agent_config into the vault (agent scope), replacing them with
    // secret_ref objects. Keys that look like credentials (heuristic over
    // the ChannelManager credential-key list) and non-empty values become
    // refs. Loud log per migrated secret. Returns migrated count or -1.
    int MigrateAgentConfigSecrets(std::string& error);

    // Seal/open exposure for tests.
    bool Seal(const std::string& plaintext, std::string& envelopeHex, std::string& error) const;
    bool Open(const std::string& envelopeHex, std::string& plaintext, std::string& error) const;

    // ── #274/#275: federation epoch DEK ────────────────────────────────────
    // Set by AgentKernel at boot when this node's key envelope unwraps.
    // #275's versioned-ciphertext migration consumes it (re-seal sweep);
    // a peer-held epoch with no local envelope is a LOUD kernel-level
    // lockout, never a silent empty key.
    void SetFederationDek(int64_t epoch, const unsigned char* dek32, size_t len);
    bool HasFederationDek() const { return !m_fedDek.empty(); }
    int64_t FederationEpoch() const { return m_fedEpoch; }

private:
    bool LoadOrCreateKey(std::string& error);

    IDataStore* m_store;
    std::string m_keyPath;
    std::vector<unsigned char> m_key;

    int64_t m_fedEpoch{0};
    std::vector<unsigned char> m_fedDek;   // #274: unwrapped epoch DEK
};

}  // namespace animus::kernel

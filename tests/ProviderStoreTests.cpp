// ProviderStoreTests — #93 P3 slice 3: provider config in the replicated store
//
// Covers the store-backed persistence seam of ProviderManager:
//   1. boot import    — empty store + legacy providers.json/auth.json ->
//                       kv rows under "__providers"; files win ONLY on the
//                       first boot (store is source of truth after)
//   2. secrets        — api_key and auth blobs land as vaulted rows (raw row
//                       carries a secret_ref; resolved reads return plaintext)
//   3. CRUD reconcile — SaveProviders upserts and deletes orphaned rows
//   4. reload         — ReloadFromStore rebuilds the model, preserving
//                       node-local runtime state of surviving providers
//   5. validation     — "__" provider-id prefix rejected (reserved namespace)
//   6. no vault       — plaintext rows, everything still round-trips
//                       (single-box legacy behavior, no worse than files)

#include "animus_kernel/admin/ProviderManager.h"

#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/api/SecretsVault.h"
#include "animus_kernel/SqliteDataStore.h"

#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <sstream>
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

std::string MakeDir() {
    static int n = 0;
    char tmp[] = "/tmp/animus_provider_store_test_XXXXXX";
    mkdtemp(tmp);
    (void)n++;
    return std::string(tmp);
}

void WriteFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::trunc);
    out << content;
}

void SeedLegacyFiles(const std::string& dir) {
    WriteFile(dir + "/providers.json", R"({
  "default_provider": "zai",
  "providers": {
    "zai": {
      "provider_type": "zai",
      "base_url": "https://api.z.ai",
      "api_key": "sk-legacy-plaintext",
      "default_model": "glm-5.3",
      "auth_type": "api_key",
      "concurrency": 3
    },
    "codex-oauth": {
      "provider_type": "openai",
      "base_url": "https://api.openai.com",
      "default_model": "gpt-5.3",
      "auth_type": "oauth",
      "concurrency": 1
    }
  }
})");
    WriteFile(dir + "/auth.json", R"({
  "codex-oauth": {
    "access_token": "tok-oauth-abc",
    "refresh_token": "tok-refresh-xyz"
  }
})");
}

}  // namespace

KernelConfig::ProviderConfigStorage MakeStorageOf(const std::string& dir) {
    KernelConfig::ProviderConfigStorage s;
    s.providersFilePath = dir + "/providers.json";
    s.authFilePath = dir + "/auth.json";
    s.persistToDisk = true;
    return s;
}

void TestBootImport() {
    std::cerr << "  [boot-import] legacy files -> kv rows, files win only once...\n";
    const std::string dir = MakeDir();
    const std::string dbPath = dir + "/db.sqlite";
    SqliteDataStore db{dbPath};
    SecretsVault vault{&db, dir + "/vault.key"};
    vault.EnsureSchema();
    AgentConfigStore config{&db};
    config.SetVault(&vault);

    SeedLegacyFiles(dir);
    ProviderManager mgr;
    mgr.Configure(KernelConfig::ProviderConfigStorage{MakeStorageOf(dir)});
    mgr.ConfigureStore(&config);

    std::string err;
    Assert(mgr.LoadProviders(&err), "boot load (import path) succeeds: " + err);
    Assert(mgr.HasProvider("zai") && mgr.HasProvider("codex-oauth"), "both providers imported");
    Assert(mgr.GetDefaultProviderId() == "zai", "default provider imported");
    {
        const auto p = mgr.GetProvider("zai");
        Assert(p && p->apiKey == "sk-legacy-plaintext", "api_key resolved from vault after import");
        Assert(p && p->concurrency == 3, "config fields intact");
    }

    // Rows exist and secrets are vaulted (raw rows carry refs, not plaintext).
    Assert(config.GetRaw("__providers", "cfg.zai.api_key").find("secret_ref") !=
               std::string::npos,
           "api_key row is a secret_ref");
    Assert(config.GetRaw("__providers", "cfg.zai.api_key").find("sk-legacy-plaintext") ==
               std::string::npos,
           "api_key plaintext never in the row");
    Assert(config.GetRaw("__providers", "cfg.codex-oauth.auth_secret")
               .find("secret_ref") != std::string::npos,
           "auth blob row is a secret_ref");
    Assert(config.GetRaw("__providers", "cfg.codex-oauth.auth_secret")
               .find("tok-oauth-abc") == std::string::npos,
           "auth blob plaintext never in the row");
    {
        Json::Value auth;
        std::string authErr;
        Assert(mgr.LoadAuthProvider("codex-oauth", &auth, &authErr),
               "auth blob loads from store: " + authErr);
        Assert(auth["codex-oauth"]["access_token"].asString() == "tok-oauth-abc",
               "auth blob resolves verbatim (root shape, caller-indexed)");
    }

    // Store is now the source of truth: delete the files, reload -> intact.
    ::unlink((dir + "/providers.json").c_str());
    ::unlink((dir + "/auth.json").c_str());
    mgr.ReloadFromStore();
    Assert(mgr.HasProvider("zai"), "provider survives file deletion (store wins)");
    {
        const auto p = mgr.GetProvider("zai");
        Assert(p && p->apiKey == "sk-legacy-plaintext", "api_key still resolves");
    }

    // New providers.json appearing LATER must NOT re-import (store wins).
    WriteFile(dir + "/providers.json", R"({
  "default_provider": "rogue",
  "providers": { "rogue": { "provider_type": "zai", "auth_type": "api_key" } }
})");
    mgr.ReloadFromStore();
    Assert(!mgr.HasProvider("rogue"), "late-arriving file is ignored");
}

long ProviderVaultEntries(SqliteDataStore& db) {
    auto q = db.Prepare(
        "SELECT COUNT(*) FROM api_package_secrets WHERE package_id = 'agent:__providers'");
    long n = -1;
    if (q && q->Step()) n = q->ColumnInt64(0);
    if (q) q->Finalize();
    return n;
}

void TestCrudReconcile() {
    std::cerr << "  [crud] upsert + orphan deletion...\n";
    const std::string dir = MakeDir();
    SqliteDataStore db{dir + "/db.sqlite"};
    SecretsVault vault{&db, dir + "/vault.key"};
    vault.EnsureSchema();
    AgentConfigStore config{&db};
    config.SetVault(&vault);
    ProviderManager mgr;
    mgr.Configure(MakeStorageOf(dir));
    mgr.ConfigureStore(&config);

    std::string err;
    Assert(mgr.LoadProviders(&err), "fresh empty load: " + err);

    ProviderState a;
    a.providerId = "zai";
    a.providerType = "zai";
    a.baseUrl = "https://api.z.ai";
    a.defaultModel = "glm-5.3";
    a.authType = "api_key";
    a.apiKey = "sk-crud-1";
    a.concurrency = 2;
    Assert(mgr.CreateProvider(a, &err), "create a: " + err);
    ProviderState b = a;
    b.providerId = "ollama-local";
    b.providerType = "ollama";
    b.authType = "api_key";
    b.apiKey = "sk-crud-b";          // PR #112 audit: secrets ride the deleted provider
    Assert(mgr.CreateProvider(b, &err), "create b: " + err);
    Json::Value bAuth(Json::objectValue);
    bAuth["access_token"] = "tok-crud-b";
    Assert(mgr.SaveAuthProvider("ollama-local", bAuth, &err),
           "auth blob for b: " + err);
    Assert(mgr.SaveProviders(&err), "persist: " + err);

    Assert(!config.GetRaw("__providers", "cfg.zai").empty(), "config row written");
    Assert(config.GetRaw("__providers", "cfg.ollama-local.api_key").find("secret_ref") !=
               std::string::npos, "b api_key vaulted");
    const long secretsBefore = ProviderVaultEntries(db);
    Assert(secretsBefore >= 3, "three vault entries (a key, b key, b auth)");

    // Delete b, persist -> rows AND vault ciphertext must be gone.
    ProviderState removed;
    Assert(mgr.DeleteProvider("ollama-local", &removed, &err), "delete b: " + err);
    Assert(mgr.SaveProviders(&err), "persist after delete: " + err);
    Assert(config.GetRaw("__providers", "cfg.ollama-local").empty(), "orphan config row deleted");
    Assert(config.GetRaw("__providers", "cfg.ollama-local.api_key").empty(),
           "orphan api_key row deleted");
    Assert(config.GetRaw("__providers", "cfg.ollama-local.auth_secret").empty(),
           "orphan auth_secret row deleted");
    Assert(ProviderVaultEntries(db) == 1,
           "b's ciphertext died with its ref rows (no orphan vault entries replicating)");

    // PR #112 audit, anchor-less case: a secret row whose plain row never
    // arrived (partial replication gap / mixed-version peer) must be
    // reconciled on the next SaveProviders, ciphertext included.
    config.Set("__providers", "cfg.ghost.api_key", "sk-ghost");
    Assert(ProviderVaultEntries(db) == 2, "ghost secret vaulted (anchor-less)");
    Assert(mgr.SaveProviders(&err), "persist with ghost row present: " + err);
    Assert(config.GetRaw("__providers", "cfg.ghost.api_key").empty(),
           "anchor-less suffix row reconciled away");
    Assert(ProviderVaultEntries(db) == 1, "ghost ciphertext cleaned too");

    // Rotation: new key on a -> row still a ref, resolved value new.
    ProviderState a2 = a;
    a2.apiKey = "sk-crud-2";
    Assert(mgr.UpdateProvider("zai", a2, &err), "rotate key: " + err);
    Assert(mgr.SaveProviders(&err), "persist rotation: " + err);
    Assert(config.GetRaw("__providers", "cfg.zai.api_key").find("secret_ref") !=
               std::string::npos, "rotated key still vaulted");
    Assert(config.Get("__providers", "cfg.zai.api_key") == "sk-crud-2", "rotation resolves");
}

void TestReloadPreservesRuntime() {
    std::cerr << "  [reload] runtime state survives sync-triggered rebuilds...\n";
    const std::string dir = MakeDir();
    SqliteDataStore db{dir + "/db.sqlite"};
    SecretsVault vault{&db, dir + "/vault.key"};
    vault.EnsureSchema();
    AgentConfigStore config{&db};
    config.SetVault(&vault);
    ProviderManager mgr;
    mgr.Configure(MakeStorageOf(dir));
    mgr.ConfigureStore(&config);

    std::string err;
    (void)mgr.LoadProviders(&err);
    ProviderState a;
    a.providerId = "zai";
    a.providerType = "zai";
    a.authType = "api_key";
    a.apiKey = "sk-rt";
    Assert(mgr.CreateProvider(a, &err), "create");
    Assert(mgr.SaveProviders(&err), "persist");
    ProviderState updated;
    Assert(mgr.UpdateProviderStatus("zai", "available", "", 12345, &updated, &err),
           "status update");
    Assert(updated.status == "available", "status in memory");

    // A remote sync apply (simulated: direct reload after touching nothing).
    mgr.ReloadFromStore();
    {
        const auto p = mgr.GetProvider("zai");
        Assert(p && p->status == "available", "runtime status carried over");
        Assert(p && p->apiKey == "sk-rt", "key still resolves after reload");
    }
}

void TestReservedNamespace() {
    std::cerr << "  [reserved] '__' provider ids rejected...\n";
    ProviderManager mgr;
    Json::Value body;
    body["provider_id"] = "__providers";
    body["provider_type"] = "zai";
    std::string err;
    Assert(!mgr.ValidateProviderPayload(body, true, &err), "'__' prefix rejected");
    body["provider_id"] = "plain-id";
    Assert(mgr.ValidateProviderPayload(body, true, &err),
           "ordinary id still valid: " + err);
}

void TestNoVault() {
    std::cerr << "  [no-vault] plaintext rows, round-trips intact...\n";
    const std::string dir = MakeDir();
    SqliteDataStore db{dir + "/db.sqlite"};
    AgentConfigStore config{&db};   // NO vault — legacy single-box shape
    ProviderManager mgr;
    mgr.Configure(MakeStorageOf(dir));
    mgr.ConfigureStore(&config);

    std::string err;
    Assert(mgr.LoadProviders(&err), "boot: " + err);
    ProviderState a;
    a.providerId = "zai";
    a.providerType = "zai";
    a.authType = "api_key";
    a.apiKey = "sk-novault";
    Assert(mgr.CreateProvider(a, &err), "create");
    Assert(mgr.SaveProviders(&err), "persist");
    Assert(config.GetRaw("__providers", "cfg.zai.api_key") == "sk-novault",
           "plaintext row without vault (legacy behavior)");
    mgr.ReloadFromStore();
    {
        const auto p = mgr.GetProvider("zai");
        Assert(p && p->apiKey == "sk-novault", "plaintext round-trips");
    }
}

// #186: pin the file-mode loud-refusal contract — persistence
// enabled with an empty providers path must refuse with a diagnostic,
// not silently proceed. At kernel boot the manager runs in store mode
// (empty path is legal there — it only disables legacy import), so the
// file-mode refusal is pinned here at its implementation site: a bare
// ProviderManager with no configured store.
void TestEmptyPathRefusal() {
    std::cerr << "  [empty-path] file-mode persistence with empty path refuses loudly...\n";
    ProviderManager mgr;  // no ConfigureStore -> file mode
    KernelConfig::ProviderConfigStorage s;
    s.persistToDisk = true;
    s.providersFilePath = "";
    s.authFilePath = "";
    mgr.Configure(s);

    std::string err;
    Assert(!mgr.LoadProviders(&err), "load must refuse empty providers path");
    Assert(err.find("providers file path must not be empty when persistence is enabled")
               != std::string::npos,
           "load refusal must be specific: " + err);
    Assert(!mgr.SaveProviders(&err), "save must refuse empty providers path");
    Assert(err.find("providers file path must not be empty when persistence is enabled")
               != std::string::npos,
           "save refusal must be specific: " + err);
}

int main() {
    std::cerr << "ProviderStore tests:\n";
    TestBootImport();
    TestCrudReconcile();
    TestReloadPreservesRuntime();
    TestReservedNamespace();
    TestNoVault();
    TestEmptyPathRefusal();
    if (g_failures == 0) std::cerr << "ALL PASSED\n";
    else std::cerr << g_failures << " FAILURE(S)\n";
    return g_failures == 0 ? 0 : 1;
}

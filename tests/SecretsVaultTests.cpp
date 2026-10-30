// SecretsVaultTests — encrypted secret storage for API packages (issue #23)
//
// Covers the four layers of the vault:
//   1. crypto boundary      — seal/open roundtrip, tamper rejection, wrong-key
//                             rejection (AES-256-GCM auth tag), keyfile hygiene
//   2. storage              — per-package namespacing, list/delete semantics
//   3. state <-> vault seam — ResolveState (3 resolution orders), SplitStateSecrets
//                             (literal vaulting, masked-echo, secret_ref pass-through),
//                             MigrateLegacyStateSecrets (sweep + idempotence)
//   4. lint gates           — manifest-level rejection of literal credentials:
//                             secret defaults, action request headers, connection
//                             headers_template (templated forms stay installable)
//
// Admin route wiring (GET/PUT/DELETE secrets) is exercised indirectly through
// the same SplitStateSecrets path the PUT handler calls; full-route tests ride
// AdminServerTests, which is currently red on the pre-existing #101 set.

#include "animus_kernel/api/SecretsVault.h"

#include "animus_kernel/AgentConfigStore.h"
#include "animus_kernel/ApiPackageStore.h"
#include "animus_kernel/SqliteDataStore.h"

#include <sys/stat.h>
#include <unistd.h>

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

std::string MakeDbPath() {
    static int n = 0;
    char tmp[] = "/tmp/animus_vault_test_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd >= 0) close(fd);
    unlink(tmp);  // fresh file per vault; keyfile derives from this path
    return std::string(tmp);
}

struct Fixture {
    std::string dbPath = MakeDbPath();
    std::string keyPath = dbPath + ".vault.key";
    SqliteDataStore db{dbPath};
    ApiPackageStore store{&db};
    SecretsVault vault{&db, keyPath};

    Fixture() {
        store.EnsureSchema();
        vault.EnsureSchema();
        vault.EnsureSchema();  // idempotent
    }
    ~Fixture() {
        unlink(dbPath.c_str());
        unlink(keyPath.c_str());
    }
};

Json::Value Parse(const std::string& s) {
    Json::Value v;
    std::istringstream ss(s);
    Json::CharReaderBuilder rb;
    std::string errs;
    Json::parseFromStream(rb, ss, &v, &errs);
    return v;
}

std::string Dump(const Json::Value& v) {
    Json::StreamWriterBuilder wb;
    return Json::writeString(wb, v);
}

// ---------------------------------------------------------------------------
// 1. crypto boundary
// ---------------------------------------------------------------------------

int TestCrypto() {
    std::cerr << "  [vault] seal/open roundtrip, tamper, wrong key, keyfile...\n";
    Fixture fx;

    std::string env, err;
    Assert(fx.vault.Seal("Bearer sk-live-abcdef", env, err), "seal ok");
    Assert(env.find("sk-live-abcdef") == std::string::npos, "ciphertext carries no plaintext");
    std::string out;
    Assert(fx.vault.Open(env, out, err) && out == "Bearer sk-live-abcdef", "open roundtrip");

    // tamper: flip one hex nibble in the ciphertext region (skip 13-byte prefix)
    Assert(env.size() > 32, "envelope has payload");
    std::string tampered = env;
    const char nib = tampered[40];
    tampered[40] = (nib == 'f') ? '0' : nib + 1;
    std::string tout;
    Assert(!fx.vault.Open(tampered, tout, err), "tampered envelope rejected (GCM tag)");

    // wrong key: same db content, different keyfile -> cannot open
    std::string env2;
    Assert(fx.vault.Seal("another-secret", env2, err), "seal 2 ok");
    {
        SqliteDataStore db2{fx.dbPath};  // vault entries already written
        SecretsVault stranger{&db2, fx.keyPath + ".stranger"};
        stranger.EnsureSchema();
        std::string sout;
        Assert(!stranger.Open(env2, sout, err), "wrong master key cannot open");
    }

    // keyfile created with tight permissions (POSIX)
    struct stat st{};
    Assert(stat(fx.keyPath.c_str(), &st) == 0, "keyfile exists");
    Assert((st.st_mode & 0777) == 0600, "keyfile mode is 0600");

    // disabled vault (empty keyPath) fails closed
    {
        SqliteDataStore db3{MakeDbPath()};
        SecretsVault off{&db3, ""};
        std::string e2;
        Assert(!off.Set("p1", "k", "v", e2), "disabled vault refuses Set");
        Assert(!off.Get("p1", "k").has_value(), "disabled vault refuses Get");
    }

    // malformed key files fail loudly (no silent whitespace eating)
    {
        const std::string kp = MakeDbPath() + ".key";
        FILE* f = fopen(kp.c_str(), "w");
        fputs("0000000000000000000000000000000000000000000000000000000000000000 \n", f);
        fclose(f);
        SqliteDataStore db4{MakeDbPath()};
        SecretsVault bad{&db4, kp};
        Assert(!bad.enabled(), "key file with trailing space rejected (not trimmed)");
        unlink(kp.c_str());
    }

    // symlinked key file is refused, not followed, and never clobbered
    {
        const std::string real = MakeDbPath() + ".realkey";
        FILE* f = fopen(real.c_str(), "w");
        fputs("0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n", f);
        fclose(f);
        const std::string link = MakeDbPath() + ".linkkey";
        unlink(link.c_str());
        if (symlink(real.c_str(), link.c_str()) == 0) {
            SqliteDataStore db5{MakeDbPath()};
            SecretsVault viaLink{&db5, link};
            Assert(!viaLink.enabled(), "symlinked key file rejected (O_NOFOLLOW)");
            struct stat sl {};
            Assert(lstat(link.c_str(), &sl) == 0 && S_ISLNK(sl.st_mode),
                   "symlink untouched (no clobber)");
            // regular file at the real path still loads fine
            SecretsVault direct{&db5, real};
            Assert(direct.enabled(), "regular key file loads after refusal");
        }
        unlink(link.c_str());
        unlink(real.c_str());
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 2. storage semantics
// ---------------------------------------------------------------------------

int TestStorage() {
    std::cerr << "  [vault] namespacing, list, delete, cascade...\n";
    Fixture fx;
    std::string err;

    Assert(fx.vault.Set("pkgA", "token", "A-secret", err), "set A");
    Assert(fx.vault.Set("pkgA", "refresh", "A-refresh", err), "set A2");
    Assert(fx.vault.Set("pkgB", "token", "B-secret", err), "set B");

    Assert(fx.vault.Get("pkgA", "token").value_or("") == "A-secret", "get A");
    Assert(!fx.vault.Get("pkgB", "refresh").has_value(), "per-package isolation");
    Assert(fx.vault.Has("pkgA", "refresh") && !fx.vault.Has("pkgB", "refresh"), "has");

    // overwrite same name
    Assert(fx.vault.Set("pkgA", "token", "A-secret-v2", err), "overwrite");
    Assert(fx.vault.Get("pkgA", "token").value_or("") == "A-secret-v2", "overwritten value");

    auto names = fx.vault.ListForPackage("pkgA");
    Assert(names.size() == 2, "list A has 2 entries");
    bool sawToken = false, sawRefresh = false;
    for (const auto& e : names) {
        Assert(e.updated_at_unix_ms > 0, "entry has timestamp");
        sawToken |= e.name == "token";
        sawRefresh |= e.name == "refresh";
    }
    Assert(sawToken && sawRefresh, "list names only, both present");

    Assert(fx.vault.Delete("pkgA", "refresh") && !fx.vault.Has("pkgA", "refresh"),
           "single delete");
    Assert(!fx.vault.Delete("pkgA", "refresh"), "double delete is a miss");
    Assert(fx.vault.Has("pkgB", "token"), "B untouched by A delete");

    fx.vault.DeleteForPackage("pkgA");
    Assert(!fx.vault.Has("pkgA", "token"), "cascade clears package");
    Assert(fx.vault.Has("pkgB", "token"), "cascade is per-package");
    return 0;
}

// ---------------------------------------------------------------------------
// 3. state <-> vault seam
// ---------------------------------------------------------------------------

Json::Value SecretSchema() {
    return Parse(R"({
        "token":   {"type": "string", "secret": true},
        "api_key": {"type": "string", "secret": true},
        "base":    {"type": "string", "default": "https://example.test"}
    })");
}

int TestResolveState() {
    std::cerr << "  [vault] ResolveState: own name, secret_ref, unset...\n";
    Fixture fx;
    std::string err;
    fx.vault.Set("p", "token", "REAL-TOKEN", err);
    fx.vault.Set("p", "alias", "ALIASED-KEY", err);

    Json::Value schema = SecretSchema();

    // order 2: vault entry named exactly the key
    Json::Value state = Parse(R"({"token": "STALE-LITERAL", "base": "https://x.test"})");
    fx.vault.ResolveState("p", schema, state);
    Assert(state["token"].asString() == "REAL-TOKEN", "vault entry wins over stale literal");
    Assert(state["base"].asString() == "https://x.test", "non-secret state untouched");

    // order 1: explicit {"secret_ref": "alias"} indirection
    Json::Value refd = Parse(R"({"api_key": {"secret_ref": "alias"}})");
    fx.vault.ResolveState("p", schema, refd);
    Assert(refd["api_key"].isString() && refd["api_key"].asString() == "ALIASED-KEY",
           "secret_ref resolves through vault");

    // order 3: unset secret (no vault entry, no literal) -> key stays absent;
    // a stale pre-migration literal is KEPT as valid in-memory (boot migration
    // moves it to the vault; dropping it here would break working packages).
    Json::Value empty = Parse(R"({"base": "https://keep.test"})");
    fx.vault.ResolveState("q", schema, empty);  // package q has no vault entries
    Assert(!empty.isMember("token"), "unset secret resolves absent");
    Assert(empty.isMember("base"), "non-secrets kept");
    Json::Value stale = Parse(R"({"token": "PRE-MIGRATION-LITERAL"})");
    fx.vault.ResolveState("q", schema, stale);
    Assert(stale["token"].asString() == "PRE-MIGRATION-LITERAL",
           "pre-migration literal stays valid until the sweep moves it");

    // ref to unset entry -> loud error + key dropped (missing-key machinery
    // then names it at the use site; dangling refs must not interpolate empty)
    Json::Value badRef = Parse(R"({"api_key": {"secret_ref": "missing"}})");
    fx.vault.ResolveState("p", schema, badRef);
    Assert(!badRef.isMember("api_key"),
           "dangling secret_ref removed with error log");
    return 0;
}

int TestSplitStateSecrets() {
    std::cerr << "  [vault] SplitStateSecrets: vault, masked-echo, refs...\n";
    Fixture fx;
    std::string err;
    Json::Value schema = SecretSchema();

    // literal on a secret-typed key -> vaulted + stripped from state
    Json::Value state = Parse(R"({"token": "LIT-1", "base": "https://keep.test"})");
    int n = fx.vault.SplitStateSecrets("p1", schema, state, err);
    Assert(n == 1, "one secret vaulted");
    Assert(!state.isMember("token"), "literal stripped from state");
    Assert(state["base"].asString() == "https://keep.test", "non-secret kept");
    Assert(fx.vault.Get("p1", "token").value_or("") == "LIT-1", "value landed in vault");

    // masked echo (***) -> not vaulted, display artifact dropped from state
    Json::Value masked = Parse(R"({"api_key": "***"})");
    Assert(fx.vault.SplitStateSecrets("p1", schema, masked, err) == 0, "masked echo no-op");
    Assert(!masked.isMember("api_key"), "mask never vaulted, dropped from state");
    Assert(!fx.vault.Has("p1", "api_key"), "mask never vaulted");

    // secret_ref object -> passes through verbatim (config, not a literal)
    Json::Value ref = Parse(R"({"api_key": {"secret_ref": "shared"}})");
    Assert(fx.vault.SplitStateSecrets("p1", schema, ref, err) == 0, "ref pass-through");
    Assert(ref["api_key"].isObject(), "ref object kept");

    // non-secret-typed key with credential-ish value -> untouched (schema decides)
    Json::Value notSecret = Parse(R"({"base": "https://user:pass@host"})");
    Assert(fx.vault.SplitStateSecrets("p1", schema, notSecret, err) == 0, "schema is authority");

    // secret_ref carrying a new literal under "value" (UI write-through) -> vaulted
    fx.vault.Set("p1", "shared", "OLD", err);
    Json::Value wt = Parse(R"({"api_key": {"secret_ref": "shared", "value": "NEW"}})");
    Assert(fx.vault.SplitStateSecrets("p1", schema, wt, err) == 1, "write-through vaults value");
    Assert(fx.vault.Get("p1", "shared").value_or("") == "NEW", "write-through updates target");
    Assert(wt["api_key"].isObject() && wt["api_key"].isMember("secret_ref") &&
               !wt["api_key"].isMember("value"),
           "ref kept, literal dropped");

    // malformed ref shapes are errors, never silently normalized
    Json::Value junk = Parse(R"({"api_key": {"secret_ref": "shared", "junk": true}})");
    Assert(fx.vault.SplitStateSecrets("p1", schema, junk, err) == -1,
           "ref object with unexpected member rejected");
    Json::Value badName = Parse(R"({"api_key": {"secret_ref": "not a name!"}})");
    Assert(fx.vault.SplitStateSecrets("p1", schema, badName, err) == -1,
           "invalid ref name rejected");
    Json::Value nonStr = Parse(R"({"api_key": {"secret_ref": "shared", "value": 123}})");
    Assert(fx.vault.SplitStateSecrets("p1", schema, nonStr, err) == -1,
           "non-string write-through value rejected");
    Json::Value maskedVal = Parse(R"({"api_key": {"secret_ref": "shared", "value": "***"}})");
    Assert(fx.vault.SplitStateSecrets("p1", schema, maskedVal, err) == -1,
           "masked write-through value rejected");
    return 0;
}

int TestMigration() {
    std::cerr << "  [vault] legacy literal migration: sweep, rewrite, idempotent...\n";
    Fixture fx;

    // install a package whose stored state carries legacy literal secrets
    const std::string manifest = R"({
        "kind": "api_package", "name": "legacy-pkg", "version": "1.0.0",
        "description": "migration fixture",
        "state_schema": {
            "token":  {"type": "string", "secret": true},
            "secret2": {"type": "string", "secret": true},
            "plain":  {"type": "string"}
        },
        "commands": [],
        "connections": []
    })";
    ApiPackage pkg = fx.store.InstallFromManifest(manifest);
    fx.store.SetPackageState(pkg.id,
        R"({"token":"LEGACY-A","secret2":"LEGACY-B","plain":"stays"})");

    std::string err;
    int moved = fx.vault.MigrateLegacyStateSecrets(fx.store, err);
    Assert(moved == 2, "two legacy secrets migrated (got " + std::to_string(moved) + ")");

    auto now = fx.store.GetPackage(pkg.id);
    Assert(now->state.find("LEGACY-A") == std::string::npos &&
               now->state.find("LEGACY-B") == std::string::npos,
           "state JSON no longer holds secrets");
    Assert(now->state.find("stays") != std::string::npos, "non-secret state intact");
    Assert(fx.vault.Get(pkg.id, "token").value_or("") == "LEGACY-A" &&
               fx.vault.Get(pkg.id, "secret2").value_or("") == "LEGACY-B",
           "values preserved in vault");

    // idempotent: second sweep is a no-op
    Assert(fx.vault.MigrateLegacyStateSecrets(fx.store, err) == 0, "second sweep moves nothing");

    // runtime resolution end-to-end: resolved state contains the secret again
    Json::Value schema = Parse(now->state_schema.empty() ? "{}" : now->state_schema);
    Json::Value state = Parse(now->state.empty() ? "{}" : now->state);
    fx.vault.ResolveState(pkg.id, schema, state);
    Assert(state["token"].asString() == "LEGACY-A", "post-migration resolution round-trips");
    return 0;
}

// ---------------------------------------------------------------------------
// 4. manifest lint gates
// ---------------------------------------------------------------------------

std::string ManifestWith(const std::string& stateSchema,
                         const std::string& command,
                         const std::string& connection) {
    return R"({
        "kind": "api_package", "name": "lint-pkg", "version": "1.0.0",
        "description": "lint fixture",
        "state_schema": )" + stateSchema + R"(,
        "commands": [)" + command + R"(],
        "connections": [)" + connection + R"(]
    })";
}

bool InstallThrowsWith(Fixture& fx, const std::string& manifest, const std::string& needle) {
    try {
        fx.store.InstallFromManifest(manifest);
        return false;
    } catch (const std::runtime_error& e) {
        return std::string(e.what()).find(needle) != std::string::npos;
    }
}

int TestLintBodyAndUrlGates() {
    Fixture fx;
    const std::string okSchema = R"({"token": {"type": "string", "secret": true},
                                     "base":  {"type": "string", "default": "https://x.test"}})";
    const std::string tmplTok = std::string("{{state") + ".token}}";
    const std::string okConn = R"({"name": "poll", "type": "longpoll",
        "url_template": "{{state.base}}/poll",
        "headers_template": {"X-Api-Token": ")" + tmplTok + R"("},
        "poll": {"cursor_path": "next", "interval_s": 5, "dispatch": {"command": "fetch"}},
        "hooks": {}})";
    const std::string okAction = R"({"name": "fetch", "kind": "action", "description": "d",
        "request": {"method": "GET", "url": "{{state.base}}/x",
                    "headers": {"Authorization": ")" + tmplTok + R"("}},
        "script": "function run(ctx) return {output='ok'} end"})";

    // baseline: templated everywhere -> installs
    try {
        fx.store.InstallFromManifest(ManifestWith(okSchema, okAction, okConn));
    } catch (const std::runtime_error& e) {
        Assert(false, "baseline body/url manifest installs (got: " + std::string(e.what()) + ")");
    }

    // literal api_key in action request body JSON
    Assert(InstallThrowsWith(fx,
            ManifestWith(okSchema, R"({"name": "post", "kind": "action", "description": "d",
                "request": {"method": "POST", "url": "{{state.base}}/orders",
                            "headers": {"Authorization": ")" + tmplTok + R"("},
                            "body": "{\"api_key\": \"sk-liv-qq77-notreal\", \"qty\": 2}"},
                "script": "function run(ctx) return {output='ok'} end"})", okConn),
            "forbidden in package files"),
           "literal api_key in body rejected (#104)");

    // templated credential in body installs
    try {
        fx.store.InstallFromManifest(ManifestWith(okSchema, R"({"name": "post2", "kind": "action", "description": "d",
            "request": {"method": "POST", "url": "{{state.base}}/orders",
                        "headers": {"Authorization": ")" + tmplTok + R"("},
                        "body": "{\"api_key\": \")" + tmplTok + R"(\", \"qty\": 2}"},
            "script": "function run(ctx) return {output='ok'} end"})", okConn));
    } catch (const std::runtime_error& e) {
        Assert(false, "templated body credential installs (got: " + std::string(e.what()) + ")");
    }

    // literal credential as url query param value
    Assert(InstallThrowsWith(fx,
            ManifestWith(okSchema, R"({"name": "q", "kind": "action", "description": "d",
                "request": {"method": "GET", "url": "{{state.base}}/v2?key=sk-liv-qq88-notreal",
                            "headers": {"Authorization": ")" + tmplTok + R"("}},
                "script": "function run(ctx) return {output='ok'} end"})", okConn),
            "forbidden in package files"),
           "literal key= query param rejected (#104)");

    // templated query param installs
    try {
        fx.store.InstallFromManifest(ManifestWith(okSchema, R"({"name": "q2", "kind": "action", "description": "d",
            "request": {"method": "GET", "url": "{{state.base}}/v2?key=)" + tmplTok + R"(",
                        "headers": {"Authorization": ")" + tmplTok + R"("}},
            "script": "function run(ctx) return {output='ok'} end"})", okConn));
    } catch (const std::runtime_error& e) {
        Assert(false, "templated query param installs (got: " + std::string(e.what()) + ")");
    }

    // non-credential query params stay untouched (no false positive)
    try {
        fx.store.InstallFromManifest(ManifestWith(okSchema, R"({"name": "q3", "kind": "action", "description": "d",
            "request": {"method": "GET", "url": "{{state.base}}/v2?market=BTC-EUR&limit=10",
                        "headers": {"Authorization": ")" + tmplTok + R"("}},
            "script": "function run(ctx) return {output='ok'} end"})", okConn));
    } catch (const std::runtime_error& e) {
        Assert(false, "non-credential query param installs (got: " + std::string(e.what()) + ")");
    }

    // nested credential inside body JSON object
    Assert(InstallThrowsWith(fx,
            ManifestWith(okSchema, R"({"name": "post3", "kind": "action", "description": "d",
                "request": {"method": "POST", "url": "{{state.base}}/orders",
                            "headers": {"Authorization": ")" + tmplTok + R"("},
                            "body": "{\"order\": {\"password\": \"hunter2-notreal\", \"qty\": 2}}"},
                "script": "function run(ctx) return {output='ok'} end"})", okConn),
            "forbidden in package files"),
           "nested credential in body rejected (#104)");

    // literal credential inside connection poll config
    Assert(InstallThrowsWith(fx,
            ManifestWith(okSchema, okAction, R"({"name": "poll3", "type": "longpoll",
                "url_template": "{{state.base}}/poll",
                "headers_template": {"X-Api-Token": ")" + tmplTok + R"("},
                "poll": {"cursor_path": "next", "interval_s": 5,
                         "dispatch": {"command": "fetch"},
                         "headers": {"Authorization": "Basic bGl0ZXJhbC1ub3RyZWFs"}},
                "hooks": {}})"),
            "forbidden in package files"),
           "literal credential in poll config rejected (#104)");

    return 0;
}


int TestLintGates() {
    std::cerr << "  [vault] lint: secret defaults, literal credential headers...\n";
    Fixture fx;

    const std::string okAction = R"({"name": "fetch", "kind": "action", "description": "d",
        "request": {"method": "GET", "url": "{{state.base}}/x",
                    "headers": {"Authorization": "Bearer {{state.token}}"}},
        "script": "function run(ctx) return {output='ok'} end"})";
    const std::string okConn = R"({"name": "poll", "type": "longpoll",
        "url_template": "{{state.base}}/poll",
        "headers_template": {"Authorization": "Bearer {{state.token}}"},
        "poll": {"cursor_path": "next", "interval_s": 5, "dispatch": {"command": "fetch"}},
        "hooks": {}})";
    const std::string okSchema = R"({"token": {"type": "string", "secret": true},
                                     "base":  {"type": "string", "default": "https://x.test"}})";

    // baseline: everything templated -> installs
    try {
        fx.store.InstallFromManifest(ManifestWith(okSchema, okAction, okConn));
    } catch (const std::runtime_error& e) {
        Assert(false, "templated manifest installs (got: " + std::string(e.what()) + ")");
    }

    // secret-typed key with a default
    Assert(InstallThrowsWith(fx,
            ManifestWith(R"({"token": {"type": "string", "secret": true, "default": "sk-lit"}})",
                         okAction, okConn),
            "must not declare a default"),
           "secret default rejected");

    // secret-typed key with a non-string type (masking/vault assume strings)
    Assert(InstallThrowsWith(fx,
            ManifestWith(R"({"token": {"type": "object", "secret": true}})",
                         okAction, okConn),
            "must be of type string"),
           "non-string secret type rejected");

    // credential header templated from an unknown root (env.X) — grammar check
    Assert(InstallThrowsWith(fx,
            ManifestWith(okSchema, R"({"name": "fetch", "kind": "action", "description": "d",
                "request": {"method": "GET", "url": "{{state.base}}/x",
                            "headers": {"Authorization": "***}}"}},
                "script": "function run(ctx) return {output='ok'} end"})", okConn),
            "state.* / args.*"),
           "unknown-root template in auth header rejected");

    // literal Authorization in action request headers
    Assert(InstallThrowsWith(fx,
            ManifestWith(okSchema, R"({"name": "fetch", "kind": "action", "description": "d",
                "request": {"method": "GET", "url": "{{state.base}}/x",
                            "headers": {"Authorization": "Bearer sk-live-literal"}},
                "script": "function run(ctx) return {output='ok'} end"})", okConn),
            "forbidden in package files"),
           "literal auth header rejected (action)");

    // literal X-Api-Key in connection headers_template (substring family)
    Assert(InstallThrowsWith(fx,
            ManifestWith(okSchema, okAction, R"({"name": "poll", "type": "longpoll",
                "url_template": "{{state.base}}/poll",
                "headers_template": {"X-Api-Key": "abc123"},
                "poll": {"cursor_path": "next", "interval_s": 5, "dispatch": {"command": "fetch"}},
                "hooks": {}})"),
            "forbidden in package files"),
           "literal api-key header rejected (connection)");

    // templated credential header stays installable (re-check via schema w/o default)
    try {
        fx.store.InstallFromManifest(ManifestWith(okSchema, okAction,
            R"({"name": "poll2", "type": "longpoll",
                "url_template": "{{state.base}}/poll",
                "headers_template": {"X-Api-Token": "tok-{{state.token}}"},
                "poll": {"cursor_path": "next", "interval_s": 5, "dispatch": {"command": "fetch"}},
                "hooks": {}})"));
    } catch (const std::runtime_error& e) {
        Assert(false, "templated api-token header installs (got: " + std::string(e.what()) + ")");
    }
    return 0;
}

}  // namespace

// ── #93 P3a: agent-scoped secrets (channel/provider credentials) ─────────

void TestAgentScope() {
    std::cerr << "  [agent-scope] namespacing, ref write/read, disabled vault...\n";
    // Agent secrets live in the SAME vault under "agent:<id>" — the colon
    // cannot appear in package ids, so the namespaces cannot collide. A
    // package with the same secret name never sees an agent secret and
    // vice versa.
    std::string dbPath = MakeDbPath();
    SqliteDataStore db{dbPath};
    ApiPackageStore store{&db};
    SecretsVault vault{&db, dbPath + ".vault.key"};
    store.EnsureSchema();
    vault.EnsureSchema();

    std::string err;
    Assert(vault.VaultAgentValue("default", "telegram_bot_token", "tok-123", err),
           "vault agent write");
    Assert(vault.Has(vault.AgentScope("default"), "telegram_bot_token"),
           "agent entry visible under agent scope");
    Assert(!vault.Has("some-package-id", "telegram_bot_token"),
           "agent entry NOT visible under an arbitrary package scope");
    auto got = vault.ResolveAgentValue("default",
        "{\"secret_ref\":\"telegram_bot_token\"}");
    Assert(got == "tok-123", "ResolveAgentValue opens the ref");
    Assert(vault.ResolveAgentValue("default", "plain-value") == "plain-value",
           "non-ref value passes through untouched");
    // Cross-agent isolation
    Assert(vault.ResolveAgentValue("other", "{\"secret_ref\":\"telegram_bot_token\"}")
               .empty(),
           "agent scope isolates per agent");

    // Disabled vault: VaultAgentValue fails closed
    SqliteDataStore db2{MakeDbPath()};
    SecretsVault disabled{&db2, ""};   // empty key path = disabled
    std::string derr;
    Assert(!disabled.VaultAgentValue("default", "name", "value", derr),
           "disabled vault refuses agent writes");
}

void TestConfigStoreIntegration() {
    std::cerr << "  [config-store] write intercept, transparent resolution, raw read...\n";
    std::string dbPath = MakeDbPath();
    SqliteDataStore db{dbPath};
    SecretsVault vault{&db, dbPath + ".vault.key"};
    vault.EnsureSchema();
    AgentConfigStore config{&db};
    config.SetVault(&vault);

    // Credential-shaped Set -> row carries a ref object, not plaintext
    config.Set("default", "channels.telegram:main.bot_token", "AAA-plaintext");
    std::string raw = config.GetRaw("default", "channels.telegram:main.bot_token");
    Assert(raw.find("secret_ref") != std::string::npos,
           "credential Set stored as secret_ref object");
    Assert(raw.find("AAA-plaintext") == std::string::npos,
           "plaintext never lands in the row");
    Assert(config.Get("default", "channels.telegram:main.bot_token") == "AAA-plaintext",
           "Get resolves the ref transparently");

    // Non-credential Set -> plaintext passthrough
    config.Set("default", "channels.telegram:main.group_id", "12345");
    Assert(config.Get("default", "channels.telegram:main.group_id") == "12345",
           "non-credential value untouched");
    Assert(config.GetRaw("default", "channels.telegram:main.group_id") == "12345",
           "non-credential raw == resolved");

    // Heuristic boundary: 'handle' and 'did' (bluesky) are NOT credential-shaped
    config.Set("default", "channels.bluesky:personal.handle", "@kestrel");
    Assert(config.GetRaw("default", "channels.bluesky:personal.handle") == "@kestrel",
           "non-credential key stores raw");

    // Overwrite with a new plaintext -> same vault name, new value
    config.Set("default", "channels.telegram:main.bot_token", "BBB-rotated");
    Assert(config.Get("default", "channels.telegram:main.bot_token") == "BBB-rotated",
           "credential rotation resolves to the new value");
    Assert(config.GetRaw("default", "channels.telegram:main.bot_token")
               .find("BBB-rotated") == std::string::npos,
           "rotated plaintext still not in the row");
}

void TestAgentMigration() {
    std::cerr << "  [agent-migration] sweep, idempotence, non-credentials untouched...\n";
    std::string dbPath = MakeDbPath();
    SqliteDataStore db{dbPath};
    SecretsVault vault{&db, dbPath + ".vault.key"};
    vault.EnsureSchema();
    AgentConfigStore config0{&db};   // creates agent_config (pre-P3a shape:
    config0.SetVault(&vault);        // table exists, values still plaintext)

    // Seed plaintext credentials directly (pre-P3a database shape)
    {
        auto ins = db.Prepare(
            "INSERT INTO agent_config (agent_id, key, value) VALUES (?, ?, ?)");
        ins->BindText(1, "default");
        ins->BindText(2, "channels.telegram:main.bot_token");
        ins->BindText(3, "legacy-plaintext-tok");
        ins->ExecDML();
        auto ins2 = db.Prepare(
            "INSERT INTO agent_config (agent_id, key, value) VALUES (?, ?, ?)");
        ins2->BindText(1, "buffett");
        ins2->BindText(2, "channels.discord:trading.api_key");
        ins2->BindText(3, "legacy-key-456");
        ins2->ExecDML();
        auto ins3 = db.Prepare(
            "INSERT INTO agent_config (agent_id, key, value) VALUES (?, ?, ?)");
        ins3->BindText(1, "default");
        ins3->BindText(2, "channels.irc:libera.nick");
        ins3->BindText(3, "kestrel");
        ins3->ExecDML();
    }
    std::string err;
    const int migrated = vault.MigrateAgentConfigSecrets(err);
    Assert(migrated == 2, "two credential rows migrated (nick untouched), got " +
                              std::to_string(migrated));
    Assert(err.empty(), "no migration error");

    AgentConfigStore config{&db};
    config.SetVault(&vault);
    Assert(config.Get("default", "channels.telegram:main.bot_token") ==
               "legacy-plaintext-tok",
           "migrated credential still resolves via vault");
    Assert(config.Get("buffett", "channels.discord:trading.api_key") == "legacy-key-456",
           "second agent migrated too");
    Assert(config.Get("default", "channels.irc:libera.nick") == "kestrel",
           "non-credential untouched by sweep");

    // Idempotence: second sweep migrates nothing
    const int again = vault.MigrateAgentConfigSecrets(err);
    Assert(again == 0, "second sweep migrates zero, got " + std::to_string(again));

    // Raw rows carry refs, not plaintext
    std::string raw = config.GetRaw("default", "channels.telegram:main.bot_token");
    Assert(raw.find("legacy-plaintext-tok") == std::string::npos,
           "plaintext gone from the raw row");
}

// ── #108 audit: vault hardening regressions ──────────────────────────────

void TestAuditF1JsonObjectCredentials() {
    std::cerr << "  [audit-f1] JSON-object credentials are vaulted, not bypassed...\n";
    std::string dbPath = MakeDbPath();
    SqliteDataStore db{dbPath};
    SecretsVault vault{&db, dbPath + ".vault.key"};
    vault.EnsureSchema();
    AgentConfigStore config{&db};
    config.SetVault(&vault);

    // The old '{'-prefix exemption let this through raw
    config.Set("default", "channels.matrix:home.bot_token",
               "{\"access_token\":\"syt_plain\",\"device_id\":\"DEV1\"}");
    std::string raw = config.GetRaw("default", "channels.matrix:home.bot_token");
    Assert(raw.find("secret_ref") != std::string::npos,
           "JSON credential stored as ref object");
    Assert(raw.find("syt_plain") == std::string::npos,
           "JSON credential plaintext never lands in the row");
    Assert(config.Get("default", "channels.matrix:home.bot_token")
               .find("syt_plain") != std::string::npos,
           "Get returns the serialized JSON credential verbatim");

    // A legit ref object still passes through untouched (no double vault)
    config.Set("default", "channels.matrix:home.bot_token",
               "{\"secret_ref\":\"custom_name\"}");
    Assert(config.GetRaw("default", "channels.matrix:home.bot_token")
               .find("custom_name") != std::string::npos,
           "valid ref object passes through on write");
    Assert(config.GetRaw("default", "channels.matrix:home.bot_token")
               .find("secret_ref") != std::string::npos,
           "ref object not re-vaulted");
}

void TestAuditF2NameCollision() {
    std::cerr << "  [audit-f2] colliding keys derive distinct vault names...\n";
    std::string dbPath = MakeDbPath();
    SqliteDataStore db{dbPath};
    SecretsVault vault{&db, dbPath + ".vault.key"};
    vault.EnsureSchema();
    AgentConfigStore config{&db};
    config.SetVault(&vault);

    // ':' and '.' both sanitize to '_' — pre-fix these two shared ONE vault
    // entry; the second Set clobbered the first and BOTH rows resolved to
    // the wrong credential.
    config.Set("default", "channels.foo:bar.bot_token", "AAA-first");
    config.Set("default", "channels.foo.bar.bot_token", "BBB-second");
    Assert(config.Get("default", "channels.foo:bar.bot_token") == "AAA-first",
           "colon key resolves to ITS credential");
    Assert(config.Get("default", "channels.foo.bar.bot_token") == "BBB-second",
           "dot key resolves to ITS credential");
    const std::string r1 = config.GetRaw("default", "channels.foo:bar.bot_token");
    const std::string r2 = config.GetRaw("default", "channels.foo.bar.bot_token");
    Assert(r1 != r2, "the two rows carry distinct refs");
}

void TestAuditF3GetAllResolves() {
    std::cerr << "  [audit-f3] GetAll resolves refs like Get...\n";
    std::string dbPath = MakeDbPath();
    SqliteDataStore db{dbPath};
    SecretsVault vault{&db, dbPath + ".vault.key"};
    vault.EnsureSchema();
    AgentConfigStore config{&db};
    config.SetVault(&vault);

    config.Set("default", "channels.telegram:main.bot_token", "tok-xyz");
    config.Set("default", "channels.telegram:main.group_id", "42");
    auto all = config.GetAll("default");
    Assert(all.count("channels.telegram:main.bot_token") &&
               all["channels.telegram:main.bot_token"] == "tok-xyz",
           "GetAll returns the RESOLVED credential");
    Assert(all.count("channels.telegram:main.group_id") &&
               all["channels.telegram:main.group_id"] == "42",
           "non-credential value unchanged through GetAll");
}

void TestAuditMigrationJsonObject() {
    std::cerr << "  [audit-mig] migration vaults JSON credentials too...\n";
    std::string dbPath = MakeDbPath();
    SqliteDataStore db{dbPath};
    SecretsVault vault{&db, dbPath + ".vault.key"};
    vault.EnsureSchema();
    AgentConfigStore config{&db};
    config.SetVault(&vault);

    {   // pre-P3a shape: JSON credential sitting raw in the row
        auto ins = db.Prepare(
            "INSERT INTO agent_config (agent_id, key, value) VALUES (?, ?, ?)");
        ins->BindText(1, "default");
        ins->BindText(2, "channels.slack:work.api_key");
        ins->BindText(3, "{\"key\":\"sk_live_raw\"}");
        ins->ExecDML();
    }
    std::string err;
    const int migrated = vault.MigrateAgentConfigSecrets(err);
    Assert(migrated == 1, "JSON credential migrated, got " + std::to_string(migrated));
    const std::string raw = config.GetRaw("default", "channels.slack:work.api_key");
    Assert(raw.find("sk_live_raw") == std::string::npos,
           "JSON credential plaintext gone from the row");
    Assert(config.Get("default", "channels.slack:work.api_key")
               .find("sk_live_raw") != std::string::npos,
           "migrated JSON credential resolves verbatim");
}

void TestDisabledVaultPassthrough() {
    std::cerr << "  [disabled-vault] config store without a vault = legacy behavior...\n";
    std::string dbPath = MakeDbPath();
    SqliteDataStore db{dbPath};
    AgentConfigStore config{&db};   // NO SetVault — single-box legacy shape

    config.Set("default", "channels.telegram:main.bot_token", "plain");
    Assert(config.Get("default", "channels.telegram:main.bot_token") == "plain",
           "no vault -> plaintext round-trip (legacy)");
}

int main() {
    std::cerr << "SecretsVault tests:\n";
    TestCrypto();
    TestStorage();
    TestResolveState();
    TestSplitStateSecrets();
    TestMigration();
    TestLintGates();
    TestLintBodyAndUrlGates();
    TestAgentScope();
    TestConfigStoreIntegration();
    TestAgentMigration();
    TestAuditF1JsonObjectCredentials();
    TestAuditF2NameCollision();
    TestAuditF3GetAllResolves();
    TestAuditMigrationJsonObject();
    TestDisabledVaultPassthrough();
    if (g_failures == 0) {
        std::cerr << "SecretsVault tests: ALL PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " failures.\n";
    return 1;
}

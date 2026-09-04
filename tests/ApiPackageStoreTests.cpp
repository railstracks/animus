// ApiPackageStoreTests — persistence layer for the API Package Framework
// (build order b). Covers: schema idempotency, package CRUD + uniqueness,
// command/connection CRUD + replace, per-agent enablement overlay (D13),
// explicit cascade deletes, state roundtrip, and manifest v1 install lint.

#include "animus_kernel/ApiPackageStore.h"
#include "animus_kernel/SqliteDataStore.h"

#include <iostream>
#include <string>
#include <unistd.h>

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
    char tmp[] = "/tmp/animus_apipkg_test_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd >= 0) close(fd);
    return std::string(tmp) + ".db";
}

ApiPackage MakePackage(const std::string& name) {
    ApiPackage p;
    p.name = name;
    p.description = "test package";
    p.version = "0.1.0";
    p.keywords = "[\"test\"]";
    p.state_schema = "{\"token\":{\"type\":\"string\",\"secret\":true}}";
    return p;
}

int TestSchemaIdempotent() {
    std::cerr << "  [store] EnsureSchema is idempotent...\n";
    const std::string path = MakeTempDbPath();
    {
        SqliteDataStore db(path);
        ApiPackageStore store(&db);
        store.EnsureSchema();  // second call must not throw
        ApiPackage p = store.CreatePackage(MakePackage("idem"));
        Assert(store.GetPackageByName("idem").has_value(), "package exists");
    }
    unlink(path.c_str());
    return 0;
}

int TestPackageCrud() {
    std::cerr << "  [store] package CRUD + uniqueness + state...\n";
    const std::string path = MakeTempDbPath();
    {
        SqliteDataStore db(path);
        ApiPackageStore store(&db);

        ApiPackage p = store.CreatePackage(MakePackage("alpaca"));
        Assert(!p.id.empty(), "id generated");
        Assert(p.created_at_unix_ms > 0, "timestamps set");
        Assert(!p.enabled, "installs disabled by default");

        bool dup = false;
        try { store.CreatePackage(MakePackage("alpaca")); }
        catch (const std::runtime_error&) { dup = true; }
        Assert(dup, "duplicate name throws");

        auto got = store.GetPackageByName("alpaca");
        Assert(got.has_value() && got->id == p.id, "get by name");
        Assert(got->keywords == "[\"test\"]", "keywords roundtrip");
        Assert(got->state_schema.find("secret") != std::string::npos, "state_schema roundtrip");

        Assert(store.SetPackageEnabled(p.id, true), "enable");
        Assert(store.GetPackage(p.id)->enabled, "enabled persisted");
        Assert(!store.SetPackageEnabled("no-such-id", true), "enable missing id false");

        Assert(store.SetPackageState(p.id, "{\"token\":\"abc\"}"), "set state");
        Assert(store.GetPackage(p.id)->state.find("abc") != std::string::npos, "state roundtrip");
        Assert(!store.SetPackageState(p.id, "[1,2]"), "array state rejected");
        Assert(!store.SetPackageState(p.id, "not json"), "garbage state rejected");

        got->version = "0.2.0";
        got->locally_modified = true;
        Assert(store.UpdatePackageMeta(*got), "meta update");
        auto again = store.GetPackage(p.id);
        Assert(again->version == "0.2.0" && again->locally_modified, "meta roundtrip");

        Assert(store.DeletePackage(p.id), "delete");
        Assert(!store.GetPackage(p.id).has_value(), "gone after delete");
        Assert(!store.DeletePackage(p.id), "second delete false");
    }
    unlink(path.c_str());
    return 0;
}

int TestCommandsAndConnections() {
    std::cerr << "  [store] command/connection CRUD + replace...\n";
    const std::string path = MakeTempDbPath();
    {
        SqliteDataStore db(path);
        ApiPackageStore store(&db);
        ApiPackage p = store.CreatePackage(MakePackage("pkg"));

        ApiPackageCommand c;
        c.package_id = p.id;
        c.name = "portfolio list";
        c.kind = "action";
        c.description = "List positions";
        c.script = "function run(ctx) return {output='x'} end";
        c.request = "{\"method\":\"GET\"}";
        store.AddCommand(c);

        bool dup = false;
        try { store.AddCommand(c); } catch (const std::runtime_error&) { dup = true; }
        Assert(dup, "duplicate command name throws");

        auto cmds = store.ListCommands(p.id);
        Assert(cmds.size() == 1, "one command");
        Assert(cmds[0].name == "portfolio list" && cmds[0].request == "{\"method\":\"GET\"}",
               "command roundtrip");

        ApiPackageCommand h;
        h.package_id = p.id;
        h.name = "on ticker";
        h.kind = "hook";
        h.event = "on_message";
        h.description = "Ticker updates";
        h.script = "function run(ctx) return {output='y'} end";
        store.AddCommand(h);
        cmds = store.ListCommands(p.id);
        Assert(cmds.size() == 2 && cmds[0].kind == "action", "actions sort before hooks");

        c.script = "function run(ctx) return {output='z'} end";
        Assert(store.ReplaceCommands(p.id, {c, h}) == 2, "replace 2");
        Assert(store.GetCommand(p.id, "portfolio list")->script.find("'z'") != std::string::npos,
               "replace persisted");
        Assert(store.DeleteCommand(cmds[1].id), "delete command");

        ApiPackageConnection conn;
        conn.package_id = p.id;
        conn.name = "stream";
        conn.type = "websocket";
        conn.url_template = "wss://example.test/v2";
        conn.hooks = "{\"on_connect\":\"subscribe\"}";
        store.AddConnection(conn);
        dup = false;
        try { store.AddConnection(conn); } catch (const std::runtime_error&) { dup = true; }
        Assert(dup, "duplicate connection name throws");
        auto conns = store.ListConnections(p.id);
        Assert(conns.size() == 1 && conns[0].hooks.find("on_connect") != std::string::npos,
               "connection roundtrip");
        Assert(store.ReplaceConnections(p.id, {conn}) == 1, "replace connections");
    }
    unlink(path.c_str());
    return 0;
}

int TestCascadeDelete() {
    std::cerr << "  [store] package delete cascades to children + agent rows...\n";
    const std::string path = MakeTempDbPath();
    {
        SqliteDataStore db(path);
        ApiPackageStore store(&db);
        ApiPackage p = store.CreatePackage(MakePackage("cascade"));

        ApiPackageCommand c;
        c.package_id = p.id; c.name = "go"; c.kind = "action";
        c.description = "d"; c.script = "s";
        store.AddCommand(c);

        ApiPackageConnection conn;
        conn.package_id = p.id; conn.name = "ws"; conn.type = "websocket";
        conn.url_template = "wss://x";
        store.AddConnection(conn);

        store.SetAgentEnablement(p.id, "agent-a", true);
        Assert(store.DeletePackage(p.id), "delete cascades");
        Assert(store.ListCommands(p.id).empty(), "commands gone");
        Assert(store.ListConnections(p.id).empty(), "connections gone");
        Assert(!store.GetAgentEnablement(p.id, "agent-a").has_value(), "agent rows gone");
    }
    unlink(path.c_str());
    return 0;
}

int TestAgentOverlay() {
    std::cerr << "  [store] per-agent enablement overlay (D13)...\n";
    const std::string path = MakeTempDbPath();
    {
        SqliteDataStore db(path);
        ApiPackageStore store(&db);
        ApiPackage p = store.CreatePackage(MakePackage("overlay"));

        // Package disabled -> effective false regardless of rows.
        Assert(!store.EffectiveEnabled(p.id, "agent-a"), "disabled package -> false");
        store.SetAgentEnablement(p.id, "agent-a", true);
        Assert(!store.EffectiveEnabled(p.id, "agent-a"), "row can't override disabled package");

        store.SetPackageEnabled(p.id, true);
        Assert(store.EffectiveEnabled(p.id, "agent-a"), "enabled row + enabled package");
        Assert(store.EffectiveEnabled(p.id, "agent-b"), "no row inherits default (enabled)");

        store.SetAgentEnablement(p.id, "agent-b", false);
        Assert(!store.EffectiveEnabled(p.id, "agent-b"), "disabled row blocks agent");
        store.SetAgentEnablement(p.id, "agent-b", true);
        Assert(store.EffectiveEnabled(p.id, "agent-b"), "re-enable works");

        Assert(store.ClearAgentEnablement(p.id, "agent-b"), "clear row");
        Assert(!store.GetAgentEnablement(p.id, "agent-b").has_value(), "row gone -> inherit");
        Assert(store.EffectiveEnabled(p.id, "agent-b"), "back to inherited default");

        Assert(!store.EffectiveEnabled("missing", "agent-a"), "missing package -> false");
    }
    unlink(path.c_str());
    return 0;
}

const char* kAlpacaManifest = R"({
  "kind": "api_package",
  "name": "alpaca",
  "version": "0.1.0",
  "description": "Alpaca paper + live trading API",
  "keywords": ["trading", "stocks"],
  "state_schema": {
    "token":    {"type": "string", "secret": true},
    "base_url": {"type": "string", "default": "https://api.alpaca.markets"}
  },
  "commands": [
    {
      "name": "token set", "kind": "action",
      "description": "Store the Alpaca API token",
      "parameters": {"token": {"type": "string", "required": true, "secret": true}},
      "script": "function run(ctx) ctx.package.set_state('token', ctx.args.token) return {output='Token stored.'} end"
    },
    {
      "name": "portfolio list", "kind": "action",
      "description": "List current positions",
      "request": {"method": "GET", "url": "{{state.base_url}}/v2/positions",
                  "headers": {"Authorization": "Bearer {{state.token}}"}},
      "script": "function run(ctx) return {output='ok'} end"
    },
    {
      "name": "on ticker", "kind": "hook", "event": "on_message",
      "description": "Handle stream ticks",
      "script": "function run(ctx) return {output='tick'} end"
    }
  ],
  "connections": [
    {
      "name": "stream", "type": "websocket",
      "url_template": "wss://stream.data.alpaca.markets/v2/iex",
      "hooks": {"on_connect": "token set", "on_message": "on ticker"}
    }
  ]
})";

int TestManifestInstall() {
    std::cerr << "  [store] manifest v1 install (happy path + lint rejects)...\n";
    const std::string path = MakeTempDbPath();
    {
        SqliteDataStore db(path);
        ApiPackageStore store(&db);

        ApiPackage p = store.InstallFromManifest(kAlpacaManifest);
        Assert(p.name == "alpaca" && !p.enabled, "installed, disabled");
        Assert(p.state_schema.find("secret") != std::string::npos, "schema carried");
        auto cmds = store.ListCommands(p.id);
        Assert(cmds.size() == 3, "3 commands installed");
        auto hooks = store.ListConnections(p.id);
        Assert(hooks.size() == 1 && hooks[0].hooks.find("on_message") != std::string::npos,
               "1 connection installed");

        bool threw = false;
        try { store.InstallFromManifest(kAlpacaManifest); }
        catch (const std::runtime_error& e) {
            threw = std::string(e.what()).find("already installed") != std::string::npos;
        }
        Assert(threw, "duplicate install rejected");

        // --- lint rejects -----------------------------------------------------
        auto Rejects = [&](const std::string& manifest, const std::string& needle,
                           const std::string& label) {
            bool hit = false;
            try { store.InstallFromManifest(manifest); }
            catch (const std::runtime_error& e) {
                hit = std::string(e.what()).find(needle) != std::string::npos;
                if (!hit) std::cerr << "    (got: " << e.what() << ")\n";
            }
            Assert(hit, label);
        };

        Rejects("{\"kind\":\"sop\",\"name\":\"wrongkind\",\"version\":\"1\",\"description\":\"x\"}",
                "kind must be", "wrong kind rejected");
        Rejects("{\"kind\":\"api_package\",\"name\":\"download\",\"version\":\"1\",\"description\":\"x\"}",
                "reserved", "reserved name rejected");
        Rejects("{\"kind\":\"api_package\",\"name\":\"UPPER\",\"version\":\"1\",\"description\":\"x\"}",
                "slug", "uppercase name rejected");
        Rejects(R"({"kind":"api_package","name":"nohook","version":"1","description":"x",
                  "commands":[{"name":"h","kind":"hook","description":"d","script":"s"}]})",
                "event", "hook without event rejected");
        Rejects(R"({"kind":"api_package","name":"hookparams","version":"1","description":"x",
                  "commands":[{"name":"h","kind":"hook","event":"on_message","description":"d",
                               "parameters":{"a":{"type":"string"}},"script":"s"}]})",
                "must not declare parameters", "hook with parameters rejected");
        Rejects(R"({"kind":"api_package","name":"badparam","version":"1","description":"x",
                  "commands":[{"name":"a","kind":"action","description":"d",
                               "parameters":{"a":{"required":true}},"script":"s"}]})",
                "string 'type'", "parameter without type rejected");
        Rejects(R"({"kind":"api_package","name":"badconn","version":"1","description":"x",
                  "connections":[{"name":"c","type":"carrier-pigeon","url_template":"x"}]})",
                "type must be", "unknown connection type rejected");
        Rejects(R"({"kind":"api_package","name":"nourl","version":"1","description":"x",
                  "connections":[{"name":"c","type":"websocket"}]})",
                "url_template", "missing url_template rejected");
        Rejects(R"({"kind":"api_package","name":"nopoll","version":"1","description":"x",
                  "connections":[{"name":"c","type":"longpoll","url_template":"x"}]})",
                "poll", "longpoll without poll rejected");
        Rejects(R"({"kind":"api_package","name":"statchema","version":"1","description":"x",
                  "state_schema":{"tok":{"secret":true}}})",
                "string 'type'", "state_schema entry without type rejected");

        // Failed installs must leave no package behind.
        for (const auto& name : std::vector<std::string>{"bad kind", "download", "UPPER",
                                                        "nohook", "badconn"})
            Assert(!store.GetPackageByName(name).has_value(), "no residue: " + name);
        Assert(store.ListPackages().size() == 1, "only the good install persisted");
    }
    unlink(path.c_str());
    return 0;
}

}  // namespace

int main() {
    std::cerr << "ApiPackageStore tests:\n";
    TestSchemaIdempotent();
    TestPackageCrud();
    TestCommandsAndConnections();
    TestCascadeDelete();
    TestAgentOverlay();
    TestManifestInstall();
    if (g_failures == 0) std::cerr << "All api package store tests passed.\n";
    else std::cerr << g_failures << " failures.\n";
    return g_failures == 0 ? 0 : 1;
}

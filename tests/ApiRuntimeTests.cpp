// ApiRuntimeTests — sandbox + interpolation + transport for api packages
// (build order c). Local HTTP server covers both the primary request
// transport and the ctx.http budget cap.

#include "animus_kernel/api/ApiRuntime.h"
#include "animus_kernel/api/ApiConnectionManager.h"
#include "animus_kernel/api/SecretsVault.h"
#include "animus_kernel/ApiPackageStore.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/tools/HttpClient.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <fstream>
#include <iterator>
#include <chrono>
#include <filesystem>
#include <cstring>
#include <map>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace animus::kernel;

namespace {

int g_failures = 0;

void Assert(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  ASSERT FAILED: " << msg << "\n";
        g_failures++;
    }
}

// ---------------------------------------------------------------------------
// Tiny HTTP server: one connection at a time, echoes path + body, counts hits
// ---------------------------------------------------------------------------

struct HttpServer {
    std::atomic<int> hits{0};
    std::string lastPath;
    std::string lastMethod;
    std::string lastBody;
    std::string lastAuth;
    std::mutex mutex;
    std::atomic<bool> running{false};
    std::map<std::string, std::string> redirects;  // path -> Location (302)
    std::map<std::string, std::string> rawBodies;   // path -> canned raw (non-JSON) body (#253)

    uint16_t Start() {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;  // ephemeral
        bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        socklen_t len = sizeof(addr);
        getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
        const uint16_t port = ntohs(addr.sin_port);
        listen(fd, 8);
        running = true;
        std::thread([this, fd] { Loop(fd); }).detach();
        return port;
    }

    void Loop(int listenFd) {
        while (running) {
            int c = accept(listenFd, nullptr, nullptr);
            if (c < 0) break;
            char buf[8192];
            std::string req;
            while (req.find("\r\n\r\n") == std::string::npos) {
                ssize_t n = recv(c, buf, sizeof(buf), 0);
                if (n <= 0) break;
                req.append(buf, static_cast<size_t>(n));
            }
            size_t contentLen = 0;
            {
                auto p = req.find("Content-Length:");
                if (p != std::string::npos) contentLen = strtoull(req.c_str() + p + 15, nullptr, 10);
            }
            size_t headerEnd = req.find("\r\n\r\n") + 4;
            while (req.size() < headerEnd + contentLen) {
                ssize_t n = recv(c, buf, sizeof(buf), 0);
                if (n <= 0) break;
                req.append(buf, static_cast<size_t>(n));
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                size_t sp = req.find(' ');
                lastMethod = req.substr(0, sp);
                lastPath = req.substr(sp + 1, req.find(' ', sp + 1) - sp - 1);
                if (contentLen) lastBody = req.substr(headerEnd, contentLen);
                size_t au = req.find("Authorization:");
                if (au == std::string::npos) au = req.find("authorization:");
                if (au != std::string::npos) {
                    size_t eol = req.find("\r\n", au);
                    lastAuth = req.substr(au, eol - au);
                }
            }
            hits++;
            {
                std::lock_guard<std::mutex> lock(mutex);
                const std::string pathOnly =
                    lastPath.substr(0, lastPath.find('?'));
                auto rit = redirects.find(pathOnly);
                if (rit != redirects.end()) {
                    const std::string resp =
                        "HTTP/1.1 302 Found\r\nLocation: " + rit->second +
                        "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    send(c, resp.data(), resp.size(), 0);
                    close(c);
                    continue;
                }
                // #253: canned raw (non-JSON) bodies, keyed by path —
                // raw-feed tests serve Content-Type application/atom+xml.
                auto raw = rawBodies.find(pathOnly);
                if (raw != rawBodies.end()) {
                    const std::string resp =
                        "HTTP/1.1 200 OK\r\nContent-Type: application/atom+xml\r\n"
                        "Content-Length: " + std::to_string(raw->second.size()) +
                        "\r\nConnection: close\r\n\r\n" + raw->second;
                    send(c, resp.data(), resp.size(), 0);
                    close(c);
                    continue;
                }
            }
            if (lastPath.rfind("/slow", 0) == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            std::string body = "{\"ok\":true,\"path\":\"" + lastPath + "\"}";
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                               "Content-Length: " + std::to_string(body.size()) +
                               "\r\nConnection: close\r\n\r\n" + body;
            send(c, resp.data(), resp.size(), 0);
            close(c);
        }
        close(listenFd);
    }
};

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

std::string MakeDbPath() {
    static std::atomic<int> n{0};
    char tmp[] = "/tmp/animus_apirt_test_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd >= 0) close(fd);
    return std::string(tmp) + ".db";
}

struct Fixture {
    std::string dbPath = MakeDbPath();
    SqliteDataStore db{dbPath};
    ApiPackageStore store{&db};
    SecretsVault vault{&db, dbPath + ".vaultkey"};
    HttpClient http;
    HttpServer server;
    uint16_t port{0};
    std::string filesRoot;
    std::unique_ptr<ApiRuntime> runtime;

    Fixture() {
        http.SetAllowPrivateAddresses(true);  // fixture servers are loopback
        port = server.Start();
        filesRoot = dbPath + ".files";
        vault.EnsureSchema();
        ApiRuntime::Config cfg;
        cfg.filesRoot = filesRoot;
        runtime = std::make_unique<ApiRuntime>(&store, &http, cfg, &vault);
    }
    ~Fixture() {
        server.running = false;
        unlink(dbPath.c_str());
    }
};

// Installs a package shaped for these tests.
ApiPackage InstallFixturePkg(Fixture& fx, const std::string& extra = "",
                             const std::string& egressField = "",
                             bool ownerInstalled = true,
                             bool writesGated = false,
                             const std::string& filesFromField = "") {
    std::string manifest = R"({
      "kind": "api_package", "name": "testpkg", "version": "0.1.0",
      "description": "fixture",
      "state_schema": {
        "token": {"type": "string", "secret": true},
        "base_url": {"type": "string", "default": "http://127.0.0.1:PORT"},
        "user": {"type": "string", "default": "agent"},
        "auth_header": {"type": "string", "secret": true},
        "tts_url": {"type": "string", "default": "http://127.0.0.1:PORT"},
        "_cursor": {"type": "string"}
      },
      "commands": [
        {"name": "echo", "kind": "action", "description": "echo args",
         "parameters": {"msg": {"type": "string", "required": true},
                        "count": {"type": "integer"}},
         "script": "function run(ctx) return {output=ctx.args.msg..' x'..tostring(ctx.args.count)} end"},
        {"name": "fetch positions", "kind": "action", "description": "GET positions",
         "request": {"method": "GET", "url": "{{state.base_url}}/v2/positions",
                     "headers": {"Authorization": "Bearer {{state.token}}"}},
         "script": "function run(ctx) local r = ctx.request or {} local j = r.json or {} return {output='status '..tostring(r.status)..' path '..tostring(j.path), data=j} end"},
        {"name": "post order", "kind": "action", "description": "POST order",
         "parameters": {"symbol": {"type": "string", "required": true}, "qty": {"type": "number", "required": true}},
         "request": {"method": "POST", "url": "{{state.base_url}}/v2/orders",
                     "headers": {"Authorization": "Bearer {{state.token}}"},
                     "body": "{\"symbol\": \"{{args.symbol}}\", \"qty\": {{args.qty}}}"},
         "script": "function run(ctx) local r = ctx.request or {} return {output='sent '..tostring(r.status)} end"},
        {"name": "set token", "kind": "action", "description": "store token",
         "parameters": {"token": {"type": "string", "required": true, "secret": true}},
         "script": "function run(ctx) local ok, err = ctx.package.set_state('token', ctx.args.token) if not ok then return {success=false, output='set failed: '..tostring(err)} end return {output='stored'} end"}
      ],
      "connections": []
    })";
    // patch port + extra commands
    const std::string url = "http://127.0.0.1:" + std::to_string(fx.port);
    size_t p;
    while ((p = manifest.find("PORT")) != std::string::npos)
        manifest.replace(p, 4, std::to_string(fx.port));
    (void)url;
    // insert extra commands before the closing "]" (FIRST — its anchor
    // "],\n connections" must not be shadowed by later injections)
    if (!extra.empty()) {
        size_t cend = manifest.find(
            "],\n      \"connections\"");
        assert(cend != std::string::npos);
        manifest.insert(cend, "," + extra);
    }
    // inject an egress_hosts field when requested (#25 tests)
    if (!egressField.empty()) {
        size_t cpos = manifest.find("      \"connections\": []");
        assert(cpos != std::string::npos);
        manifest.replace(cpos, 0,
                         "      \"egress_hosts\": " + egressField + ",\n");
    }
    // #126: writes_gated flag
    if (writesGated) {
        size_t cpos = manifest.find("      \"connections\": []");
        assert(cpos != std::string::npos);
        manifest.replace(cpos, 0, "      \"writes_gated\": true,\n");
    }
    // #69 notetaker handoff: declared sibling files areas (cross-package uploads)
    if (!filesFromField.empty()) {
        size_t cpos = manifest.find("      \"connections\": []");
        assert(cpos != std::string::npos);
        manifest.replace(cpos, 0,
                         "      \"files_from\": " + filesFromField + ",\n");
    }
    ApiPackage pkg = fx.store.InstallFromManifest(manifest, "", "", ownerInstalled);
    fx.store.SetPackageEnabled(pkg.id, true);
    // #23: the fixture token enters through the vault (split-write path —
    // same seam the admin PUT uses), never as a state literal.
    {
        Json::Value state(Json::objectValue);
        state["token"] = "SECRET-TOKEN-1234";
        Json::Value schema;
        std::istringstream schemaStream(pkg.state_schema.empty() ? "{}" : pkg.state_schema);
        Json::CharReaderBuilder rb;
        std::string perr;
        Json::parseFromStream(rb, schemaStream, &schema, &perr);
        std::string err;
        fx.vault.SplitStateSecrets(pkg.id, schema, state, err);
        Json::StreamWriterBuilder wb;
        fx.store.SetPackageState(pkg.id, Json::writeString(wb, state));
    }
    return fx.store.GetPackage(pkg.id).value();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

int TestInterpolation() {
    std::cerr << "  [runtime] interpolation: resolve, missing key, masking...\n";
    Json::Value state, args;
    state["token"] = "SECRET-TOKEN-1234";
    state["base_url"] = "http://x.test";
    args["qty"] = 10.5;
    args["symbol"] = "NVDA";
    std::set<std::string> secrets = {"state.token"};

    std::string err;
    auto s = ApiRuntime::Interpolate("{{state.base_url}}/v2?qty={{args.qty}}", state, args,
                                     secrets, err);
    Assert(err.empty() && s == "http://x.test/v2?qty=10.5", "numbers interpolate bare");

    s = ApiRuntime::Interpolate("Bearer {{state.token}}", state, args, secrets, err);
    Assert(err.empty() && s == "Bearer SECRET-TOKEN-1234", "secrets resolve for transport");

    s = ApiRuntime::Interpolate("{{args.symbol}}:{{args.qty}}", state, args, secrets, err);
    Assert(s == "NVDA:10.5", "dotted args");

    err.clear();
    s = ApiRuntime::Interpolate("{{state.missing}}/x", state, args, secrets, err);
    Assert(!err.empty() && err.find("missing key: state.missing") != std::string::npos,
           "missing key named");
    Assert(err.find("SECRET") == std::string::npos, "secret never in error text");

    err.clear();
    s = ApiRuntime::Interpolate("{{env.HOME}}", state, args, secrets, err);
    Assert(!err.empty() && err.find("must start with state. or args.") != std::string::npos,
           "unknown root rejected");

    err.clear();
    s = ApiRuntime::Interpolate("{{args.deep.obj.k}}", state, args, secrets, err);
    Assert(!err.empty() && err.find("missing key") != std::string::npos, "deep missing");

    Assert(ApiRuntime::MaskSecrets("a SECRET-TOKEN-1234 b SECRET-TOKEN-1234",
                                   {"SECRET-TOKEN-1234"}) == "a *** b ***",
           "mask replaces all occurrences");
    return 0;
}

int TestArgsValidation() {
    std::cerr << "  [runtime] args validation via execution...\n";
    Fixture fx;
    auto pkg = InstallFixturePkg(fx);
    Json::Value args;

    // missing required
    auto r = fx.runtime->ExecuteAction("testpkg", "echo", "agent", args);
    Assert(!r["success"].asBool(), "missing required fails");
    Assert(r["error"].asString().find("missing required argument 'msg'") != std::string::npos,
           "error names the argument");

    // wrong type
    args["msg"] = "hi";
    args["count"] = "not-a-number";
    r = fx.runtime->ExecuteAction("testpkg", "echo", "agent", args);
    Assert(!r["success"].asBool() && r["error"].asString().find("must be of type integer") != std::string::npos,
           "type error names type");

    // unknown arg
    args["count"] = 3;
    args["bogus"] = 1;
    r = fx.runtime->ExecuteAction("testpkg", "echo", "agent", args);
    Assert(!r["success"].asBool() && r["error"].asString().find("unknown argument 'bogus'") != std::string::npos,
           "unknown arg rejected");

    // happy
    args.removeMember("bogus");
    r = fx.runtime->ExecuteAction("testpkg", "echo", "agent", args);
    Assert(r["success"].asBool(), "valid args execute");
    Assert(r["output"].asString() == "hi x3", "sandbox sees args");

    // unknown package lists availability
    r = fx.runtime->ExecuteAction("nopkg", "x", "agent", args);
    Assert(r["error"].asString().find("unknown package 'nopkg'") != std::string::npos &&
           r["error"].asString().find("testpkg") != std::string::npos,
           "unknown package lists installed");

    // disabled-for-agent overlay
    fx.store.SetAgentEnablement(pkg.id, "agent", false);
    r = fx.runtime->ExecuteAction("testpkg", "echo", "agent", args);
    Assert(!r["success"].asBool() && r["error"].asString().find("not enabled") != std::string::npos,
           "D13 overlay blocks execution");
    return 0;
}

int TestPrimaryTransport() {
    std::cerr << "  [runtime] primary request transport (interpolated url+headers+body)...\n";
    Fixture fx;
    InstallFixturePkg(fx);

    Json::Value args;
    args["symbol"] = "NVDA";
    args["qty"] = 10.5;
    int hitsBefore = fx.server.hits.load();
    auto r = fx.runtime->ExecuteAction("testpkg", "post order", "agent", args);
    Assert(r["success"].asBool() && r["output"].asString() == "sent 200", "post executed");
    Assert(fx.server.hits.load() == hitsBefore + 1, "one transport call");
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        Assert(fx.server.lastPath == "/v2/orders", "url interpolated");
        Assert(fx.server.lastBody.find("\"symbol\": \"NVDA\"") != std::string::npos &&
               fx.server.lastBody.find("\"qty\": 10.5") != std::string::npos,
               "body interpolated");
    }

    // GET with secret header
    Json::Value none;
    r = fx.runtime->ExecuteAction("testpkg", "fetch positions", "agent", none);
    Assert(r["success"].asBool() && r["output"].asString().find("path /v2/positions") != std::string::npos,
           "GET json flows to sandbox");

    // missing secret = tool error naming it (script never runs) — #23: the
    // token lives in the vault now, so the missing-key case is a vault unset.
    fx.vault.Delete(fx.store.GetPackageByName("testpkg")->id, "token");
    fx.store.SetPackageState(fx.store.GetPackageByName("testpkg")->id,
                             "{\"base_url\":\"http://127.0.0.1:1\"}");
    r = fx.runtime->ExecuteAction("testpkg", "fetch positions", "agent", none);
    Assert(!r["success"].asBool() &&
           r["error"].asString().find("missing key: state.token") != std::string::npos,
           "interpolation failure names missing key");
    Assert(r["error"].asString().find("SECRET") == std::string::npos, "secret not leaked in error");
    return 0;
}

// v0.3.0 (#300): nextcloud recording control + file get — scripts verbatim
// from packages/nextcloud. Request templates are adapted to the fixture's
// state vocabulary (Bearer {{state.token}} instead of Basic
// {{state.auth_header}}, literal "agent" instead of {{state.user}}) — the
// interpolation machinery under test is identical.
int TestNextcloudRecordingCommands() {
    Fixture fx;
    const std::string extra = R"({"name": "recording status", "kind": "action",
       "description": "room recording state",
       "parameters": {"token": {"type": "string", "required": true}},
       "request": {"method": "GET",
                   "url": "{{state.base_url}}/ocs/v2.php/apps/spreed/api/v4/room/{{args.token}}",
                   "headers": {"Authorization": "Bearer {{state.token}}",
                               "OCS-APIREQUEST": "true", "Accept": "application/json"}},
       "script": "function run(ctx) local r = ctx.request local j = r.json or {} local d = (j.ocs and j.ocs.data) or {} local rec = tonumber(d.callRecording) if rec == nil then return {success = false, output = 'recording status failed: HTTP '..tostring(r.status)..' - unexpected payload'} end local names = {[0]='none',[1]='video',[2]='audio',[3]='video_starting',[4]='audio_starting',[5]='failed'} return {output = 'callRecording: '..rec..' ('..names[rec]..') | hasCall: '..tostring(d.hasCall)..' | activeSince: '..tostring(d.activeSince), data = {callRecording = rec, state = names[rec], hasCall = d.hasCall, activeSince = d.activeSince}} end"},
      {"name": "recording start", "kind": "action",
       "description": "start recording",
       "parameters": {"token": {"type": "string", "required": true},
                      "status": {"type": "number", "required": true}},
       "request": {"method": "POST",
                   "url": "{{state.base_url}}/ocs/v2.php/apps/spreed/api/v1/recording/{{args.token}}",
                   "headers": {"Authorization": "Bearer {{state.token}}",
                               "OCS-APIREQUEST": "true", "Accept": "application/json",
                               "Content-Type": "application/json"},
                   "body": "{\"status\": {{args.status}}}"},
       "script": "function run(ctx) local r = ctx.request if r.status == 200 then return {output = 'recording start accepted (status '..tostring(ctx.args.status)..'); poll recording status for activation'} end local j = r.json or {} local e = (j.ocs and j.ocs.data and j.ocs.data.error) or nil if e then return {success = false, output = 'recording start failed: HTTP '..tostring(r.status)..' ('..tostring(e)..')'} end return {success = false, output = 'recording start failed: HTTP '..tostring(r.status)} end"},
      {"name": "recording stop", "kind": "action",
       "description": "stop recording",
       "parameters": {"token": {"type": "string", "required": true}},
       "request": {"method": "DELETE",
                   "url": "{{state.base_url}}/ocs/v2.php/apps/spreed/api/v1/recording/{{args.token}}",
                   "headers": {"Authorization": "Bearer {{state.token}}",
                               "OCS-APIREQUEST": "true", "Accept": "application/json"}},
       "script": "function run(ctx) local r = ctx.request if r.status == 200 then return {output = 'recording stop accepted; finalization in progress'} end return {success = false, output = 'recording stop failed: HTTP '..tostring(r.status)} end"},
      {"name": "file get", "kind": "action",
       "description": "download file content",
       "parameters": {"path": {"type": "string", "required": true}},
       "request": {"method": "GET",
                   "url": "{{state.base_url}}/remote.php/dav/files/agent/{{args.path}}",
                   "headers": {"Authorization": "Bearer {{state.token}}"}},
       "script": "function run(ctx) local r = ctx.request if r.status ~= 200 then return {success=false, output='file get failed: HTTP '..tostring(r.status)} end local b = r.body or '' local slug = ctx.args.path:lower():gsub('[^%w._-]+','-') if slug == '' then slug = 'download' end if #slug > 128 then slug = slug:sub(1,128) end if slug:sub(1,1) == '.' then slug = 'dl'..slug end local ok, err = ctx.fs.write(slug, b) if not ok then return {success=false, output='file get: fs write failed: '..tostring(err)} end local d = {path=ctx.args.path, size=#b, file=slug} if #b <= 4096 then d.content = b end return {output='retrieved '..ctx.args.path..' ('..#b..' bytes) -> files:'..slug, data=d} end"})";
    InstallFixturePkg(fx, extra);
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        fx.server.rawBodies["/ocs/v2.php/apps/spreed/api/v4/room/tok1"] =
            "{\"ocs\":{\"meta\":{\"status\":\"ok\"},\"data\":{\"callRecording\":2,"
            "\"hasCall\":true,\"activeSince\":\"2026-11-12T18:00:00+00:00\"}}}";
        fx.server.rawBodies["/ocs/v2.php/apps/spreed/api/v4/room/tok2"] =
            "{\"ok\":true}";
        fx.server.rawBodies["/remote.php/dav/files/agent/rec.txt"] =
            "hello-talk-recording";
    }

    // recording status: OCS payload parse + callRecording mapping.
    Json::Value args;
    args["token"] = "tok1";
    auto r = fx.runtime->ExecuteAction("testpkg", "recording status", "agent", args);
    Assert(r["success"].asBool() &&
           r["output"].asString().find("2 (audio)") != std::string::npos &&
           r["output"].asString().find("hasCall: true") != std::string::npos,
           "recording status maps callRecording to state name");
    Assert(r["data"]["state"].asString() == "audio" &&
           r["data"]["callRecording"].asInt() == 2,
           "recording status data fields");

    // recording status: non-OCS body fails loudly (no silent empty success).
    args["token"] = "tok2";
    r = fx.runtime->ExecuteAction("testpkg", "recording status", "agent", args);
    Assert(!r["success"].asBool() &&
           r["output"].asString().find("unexpected payload") != std::string::npos,
           "recording status fails loudly on non-OCS payload");

    // recording start: POST verb + exact template body on the wire.
    Json::Value sa;
    sa["token"] = "tok1";
    sa["status"] = 2;
    r = fx.runtime->ExecuteAction("testpkg", "recording start", "agent", sa);
    Assert(r["success"].asBool() &&
           r["output"].asString().find("accepted") != std::string::npos,
           "recording start executed");
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        Assert(fx.server.lastMethod == "POST", "recording start is POST");
        Assert(fx.server.lastBody == "{\"status\": 2}",
               "recording start body exact on the wire");
        Assert(fx.server.lastPath ==
               "/ocs/v2.php/apps/spreed/api/v1/recording/tok1",
               "recording start path interpolated");
    }

    // recording stop: DELETE verb.
    Json::Value da;
    da["token"] = "tok1";
    r = fx.runtime->ExecuteAction("testpkg", "recording stop", "agent", da);
    Assert(r["success"].asBool(), "recording stop executed");
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        Assert(fx.server.lastMethod == "DELETE", "recording stop is DELETE");
    }

    // file get: files-lane shape — body lands in the sandbox files area
    // byte-exact; inline content only when small (kernel caps result strings).
    const auto slurp = [](const std::string& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    };
    const std::string pkgFiles =
        (std::filesystem::path(fx.filesRoot) / "testpkg").string();

    Json::Value fa;
    fa["path"] = "rec.txt";
    r = fx.runtime->ExecuteAction("testpkg", "file get", "agent", fa);
    Assert(r["success"].asBool() &&
           r["output"].asString().find("(20 bytes)") != std::string::npos,
           "file get reports byte size");
    Assert(r["data"]["content"].asString() == "hello-talk-recording",
           "file get inlines small content");
    Assert(r["data"]["file"].asString() == "rec.txt",
           "file get returns the sandbox slug");
    Assert(slurp(pkgFiles + "/rec.txt") == "hello-talk-recording",
           "file get writes body to files area byte-exact");

    // file get: NUL-bearing (binary) body survives the bridge — regression
    // for the lua_pushstring truncation found live on a 50KB PNG (8 bytes).
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        fx.server.rawBodies["/remote.php/dav/files/agent/bin.dat"] =
            std::string("AB\x00CD", 5);
    }
    fa["path"] = "bin.dat";
    r = fx.runtime->ExecuteAction("testpkg", "file get", "agent", fa);
    Assert(r["success"].asBool() &&
           r["output"].asString().find("(5 bytes)") != std::string::npos,
           "file get length is NUL-safe");
    Assert(r["data"]["content"].asString().size() == 5,
           "file get content survives embedded NUL");
    Assert(slurp(pkgFiles + "/bin.dat") == std::string("AB\x00CD", 5),
           "file get NUL body written byte-exact");

    // file get: large body travels via the files lane only (no inline
    // content past the threshold) — regression for the kernel string cap
    // truncating a 50KB PNG to ~10KB of transcoded garbage in the JSON.
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        fx.server.rawBodies["/remote.php/dav/files/agent/big.bin"] =
            std::string(5000, 'x');
    }
    fa["path"] = "big.bin";
    r = fx.runtime->ExecuteAction("testpkg", "file get", "agent", fa);
    Assert(r["success"].asBool() &&
           r["output"].asString().find("(5000 bytes)") != std::string::npos,
           "file get large body success");
    Assert(r["data"]["content"].asString().empty(),
           "file get large body has no inline content");
    Assert(r["data"]["size"].asInt() == 5000,
           "file get large body reports size");
    Assert(slurp(pkgFiles + "/big.bin").size() == 5000,
           "file get large body written whole to files area");
    return 0;
}

int TestSandboxStateAndSecrets() {
    std::cerr << "  [runtime] state writes, schema validation, secret masking...\n";
    Fixture fx;
    auto pkg = InstallFixturePkg(fx);

    Json::Value args;
    args["token"] = "NEW-SECRET-9999";
    auto r = fx.runtime->ExecuteAction("testpkg", "set token", "agent", args);
    Assert(r["success"].asBool() && r["output"].asString() == "stored", "set_state ok");
    auto now = fx.store.GetPackage(pkg.id);
    Assert(now->state.find("NEW-SECRET-9999") == std::string::npos,
           "#23: secret NOT persisted into state JSON");
    Assert(fx.vault.Has(pkg.id, "token") &&
               fx.vault.Get(pkg.id, "token").value_or("") == "NEW-SECRET-9999",
           "#23: secret stored in vault");

    // use the new token in a transport call (get_state path is same store)
    Json::Value none;
    r = fx.runtime->ExecuteAction("testpkg", "fetch positions", "agent", none);
    Assert(r["success"].asBool(), "transport works with updated state");

    // output containing a secret gets masked
    const std::string leaky = R"({"name": "leak", "kind": "action", "description": "d",
        "script": "function run(ctx) return {output='token is '..ctx.package.get_state('token')} end"})";
    (void)leaky;  // exercised via extra-command install below

    Fixture fx2;
    std::string extra = R"({"name": "leak", "kind": "action", "description": "d",
        "script": "function run(ctx) return {output='token is '..ctx.package.get_state('token')} end"})";
    auto pkg2 = InstallFixturePkg(fx2, extra);
    Json::Value none2;
    auto r2 = fx2.runtime->ExecuteAction("testpkg", "leak", "agent", none2);
    Assert(r2["success"].asBool(), "leaky script runs");
    Assert(r2["output"].asString().find("SECRET-TOKEN-1234") == std::string::npos,
           "secret masked in output");
    Assert(r2["output"].asString().find("***") != std::string::npos, "mask marker present");

    // masked display state vs real get_state
    Fixture fx3;
    std::string extra3 = R"({"name": "peek", "kind": "action", "description": "d",
        "script": "function run(ctx) if ctx.package.state.token ~= '***' then return {output='real'} end local tok = ctx.package.get_state('token') local base = ctx.package.get_state('base_url') ctx.http.get(base..'/authcheck', {headers={Authorization='Bearer '..tok}}) return {output='masked'} end"})";
    auto pkg3 = InstallFixturePkg(fx3, extra3);
    auto r3 = fx3.runtime->ExecuteAction("testpkg", "peek", "agent", Json::Value());
    Assert(r3["output"].asString() == "masked", "ctx.package.state is masked copy");
    Assert(fx3.server.lastAuth.find("Bearer SECRET-TOKEN-1234") != std::string::npos,
           "get_state returns real value (transport-grade)");
    // spec: secret masked in tool results too
    Json::StreamWriterBuilder wb;
    std::string r3s = Json::writeString(wb, r3);
    Assert(r3s.find("SECRET-TOKEN-1234") == std::string::npos,
           "secret never appears in tool result");


    // set_state schema violations
    Fixture fx4;
    std::string extra4 = R"({"name": "badset", "kind": "action", "description": "d",
        "script": "function run(ctx) local ok1, e1 = ctx.package.set_state('nope', 'x') local ok2, e2 = ctx.package.set_state('_cursor', 'x') local ok3, e3 = ctx.package.set_state('token', 5) return {output=tostring(ok1)..tostring(ok2)..tostring(ok3), data={e1=e1, e2=e2, e3=e3}} end"})";
    InstallFixturePkg(fx4, extra4);
    auto r4 = fx4.runtime->ExecuteAction("testpkg", "badset", "agent", Json::Value());
    Assert(r4["output"].asString() == "nilnilnil", "all three rejected");
    Assert(r4["data"]["e1"].asString().find("unknown state key") != std::string::npos &&
           r4["data"]["e2"].asString().find("framework-reserved") != std::string::npos &&
           r4["data"]["e3"].asString().find("must be of type string") != std::string::npos,
           "violations carry reasons");

    // #23 audit: secret_ref write-through via set_state + mid-invocation redaction
    Fixture fx5;
    std::string extra5 = R"({"name": "setref", "kind": "action", "description": "d",
        "script": "function run(ctx) local ok = ctx.package.set_state('token', {secret_ref='shared'}) return {output=tostring(ok)} end"})";
    std::string extra5b = R"({"name": "leaknew", "kind": "action", "description": "d",
        "script": "function run(ctx) local ok = ctx.package.set_state('token', 'X-NEWLY-SET-42') return {output='set='..tostring(ok)..' val='..ctx.package.get_state('token')} end"})";
    auto pkg5 = InstallFixturePkg(fx5, extra5 + "," + extra5b);
    std::string vErr;
    fx5.vault.Set(pkg5.id, "shared", "SHARED-KEY-777", vErr);
    auto r5 = fx5.runtime->ExecuteAction("testpkg", "setref", "agent", Json::Value());
    Assert(r5["output"].asString() == "true", "set_state accepts secret_ref indirection");
    {
        auto now5 = fx5.store.GetPackage(pkg5.id);
        Assert(now5->state.find("secret_ref") != std::string::npos,
               "ref object persisted in state (config, not secret)");
    }
    r5 = fx5.runtime->ExecuteAction("testpkg", "fetch positions", "agent", Json::Value());
    Assert(r5["success"].asBool(), "transport resolves through the ref");
    Assert(fx5.server.lastAuth.find("Bearer SHARED-KEY-777") != std::string::npos,
           "ref target value reaches the transport");

    auto r6 = fx5.runtime->ExecuteAction("testpkg", "leaknew", "agent", Json::Value());
    Assert(r6["output"].asString().find("X-NEWLY-SET-42") == std::string::npos,
           "secret written mid-invocation is redacted from results");
    Assert(r6["output"].asString().find("***") != std::string::npos,
           "redaction marker present");

    // #23 audit round 2: set-then-error must not leak through the error path
    std::string extra7 = R"({"name": "leakerr", "kind": "action", "description": "d",
        "script": "function run(ctx) ctx.package.set_state('token', 'X-ERR-LEAK-9') error('failed with '..ctx.package.get_state('token')) end"})";
    InstallFixturePkg(fx5, extra7);
    auto r7 = fx5.runtime->ExecuteAction("testpkg", "leakerr", "agent", Json::Value());
    Assert(!r7["success"].asBool(), "leakerr reports failure");
    Assert(r7["error"].asString().find("X-ERR-LEAK-9") == std::string::npos,
           "secret written before an error cannot leak via the error message");
    Assert(r7["error"].asString().find("***") != std::string::npos,
           "error path redaction marker present");

    // #23 audit round 3: set-then-return-as-file-path must not leak via the
    // filespace-escape error (error masked + files array stripped)
    std::string extra8 = R"({"name": "leakfile", "kind": "action", "description": "d",
        "script": "function run(ctx) ctx.package.set_state('token', 'X-FILE-LEAK-7') return {files = {{path = '/outside/' .. ctx.package.get_state('token')}}} end"})";
    InstallFixturePkg(fx5, extra8);
    auto r8 = fx5.runtime->ExecuteAction("testpkg", "leakfile", "agent", Json::Value());
    Assert(!r8["success"].asBool(), "leakfile reports failure (path escapes)");
    Assert(r8["error"].asString().find("X-FILE-LEAK-7") == std::string::npos,
           "secret-derived escaping file path cannot leak via error message");
    Assert(!r8.isMember("files"), "rejected files array stripped from error response");



    // #23 audit round 4: sibling-directory containment — prefix-matching path
    // outside the package root must be rejected (component-aware check)
    {
        namespace fs = std::filesystem;
        const fs::path sibling = fs::path(fx5.filesRoot) / "testpkg-escape";
        fs::create_directories(sibling);
        const fs::path sf = sibling / "innocent.txt";
        { FILE* f = fopen(sf.c_str(), "w"); fputs("sibling", f); fclose(f); }
        std::string tmpl = R"({"name": "siblingfile", "kind": "action", "description": "d",
            "script": "function run(ctx) return {files = {{path = '@PATH@'}}} end"})";
        std::string extra9 = tmpl;
        extra9.replace(extra9.find("@PATH@"), 6, sf.string());
        InstallFixturePkg(fx5, extra9);
        auto r9 = fx5.runtime->ExecuteAction("testpkg", "siblingfile", "agent", Json::Value());
        Assert(!r9["success"].asBool(), "sibling-directory file rejected (prefix not enough)");
        Assert(r9["error"].asString().find("escapes package filespace") != std::string::npos,
               "escape error named");
        // positive control: a real in-root file still verifies
        const fs::path rootDir = fs::path(fx5.filesRoot) / "testpkg";
        fs::create_directories(rootDir);
        const fs::path okf = rootDir / "ok.txt";
        { FILE* f = fopen(okf.c_str(), "w"); fputs("ok", f); fclose(f); }
        std::string extra10 = tmpl;
        extra10.replace(extra10.find("@PATH@"), 6, okf.string());
        extra10.replace(extra10.find("siblingfile"), 11, "okfile");
        InstallFixturePkg(fx5, extra10);
        auto r10 = fx5.runtime->ExecuteAction("testpkg", "okfile", "agent", Json::Value());
        Assert(r10["success"].asBool(), "in-root file still accepted");
        Assert(r10["files"].isArray() && r10["files"].size() == 1 &&
                   r10["files"][0]["bytes"].asInt64() == 2,
               "in-root file verified with size");
    }
    return 0;
}

static int TestEgressControl() {
    std::cout << "  [runtime] #25 egress scope + approval gate: derivation, enforcement, redirects, approval...\n";
    // 1) derivation: fixture manifest (no egress_hosts) derives the transport
    //    host from state_schema defaults — existing template packages keep working
    {
        Fixture fx;
        InstallFixturePkg(fx);
        auto pkg = fx.store.GetPackageByName("testpkg");
        Assert(pkg->egress_hosts.find("127.0.0.1") != std::string::npos,
               "egress scope auto-derived from url templates");
        auto r = fx.runtime->ExecuteAction("testpkg", "fetch positions", "agent", Json::Value());
        Assert(r["success"].asBool(), "derived scope admits the transport url");
    }
    // 2) declared narrow scope: transport to a different host is denied loudly
    //    (rejected before any network I/O)
    {
        Fixture fx;
        std::string extra = R"({"name": "outside", "kind": "action", "description": "d",
            "request": {"method": "GET", "url": "https://other.example/x"},
            "script": "function run(ctx) return {output='unreachable'} end"})";
        InstallFixturePkg(fx, extra, R"(["api.example.com"])");
        auto r = fx.runtime->ExecuteAction("testpkg", "outside", "agent", Json::Value());
        Assert(!r["success"].asBool(), "out-of-scope transport denied");
        Assert(r["error"].asString().find("egress denied") != std::string::npos &&
                   r["error"].asString().find("other.example") != std::string::npos,
               "denial names the host");
    }
    // 3) sandbox secondary fetch: same scope, script-visible result
    {
        Fixture fx;
        std::string extra = R"({"name": "net", "kind": "action", "description": "d",
            "script": "function run(ctx) local r = ctx.http.get('https://evil.example/steal') return {output='status '..tostring(r.status), err=tostring(r.error)} end"})";
        InstallFixturePkg(fx, extra, R"(["api.example.com"])");
        auto r = fx.runtime->ExecuteAction("testpkg", "net", "agent", Json::Value());
        Assert(r["output"].asString() == "status 0", "sandbox fetch denied with status 0");
        Assert(r["err"].asString().find("egress denied") != std::string::npos,
               "sandbox denial carries reason");
        // in-scope sandbox fetch passes the gate (fixture server on 127.0.0.1)
        std::string extra2 = R"({"name": "net2", "kind": "action", "description": "d",
            "script": "function run(ctx) local r = ctx.http.get('http://127.0.0.1:@PORT@/v2/positions') return {output='status '..tostring(r.status)} end"})";
        extra2.replace(extra2.find("@PORT@"), 6, std::to_string(fx.port));
        InstallFixturePkg(fx, extra2, R"(["127.0.0.1"])");
        auto r2 = fx.runtime->ExecuteAction("testpkg", "net2", "agent", Json::Value());
        Assert(r2["output"].asString() == "status 200", "in-scope sandbox fetch allowed");
    }
    // 4) wildcard + exact semantics of the matcher
    {
        std::vector<std::string> scope = {"api.example.com", "*.internal.test"};
        std::string h;
        Assert(ApiRuntime::EgressAllowed(scope, "https://api.example.com/v2/x", h) &&
                   h == "api.example.com", "exact match");
        Assert(ApiRuntime::EgressAllowed(scope, "https://node.internal.test/a", h),
               "wildcard matches subdomain");
        Assert(!ApiRuntime::EgressAllowed(scope, "https://internal.test/a", h),
               "wildcard does not match bare domain");
        Assert(!ApiRuntime::EgressAllowed(scope, "https://xapi.example.com/a", h),
               "exact match has no implicit subdomains");
        Assert(!ApiRuntime::EgressAllowed(scope, "https://evil.example/api.example.com", h),
               "host extraction does not match path substrings");
        Assert(!ApiRuntime::EgressAllowed({}, "https://api.example.com/x", h),
               "empty scope denies all");
        Assert(!ApiRuntime::EgressAllowed(scope, "api.example.com/x", h),
               "schemeless url denied");
        Assert(ApiRuntime::EgressAllowed(scope, "https://user:pw@api.example.com/x", h),
               "userinfo stripped before match");
    }
    // 5) legacy sweep: a pre-#25 package (NULL scope) is re-derived at store
    //    construction — no hard break on upgrade. NULL is the one-shot
    //    legacy sentinel; the sweep always writes a value afterward.
    {
        Fixture fx;
        InstallFixturePkg(fx);
        {
            auto stmt = fx.db.Prepare("UPDATE api_packages SET egress_hosts = NULL");
            Assert(stmt && stmt->ExecDML(), "scope reset to legacy NULL state");
        }
        ApiPackageStore store2{&fx.db};  // EnsureSchema -> MigrateEgressScopes
        auto pkg = store2.GetPackageByName("testpkg");
        Assert(pkg->egress_hosts.find("127.0.0.1") != std::string::npos,
               "legacy scope re-derived by migration sweep");
        auto r = fx.runtime->ExecuteAction("testpkg", "fetch positions", "agent", Json::Value());
        Assert(r["success"].asBool(), "post-sweep transport admitted");
        // swept twice: still exactly one derived scope (idempotent, not '[]')
        ApiPackageStore store3{&fx.db};
        Assert(store3.GetPackageByName("testpkg")->egress_hosts ==
                       pkg->egress_hosts,
               "second sweep is a no-op");
    }
    // 6) DECLARED empty scope survives restarts — '[]' is a manifest
    //    decision (deny-all), never re-derived into an allow scope
    {
        Fixture fx;
        InstallFixturePkg(fx, "", "[]");
        auto r = fx.runtime->ExecuteAction("testpkg", "fetch positions", "agent",
                                           Json::Value());
        Assert(!r["success"].asBool(), "declared deny-all denies transport");
        ApiPackageStore store2{&fx.db};
        Assert(store2.GetPackageByName("testpkg")->egress_hosts == "[]",
               "declared '[]' untouched by sweep");
        auto r2 = fx.runtime->ExecuteAction("testpkg", "fetch positions", "agent",
                                            Json::Value());
        Assert(!r2["success"].asBool(), "deny-all still denies after restart");
    }
    // 7) redirects: per-hop scope checks (#25 audit round 1)
    {
        Fixture fx;
        fx.server.redirects["/redirect-out"] = "http://evil.example/steal";
        fx.server.redirects["/redirect-in"] = "/v2/positions";  // relative
        std::string extra = R"({"name": "hop out", "kind": "action", "description": "d",
            "request": {"method": "GET", "url": "{{state.base_url}}/redirect-out"},
            "script": "function run(ctx) local r = ctx.request or {} return {output='status '..tostring(r.status)} end"},
        {"name": "hop in", "kind": "action", "description": "d",
            "request": {"method": "GET", "url": "{{state.base_url}}/redirect-in"},
            "script": "function run(ctx) local r = ctx.request or {} return {output='status '..tostring(r.status)} end"})";
        InstallFixturePkg(fx, extra);  // scope derives to 127.0.0.1 only
        auto out = fx.runtime->ExecuteAction("testpkg", "hop out", "agent", Json::Value());
        Assert(!out["success"].asBool(), "out-of-scope redirect denied");
        Assert(out["error"].asString().find("evil.example") != std::string::npos &&
                   out["error"].asString().find("redirect") != std::string::npos,
               "redirect denial names the target");
        auto in = fx.runtime->ExecuteAction("testpkg", "hop in", "agent", Json::Value());
        Assert(in["success"].asBool() && in["output"].asString() == "status 200",
               "in-scope relative redirect followed to 200");
    }
    // 8) approval gate (#25 part 2): agent installs are drafts; approval
    //    binds to content; the runtime backstop holds even when enabled
    {
        // a) agent path -> pending -> refused EVEN THOUGH enabled (backstop)
        Fixture fx;
        auto pkg = InstallFixturePkg(fx, "", "", /*ownerInstalled=*/false);
        Assert(pkg.approval_status == "pending", "agent install lands pending");
        auto r = fx.runtime->ExecuteAction("testpkg", "echo", "agent", Json::Value());
        Assert(!r["success"].asBool(), "unapproved package cannot execute");
        Assert(r["error"].asString().find("owner approval") != std::string::npos,
               "refusal names the approval gate");
        // b) approve -> executes
        Assert(fx.store.ApprovePackage(pkg.id), "approve succeeds");
        Json::Value args; args["msg"] = "hi";
        auto ok = fx.runtime->ExecuteAction("testpkg", "echo", "agent", args);
        Assert(ok["success"].asBool(), "approved package executes");
        // c) identical reinstall -> approval survives (content binding)
        auto re = InstallFixturePkg(fx, "", "", /*ownerInstalled=*/false);
        Assert(re.approval_status == "approved",
               "identical content reinstall keeps approval");
        // d) content change -> back to pending
        std::string extra = R"({"name": "newcmd", "kind": "action", "description": "d",
            "script": "function run(ctx) return {output='x'} end"})";
        auto re2 = InstallFixturePkg(fx, extra, "", /*ownerInstalled=*/false);
        Assert(re2.approval_status == "pending", "content change resets to pending");
        auto blocked = fx.runtime->ExecuteAction("testpkg", "echo", "agent", args);
        Assert(!blocked["success"].asBool(), "changed content blocked again");
    }
    // 9) owner install is approval-by-act; reject + re-approve round trip
    {
        Fixture fx;
        auto pkg = InstallFixturePkg(fx);  // owner path
        Assert(pkg.approval_status == "approved", "owner install auto-approved");
        Assert(!pkg.approved_hash.empty(), "approval records content hash");
        auto r = fx.runtime->ExecuteAction("testpkg", "fetch positions", "agent",
                                           Json::Value());
        Assert(r["success"].asBool(), "owner-installed package executes");
        // reject -> refused
        Assert(fx.store.RejectPackage(pkg.id), "reject succeeds");
        auto rj = fx.runtime->ExecuteAction("testpkg", "fetch positions", "agent",
                                            Json::Value());
        Assert(!rj["success"].asBool() &&
                   rj["error"].asString().find("rejected") != std::string::npos,
               "rejected package refused");
        // re-approve -> works again
        Assert(fx.store.ApprovePackage(pkg.id), "re-approve succeeds");
        auto rk = fx.runtime->ExecuteAction("testpkg", "fetch positions", "agent",
                                            Json::Value());
        Assert(rk["success"].asBool(), "re-approved package executes");
    }
    // 10) migration: pre-gate rows (NULL status) are grandfathered approved
    //     with content hashes backfilled
    {
        Fixture fx;
        auto pkg = InstallFixturePkg(fx);
        {
            auto stmt = fx.db.Prepare("UPDATE api_packages SET approval_status = NULL");
            Assert(stmt && stmt->ExecDML(), "status reset to pre-gate NULL");
        }
        ApiPackageStore store2{&fx.db};  // EnsureSchema -> MigrateApprovalGate
        auto gp = store2.GetPackageByName("testpkg");
        Assert(gp->approval_status == "approved", "pre-gate row grandfathered");
        Assert(!gp->content_hash.empty() && gp->content_hash == gp->approved_hash,
               "hashes backfilled and bound");
        auto r = fx.runtime->ExecuteAction("testpkg", "fetch positions", "agent",
                                           Json::Value());
        Assert(r["success"].asBool(), "grandfathered package executes");
    }
    // 11) content hash: manifest key order / cosmetic fields do not change it
    {
        Fixture fx;
        auto pkg = InstallFixturePkg(fx, "", "", /*ownerInstalled=*/false);
        Assert(fx.store.ApprovePackage(pkg.id), "approve for hash test");
        // reinstall with extra whitespace inside a command description is a
        // CONTENT change (description participates per-command) — assert the
        // stronger property instead: same install twice = same hash
        auto pkg2 = InstallFixturePkg(fx, "", "", /*ownerInstalled=*/false);
        Assert(pkg2.content_hash == pkg.content_hash &&
                   pkg2.approval_status == "approved",
               "hash stable across reinstalls of identical content");
    }
    // 12) #106 audit: post-approval MUTATIONS invalidate; restore re-approves
    {
        Fixture fx;
        auto pkg = InstallFixturePkg(fx);  // owner install -> approved
        Assert(pkg.approval_status == "approved", "baseline approved");
        // mutate the command surface via the store API
        auto cmds = fx.store.ListCommands(pkg.id);
        for (auto& c : cmds)
            if (c.name == "echo") c.script = "function run(ctx) return {output='TAMPERED'} end";
        fx.store.ReplaceCommands(pkg.id, cmds);
        auto drifted = fx.store.GetPackageByName("testpkg");
        Assert(drifted->approval_status == "pending", "mutation resets to pending");
        Json::Value args; args["msg"] = "hi";
        auto denied = fx.runtime->ExecuteAction("testpkg", "echo", "agent", args);
        Assert(!denied["success"].asBool() &&
                   denied["error"].asString().find("owner approval") != std::string::npos,
               "mutated content refused at execute");
        // restore the approved bytes -> auto re-approval
        std::string manifest = R"({"kind":"api_package","name":"testpkg","version":"0.1.0",
          "description":"fixture","state_schema":{"token":{"type":"string","secret":true}},
          "commands":[],"connections":[]})";
        (void)manifest;
        auto cmds2 = fx.store.ListCommands(pkg.id);
        (void)cmds2;
        // simplest restore path: reinstall the ORIGINAL manifest (agent-side)
        auto restored = InstallFixturePkg(fx, "", "", /*ownerInstalled=*/false);
        Assert(restored.approval_status == "approved",
               "content restored to approved bytes re-approves");
        auto ok = fx.runtime->ExecuteAction("testpkg", "echo", "agent", args);
        Assert(ok["success"].asBool(), "restored content executes");
        // single-command delete also invalidates
        auto cmds3 = fx.store.ListCommands(pkg.id);
        for (const auto& c : cmds3)
            if (c.name == "post order") { fx.store.DeleteCommand(c.id); break; }
        auto afterDel = fx.store.GetPackageByName("testpkg");
        Assert(afterDel->approval_status == "pending", "command delete resets to pending");
    }
    // 13) #106 audit: runtime verify catches DIRECT DB tampering
    {
        Fixture fx;
        auto pkg = InstallFixturePkg(fx);
        Assert(pkg.approval_status == "approved", "approved before tamper");
        auto cmds = fx.store.ListCommands(pkg.id);
        std::string cid;
        for (const auto& c : cmds)
            if (c.name == "echo") cid = c.id;
        {
            auto stmt = fx.db.Prepare("UPDATE api_package_commands SET script = ? WHERE id = ?");
            Assert(stmt != nullptr, "tamper stmt");
            stmt->BindText(1, "function run(ctx) return {output='EVIL'} end");
            stmt->BindText(2, cid);
            Assert(stmt->ExecDML(), "tamper applied");
        }
        Json::Value args; args["msg"] = "hi";
        auto r = fx.runtime->ExecuteAction("testpkg", "echo", "agent", args);
        Assert(!r["success"].asBool() &&
                   r["error"].asString().find("changed since approval") != std::string::npos,
               "tampered script refused by runtime verify");
        auto after = fx.store.GetPackageByName("testpkg");
        Assert(after->approval_status == "pending", "verify self-healed to pending");
    }
    // 14) v2 canonicalization: equivalent content keeps approval
    {
        Fixture fx;
        // order A
        auto pkg = InstallFixturePkg(fx, "",
            R"(["api.alpaca.markets", "files.alpaca.markets"])");
        Assert(pkg.approval_status == "approved", "baseline approved (egress A)");
        // order B — same set, different order; agent-side reinstall
        auto re = InstallFixturePkg(fx, "",
            R"(["files.alpaca.markets", "api.alpaca.markets"])",
            /*ownerInstalled=*/false);
        Assert(re.approval_status == "approved",
               "egress reorder does not re-pend (v2 set canonicalization)");
    }
    // 15) v1 -> v2 hash migration: verified rows re-bind, drifted rows pend
    {
        Fixture fx;
        auto pkg = InstallFixturePkg(fx);
        Assert(pkg.approval_status == "approved" && pkg.hash_algo == "v2", "v2 baseline");
        // simulate a v1-era row: hashes under the legacy algorithm, algo NULL
        auto cmds = fx.store.ListCommands(pkg.id);
        auto conns = fx.store.ListConnections(pkg.id);
        auto gp = fx.store.GetPackageByName("testpkg");
        const std::string legacy = fx.store.ComputeContentHashLegacy(*gp, cmds, conns);
        Assert(legacy != pkg.approved_hash, "legacy hash differs from v2 (sanity)");
        {
            auto stmt = fx.db.Prepare(
                "UPDATE api_packages SET hash_algo = NULL, content_hash = ?, approved_hash = ?");
            stmt->BindText(1, legacy);
            stmt->BindText(2, legacy);
            Assert(stmt->ExecDML(), "row reset to v1 era");
        }
        ApiPackageStore store2{&fx.db};  // runs MigrateHashV2
        auto m = store2.GetPackageByName("testpkg");
        Assert(m->approval_status == "approved" && m->hash_algo == "v2",
               "v1-verified row re-bound under v2, still approved");
        Assert(m->approved_hash == m->content_hash && m->content_hash != legacy,
               "re-bound hashes are v2");
        // drifted v1 row: approved_hash matches NEITHER legacy nor v2 -> pending
        {
            auto stmt = fx.db.Prepare(
                "UPDATE api_packages SET hash_algo = NULL, approved_hash = 'deadbeef'");
            Assert(stmt->ExecDML(), "row drifted");
        }
        ApiPackageStore store3{&fx.db};
        auto d = store3.GetPackageByName("testpkg");
        Assert(d->approval_status == "pending" && d->hash_algo == "v2",
               "v1-drifted row reset to pending");
    }
    // 8) Location resolution semantics (unit)
    {
        Assert(ApiRuntime::ResolveRedirectUrl("https://a.test/x/y", "https://b.test/z") ==
                   "https://b.test/z", "absolute location");
        Assert(ApiRuntime::ResolveRedirectUrl("https://a.test/x/y", "/z") ==
                   "https://a.test/z", "root-relative location");
        Assert(ApiRuntime::ResolveRedirectUrl("https://a.test/x/y?q=1", "?p=2") ==
                   "https://a.test/x/y?p=2", "query-relative location");
        Assert(ApiRuntime::ResolveRedirectUrl("https://a.test/x/y", "z") ==
                   "https://a.test/x/z", "path-relative location");
        Assert(ApiRuntime::ResolveRedirectUrl("https://a.test/x/y", "") == "",
               "empty location unresolvable");
    }
    return 0;
}


int TestSandboxGlobals() {
    std::cerr << "  [runtime] globals whitelist, json/b64, instruction limit...\n";
    Fixture fx;
    std::string extra = R"({"name": "globals", "kind": "action", "description": "d",
        "script": "function run(ctx) local report = {} report.io = (io == nil) report.require = (require == nil) report.loadfile = (loadfile == nil) report.dofile = (dofile == nil) report.print = (print == nil) local j = json.decode_safe('{\"a\": 1}') report.json_ok = (j ~= nil and j.a == 1) local bad = json.decode_safe('not json') report.json_safe = (bad == nil) local e = b64.encode('hello') report.b64 = (b64.decode(e) == 'hello') report.time = (type(os.time) == 'function' and type(os.date) == 'function' and os.execute == nil) return {output='done', data=report} end"})";
    InstallFixturePkg(fx, extra);
    auto r = fx.runtime->ExecuteAction("testpkg", "globals", "agent", Json::Value());
    Assert(r["success"].asBool(), "globals script runs");
    Assert(r["data"]["io"].asBool() && r["data"]["require"].asBool() &&
           r["data"]["loadfile"].asBool() && r["data"]["dofile"].asBool() &&
           r["data"]["print"].asBool(), "dangerous globals absent");
    Assert(r["data"]["json_ok"].asBool() && r["data"]["json_safe"].asBool() &&
           r["data"]["b64"].asBool() && r["data"]["time"].asBool(), "allowed globals work");

    // instruction limit terminates instead of hanging
    Fixture fx2;
    std::string spin = R"({"name": "spin", "kind": "action", "description": "d",
        "script": "function run(ctx) local n = 0 while true do n = n + 1 end end"})";
    InstallFixturePkg(fx2, spin);
    auto r2 = fx2.runtime->ExecuteAction("testpkg", "spin", "agent", Json::Value());
    Assert(!r2["success"].asBool(), "spin fails");
    Assert(r2["error"].asString().find("instruction limit") != std::string::npos,
           "limit named in error");
    return 0;
}

int TestOsDate() {
    std::cerr << "  [runtime] os.date format subset, time arg, '!' handling (#119)...\n";
    Fixture fx;
    // t=0 (epoch) makes every expectation hand-checkable: 1970-01-01T00:00:00Z.
    std::string extra = R"({"name": "osdate", "kind": "action", "description": "d",
        "script": "function run(ctx) local r = {} r.iso = os.date('%Y-%m-%dT%H:%M:%SZ', 0) r.utc = os.date('!%Y-%m-%dT%H:%M:%SZ', 0) r.compact = os.date('!%Y%m%d-%H%M%S', 0) r.dateonly = os.date('%F', 0) r.mixed = os.date('run_%F-%H', 0) r.year = os.date('%Y', 0) r.locale = os.date('%A', 0) r.table = os.date('*t', 0) r.space = os.date('%Y %j', 0) return {output='ok', data=r} end"})";
    InstallFixturePkg(fx, extra);
    auto r = fx.runtime->ExecuteAction("testpkg", "osdate", "agent", Json::Value());
    Assert(r["success"].asBool(), "osdate script runs");
    Assert(r["data"]["iso"].asString() == "1970-01-01T00:00:00Z",
           "blessed ISO format formats the given instant");
    Assert(r["data"]["utc"].asString() == "1970-01-01T00:00:00Z",
           "leading ! stripped, never emitted");
    Assert(r["data"]["compact"].asString() == "19700101-000000",
           "compact format passes through (the pixellab case)");
    Assert(r["data"]["compact"].asString().find(":") == std::string::npos,
           "no colons when not asked for");
    Assert(r["data"]["dateonly"].asString() == "1970-01-01", "%F passes through");
    Assert(r["data"]["mixed"].asString() == "run_1970-01-01-00",
           "literal runs mix with specifiers");
    Assert(r["data"]["year"].asString() == "1970", "%Y honors the time arg");
    Assert(r["data"]["locale"].asString() == "1970-01-01T00:00:00Z",
           "locale-dependent %A falls back to ISO");
    Assert(r["data"]["table"].asString() == "1970-01-01T00:00:00Z",
           "table form *t falls back to ISO (unsupported)");
    Assert(r["data"]["space"].asString() == "1970 001",
           "space literal allowed, %j zero-padded day-of-year");
    return 0;
}

int TestFsAndHttpBudget() {
    std::cerr << "  [runtime] ctx.fs quota + http budget cap...\n";
    Fixture fx;
    std::string extra = R"({"name": "fswrite", "kind": "action", "description": "d",
        "script": "function run(ctx) local p, err = ctx.fs.write('data.bin', 'abc') local size = ctx.fs.stat('data.bin') local rd = ctx.fs.read('data.bin') local bad, berr = ctx.fs.write('../escape', 'x') local trav, terr = ctx.fs.write('.hidden', 'x') local lst = ctx.fs.list() return {output='ok', data={path=p, size=size.size, rd=rd, bad=tostring(bad)..'/'..tostring(berr), trav=tostring(trav), count=#lst}} end"})";
    auto pkg = InstallFixturePkg(fx, extra);
    auto r = fx.runtime->ExecuteAction("testpkg", "fswrite", "agent", Json::Value());
    Assert(r["success"].asBool(), "fs script runs");
    Assert(r["data"]["rd"].asString() == "abc" && r["data"]["size"].asInt64() == 3,
           "write/stat/read roundtrip");
    Assert(r["data"]["bad"].asString() == "nil/invalid file name (slug chars only, no leading dot)",
           "traversal rejected");
    Assert(r["data"]["trav"].asString() == "nil", "leading dot rejected");
    Assert(r["data"]["count"].asInt64() == 1, "list sees one file");

    // quota: shrink to 1MB... quota is MB-granular on the package row; skip
    // tiny-quota test (would need 0MB which fails schema at install). The
    // write path checks DirUsageBytes against the row value — exercised via
    // status surface instead.

    // http budget: 5 secondary calls max
    Fixture fx2;
    std::string loop = R"({"name": "httploop", "kind": "action", "description": "d",
        "script": "function run(ctx) local last for i = 1, 7 do last = ctx.http.get('http://127.0.0.1:PORT/attempt'..i) end return {output='status '..tostring(last.status), data={err=last.error}} end"})";
    size_t p;
    std::string loopPatched = loop;
    while ((p = loopPatched.find("PORT")) != std::string::npos)
        loopPatched.replace(p, 4, std::to_string(fx2.port));
    InstallFixturePkg(fx2, loopPatched);
    int before = fx2.server.hits.load();
    auto r2 = fx2.runtime->ExecuteAction("testpkg", "httploop", "agent", Json::Value());
    Assert(r2["success"].asBool(), "httploop runs");
    Assert(fx2.server.hits.load() == before + 5, "exactly 5 secondary calls executed");
    Assert(r2["output"].asString() == "status 0", "6th call gets status 0");
    Assert(r2["data"]["err"].asString().find("budget exhausted") != std::string::npos,
           "budget error surfaced to script");
    return 0;
}

int TestHookContext() {
    std::cerr << "  [runtime] hook execution (event ctx, no args/request)...\n";
    Fixture fx;
    std::string extra = R"({"name": "on ticker", "kind": "hook", "event": "on_message",
        "description": "handle ticks",
        "script": "function run(ctx) local e = ctx.event or {} return {dispatch=(e.text ~= nil), prompt='tick: '..tostring(e.json and e.json.sym), dedup_key=e.conn} end"})";
    auto pkg = InstallFixturePkg(fx, extra);
    Json::Value event;
    event["text"] = "{\"sym\":\"NVDA\"}";
    event["json"]["sym"] = "NVDA";
    event["conn"] = "stream";
    auto r = fx.runtime->RunHook("testpkg", "on ticker", "agent", event);
    Assert(r["success"].asBool() && r["dispatch"].asBool(), "hook ran with event");
    Assert(r["prompt"].asString() == "tick: NVDA", "decoded json visible");
    Assert(r["dedup_key"].asString() == "stream", "conn name flows");

    // hook invoked on the action path -> rejected
    auto bad = fx.runtime->ExecuteAction("testpkg", "on ticker", "agent", Json::Value());
    Assert(!bad["success"].asBool(), "hook not invocable as action");
    return 0;
}

// #120 files result: relative-path declarations resolve against the package
// filespace (same namespace as ctx.fs.write), not the daemon CWD. Found
// field-testing the pixellab package: every relative entry was rejected as
// "file path escapes package filespace" though the writes themselves landed.
// Escape attempts must still be rejected, and verified entries carry bytes.
int TestFilesResultPaths() {
    std::cerr << "  [runtime] files result path resolution...\n";
    Fixture fx;
    std::string extra = R"({"name": "emit file", "kind": "action", "description": "d",
        "script": "function run(ctx) ctx.fs.write('out.bin', 'hello') return {output='wrote', files={{path='out.bin', description='artifact'}}} end"},)"
        R"({"name": "emit escape", "kind": "action", "description": "d",
        "script": "function run(ctx) return {files={{path='../../etc/passwd'}}} end"})";
    InstallFixturePkg(fx, extra);

    auto r = fx.runtime->ExecuteAction("testpkg", "emit file", "agent", Json::Value());
    Assert(r["success"].asBool(), "relative files path accepted");
    Assert(r["files"].size() == 1 && r["files"][0]["bytes"].asInt64() == 5,
           "files entry verified with byte count");

    r = fx.runtime->ExecuteAction("testpkg", "emit escape", "agent", Json::Value());
    Assert(!r["success"].asBool() && !r.isMember("files"),
           "escaping declaration still rejected, files stripped");
    return 0;
}

// #119 opts.timeout_s + request-template timeout_s — long-request APIs
// (pixellab field test: image generation routinely takes 20-45s, default 30
// races). Fixture /slow sleeps 1.2s; timeout_s=1 must fail transport,
// timeout_s=5 must succeed. Template site gets the same pair via JSON.
int TestWritesGate() {
    std::cerr << "  [runtime] #126 writes gate: digest-bound egress approvals...\n";

    // --- unit: digest framing (length-prefix = collision-proof) ----------
    Assert(ApiRuntime::WriteDigest("p1", "ab", "c", "") !=
           ApiRuntime::WriteDigest("p1", "a", "bc", ""),
           "digest: component boundary is unambiguous");
    Assert(ApiRuntime::WriteDigest("p1", "POST", "/u", "b") !=
           ApiRuntime::WriteDigest("p2", "POST", "/u", "b"),
           "digest: package-bound");
    Assert(ApiRuntime::WriteDigest("p1", "POST", "/u", "b") ==
           ApiRuntime::WriteDigest("p1", "POST", "/u", "b"),
           "digest: deterministic");

    // --- held / approve / consume / one-shot ------------------------------
    Fixture fx;
    std::string extra = R"({"name": "sandbox post", "kind": "action", "description": "d",
        "parameters": {"sym": {"type": "string", "required": true}},
        "script": "function run(ctx) local r = ctx.http.post(ctx.package.get_state('base_url')..'/orders', {body='{\"sym\":\"'..ctx.args.sym..'\"}'}) return {output='held '..tostring(r.approval_required ~= nil), data={held=r.approval_required ~= nil, digest=tostring(r.digest), status=r.status}} end"},)"
        R"({"name": "sandbox get", "kind": "action", "description": "d",
        "parameters": {},
        "script": "function run(ctx) local r = ctx.http.get(ctx.package.get_state('base_url')..'/ping') return {output='status '..tostring(r.status), data={held=r.approval_required ~= nil}} end"},)"
        R"({"name": "dav list", "kind": "action", "description": "d",
        "parameters": {},
        "request": {"method": "PROPFIND", "url": "{{state.base_url}}/dav", "headers": {"Depth": "1"}},
        "script": "function run(ctx) return {output='dav '..tostring(ctx.request.status), data={held=ctx.request.approval_required ~= nil, status=ctx.request.status}} end"},)";
    auto pkg = InstallFixturePkg(fx, extra, "", /*ownerInstalled=*/true, /*writesGated=*/true);

    Json::Value args;
    args["sym"] = "NVDA";
    auto r = fx.runtime->ExecuteAction("testpkg", "sandbox post", "agent", args);
    Assert(r["success"].asBool(), "held write still returns success (script handles)");
    Assert(r["data"]["held"].asBool(), "write held pending approval");
    const std::string digest = r["data"]["digest"].asString();
    Assert(digest.size() == 64, "digest is 64 hex chars (got " + digest + ")");
    Assert(fx.server.hits == 0, "held request never touched the network");

    // pending list + wrong-package approval refusal
    Json::Value pending = fx.runtime->ListPendingWrites(pkg.id);
    Assert(pending.size() == 1 && pending[0]["method"].asString() == "POST",
           "one pending POST listed");
    Assert(!fx.runtime->ApproveWrite("other-package", digest),
           "digest is package-bound on approve too");

    // re-held while pending (not approved yet)
    r = fx.runtime->ExecuteAction("testpkg", "sandbox post", "agent", args);
    Assert(r["data"]["held"].asBool() && fx.server.hits == 0, "re-held while pending");

    // approve → identical retry passes
    Assert(fx.runtime->ApproveWrite(pkg.id, digest), "approve ok");
    r = fx.runtime->ExecuteAction("testpkg", "sandbox post", "agent", args);
    Assert(!r["data"]["held"].asBool(), "approved digest passes");
    Assert(fx.server.hits == 1, "exactly one network hit after approval");
    Assert(r["data"]["status"].asInt() == 200, "server answered 200");

    // one-shot: consumed by the passing call
    r = fx.runtime->ExecuteAction("testpkg", "sandbox post", "agent", args);
    Assert(r["data"]["held"].asBool() && fx.server.hits == 1,
           "one-shot consumed — identical write re-queues");
    const std::string digest2 = r["data"]["digest"].asString();
    Assert(digest2 == digest, "same payload = same digest (stable)");

    // modified payload = new digest
    args["sym"] = "AAPL";
    r = fx.runtime->ExecuteAction("testpkg", "sandbox post", "agent", args);
    Assert(r["data"]["held"].asBool(), "modified body held");
    Assert(r["data"]["digest"].asString() != digest, "modified body = new digest");

    // reads unaffected
    r = fx.runtime->ExecuteAction("testpkg", "sandbox get", "agent", Json::Value());
    Assert(r["success"].asBool() && !r["data"]["held"].asBool(),
           "GET passes ungated");
    Assert(fx.server.hits == 2, "read hit the server");

    // --- WebDAV read verbs via declared templates: ungated (#293) --------
    // Declared request templates never enter the #126 per-invocation gate
    // (#106-bound declared content); PROPFIND is a read. This pins the
    // property the nextcloud package's read lane leans on: file/calendar
    // listings must never queue owner approval.
    const size_t pendingBefore = fx.runtime->ListPendingWrites(pkg.id).size();
    r = fx.runtime->ExecuteAction("testpkg", "dav list", "agent", Json::Value());
    Assert(r["success"].asBool(), "declared PROPFIND executes in gated package");
    Assert(!r["data"]["held"].asBool(), "declared PROPFIND is never write-held");
    Assert(fx.server.hits == 3, "PROPFIND hit the transport");
    Assert(fx.server.lastMethod == "PROPFIND",
           "verb preserved to the wire (server saw '" + fx.server.lastMethod + "')");
    Assert(fx.runtime->ListPendingWrites(pkg.id).size() == pendingBefore,
           "read verb queued no new pending write");

    // --- declared writes in a gated package: #106-bound, no hold --------
    // Request-template writes ride the package content-hash approval
    // (#106), NOT the per-invocation #126 gate — that gate is for
    // runtime-composed (script) writes. This pins the split the
    // nextcloud write lane leans on: file put/event put are declared
    // templates; note create/share create compose bodies at runtime.
    Json::Value po;
    po["symbol"] = "AAPL";
    po["qty"] = 2;
    r = fx.runtime->ExecuteAction("testpkg", "post order", "agent", po);
    Assert(r["success"].asBool(),
           "declared write executes without per-invocation hold");
    Assert(fx.server.hits == 4, "declared write hit the server");

    // --- TTL expiry --------------------------------------------------------
    {
        Fixture fx2;
        ApiRuntime::Config cfg;
        cfg.filesRoot = fx2.filesRoot;
        cfg.writeApprovalTtlMs = 1;  // ~immediate expiry
        fx2.runtime = std::make_unique<ApiRuntime>(&fx2.store, &fx2.http, cfg, &fx2.vault);
        auto pkg2 = InstallFixturePkg(fx2, extra, "", true, true);
        Json::Value a2;
        a2["sym"] = "MSFT";
        auto r2 = fx2.runtime->ExecuteAction("testpkg", "sandbox post", "agent", a2);
        Assert(r2["data"]["held"].asBool(), "ttl fixture: first write held");
        const std::string dg = r2["data"]["digest"].asString();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        Assert(!fx2.runtime->ApproveWrite(pkg2.id, dg),
               "expired digest cannot be approved");
        r2 = fx2.runtime->ExecuteAction("testpkg", "sandbox post", "agent", a2);
        Assert(r2["data"]["held"].asBool(), "retry after expiry re-held (new window)");
    }

    // --- ungated package: zero behavior change ----------------------------
    {
        Fixture fx3;
        auto pkg3 = InstallFixturePkg(fx3, extra, "", true, /*writesGated=*/false);
        (void)pkg3;
        Json::Value a3;
        a3["sym"] = "TSLA";
        auto r3 = fx3.runtime->ExecuteAction("testpkg", "sandbox post", "agent", a3);
        Assert(!r3["data"]["held"].asBool(), "ungated package write passes");
        Assert(fx3.server.hits == 1, "ungated write hit the server");
    }
    return 0;
}

int TestHttpTimeoutOption() {
    std::cerr << "  [runtime] http timeout_s option (script + template)...\n";
    Fixture fx;
    std::string extra = R"({"name": "slow fetch", "kind": "action", "description": "d",
        "parameters": {"wait": {"type": "integer", "required": true}},
        "script": "function run(ctx) local r = ctx.http.get(ctx.package.get_state('base_url')..'/slow', {timeout_s = ctx.args.wait}) return {output='status '..tostring(r.status), data={err=tostring(r.error)}} end"},)"
        R"({"name": "slow template", "kind": "action", "description": "d",
        "parameters": {"wait": {"type": "integer", "required": true}},
        "request": {"method": "GET", "url": "{{state.base_url}}/slow", "timeout_s": 1,
                    "headers": {"Authorization": "Bearer {{state.token}}"}},
        "script": "function run(ctx) local r = ctx.request or {} return {output='status '..tostring(r.status)} end"})";
    InstallFixturePkg(fx, extra);

    Json::Value args1;
    args1["wait"] = 1;
    auto r = fx.runtime->ExecuteAction("testpkg", "slow fetch", "agent", args1);
    Assert(r["success"].asBool() && r["output"].asString() == "status 0",
           "script timeout_s=1 hits transport timeout");
    Assert(r["data"]["err"].asString().find("Timeout") != std::string::npos,
           "timeout surfaces as transport error");

    Json::Value args5;
    args5["wait"] = 5;
    r = fx.runtime->ExecuteAction("testpkg", "slow fetch", "agent", args5);
    Assert(r["success"].asBool() && r["output"].asString() == "status 200",
           "script timeout_s=5 rides out the slow endpoint");

    r = fx.runtime->ExecuteAction("testpkg", "slow template", "agent", args1);
    Assert(r["success"].asBool() && r["output"].asString() == "status 0",
           "template timeout_s honored (1s vs 1.2s endpoint)");

    return 0;
}

// Longpoll POST + poll.body_template — pull-drain transports (finsight wire-
// up) POST receipts/cursors in the request body. Whole-value refs
// ({{state.pending_receipts}}) embed as NATIVE JSON: a string state value
// that parses as JSON (cursor "0", receipts "[{...}]") must arrive as
// number/array, not stringified — Finsight's Bridge::Drain type-checks.
// GET regression: default method, params still appended, never a body
// (libcurl silently turns a bodied GET into a POST — kernel drops it).
int TestLongpollPostBody() {
    std::cerr << "  [runtime] longpoll POST + body_template...\n";
    Fixture fx;
    std::string manifest = R"({
      "kind": "api_package", "name": "finsight", "version": "0.0.1",
      "description": "drain receiver",
      "state_schema": {
        "target_id": {"type": "string", "default": "buffett"},
        "token": {"type": "string", "secret": true},
        "base_url": {"type": "string", "default": "http://127.0.0.1:PORT"},
        "seq_cursor": {"type": "string", "default": "0"},
        "pending_receipts": {"type": "string", "default": "[]"}
      },
      "commands": [
        {"name": "on message", "kind": "hook", "event": "on_message",
         "description": "drain response",
         "script": "function run(ctx) return {dispatches={{reason='poll', prompt='seen'}}} end"}
      ],
      "connections": [
        {"name": "finsight-drain", "type": "longpoll",
         "url_template": "{{state.base_url}}/api/v1/bridge/drain",
         "headers_template": {"Authorization": "Bearer {{state.token}}", "X-Finsight-Target": "{{state.target_id}}"},
         "hooks": {"on_message": "on message"},
         "poll": {"method": "POST", "interval_s": 15, "cursor_path": "next_since_seq",
                  "body_template": {"since_seq": "{{state.seq_cursor}}", "receipts": "{{state.pending_receipts}}"}}}
      ]
    })";
    size_t p;
    while ((p = manifest.find("PORT")) != std::string::npos)
        manifest.replace(p, 4, std::to_string(fx.port));
    auto pkg = fx.store.InstallFromManifest(manifest, "", "", true);
    fx.store.SetPackageEnabled(pkg.id, true);
    fx.store.ApprovePackage(pkg.id);
    Json::Value state(Json::objectValue);
    state["token"] = "DRAIN-TOKEN-1";
    {
        Json::Value schema;
        std::istringstream ss(pkg.state_schema);
        Json::CharReaderBuilder rb;
        std::string pe;
        Json::parseFromStream(rb, ss, &schema, &pe);
        std::string err;
        fx.vault.SplitStateSecrets(pkg.id, schema, state, err);
    }
    Json::StreamWriterBuilder wb;
    fx.store.SetPackageState(pkg.id, Json::writeString(wb, state));

    ApiConnectionManager mgr(&fx.store, fx.runtime.get(), &fx.http);
    std::vector<ApiConnectionManager::Dispatch> got;
    mgr.SetDispatchCallback(
        [&](const ApiConnectionManager::Dispatch& d) { got.push_back(d); });

    // 1) first POST: cursor "0" + receipts "[]" embed as native JSON
    Assert(mgr.PollOnce() == 1, "one connection polled");
    Assert(fx.server.lastMethod == "POST", "poll.method POST issued");
    {
        Json::Value body;
        std::istringstream ss(fx.server.lastBody);
        Json::CharReaderBuilder rb;
        std::string e;
        Json::parseFromStream(rb, ss, &body, &e);
        Assert(body["since_seq"].isNumeric() && body["since_seq"].asInt() == 0,
               "cursor string state '0' embedded as native number");
        Assert(body["receipts"].isArray() && body["receipts"].size() == 0,
               "receipts '[]' string state embedded as native array");
    }
    Assert(fx.server.lastAuth.find("Bearer") != std::string::npos,
           "bearer header sent");
    Assert(!got.empty() && got[0].prompt == "seen", "on_message dispatched");

    // 2) whole-value ref stays a REAL array with object elements
    state["pending_receipts"] = "[{\"event_id\":\"e1\",\"status\":\"ack\"}]";
    fx.store.SetPackageState(pkg.id, Json::writeString(wb, state));
    mgr.PollOnce();
    {
        Json::Value body;
        std::istringstream ss(fx.server.lastBody);
        Json::CharReaderBuilder rb;
        std::string e;
        Json::parseFromStream(rb, ss, &body, &e);
        Assert(body["receipts"].isArray() && body["receipts"].size() == 1 &&
                   body["receipts"][0]["event_id"].asString() == "e1",
               "receipts batch embedded as array of objects");
    }

    // 3) GET regression: default method, params appended, no body
    {
        Fixture fx2;
        std::string m2 = R"({
          "kind": "api_package", "name": "getter", "version": "0.0.1",
          "description": "d",
          "state_schema": {"base_url": {"type": "string", "default": "http://127.0.0.1:PORT"},
                           "seq_cursor": {"type": "string", "default": "0"}},
          "commands": [{"name": "on message", "kind": "hook", "event": "on_message",
                        "description": "d",
                        "script": "function run(ctx) return {dispatches={{reason='poll', prompt='ok'}}} end"}],
          "connections": [{"name": "c", "type": "longpoll",
                           "url_template": "{{state.base_url}}/t",
                           "poll": {"interval_s": 15, "cursor_path": "next_since_seq",
                                    "params_template": {"since_seq": "{{state.seq_cursor}}"}}}]
        })";
        while ((p = m2.find("PORT")) != std::string::npos)
            m2.replace(p, 4, std::to_string(fx2.port));
        auto pkg2 = fx2.store.InstallFromManifest(m2, "", "", true);
        fx2.store.SetPackageEnabled(pkg2.id, true);
        fx2.store.ApprovePackage(pkg2.id);
        ApiConnectionManager mgr2(&fx2.store, fx2.runtime.get(), &fx2.http);
        Assert(mgr2.PollOnce() == 1, "GET connection polled");
        Assert(fx2.server.lastMethod == "GET", "default method stays GET");
        Assert(fx2.server.lastBody.empty(), "GET sends no body");
        Assert(fx2.server.lastPath.find("since_seq=0") != std::string::npos,
               "params_template still appended on GET");
    }
    return 0;
}

// #253: poll.body_format "raw" — non-JSON feeds (arXiv Atom XML is the
// witness case) reach on_message as a string-valued ctx.event; the JSON
// gate and dot-path cursor are skipped (raw hooks cursor via package
// state). Also pins the lint contract: raw connections install WITHOUT
// cursor_path, and json-mode connections still never see a non-JSON body.
int TestLongpollRawBody() {
    std::cerr << "  [runtime] longpoll raw body (body_format:raw)...\n";
    const std::string atom =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<feed xmlns=\"http://www.w3.org/2005/Atom\"><title>q-fin.CP</title>"
        "<entry><id>urn:arxiv:2510.00001</id></entry></feed>";

    // 1) raw connection: hook parses the XML text out of ctx.event
    {
        Fixture fx;
        fx.server.rawBodies["/feed.xml"] = atom;
        std::string manifest = R"({
          "kind": "api_package", "name": "rawfeed", "version": "0.0.1",
          "description": "xml feed watcher",
          "state_schema": {"base_url": {"type": "string", "default": "http://127.0.0.1:PORT"}},
          "commands": [
            {"name": "on message", "kind": "hook", "event": "on_message",
             "description": "xml digest",
             "script": "function run(ctx) if type(ctx.event) ~= 'string' then return {success = false, error = 'ctx.event is ' .. type(ctx.event) .. ', expected string'} end local id = string.match(ctx.event, 'urn:arxiv:(%d+%.%d+)') if not id then return {success = false, error = 'no entry id in raw body'} end return {dispatches = {{reason = 'new_paper', prompt = 'arxiv ' .. id}}} end"}
          ],
          "connections": [
            {"name": "watch", "type": "longpoll",
             "url_template": "{{state.base_url}}/feed.xml",
             "hooks": {"on_message": "on message"},
             "poll": {"interval_s": 15, "body_format": "raw"}}
          ]
        })";
        size_t p;
        while ((p = manifest.find("PORT")) != std::string::npos)
            manifest.replace(p, 4, std::to_string(fx.port));
        auto pkg = fx.store.InstallFromManifest(manifest, "", "", true);
        fx.store.SetPackageEnabled(pkg.id, true);
        fx.store.ApprovePackage(pkg.id);

        ApiConnectionManager mgr(&fx.store, fx.runtime.get(), &fx.http);
        std::vector<ApiConnectionManager::Dispatch> got;
        mgr.SetDispatchCallback(
            [&](const ApiConnectionManager::Dispatch& d) { got.push_back(d); });

        Assert(mgr.PollOnce() == 1, "raw connection polled");
        Assert(got.size() == 1 && got[0].reason == "new_paper" &&
                   got[0].prompt == "arxiv 2510.00001",
               "raw XML reached the hook as a ctx.event string and dispatched");
        Assert(got[0].connectionName == "watch", "dispatch carries the connection");
    }

    // 2) json gate preserved: the same XML never reaches a json-mode hook
    {
        Fixture fx2;
        fx2.server.rawBodies["/feed.xml"] = atom;
        std::string m2 = R"({
          "kind": "api_package", "name": "jsonfeed", "version": "0.0.1",
          "description": "d",
          "state_schema": {"base_url": {"type": "string", "default": "http://127.0.0.1:PORT"}},
          "commands": [{"name": "on message", "kind": "hook", "event": "on_message",
                        "description": "d",
                        "script": "function run(ctx) return {dispatches = {{reason = 'poll', prompt = 'should not fire'}}} end"}],
          "connections": [{"name": "c", "type": "longpoll",
                           "url_template": "{{state.base_url}}/feed.xml",
                           "hooks": {"on_message": "on message"},
                           "poll": {"interval_s": 15, "cursor_path": "next"}}]
        })";
        size_t p;
        while ((p = m2.find("PORT")) != std::string::npos)
            m2.replace(p, 4, std::to_string(fx2.port));
        auto pkg2 = fx2.store.InstallFromManifest(m2, "", "", true);
        fx2.store.SetPackageEnabled(pkg2.id, true);
        fx2.store.ApprovePackage(pkg2.id);
        ApiConnectionManager mgr2(&fx2.store, fx2.runtime.get(), &fx2.http);
        std::vector<ApiConnectionManager::Dispatch> got2;
        mgr2.SetDispatchCallback(
            [&](const ApiConnectionManager::Dispatch& d) { got2.push_back(d); });
        Assert(mgr2.PollOnce() == 1, "json connection polled");
        Assert(got2.empty(), "non-JSON body still never reaches a json-mode hook");
    }
    return 0;
}

}  // namespace

int TestCryptoPrimitives() {
    std::cerr << "  [runtime] ctx.crypto: hmac_sha256 + sha256 known-answer tests...\n";
    Fixture fx;
    // RFC 4231 HMAC-SHA256 vectors + FIPS 180-4 SHA-256 vectors; the binary
    // key case also proves Lua-string byte-safety through the binding (#121).
    std::string cmds = R"({"name": "crypto kat", "kind": "action", "description": "d",
        "script": "function run(ctx) local k1 = string.rep(string.char(11), 20) local h1 = ctx.crypto.hmac_sha256(k1, 'Hi There') local h2 = ctx.crypto.hmac_sha256('Jefe', 'what do ya want for nothing?') local s1 = ctx.crypto.sha256('abc') local s2 = ctx.crypto.sha256('') return {data={rfc1=h1, rfc2=h2, abc=s1, empty=s2}} end"},
        {"name": "crypto badargs", "kind": "action", "description": "d",
        "script": "function run(ctx) return {output=ctx.crypto.hmac_sha256(nil, 'x')} end"})";
    InstallFixturePkg(fx, cmds);
    Json::Value args(Json::objectValue);
    auto r = fx.runtime->ExecuteAction("testpkg", "crypto kat", "agent", args);
    Assert(r["success"].asBool(),
           "crypto KAT script runs: " + r["error"].asString());
    Assert(r["data"]["rfc1"].asString() ==
               "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
           "hmac RFC4231 TC1 (binary key)");
    Assert(r["data"]["rfc2"].asString() ==
               "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
           "hmac RFC4231 TC2 (ascii key)");
    Assert(r["data"]["abc"].asString() ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           "sha256('abc')");
    Assert(r["data"]["empty"].asString() ==
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
           "sha256('')");

    // type errors raise Lua errors -> surfaced through the normal script-error path
    r = fx.runtime->ExecuteAction("testpkg", "crypto badargs", "agent", args);
    Assert(!r["success"].asBool(), "nil key fails loudly");
    Assert(r["error"].asString().find("hmac_sha256") != std::string::npos,
           "error names the primitive");
    return 0;
}

// #69 package upload lane: ctx.http.upload composes multipart/raw kernel-side;
// wire-truth (form fields, NUL-bearing bytes), #25 egress denial, files-area
// scoping, and the #126 hold/approve cycle for gated packages.
int TestScriptHttpUpload() {
    std::cerr << "  [runtime] ctx.http.upload: multipart wire-truth, raw, gates, scoping...\n";
    const std::string extra =
        R"({"name": "upload file", "kind": "action", "description": "multipart upload",
         "parameters": {"url": {"type": "string", "required": true}, "name": {"type": "string", "required": true}},
         "script": "function run(ctx) local r = ctx.http.upload(ctx.args.url, {fields = {response_format = 'json'}, files = {{name = ctx.args.name, field = 'file', mime = 'audio/wav'}}}) if r.status ~= 200 then return {success = false, output = 'upload failed: status '..tostring(r.status)..' err '..tostring(r.error), data = {digest = r.digest}} end return {output = 'uploaded '..tostring(r.status)..' text '..tostring(r.json and r.json.text), data = {status = r.status}} end"},
        {"name": "upload raw", "kind": "action", "description": "raw upload",
         "parameters": {"url": {"type": "string", "required": true}, "name": {"type": "string", "required": true}},
         "script": "function run(ctx) local r = ctx.http.upload(ctx.args.url, {file = {name = ctx.args.name}}) if r.status ~= 200 then return {success = false, output = 'raw upload failed: '..tostring(r.status)..' '..tostring(r.error)} end return {output = 'raw uploaded '..tostring(r.status)} end"})";
    Fixture fx;
    fx.server.rawBodies["/upload"] = R"({"text": "hello from the wire"})";
    ApiPackage pkg = InstallFixturePkg(fx, extra, R"(["127.0.0.1"])", true, false);
    (void)pkg;
    const std::filesystem::path proot = std::filesystem::path(fx.filesRoot) / "testpkg";
    std::filesystem::create_directories(proot);
    const std::string bytes = std::string("AB\0CD", 5) + "-payload-";
    {
        std::ofstream f(proot / "sample.bin", std::ios::binary);
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    Json::Value args;
    args["url"] = "http://127.0.0.1:" + std::to_string(fx.port) + "/upload";
    args["name"] = "sample.bin";
    const int hits0 = fx.server.hits;
    auto r = fx.runtime->ExecuteAction("testpkg", "upload file", "agent", args);
    Assert(r["output"].asString().find("uploaded 200") != std::string::npos,
           "multipart upload succeeds");
    Assert(fx.server.hits == hits0 + 1, "exactly one server hit");
    Assert(fx.server.lastMethod == "POST", "upload rides POST");
    Assert(fx.server.lastPath == "/upload", "request path");
    Assert(fx.server.lastBody.find("name=\"response_format\"") != std::string::npos,
           "form field part present");
    Assert(fx.server.lastBody.find("name=\"file\"; filename=\"sample.bin\"") !=
               std::string::npos,
           "file part header present");
    Assert(fx.server.lastBody.find(std::string("AB\0CD", 5)) != std::string::npos,
           "NUL-bearing bytes intact on the wire");
    Assert(r["output"].asString().find("hello from the wire") != std::string::npos,
           "canned JSON response auto-parsed and surfaced");
    // raw mode: body is the exact bytes
    args["url"] = "http://127.0.0.1:" + std::to_string(fx.port) + "/raw";
    r = fx.runtime->ExecuteAction("testpkg", "upload raw", "agent", args);
    Assert(r["output"].asString().find("raw uploaded") != std::string::npos,
           "raw upload succeeds");
    Assert(fx.server.lastBody == bytes, "raw body is byte-exact file content");
    // egress denial
    args["url"] = "http://foreign.test/upload";
    r = fx.runtime->ExecuteAction("testpkg", "upload file", "agent", args);
    Assert(r["output"].asString().find("upload failed") != std::string::npos &&
               r["output"].asString().find("egress denied") != std::string::npos,
           "foreign host refused by egress scope");
    // traversal slug rejected loudly
    args["url"] = "http://127.0.0.1:" + std::to_string(fx.port) + "/upload";
    args["name"] = "../evil.bin";
    r = fx.runtime->ExecuteAction("testpkg", "upload file", "agent", args);
    Assert(r["output"].asString().find("invalid files-area slug") != std::string::npos,
           "traversal slug rejected");
    // missing file
    args["name"] = "nope.bin";
    r = fx.runtime->ExecuteAction("testpkg", "upload file", "agent", args);
    Assert(r["output"].asString().find("upload read failed") != std::string::npos,
           "missing file rejected");
    const int hitsAfter = fx.server.hits;
    // #126: writes_gated — upload held pending approval, then executes
    Fixture fx2;
    ApiPackage pkg2 = InstallFixturePkg(fx2, extra, R"(["127.0.0.1"])", true, true);
    const std::filesystem::path proot2 =
        std::filesystem::path(fx2.filesRoot) / "testpkg";
    std::filesystem::create_directories(proot2);
    {
        std::ofstream f(proot2 / "sample.bin", std::ios::binary);
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    Json::Value args2;
    args2["url"] = "http://127.0.0.1:" + std::to_string(fx2.port) + "/upload";
    args2["name"] = "sample.bin";
    r = fx2.runtime->ExecuteAction("testpkg", "upload file", "agent", args2);
    Assert(r["output"].asString().find("approval_required") != std::string::npos,
           "gated upload held");
    Assert(fx2.server.hits == 0, "held upload never touched the network");
    const std::string digest = r["data"]["digest"].asString();
    Assert(!digest.empty(), "digest surfaced to the caller");
    Assert(fx2.runtime->ListPendingWrites(pkg2.id).size() == 1,
           "one pending write listed");
    Assert(fx2.runtime->ApproveWrite(pkg2.id, digest), "approve ok");
    r = fx2.runtime->ExecuteAction("testpkg", "upload file", "agent", args2);
    Assert(r["output"].asString().find("uploaded 200") != std::string::npos,
           "approved upload executes");
    Assert(fx2.server.hits == 1, "exactly one hit after approval");
    Assert(fx.server.hits == hitsAfter, "no stray hits on the first fixture");
    return 0;
}

// #69 stt package v0.1: transcribe — multipart wire shape against the
// whisper contract, transcript surfaced in data AND persisted to the files
// area (durable copy past the result-string cap), loud failure on bad input.
int TestSttPackage() {
    std::cerr << "  [runtime] stt package: transcribe...\n";
    const std::string extra =
        R"({"name": "transcribe", "kind": "action", "description": "Transcribe audio from the files area",
         "parameters": {"file": {"type": "string", "required": true}, "mime": {"type": "string"}},
         "script": "function run(ctx) local base = ctx.package.get_state('base_url') local r = ctx.http.upload(base..'/inference', {files = {{name = ctx.args.file, field = 'file', mime = ctx.args.mime or 'application/octet-stream'}}, fields = {response_format = 'json'}, timeout_s = 300}) if r.status ~= 200 or not (r.json and r.json.text) then return {success = false, output = 'transcribe failed: HTTP '..tostring(r.status)..(r.error and (' - '..tostring(r.error)) or '')} end local text = r.json.text local bn = ctx.args.file:match('[^/]+$') or ctx.args.file local ok, werr = ctx.fs.write(bn..'.txt', text) if not ok then return {success = false, output = 'transcribe failed: transcript persist: '..tostring(werr)} end return {output = 'transcribed '..tostring(#text)..' chars', data = {text = text, transcript_file = bn..'.txt'}} end"})";
    Fixture fx;
    fx.server.rawBodies["/inference"] = R"({"text": " hello from whisper"})";
    InstallFixturePkg(fx, extra, R"(["127.0.0.1"])", true, false);
    const std::filesystem::path proot = std::filesystem::path(fx.filesRoot) / "testpkg";
    std::filesystem::create_directories(proot);
    {
        std::ofstream f(proot / "meeting.wav", std::ios::binary);
        const std::string audio = "RIFFfake-audio-bytes";
        f.write(audio.data(), static_cast<std::streamsize>(audio.size()));
    }
    Json::Value args;
    args["file"] = "meeting.wav";
    auto r = fx.runtime->ExecuteAction("testpkg", "transcribe", "agent", args);
    Assert(r["output"].asString().find("transcribed 19 chars") != std::string::npos,
           "stt transcribe output char count");
    Assert(r["data"]["text"].asString() == " hello from whisper",
           "stt transcript surfaced in data");
    Assert(fx.server.lastMethod == "POST" && fx.server.lastPath == "/inference",
           "stt request verb+path");
    Assert(fx.server.lastBody.find("name=\"response_format\"") != std::string::npos,
           "stt response_format field part");
    Assert(fx.server.lastBody.find("name=\"file\"; filename=\"meeting.wav\"") !=
               std::string::npos,
           "stt file part header");
    Assert(fx.server.lastBody.find("RIFFfake-audio-bytes") != std::string::npos,
           "stt audio bytes on the wire");
    std::ifstream tf(proot / "meeting.wav.txt", std::ios::binary);
    std::string persisted((std::istreambuf_iterator<char>(tf)),
                          std::istreambuf_iterator<char>());
    Assert(persisted == " hello from whisper",
           "stt transcript persisted to files area");
    Assert(r["data"]["transcript_file"].asString() == "meeting.wav.txt",
           "stt transcript_file named in data");
    args["file"] = "missing.wav";
    r = fx.runtime->ExecuteAction("testpkg", "transcribe", "agent", args);
    Assert(!r["success"].asBool() &&
               r["output"].asString().find("transcribe failed") != std::string::npos &&
               r["output"].asString().find("upload read failed") != std::string::npos,
           "stt missing file fails loudly");
    return 0;
}

// A2 tts package v0.1.0: synthesize - JSON body against the piper1-gpl
// /synthesize contract, WAV response persisted byte-exact to the files
// area (binary body -> ctx.fs.write round-trip), phrase cap loud-fails,
// non-WAV responses fail loudly.
int TestTtsPackage() {
    std::cerr << "  [runtime] tts package: synthesize...\n";
    const std::string extra =
        R"({"name": "synthesize", "kind": "action", "description": "Synthesize phrase to WAV",
         "parameters": {"text": {"type": "string", "required": true},
                        "voice": {"type": "string"},
                        "filename": {"type": "string"}},
         "script": "function run(ctx) local text = ctx.args.text or '' if #text == 0 then return {success = false, output = 'synthesize failed: text required'} end if #text > 300 then return {success = false, output = 'synthesize failed: phrase cap is 300 chars (keep the floor open - split into phrases, never slice) - got '..#text} end local t = {text = text} local v = ctx.args.voice or ctx.package.get_state('voice') or '' if v ~= '' then t.voice = v end local base = ctx.package.get_state('base_url') local r = ctx.http.post(base..'/synthesize', {headers = {['Content-Type'] = 'application/json'}, body = json.encode(t), timeout_s = 120}) if r.status ~= 200 then return {success = false, output = 'synthesize failed: HTTP '..tostring(r.status)..(r.error and (' - '..tostring(r.error)) or '')} end local body = r.body or '' if #body < 44 or body:sub(1, 4) ~= 'RIFF' then return {success = false, output = 'synthesize failed: no WAV returned ('..#body..' bytes) - service error?'} end local stem = ctx.args.filename if not stem then local d = ctx.crypto.sha256(text) stem = 'tts-'..(d and d:sub(1, 10) or 'phrase') end local fn = stem..'.wav' local ok, werr = ctx.fs.write(fn, body) if not ok then return {success = false, output = 'synthesize failed: persist: '..tostring(werr)} end return {output = 'synthesized '..#text..' chars -> '..fn..' ('..#body..' bytes)', data = {file = fn, bytes = #body, chars = #text}} end"})";
    Fixture fx;
    std::string wav = "RIFF";
    wav += std::string("\x28\x00\x00\x00", 4);
    wav += "WAVEfmt ";
    wav.append(28, 'A');
    fx.server.rawBodies["/synthesize"] = wav;
    InstallFixturePkg(fx, extra, R"(["127.0.0.1"])", true, false);
    const std::string phrase = "He said \"go\" then listened.";
    Json::Value args;
    args["text"] = phrase;
    args["voice"] = "en_US-lessac-medium";
    auto r = fx.runtime->ExecuteAction("testpkg", "synthesize", "agent", args);
    Assert(r["output"].asString().find("synthesized " + std::to_string(phrase.size()) +
                                        " chars") != std::string::npos,
           "tts synthesize output char count");
    Assert(fx.server.lastMethod == "POST" && fx.server.lastPath == "/synthesize",
           "tts request verb+path");
    Assert(fx.server.lastBody.find("\"text\"") != std::string::npos,
           "tts JSON text field");
    Assert(fx.server.lastBody.find("He said \\\"go\\\"") != std::string::npos,
           "tts phrase escaped through json.encode");
    Assert(fx.server.lastBody.find("lessac") != std::string::npos,
           "tts voice in payload");
    const std::string fname = r["data"]["file"].asString();
    Assert(fname.size() == 18 && fname.substr(0, 4) == "tts-" &&
               fname.substr(14) == ".wav",
           "tts derived filename shape");
    std::ifstream wf(std::filesystem::path(fx.filesRoot) / "testpkg" / fname,
                     std::ios::binary);
    std::string persisted((std::istreambuf_iterator<char>(wf)),
                          std::istreambuf_iterator<char>());
    Assert(persisted == wav, "tts WAV persisted byte-exact (binary fs round-trip)");
    Assert(r["data"]["bytes"].asInt() == static_cast<int>(wav.size()),
           "tts bytes surfaced in data");
    args["filename"] = "reply-01";
    r = fx.runtime->ExecuteAction("testpkg", "synthesize", "agent", args);
    Assert(r["data"]["file"].asString() == "reply-01.wav",
           "tts explicit filename honored");
    args.removeMember("filename");
    args.removeMember("voice");
    args["text"] = std::string(301, 'a');
    r = fx.runtime->ExecuteAction("testpkg", "synthesize", "agent", args);
    Assert(!r["success"].asBool() &&
               r["output"].asString().find("phrase cap") != std::string::npos,
           "tts over-cap phrase fails loudly");
    args["text"] = "";
    r = fx.runtime->ExecuteAction("testpkg", "synthesize", "agent", args);
    Assert(!r["success"].asBool() &&
               r["output"].asString().find("text required") != std::string::npos,
           "tts empty text fails loudly");
    args["text"] = "short phrase";
    fx.server.rawBodies["/synthesize"] = "Internal Server Error";
    r = fx.runtime->ExecuteAction("testpkg", "synthesize", "agent", args);
    Assert(!r["success"].asBool() &&
               r["output"].asString().find("no WAV returned") != std::string::npos,
           "tts non-WAV response fails loudly");
    return 0;
}

// #69 notetaker handoff: files_from — declared sibling reads resolve
// kernel-side against the sibling's files area ("<pkg>/<slug>" refs),
// undeclared siblings are denied loudly egress-style, traversal via the
// pkg prefix is rejected. Bare names stay own-area (back-compat, covered
// by the upload tests above).
int TestScriptHttpUploadFilesFrom() {
    std::cerr << "  [runtime] ctx.http.upload files_from...\n";
    const std::string extra =
        R"({"name": "send", "kind": "action", "description": "Upload named file",
         "parameters": {"file": {"type": "string", "required": true},
                        "url": {"type": "string", "required": true}},
         "script": "function run(ctx) local r = ctx.http.upload(ctx.args.url, {files = {{name = ctx.args.file, field='file'}}}) if r.status ~= 200 then return {success=false, output='upload failed: '..tostring(r.status)..(r.error and (' - '..tostring(r.error)) or '')} end return {output='ok'} end"})";
    Fixture fx;
    fx.server.rawBodies["/up"] = "ok";
    InstallFixturePkg(fx, extra, R"(["127.0.0.1"])", true, false, R"(["peer"])");
    // peer = recording source (nextcloud stand-in): installed + seeded
    const std::string peerManifest = R"({
      "kind": "api_package", "name": "peer", "version": "0.1.0",
      "description": "peer fixture - recording source",
      "commands": [{"name": "noop", "kind": "action", "description": "noop",
         "script": "function run(ctx) return {output='noop'} end"}],
      "connections": []
    })";
    ApiPackage peer = fx.store.InstallFromManifest(peerManifest, "", "", true);
    fx.store.SetPackageEnabled(peer.id, true);
    const std::filesystem::path peerRoot =
        std::filesystem::path(fx.filesRoot) / "peer";
    std::filesystem::create_directories(peerRoot);
    {
        std::ofstream f(peerRoot / "rec.ogg", std::ios::binary);
        const std::string audio = "PEER-RECORDING-BYTES";
        f.write(audio.data(), static_cast<std::streamsize>(audio.size()));
    }
    Json::Value args;
    args["url"] = "http://127.0.0.1:" + std::to_string(fx.port) + "/up";
    args["file"] = "peer/rec.ogg";
    auto r = fx.runtime->ExecuteAction("testpkg", "send", "agent", args);
    Assert(r["success"].asBool(), "files_from sibling upload succeeds");
    Assert(fx.server.lastBody.find("PEER-RECORDING-BYTES") != std::string::npos,
           "sibling bytes on the wire");
    Assert(fx.server.lastBody.find("filename=\"rec.ogg\"") != std::string::npos,
           "sibling part filename is the slug");
    // undeclared sibling: denied loudly, nothing touches the wire
    const int hitsBefore = fx.server.hits.load();
    args["file"] = "other/x.dat";
    r = fx.runtime->ExecuteAction("testpkg", "send", "agent", args);
    Assert(!r["success"].asBool() &&
               r["output"].asString().find("files_from") != std::string::npos,
           "undeclared sibling denied loudly");
    Assert(fx.server.hits == hitsBefore, "denied upload never hit the wire");
    // traversal via the pkg prefix: slug carries slashes -> rejected
    args["file"] = "peer/../testpkg/secret.dat";
    r = fx.runtime->ExecuteAction("testpkg", "send", "agent", args);
    Assert(!r["success"].asBool() &&
               r["output"].asString().find("invalid files-area slug") !=
                   std::string::npos,
           "traversal via pkg prefix rejected");
    return 0;
}

// v0.4.0 (#293): office depth — connect + files list dir, recordings list,
// share list/delete, deck suite, note get/update/delete, event query/delete,
// activity feed. Scripts and request templates are VERBATIM from
// packages/nextcloud/manifest.json; the fixture state schema carries
// user/auth_header so {{state.*}} interpolation resolves without adaptation.
int TestNextcloudOfficeCommands() {
    Fixture fx;
    const std::string extra = R"nk({"name":"connect","kind":"action","description":"Validate an app password against the instance and store the precomputed Authorization header. Get the app password from the animus-agent account (Settings \u2192 Security \u2192 Devices & sessions).","parameters":{"user":{"type":"string","required":true},"app_password":{"type":"string","required":true,"secret":true}},"script":"function run(ctx) local h = 'Basic '..b64.encode(ctx.args.user..':'..ctx.args.app_password) local r = ctx.http.get(ctx.package.get_state('base_url')..'/ocs/v2.php/cloud/user?format=json', {headers = {['Authorization'] = h, ['OCS-APIREQUEST'] = 'true', ['Accept'] = 'application/json'}}) if r.status ~= 200 then return {success = false, output = 'connect failed: HTTP '..tostring(r.status)..(r.error and (' - '..tostring(r.error)) or '')} end local o = r.json and r.json.ocs if not o or not o.data or not o.data.id then return {success = false, output = 'connect failed: unexpected OCS payload (HTTP '..tostring(r.status)..')'} end local ok1 = ctx.package.set_state('user', ctx.args.user) local ok2 = ctx.package.set_state('auth_header', h) if not ok1 or not ok2 then return {success = false, output = 'connect failed: state persist error'} end return {output = 'connected as '..o.data.id..' ('..tostring(o.data['display-name'])..')', data = {user_id = o.data.id}} end"},{"name":"files list dir","kind":"action","description":"List a folder's contents (PROPFIND, Depth 1). path is relative to the account root, percent-encoded (spaces %20), trailing slash optional. Bare-root listing: files list.","parameters":{"path":{"type":"string","required":true}},"request":{"method":"PROPFIND","url":"{{state.base_url}}/remote.php/dav/files/{{state.user}}/{{args.path}}","headers":{"Authorization":"{{state.auth_header}}","Depth":"1"}},"script":"function run(ctx) local b = ctx.request.body or '' local rows = {} for resp in b:gmatch('<d:response>.-</d:response>') do local href = resp:match('<d:href>([^<]+)</d:href>') or '' local is_dir = resp:match('<d:resourcetype>%s*<d:collection') ~= nil local size = resp:match('<d:getcontentlength>([^<]+)<') or '' local mtime = resp:match('<d:getlastmodified>([^<]+)</d:getlastmodified>') or '' local name = href:match('([^/]+)/?$') or href name = name:gsub('%%20', ' ') table.insert(rows, {name = name, type = is_dir and 'dir' or 'file', size = tonumber(size) or 0, modified = mtime, href = href}) end if #rows == 0 then return {success = false, output = 'files list dir: no response rows parsed - unexpected body shape (HTTP '..tostring(ctx.request.status)..')'} end return {output = #rows..' entries', data = rows} end"},{"name":"recordings list","kind":"action","description":"List Talk rooms that have call recordings (PROPFIND on Talk/Recording/, Depth 1). Rooms appear after their first finalized recording; each room dir holds the .ogg files (list them via files list dir Talk/Recording/<token>/).","parameters":{},"request":{"method":"PROPFIND","url":"{{state.base_url}}/remote.php/dav/files/{{state.user}}/Talk/Recording/","headers":{"Authorization":"{{state.auth_header}}","Depth":"1"}},"script":"function run(ctx) local b = ctx.request.body or '' local rows = {} local n = 0 for resp in b:gmatch('<d:response>.-</d:response>') do n = n + 1 local href = resp:match('<d:href>([^<]+)</d:href>') or '' local is_dir = resp:match('<d:resourcetype>%s*<d:collection') ~= nil local name = href:match('([^/]+)/?$') or href if is_dir and name ~= 'Recording' then table.insert(rows, {room = name, href = href}) end end if n == 0 then return {success = false, output = 'recordings list: no response rows parsed - unexpected body shape (HTTP '..tostring(ctx.request.status)..') - Talk/Recording absent?'} end return {output = #rows..' recorded rooms', data = rows} end"},{"name":"share list","kind":"action","description":"List link shares (OCS files_sharing). Rows: id, type (3=public link), owner, path, url, token.","parameters":{},"request":{"method":"GET","url":"{{state.base_url}}/ocs/v2.php/apps/files_sharing/api/v1/shares?format=json","headers":{"Authorization":"{{state.auth_header}}","OCS-APIREQUEST":"true","Accept":"application/json"}},"script":"function run(ctx) local r = ctx.request local j = r.json or {} local meta = (j.ocs and j.ocs.meta) or {} local d = (j.ocs and j.ocs.data) or {} if meta.statuscode and meta.statuscode ~= 200 then return {success = false, output = 'share list failed: OCS '..tostring(meta.statuscode)..' '..tostring(meta.message or '')} end if type(d) ~= 'table' then return {success = false, output = 'share list: unexpected OCS payload (HTTP '..tostring(r.status)..')'} end local rows = {} for _, s in ipairs(d) do table.insert(rows, {id = s.id, type = s.share_type, owner = s.uid_owner, path = s.file_target or s.path, url = s.url, token = s.token}) end return {output = #rows..' shares', data = rows} end"},{"name":"share delete","kind":"action","description":"Delete a share by id (OCS files_sharing). Write - per-invocation #126 approval applies.","parameters":{"id":{"type":"string","required":true}},"script":"function run(ctx) local r = ctx.http.delete(ctx.package.get_state('base_url')..'/ocs/v2.php/apps/files_sharing/api/v1/shares/'..tostring(ctx.args.id)..'?format=json', {headers = {['Authorization'] = ctx.package.get_state('auth_header'), ['OCS-APIREQUEST'] = 'true', ['Accept'] = 'application/json'}}) if r.status ~= 200 then return {success = false, output = 'share delete failed: HTTP '..tostring(r.status)..(r.error and (' - '..tostring(r.error)) or '')} end local j = r.json or {} local meta = (j.ocs and j.ocs.meta) or {} if meta.statuscode and meta.statuscode ~= 200 then return {success = false, output = 'share delete failed: OCS '..tostring(meta.statuscode)..' '..tostring(meta.message or '')} end return {output = 'share '..tostring(ctx.args.id)..' deleted'} end"},{"name":"deck boards","kind":"action","description":"List Deck boards (REST). Rows: id, title, color, archived.","parameters":{},"request":{"method":"GET","url":"{{state.base_url}}/index.php/apps/deck/api/v1.0/boards","headers":{"Authorization":"{{state.auth_header}}","Accept":"application/json"}},"script":"function run(ctx) local r = ctx.request if r.status ~= 200 then return {success = false, output = 'deck boards failed: HTTP '..tostring(r.status)} end local j = r.json if type(j) ~= 'table' then return {success = false, output = 'deck boards: unexpected payload (HTTP '..tostring(r.status)..')'} end local rows = {} for _, b in ipairs(j) do table.insert(rows, {id = b.id, title = b.title, color = b.color, archived = b.archived}) end return {output = #rows..' boards', data = rows} end"},{"name":"deck stacks","kind":"action","description":"List a board's stacks with their cards (REST). Rows: id, title, cards[{id,title}].","parameters":{"board":{"type":"string","required":true}},"request":{"method":"GET","url":"{{state.base_url}}/index.php/apps/deck/api/v1.0/boards/{{args.board}}/stacks","headers":{"Authorization":"{{state.auth_header}}","Accept":"application/json"}},"script":"function run(ctx) local r = ctx.request if r.status ~= 200 then return {success = false, output = 'deck stacks failed: HTTP '..tostring(r.status)} end local j = r.json if type(j) ~= 'table' then return {success = false, output = 'deck stacks: unexpected payload (HTTP '..tostring(r.status)..')'} end local rows = {} for _, s in ipairs(j) do local cards = {} if type(s.cards) == 'table' then for _, c in ipairs(s.cards) do table.insert(cards, {id = c.id, title = c.title}) end end table.insert(rows, {id = s.id, title = s.title, cards = cards}) end if #rows == 0 then return {success = false, output = 'deck stacks: no stacks parsed (HTTP '..tostring(r.status)..') - board exists?'} end return {output = #rows..' stacks', data = rows} end"},{"name":"deck card create","kind":"action","description":"Create a card in a stack (REST). description optional. Write - per-invocation #126 approval applies.","parameters":{"board":{"type":"string","required":true},"stack":{"type":"string","required":true},"title":{"type":"string","required":true},"description":{"type":"string","required":false}},"script":"function run(ctx) local body = {title = ctx.args.title} if ctx.args.description then body.description = ctx.args.description end local r = ctx.http.post(ctx.package.get_state('base_url')..'/index.php/apps/deck/api/v1.0/boards/'..tostring(ctx.args.board)..'/stacks/'..tostring(ctx.args.stack)..'/cards', {headers = {['Authorization'] = ctx.package.get_state('auth_header'), ['Accept'] = 'application/json', ['Content-Type'] = 'application/json'}, body = json.encode(body)}) if r.status ~= 200 then return {success = false, output = 'deck card create failed: HTTP '..tostring(r.status)..(r.error and (' - '..tostring(r.error)) or '')} end local j = r.json or {} return {output = 'card '..tostring(j.id)..': '..tostring(j.title), data = {id = j.id, title = j.title, stackId = j.stackId}} end"},{"name":"deck card update","kind":"action","description":"Update a card's title and/or description (REST PUT). At least one of title/description required. Write - per-invocation #126 approval applies.","parameters":{"board":{"type":"string","required":true},"stack":{"type":"string","required":true},"card":{"type":"string","required":true},"title":{"type":"string","required":false},"description":{"type":"string","required":false}},"script":"function run(ctx) if ctx.args.title == nil and ctx.args.description == nil then return {success = false, output = 'deck card update: nothing to update (title or description required)'} end local body = {} if ctx.args.title ~= nil then body.title = ctx.args.title end if ctx.args.description ~= nil then body.description = ctx.args.description end local r = ctx.http.put(ctx.package.get_state('base_url')..'/index.php/apps/deck/api/v1.0/boards/'..tostring(ctx.args.board)..'/stacks/'..tostring(ctx.args.stack)..'/cards/'..tostring(ctx.args.card), {headers = {['Authorization'] = ctx.package.get_state('auth_header'), ['Accept'] = 'application/json', ['Content-Type'] = 'application/json'}, body = json.encode(body)}) if r.status ~= 200 then return {success = false, output = 'deck card update failed: HTTP '..tostring(r.status)..(r.error and (' - '..tostring(r.error)) or '')} end local j = r.json or {} return {output = 'card '..tostring(j.id)..': '..tostring(j.title), data = {id = j.id, title = j.title, stackId = j.stackId}} end"},{"name":"deck card move","kind":"action","description":"Move a card to another stack (REST PUT with stackId). to_stack is the target stack id. Write - per-invocation #126 approval applies.","parameters":{"board":{"type":"string","required":true},"stack":{"type":"string","required":true},"card":{"type":"string","required":true},"to_stack":{"type":"string","required":true}},"script":"function run(ctx) local r = ctx.http.put(ctx.package.get_state('base_url')..'/index.php/apps/deck/api/v1.0/boards/'..tostring(ctx.args.board)..'/stacks/'..tostring(ctx.args.stack)..'/cards/'..tostring(ctx.args.card), {headers = {['Authorization'] = ctx.package.get_state('auth_header'), ['Accept'] = 'application/json', ['Content-Type'] = 'application/json'}, body = json.encode({stackId = tonumber(ctx.args.to_stack)})}) if r.status ~= 200 then return {success = false, output = 'deck card move failed: HTTP '..tostring(r.status)..(r.error and (' - '..tostring(r.error)) or '')} end local j = r.json or {} return {output = 'card '..tostring(ctx.args.card)..' -> stack '..tostring(j.stackId or ctx.args.to_stack), data = {id = j.id, stackId = j.stackId}} end"},{"name":"deck card delete","kind":"action","description":"Delete a card (REST). Write - per-invocation #126 approval applies.","parameters":{"board":{"type":"string","required":true},"stack":{"type":"string","required":true},"card":{"type":"string","required":true}},"script":"function run(ctx) local r = ctx.http.delete(ctx.package.get_state('base_url')..'/index.php/apps/deck/api/v1.0/boards/'..tostring(ctx.args.board)..'/stacks/'..tostring(ctx.args.stack)..'/cards/'..tostring(ctx.args.card), {headers = {['Authorization'] = ctx.package.get_state('auth_header'), ['Accept'] = 'application/json'}}) if r.status ~= 200 and r.status ~= 204 then return {success = false, output = 'deck card delete failed: HTTP '..tostring(r.status)..(r.error and (' - '..tostring(r.error)) or '')} end return {output = 'card '..tostring(ctx.args.card)..' deleted'} end"},{"name":"note get","kind":"action","description":"Fetch one note by id (Notes app API). Returns {id, title, content, modified}. Content travels in the result lane - kernel caps oversized strings.","parameters":{"id":{"type":"string","required":true}},"request":{"method":"GET","url":"{{state.base_url}}/index.php/apps/notes/api/v1/notes/{{args.id}}","headers":{"Authorization":"{{state.auth_header}}","Accept":"application/json"}},"script":"function run(ctx) local r = ctx.request local j = r.json or {} if r.status ~= 200 or j.id == nil then return {success = false, output = 'note get failed: HTTP '..tostring(r.status)} end return {output = 'note '..tostring(j.id)..': '..tostring(j.title), data = {id = j.id, title = j.title, content = j.content, modified = j.modified}} end"},{"name":"note update","kind":"action","description":"Update a note's title and/or content by id (Notes app API PUT). At least one of title/content required. Write - per-invocation #126 approval applies.","parameters":{"id":{"type":"string","required":true},"title":{"type":"string","required":false},"content":{"type":"string","required":false}},"script":"function run(ctx) if ctx.args.title == nil and ctx.args.content == nil then return {success = false, output = 'note update: nothing to update (title or content required)'} end local body = {} if ctx.args.title ~= nil then body.title = ctx.args.title end if ctx.args.content ~= nil then body.content = ctx.args.content end local r = ctx.http.put(ctx.package.get_state('base_url')..'/index.php/apps/notes/api/v1/notes/'..tostring(ctx.args.id), {headers = {['Authorization'] = ctx.package.get_state('auth_header'), ['Accept'] = 'application/json', ['Content-Type'] = 'application/json'}, body = json.encode(body)}) if r.status ~= 200 then return {success = false, output = 'note update failed: HTTP '..tostring(r.status)..(r.error and (' - '..tostring(r.error)) or '')} end local j = r.json or {} return {output = 'note '..tostring(j.id)..': '..tostring(j.title), data = {id = j.id, title = j.title, modified = j.modified}} end"},{"name":"note delete","kind":"action","description":"Delete a note by id (Notes app API). Write - per-invocation #126 approval applies.","parameters":{"id":{"type":"string","required":true}},"script":"function run(ctx) local r = ctx.http.delete(ctx.package.get_state('base_url')..'/index.php/apps/notes/api/v1/notes/'..tostring(ctx.args.id), {headers = {['Authorization'] = ctx.package.get_state('auth_header'), ['Accept'] = 'application/json'}}) if r.status ~= 200 and r.status ~= 204 then return {success = false, output = 'note delete failed: HTTP '..tostring(r.status)..(r.error and (' - '..tostring(r.error)) or '')} end return {output = 'note '..tostring(ctx.args.id)..' deleted'} end"},{"name":"event query","kind":"action","description":"Find events in a UTC time range (CalDAV REPORT calendar-query). start/end are iCalendar UTC datetimes like 20261116T000000Z; calendar is the calendar name from cal list. Rows: uid, summary, dtstart, href.","parameters":{"calendar":{"type":"string","required":true},"start":{"type":"string","required":true},"end":{"type":"string","required":true}},"request":{"method":"REPORT","url":"{{state.base_url}}/remote.php/dav/calendars/{{state.user}}/{{args.calendar}}/","headers":{"Authorization":"{{state.auth_header}}","Depth":"1","Content-Type":"application/xml"},"body":"<?xml version=\"1.0\" encoding=\"utf-8\"?><c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop><d:getetag/><c:calendar-data/></d:prop><c:filter><c:comp-filter name=\"VCALENDAR\"><c:comp-filter name=\"VEVENT\"><c:time-range start=\"{{args.start}}\" end=\"{{args.end}}\"/></c:comp-filter></c:comp-filter></c:filter></c:calendar-query>"},"script":"function run(ctx) local b = ctx.request.body or '' local rows = {} local n = 0 for resp in b:gmatch('<d:response>.-</d:response>') do n = n + 1 local href = resp:match('<d:href>([^<]+)</d:href>') or '' local ics = resp:match('<cal:calendar%-data>(.-)</cal:calendar%-data>') or resp:match('<x1:calendar%-data>(.-)</x1:calendar%-data>') or '' if href ~= '' and ics ~= '' then local uid = ics:match('UID:([^%c%s]+)') or (href:match('([^/]+)%.ics$') or href) local summary = ics:match('SUMMARY:([^%c]+)') or '' summary = summary:gsub('%s+$', '') local dtstart = ics:match('DTSTART:([^%c]+)') or '' dtstart = dtstart:gsub('%s+$', '') table.insert(rows, {uid = uid, summary = summary, dtstart = dtstart, href = href}) end end if n == 0 then return {success = false, output = 'event query: no response rows parsed - unexpected body shape (HTTP '..tostring(ctx.request.status)..')'} end return {output = #rows..' events in range', data = rows} end"},{"name":"event delete","kind":"action","description":"Delete an event by uid (CalDAV DELETE on the .ics resource). Write - per-invocation #126 approval applies.","parameters":{"calendar":{"type":"string","required":true},"uid":{"type":"string","required":true}},"request":{"method":"DELETE","url":"{{state.base_url}}/remote.php/dav/calendars/{{state.user}}/{{args.calendar}}/{{args.uid}}.ics","headers":{"Authorization":"{{state.auth_header}}"}},"script":"function run(ctx) local r = ctx.request if r.status ~= 200 and r.status ~= 204 then return {success = false, output = 'event delete failed: HTTP '..tostring(r.status)} end return {output = 'event '..tostring(ctx.args.uid)..' deleted'} end"},{"name":"activity feed","kind":"action","description":"Read the Activity feed (OCS v2). limit is rows per page (20-50 sane). Rows: id, app, type, user, subject, when.","parameters":{"limit":{"type":"number","required":true}},"request":{"method":"GET","url":"{{state.base_url}}/ocs/v2.php/apps/activity/api/v2/activity?format=json&limit={{args.limit}}","headers":{"Authorization":"{{state.auth_header}}","OCS-APIREQUEST":"true","Accept":"application/json"}},"script":"function run(ctx) local r = ctx.request local j = r.json or {} local meta = (j.ocs and j.ocs.meta) or {} local d = (j.ocs and j.ocs.data) or {} if meta.statuscode and meta.statuscode ~= 200 then return {success = false, output = 'activity feed failed: OCS '..tostring(meta.statuscode)..' '..tostring(meta.message or '')} end if type(d) ~= 'table' then return {success = false, output = 'activity feed: unexpected OCS payload (HTTP '..tostring(r.status)..')'} end local rows = {} for _, a in ipairs(d) do table.insert(rows, {id = a.activity_id, app = a.app, type = a.type, user = a.user, subject = a.subject, when = a.datetime}) end return {output = #rows..' activity rows', data = rows} end"})nk";
    InstallFixturePkg(fx, extra);
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        fx.server.rawBodies["/ocs/v2.php/cloud/user"] =
            "{\"ocs\":{\"meta\":{\"status\":\"ok\",\"statuscode\":200},"
            "\"data\":{\"id\":\"agent\",\"displayname\":\"Agent\"}}}";
        fx.server.rawBodies["/index.php/apps/deck/api/v1.0/boards"] =
            "[{\"id\":7,\"title\":\"Roadmap\",\"color\":\"00ff00\",\"archived\":false},"
            "{\"id\":9,\"title\":\"Ops\",\"color\":\"ff0000\",\"archived\":false}]";
        fx.server.rawBodies["/index.php/apps/deck/api/v1.0/boards/7/stacks"] =
            "[{\"id\":21,\"title\":\"To do\",\"cards\":[{\"id\":101,\"title\":\"Ship v0.4.0\",\"stackId\":21}]},"
            "{\"id\":22,\"title\":\"Done\",\"cards\":[]}]";
        fx.server.rawBodies["/index.php/apps/deck/api/v1.0/boards/7/stacks/21/cards"] =
            "{\"id\":102,\"title\":\"Test card\",\"stackId\":21}";
        fx.server.rawBodies["/index.php/apps/notes/api/v1/notes/42"] =
            "{\"id\":42,\"title\":\"Scratch\",\"content\":\"hello note\",\"modified\":1795000000}";
        fx.server.rawBodies["/ocs/v2.php/apps/files_sharing/api/v1/shares"] =
            "{\"ocs\":{\"meta\":{\"status\":\"ok\",\"statuscode\":200},\"data\":[{"
            "\"id\":\"12\",\"share_type\":3,\"uid_owner\":\"agent\","
            "\"file_target\":\"/Talk/Recording/d7vv2vaa\","
            "\"url\":\"https://nc.example/s/abc123\",\"token\":\"abc123\"}]}}";
        fx.server.rawBodies["/ocs/v2.php/apps/activity/api/v2/activity"] =
            "{\"ocs\":{\"meta\":{\"status\":\"ok\",\"statuscode\":200},\"data\":[{"
            "\"activity_id\":\"55\",\"app\":\"spreed\",\"type\":\"call\","
            "\"user\":\"melvin\",\"subject\":\"You attended a call\","
            "\"datetime\":\"2026-11-15T20:31:00+00:00\"}]}}";
        fx.server.rawBodies["/remote.php/dav/files/agent/sub/"] =
            "<d:multistatus xmlns:d=\"DAV:\">"
            "<d:response><d:href>/remote.php/dav/files/agent/sub/</d:href>"
            "<d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop>"
            "<d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>"
            "<d:response><d:href>/remote.php/dav/files/agent/sub/notes.txt</d:href>"
            "<d:propstat><d:prop><d:resourcetype/><d:getcontentlength>20</d:getcontentlength>"
            "<d:getlastmodified>Mon, 16 Nov 2026 06:00:00 GMT</d:getlastmodified></d:prop>"
            "<d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>"
            "</d:multistatus>";
        fx.server.rawBodies["/remote.php/dav/files/agent/Talk/Recording/"] =
            "<d:multistatus xmlns:d=\"DAV:\">"
            "<d:response><d:href>/remote.php/dav/files/agent/Talk/Recording/</d:href>"
            "<d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop>"
            "<d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>"
            "<d:response><d:href>/remote.php/dav/files/agent/Talk/Recording/d7vv2vaa/</d:href>"
            "<d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop>"
            "<d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>"
            "</d:multistatus>";
        fx.server.rawBodies["/remote.php/dav/calendars/agent/personal/"] =
            "<d:multistatus xmlns:d=\"DAV:\" xmlns:cal=\"urn:ietf:params:xml:ns:caldav\">"
            "<d:response><d:href>/remote.php/dav/calendars/agent/personal/standup-1.ics</d:href>"
            "<d:propstat><d:prop><cal:calendar-data>BEGIN:VCALENDAR\n"
            "VERSION:2.0\nBEGIN:VEVENT\nUID:standup-1\nSUMMARY:Standup\n"
            "DTSTART:20261116T080000Z\nEND:VEVENT\nEND:VCALENDAR</cal:calendar-data></d:prop>"
            "<d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>"
            "</d:multistatus>";
        fx.server.rawBodies["/remote.php/dav/calendars/agent/empty/"] =
            "<d:multistatus xmlns:d=\"DAV:\"/>";
    }

    // connect: validates against OCS cloud/user, stores user + auth_header.
    Json::Value ca;
    ca["user"] = "agent";
    ca["app_password"] = "apw-123";
    auto r = fx.runtime->ExecuteAction("testpkg", "connect", "agent", ca);
    Assert(r["success"].asBool(), "connect succeeds against OCS cloud/user shape");

    // files list dir: dir + file rows parse.
    Json::Value fa;
    fa["path"] = "sub/";
    r = fx.runtime->ExecuteAction("testpkg", "files list dir", "agent", fa);
    Assert(r["success"].asBool() && r["data"].size() == 2,
           "files list dir: 2 rows");
    Assert(r["data"][1]["name"].asString() == "notes.txt" &&
               r["data"][1]["size"].asInt() == 20,
           "files list dir: file row name+size");

    // recordings list: room dirs only, root row skipped.
    r = fx.runtime->ExecuteAction("testpkg", "recordings list", "agent",
                                  Json::Value());
    Assert(r["success"].asBool() && r["data"].size() == 1 &&
               r["data"][0]["room"].asString() == "d7vv2vaa",
           "recordings list: room dir parsed, root skipped");

    // share list: OCS rows.
    r = fx.runtime->ExecuteAction("testpkg", "share list", "agent",
                                  Json::Value());
    Assert(r["success"].asBool() && r["data"].size() == 1 &&
               r["data"][0]["token"].asString() == "abc123",
           "share list: OCS share row parsed");

    // share delete: script-composed OCS DELETE (default 200 body tolerated).
    Json::Value sd;
    sd["id"] = "12";
    r = fx.runtime->ExecuteAction("testpkg", "share delete", "agent", sd);
    Assert(r["success"].asBool(), "share delete executed");
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        Assert(fx.server.lastMethod == "DELETE" &&
                   fx.server.lastPath.find("/shares/12") != std::string::npos,
               "share delete wire: DELETE /shares/12");
    }

    // deck boards / stacks.
    r = fx.runtime->ExecuteAction("testpkg", "deck boards", "agent",
                                  Json::Value());
    Assert(r["success"].asBool() && r["data"].size() == 2 &&
               r["data"][0]["title"].asString() == "Roadmap",
           "deck boards: 2 boards parsed");
    Json::Value ds;
    ds["board"] = "7";
    r = fx.runtime->ExecuteAction("testpkg", "deck stacks", "agent", ds);
    Assert(r["success"].asBool() && r["data"].size() == 2 &&
               r["data"][0]["cards"].size() == 1 &&
               r["data"][0]["cards"][0]["title"].asString() == "Ship v0.4.0",
           "deck stacks: nested cards parsed");

    // deck card create: script-composed POST with json.encode body.
    Json::Value dcc;
    dcc["board"] = "7";
    dcc["stack"] = "21";
    dcc["title"] = "Test card";
    r = fx.runtime->ExecuteAction("testpkg", "deck card create", "agent", dcc);
    Assert(r["success"].asBool() &&
               r["output"].asString().find("card 102") != std::string::npos,
           "deck card create: id+title echoed");
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        Assert(fx.server.lastMethod == "POST" &&
                   fx.server.lastBody.find("Test card") != std::string::npos,
               "deck card create wire: POST + json body");
    }

    // deck card update: loud fail on no fields; title-only PUT.
    Json::Value du;
    du["board"] = "7";
    du["stack"] = "21";
    du["card"] = "101";
    r = fx.runtime->ExecuteAction("testpkg", "deck card update", "agent", du);
    Assert(!r["success"].asBool() &&
               r["output"].asString().find("nothing to update") !=
                   std::string::npos,
           "deck card update: loud fail when no fields");
    du["title"] = "Renamed";
    r = fx.runtime->ExecuteAction("testpkg", "deck card update", "agent", du);
    Assert(r["success"].asBool(), "deck card update executed");
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        Assert(fx.server.lastMethod == "PUT" &&
                   fx.server.lastBody.find("Renamed") != std::string::npos,
               "deck card update wire: PUT + field");
    }

    // deck card move: PUT with stackId.
    Json::Value dm;
    dm["board"] = "7";
    dm["stack"] = "21";
    dm["card"] = "101";
    dm["to_stack"] = "22";
    r = fx.runtime->ExecuteAction("testpkg", "deck card move", "agent", dm);
    Assert(r["success"].asBool(), "deck card move executed");
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        Assert(fx.server.lastBody.find("stackId") != std::string::npos,
               "deck card move wire: stackId body");
    }

    // deck card delete (clean args — unknown-arg validation rejects to_stack).
    Json::Value dcd;
    dcd["board"] = "7";
    dcd["stack"] = "21";
    dcd["card"] = "101";
    r = fx.runtime->ExecuteAction("testpkg", "deck card delete", "agent", dcd);
    Assert(r["success"].asBool(), "deck card delete executed");
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        Assert(fx.server.lastMethod == "DELETE", "deck card delete wire");
    }

    // note get.
    Json::Value ng;
    ng["id"] = "42";
    r = fx.runtime->ExecuteAction("testpkg", "note get", "agent", ng);
    Assert(r["success"].asBool() &&
               r["data"]["content"].asString() == "hello note",
           "note get: content parsed");

    // note update: loud fail on no fields; title-only PUT.
    Json::Value nu;
    nu["id"] = "42";
    r = fx.runtime->ExecuteAction("testpkg", "note update", "agent", nu);
    Assert(!r["success"].asBool() &&
               r["output"].asString().find("nothing to update") !=
                   std::string::npos,
           "note update: loud fail when no fields");
    nu["title"] = "Renamed note";
    r = fx.runtime->ExecuteAction("testpkg", "note update", "agent", nu);
    Assert(r["success"].asBool(), "note update executed");
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        Assert(fx.server.lastMethod == "PUT", "note update wire: PUT");
    }

    // note delete (uncanned path -> default 200 body tolerated).
    Json::Value nd;
    nd["id"] = "43";
    r = fx.runtime->ExecuteAction("testpkg", "note delete", "agent", nd);
    Assert(r["success"].asBool(), "note delete executed (200-tolerant)");
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        Assert(fx.server.lastMethod == "DELETE" &&
                   fx.server.lastPath.find("/notes/43") != std::string::npos,
               "note delete wire");
    }

    // event query: REPORT verb + interpolated time-range XML; ics parsed.
    Json::Value eq;
    eq["calendar"] = "personal";
    eq["start"] = "20261116T000000Z";
    eq["end"] = "20261116T235959Z";
    r = fx.runtime->ExecuteAction("testpkg", "event query", "agent", eq);
    Assert(r["success"].asBool() && r["data"].size() == 1 &&
               r["data"][0]["summary"].asString() == "Standup" &&
               r["data"][0]["uid"].asString() == "standup-1" &&
               r["data"][0]["dtstart"].asString() == "20261116T080000Z",
           "event query: uid/summary/dtstart parsed from calendar-data");
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        Assert(fx.server.lastMethod == "REPORT",
               "event query wire: REPORT verb");
        Assert(fx.server.lastBody.find("start=\"20261116T000000Z\"") !=
                   std::string::npos &&
                   fx.server.lastBody.find("end=\"20261116T235959Z\"") !=
                       std::string::npos,
               "event query wire: time-range interpolated into XML body");
    }

    // event query: zero responses -> loud fail, not empty success.
    eq["calendar"] = "empty";
    r = fx.runtime->ExecuteAction("testpkg", "event query", "agent", eq);
    Assert(!r["success"].asBool() &&
               r["output"].asString().find("no response rows") !=
                   std::string::npos,
           "event query: loud fail on empty multistatus");

    // event delete: declarative DELETE on the .ics resource.
    Json::Value ed;
    ed["calendar"] = "personal";
    ed["uid"] = "standup-1";
    r = fx.runtime->ExecuteAction("testpkg", "event delete", "agent", ed);
    Assert(r["success"].asBool(), "event delete executed");
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        Assert(fx.server.lastMethod == "DELETE" &&
                   fx.server.lastPath.find("standup-1.ics") !=
                       std::string::npos,
               "event delete wire: .ics path");
    }

    // activity feed: OCS rows parsed.
    Json::Value af;
    af["limit"] = 20;
    r = fx.runtime->ExecuteAction("testpkg", "activity feed", "agent", af);
    Assert(r["success"].asBool() && r["data"].size() == 1 &&
               r["data"][0]["subject"].asString() == "You attended a call",
           "activity feed: OCS row parsed");

    return 0;
}

// v0.5.0 (A2): talk send — text into a Talk room via the OCS spreed chat
// API. Scripts VERBATIM from packages/nextcloud/manifest.json (connect +
// talk send). Pins the wire truth (POST + chat path + body carrying the
// message), the OCS envelope parse, and the loud failure on a non-OCS body.
int TestNextcloudTalkSend() {
    Fixture fx;
    const std::string extra = R"nk({"name":"connect","kind":"action","description":"Validate an app password against the instance and store the precomputed Authorization header. Get the app password from the animus-agent account (Settings \u2192 Security \u2192 Devices & sessions).","parameters":{"user":{"type":"string","required":true},"app_password":{"type":"string","required":true,"secret":true}},"script":"function run(ctx) local h = 'Basic '..b64.encode(ctx.args.user..':'..ctx.args.app_password) local r = ctx.http.get(ctx.package.get_state('base_url')..'/ocs/v2.php/cloud/user?format=json', {headers = {['Authorization'] = h, ['OCS-APIREQUEST'] = 'true', ['Accept'] = 'application/json'}}) if r.status ~= 200 then return {success = false, output = 'connect failed: HTTP '..tostring(r.status)..(r.error and (' - '..tostring(r.error)) or '')} end local o = r.json and r.json.ocs if not o or not o.data or not o.data.id then return {success = false, output = 'connect failed: unexpected OCS payload (HTTP '..tostring(r.status)..')'} end local ok1 = ctx.package.set_state('user', ctx.args.user) local ok2 = ctx.package.set_state('auth_header', h) if not ok1 or not ok2 then return {success = false, output = 'connect failed: state persist error'} end return {output = 'connected as '..o.data.id..' ('..tostring(o.data['display-name'])..')', data = {user_id = o.data.id}} end"},{"name":"talk send","kind":"action","description":"Send a text message into a Talk room (OCS spreed chat API). token is the room token (watch_tokens, recordings list, or share rows). Write - per-invocation #126 approval applies.","parameters":{"token":{"type":"string","required":true},"message":{"type":"string","required":true}},"script":"function run(ctx) local r = ctx.http.post(ctx.package.get_state('base_url')..'/ocs/v2.php/apps/spreed/api/v1/chat/'..tostring(ctx.args.token)..'?format=json', {headers = {['Authorization'] = ctx.package.get_state('auth_header'), ['OCS-APIREQUEST'] = 'true', ['Accept'] = 'application/json', ['Content-Type'] = 'application/json'}, body = json.encode({message = ctx.args.message})}) if r.status ~= 200 and r.status ~= 201 then return {success = false, output = 'talk send failed: HTTP '..tostring(r.status)..(r.error and (' - '..tostring(r.error)) or '')} end local d = r.json and r.json.ocs and r.json.ocs.data if not d or d.id == nil then return {success = false, output = 'talk send: unexpected OCS payload (HTTP '..tostring(r.status)..')'} end return {output = 'message '..tostring(d.id)..' sent to room '..tostring(ctx.args.token), data = {id = d.id, token = d.token or ctx.args.token, actor = d.actorId, timestamp = d.timestamp}} end"})nk";
    InstallFixturePkg(fx, extra);
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        fx.server.rawBodies["/ocs/v2.php/cloud/user"] =
            "{\"ocs\":{\"meta\":{\"status\":\"ok\",\"statuscode\":200},"
            "\"data\":{\"id\":\"agent\",\"displayname\":\"Agent\"}}}";
        fx.server.rawBodies["/ocs/v2.php/apps/spreed/api/v1/chat/d7vv2vaa"] =
            "{\"ocs\":{\"meta\":{\"status\":\"ok\",\"statuscode\":201},"
            "\"data\":{\"id\":321,\"token\":\"d7vv2vaa\","
            "\"actorId\":\"animus-agent\",\"actorType\":\"users\","
            "\"timestamp\":1795000000,\"message\":\"hello room\"}}}";
        fx.server.rawBodies["/ocs/v2.php/apps/spreed/api/v1/chat/blank"] =
            "not-json";
    }

    // connect first: stores auth_header (office-test pattern).
    Json::Value ca;
    ca["user"] = "agent";
    ca["app_password"] = "apw-123";
    auto r = fx.runtime->ExecuteAction("testpkg", "connect", "agent", ca);
    Assert(r["success"].asBool(), "connect succeeds (auth_header state)");

    // happy path: executes, parses the envelope, wire carries the message.
    Json::Value ts;
    ts["token"] = "d7vv2vaa";
    ts["message"] = "hello room";
    r = fx.runtime->ExecuteAction("testpkg", "talk send", "agent", ts);
    Assert(r["success"].asBool(), "talk send executed");
    Assert(r["output"].asString().find("message 321 sent to room d7vv2vaa") !=
               std::string::npos,
           "talk send output names message id + room");
    Assert(r["data"]["id"].asInt() == 321, "talk send data.id parsed");
    Assert(r["data"]["token"].asString() == "d7vv2vaa",
           "talk send data.token echoed");
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        Assert(fx.server.lastMethod == "POST" &&
                   fx.server.lastPath.find(
                       "/ocs/v2.php/apps/spreed/api/v1/chat/d7vv2vaa") !=
                       std::string::npos,
               "talk send wire: POST to chat path");
        Assert(fx.server.lastBody.find("hello room") != std::string::npos,
               "talk send wire: body carries the message");
    }

    // loud failure: non-OCS body -> named error, never silent success.
    Json::Value bad;
    bad["token"] = "blank";
    bad["message"] = "x";
    r = fx.runtime->ExecuteAction("testpkg", "talk send", "agent", bad);
    Assert(!r["success"].asBool(), "talk send fails on non-OCS body");
    Assert(r["output"].asString().find("unexpected OCS payload") !=
               std::string::npos,
           "talk send failure names the payload problem");
    return 0;
}

// v0.6.0 (A2): talk say — voice note: TTS render -> WebDAV upload -> room
// share with talkMetaData (voice-message + caption, caption defaults to the
// text). talk say + connect VERBATIM from packages/nextcloud/manifest.json;
// tts_url rides the fixture state schema (loopback default). Pins all three hops, the always-attached caption, the
// phrase-cap loud fail, and the RIFF magic guard.
int TestNextcloudTalkSay() {
    Fixture fx;
    const std::string extra = R"nk({"name":"connect","kind":"action","description":"Validate an app password against the instance and store the precomputed Authorization header. Get the app password from the animus-agent account (Settings \u2192 Security \u2192 Devices & sessions).","parameters":{"user":{"type":"string","required":true},"app_password":{"type":"string","required":true,"secret":true}},"script":"function run(ctx) local h = 'Basic '..b64.encode(ctx.args.user..':'..ctx.args.app_password) local r = ctx.http.get(ctx.package.get_state('base_url')..'/ocs/v2.php/cloud/user?format=json', {headers = {['Authorization'] = h, ['OCS-APIREQUEST'] = 'true', ['Accept'] = 'application/json'}}) if r.status ~= 200 then return {success = false, output = 'connect failed: HTTP '..tostring(r.status)..(r.error and (' - '..tostring(r.error)) or '')} end local o = r.json and r.json.ocs if not o or not o.data or not o.data.id then return {success = false, output = 'connect failed: unexpected OCS payload (HTTP '..tostring(r.status)..')'} end local ok1 = ctx.package.set_state('user', ctx.args.user) local ok2 = ctx.package.set_state('auth_header', h) if not ok1 or not ok2 then return {success = false, output = 'connect failed: state persist error'} end return {output = 'connected as '..o.data.id..' ('..tostring(o.data['display-name'])..')', data = {user_id = o.data.id}} end"},{"name":"talk say","kind":"action","description":"Send a voice note into a Talk room: TTS render (Piper at tts_url) -> upload (WebDAV, Talk/ root) -> room share with talkMetaData voice-message + caption (defaults to the text - caption always attached). Phrase cap 300 chars, loud fail over - split into phrases, never slice. Write - per-invocation #126 approval applies.","parameters":{"token":{"type":"string","required":true},"text":{"type":"string","required":true},"caption":{"type":"string"},"voice":{"type":"string"}},"script":"function run(ctx) local token = ctx.args.token or '' if token == '' then return {success = false, output = 'talk say failed: token required'} end local text = ctx.args.text or '' if #text == 0 then return {success = false, output = 'talk say failed: text required'} end if #text > 300 then return {success = false, output = 'talk say failed: phrase cap is 300 chars (keep the floor open - split into phrases, never slice) - got '..#text} end local cap = ctx.args.caption or text local t = {text = text} local v = ctx.args.voice or ctx.package.get_state('tts_voice') or '' if v ~= '' then t.voice = v end local r1 = ctx.http.post(ctx.package.get_state('tts_url')..'/synthesize', {headers = {['Content-Type'] = 'application/json'}, body = json.encode(t), timeout_s = 120}) if r1.status ~= 200 then return {success = false, output = 'talk say failed: tts HTTP '..tostring(r1.status)..(r1.error and (' - '..tostring(r1.error)) or '')} end local wav = r1.body or '' if #wav < 44 or wav:sub(1, 4) ~= 'RIFF' then return {success = false, output = 'talk say failed: no WAV returned ('..#wav..' bytes) - service error?'} end local dg = ctx.crypto.sha256(text) local fn = 'tts-'..(dg and dg:sub(1, 10) or 'phrase')..'.wav' local path = 'Talk/'..fn local r2 = ctx.http.put(ctx.package.get_state('base_url')..'/remote.php/dav/files/'..ctx.package.get_state('user')..'/'..path, {headers = {['Authorization'] = ctx.package.get_state('auth_header')}, body = wav}) if r2.status ~= 200 and r2.status ~= 201 and r2.status ~= 204 then return {success = false, output = 'talk say failed: upload HTTP '..tostring(r2.status)} end local body = json.encode({path = path, shareType = 10, shareWith = token, talkMetaData = json.encode({messageType = 'voice-message', caption = cap})}) local r3 = ctx.http.post(ctx.package.get_state('base_url')..'/ocs/v2.php/apps/files_sharing/api/v1/shares?format=json', {headers = {['Authorization'] = ctx.package.get_state('auth_header'), ['OCS-APIREQUEST'] = 'true', ['Accept'] = 'application/json', ['Content-Type'] = 'application/json'}, body = body}) if r3.status ~= 200 and r3.status ~= 201 then return {success = false, output = 'talk say failed: share HTTP '..tostring(r3.status)..(r3.error and (' - '..tostring(r3.error)) or '')} end local sd = r3.json and r3.json.ocs and r3.json.ocs.data if not sd or sd.id == nil then return {success = false, output = 'talk say: unexpected share payload (HTTP '..tostring(r3.status)..')'} end return {output = 'voice note '..tostring(sd.id)..' sent to room '..token..' ('..#wav..' bytes, '..#text..' chars)', data = {id = sd.id, token = token, file = path, bytes = #wav, caption = cap}} end"})nk";
    InstallFixturePkg(fx, extra);
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        fx.server.rawBodies["/ocs/v2.php/cloud/user"] =
            "{\"ocs\":{\"meta\":{\"status\":\"ok\",\"statuscode\":200},"
            "\"data\":{\"id\":\"agent\",\"displayname\":\"Agent\"}}}";
        fx.server.rawBodies["/synthesize"] =
            std::string("RIFF") + std::string(60, 'x');
        fx.server.rawBodies["/ocs/v2.php/apps/files_sharing/api/v1/shares"] =
            "{\"ocs\":{\"meta\":{\"status\":\"ok\",\"statuscode\":201},"
            "\"data\":{\"id\":\"55\",\"share_type\":10,"
            "\"uid_owner\":\"agent\",\"shareWith\":\"d7vv2vaa\","
            "\"path\":\"/Talk\",\"token\":\"vwxyz\"}}}";
    }

    // connect (auth_header) + point tts_url at the fixture server.
    Json::Value ca;
    ca["user"] = "agent";
    ca["app_password"] = "apw-123";
    auto r = fx.runtime->ExecuteAction("testpkg", "connect", "agent", ca);
    Assert(r["success"].asBool(), "connect succeeds (auth_header state)");

    // happy path: render -> upload -> share, caption defaults to the text.
    Json::Value ts;
    ts["token"] = "d7vv2vaa";
    ts["text"] = "hello room";
    r = fx.runtime->ExecuteAction("testpkg", "talk say", "agent", ts);
    Assert(r["success"].asBool(), "talk say executed");
    Assert(r["output"].asString().find("voice note 55 sent to room d7vv2vaa") !=
               std::string::npos,
           "talk say output names share id + room");
    Assert(r["data"]["id"].asString() == "55", "talk say data.id parsed");
    const std::string file = r["data"]["file"].asString();
    Assert(file.rfind("Talk/tts-", 0) == 0 &&
               file.rfind(".wav") == file.size() - 4,
           "talk say data.file is a deterministic Talk/ wav path");
    Assert(r["data"]["bytes"].asInt() == 64, "talk say data.bytes is the wav size");
    Assert(r["data"]["caption"].asString() == "hello room",
           "talk say caption defaults to the text");
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        Assert(fx.server.lastMethod == "POST" &&
                   fx.server.lastPath.find(
                       "/ocs/v2.php/apps/files_sharing/api/v1/shares") !=
                       std::string::npos,
               "talk say final hop: POST to shares API");
        Assert(fx.server.lastBody.find("voice-message") != std::string::npos &&
                   fx.server.lastBody.find("d7vv2vaa") != std::string::npos &&
                   fx.server.lastBody.find("hello room") != std::string::npos,
               "talk say share body: voice-message + room + caption");
        Assert(fx.server.lastBody.find("Talk/tts-") != std::string::npos,
               "talk say share body carries the uploaded path");
        Assert(fx.server.hits == 4,
               "talk say hit the server exactly 4 times (connect + 3 hops)");
    }

    // phrase cap: loud fail BEFORE any network for the phrase.
    Json::Value over;
    over["token"] = "d7vv2vaa";
    over["text"] = std::string(301, 'x');
    r = fx.runtime->ExecuteAction("testpkg", "talk say", "agent", over);
    Assert(!r["success"].asBool(), "talk say fails over the 300-char cap");
    Assert(r["output"].asString().find("phrase cap") != std::string::npos,
           "talk say cap failure names the phrase cap");
    Assert(fx.server.hits == 4, "cap loud-fail touched no network");

    // RIFF magic guard: non-WAV synthesize response fails by name.
    {
        std::lock_guard<std::mutex> lock(fx.server.mutex);
        fx.server.rawBodies["/synthesize"] = "no-wav-here";
    }
    Json::Value bad;
    bad["token"] = "d7vv2vaa";
    bad["text"] = "short";
    r = fx.runtime->ExecuteAction("testpkg", "talk say", "agent", bad);
    Assert(!r["success"].asBool(), "talk say fails on non-WAV body");
    Assert(r["output"].asString().find("no WAV") != std::string::npos,
           "talk say failure names the missing WAV");
    Assert(fx.server.hits == 5, "RIFF-guard run made exactly one tts call");
    return 0;
}

int main() {
    std::cerr << "ApiRuntime tests:\n";
    TestInterpolation();
    TestScriptHttpUpload();
    TestSttPackage();
    TestTtsPackage();
    TestScriptHttpUploadFilesFrom();
    TestArgsValidation();
    TestPrimaryTransport();
    TestNextcloudRecordingCommands();
    TestNextcloudOfficeCommands();
    TestNextcloudTalkSend();
    TestNextcloudTalkSay();
    TestSandboxStateAndSecrets();
    TestEgressControl();
    TestSandboxGlobals();
    TestOsDate();
    TestFsAndHttpBudget();
    TestHookContext();
    TestFilesResultPaths();
    TestHttpTimeoutOption();
    TestWritesGate();
    TestLongpollPostBody();
    TestLongpollRawBody();
    TestCryptoPrimitives();
    if (g_failures == 0) std::cerr << "All api runtime tests passed.\n";
    else std::cerr << g_failures << " failures.\n";
    return g_failures == 0 ? 0 : 1;
}

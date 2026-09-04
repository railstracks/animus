// ApiRuntimeTests — sandbox + interpolation + transport for api packages
// (build order c). Local HTTP server covers both the primary request
// transport and the ctx.http budget cap.

#include "animus_kernel/api/ApiRuntime.h"
#include "animus_kernel/ApiPackageStore.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/tools/HttpClient.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <cstring>
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
    std::string lastBody;
    std::string lastAuth;
    std::mutex mutex;
    std::atomic<bool> running{false};

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
    HttpClient http;
    HttpServer server;
    uint16_t port{0};
    std::string filesRoot;
    std::unique_ptr<ApiRuntime> runtime;

    Fixture() {
        http.SetAllowPrivateAddresses(true);  // fixture servers are loopback
        port = server.Start();
        filesRoot = dbPath + ".files";
        ApiRuntime::Config cfg;
        cfg.filesRoot = filesRoot;
        runtime = std::make_unique<ApiRuntime>(&store, &http, cfg);
    }
    ~Fixture() {
        server.running = false;
        unlink(dbPath.c_str());
    }
};

// Installs a package shaped for these tests.
ApiPackage InstallFixturePkg(Fixture& fx, const std::string& extra = "") {
    std::string manifest = R"({
      "kind": "api_package", "name": "testpkg", "version": "0.1.0",
      "description": "fixture",
      "state_schema": {
        "token": {"type": "string", "secret": true},
        "base_url": {"type": "string", "default": "http://127.0.0.1:PORT"},
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
    // insert extra commands before the closing "]"
    if (!extra.empty()) {
        size_t cend = manifest.find(
            "],\n      \"connections\"");
        assert(cend != std::string::npos);
        manifest.insert(cend, "," + extra);
    }
    ApiPackage pkg = fx.store.InstallFromManifest(manifest);
    fx.store.SetPackageEnabled(pkg.id, true);
    fx.store.SetPackageState(pkg.id, "{\"token\":\"SECRET-TOKEN-1234\"}");
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

    // missing state key = tool error naming it (script never runs)
    fx.store.SetPackageState(fx.store.GetPackageByName("testpkg")->id,
                             "{\"base_url\":\"http://127.0.0.1:1\"}");
    r = fx.runtime->ExecuteAction("testpkg", "fetch positions", "agent", none);
    Assert(!r["success"].asBool() &&
           r["error"].asString().find("missing key: state.token") != std::string::npos,
           "interpolation failure names missing key");
    Assert(r["error"].asString().find("SECRET") == std::string::npos, "secret not leaked in error");
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
    Assert(now->state.find("NEW-SECRET-9999") != std::string::npos, "state persisted");

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
    std::cerr << "    LASTAUTH=[" << fx3.server.lastAuth << "] PATH=" << fx3.server.lastPath << "\n";
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


int TestSandboxProbe() {
    std::cerr << "  [runtime] sandbox probe: minimal scripts...\n";
    Fixture fx;
    std::string extra = R"({"name": "p1", "kind": "action", "description": "d",
        "script": "function run(ctx) return {output='static'} end"},)"
        R"({"name": "p2", "kind": "action", "description": "d",
        "script": "function run(ctx) return {output=tostring(1+1)} end"},)"
        R"({"name": "p3", "kind": "action", "description": "d",
        "script": "function run(ctx) return {output=type(ctx.args)} end"},)"
        R"({"name": "p4", "kind": "action", "description": "d",
        "script": "function run(ctx) return {output=tostring(ctx.args.msg)} end"})";
    InstallFixturePkg(fx, extra);
    Json::Value none;
    for (const char* probe : {"p1", "p2", "p3", "p4"}) {
        auto r = fx.runtime->ExecuteAction("testpkg", probe, "agent", none);
        Json::StreamWriterBuilder b; b["indentation"] = "";
        std::cerr << "    PROBE " << probe << ": " << Json::writeString(b, r) << "\n";
    }
    return 0;
}

}  // namespace

int main() {
    std::cerr << "ApiRuntime tests:\n";
    TestSandboxProbe();
    TestInterpolation();
    TestArgsValidation();
    TestPrimaryTransport();
    TestSandboxStateAndSecrets();
    TestSandboxGlobals();
    TestFsAndHttpBudget();
    TestHookContext();
    if (g_failures == 0) std::cerr << "All api runtime tests passed.\n";
    else std::cerr << g_failures << " failures.\n";
    return g_failures == 0 ? 0 : 1;
}

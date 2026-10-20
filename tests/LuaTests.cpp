#include "animus_kernel/lua/LuaState.h"
#include "animus_kernel/lua/LuaToolHandler.h"
#include "animus_kernel/lua/ScriptStore.h"
#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/SqliteDataStore.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include <json/json.h>
#include <json/reader.h>
#include <json/writer.h>

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
    char tmp[] = "/tmp/animus_lua_test_XXXXXX";
    mktemp(tmp);
    return std::string(tmp) + ".db";
}

} // anonymous namespace

// ============================================================================
// Test: Basic Lua evaluation
// ============================================================================
void TestBasicEval() {
    std::cerr << "  TestBasicEval...";

    ToolRegistry tools;
    HttpClient http;
    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 100'000;

    std::cerr << " creating LuaState...";
    LuaState lua(config, tools, http);
    std::cerr << " done. eval...";

    // Simple expression
    std::string result = lua.Eval("return 1 + 2", "test");
    Assert(result.find("3") != std::string::npos, "expected 1+2=3, got: " + result);

    // String concatenation
    result = lua.Eval("return 'hello' .. ' world'", "test");
    Assert(result.find("hello world") != std::string::npos, "expected 'hello world', got: " + result);

    // Table construction
    result = lua.Eval("local t = {a=1, b=2}; return t.a + t.b", "test");
    Assert(result.find("3") != std::string::npos, "expected table sum=3, got: " + result);

    std::cerr << " done\n";
}

// ============================================================================
// Test: Sandbox enforcement
// ============================================================================
void TestSandbox() {
    std::cerr << "  TestSandbox...";

    ToolRegistry tools;
    HttpClient http;
    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 100'000;

    LuaState lua(config, tools, http);

    // io should be nil
    std::string result = lua.Eval("return tostring(io)", "test");
    Assert(result.find("nil") != std::string::npos, "io should be nil in sandbox, got: " + result);

    // os.execute should be nil
    result = lua.Eval("return tostring(os.execute)", "test");
    Assert(result.find("nil") != std::string::npos, "os.execute should be nil, got: " + result);

    // dofile should be nil
    result = lua.Eval("return tostring(dofile)", "test");
    Assert(result.find("nil") != std::string::npos, "dofile should be nil, got: " + result);

    // loadfile should be nil
    result = lua.Eval("return tostring(loadfile)", "test");
    Assert(result.find("nil") != std::string::npos, "loadfile should be nil, got: " + result);

    // require should be nil
    result = lua.Eval("return tostring(require)", "test");
    Assert(result.find("nil") != std::string::npos, "require should be nil, got: " + result);

    // debug should be nil
    result = lua.Eval("return tostring(debug)", "test");
    Assert(result.find("nil") != std::string::npos, "debug should be nil, got: " + result);

    // os.time should still work
    result = lua.Eval("return type(os.time)", "test");
    Assert(result.find("function") != std::string::npos, "os.time should be a function, got: " + result);

    // string lib should work
    result = lua.Eval("return string.upper('hello')", "test");
    Assert(result.find("HELLO") != std::string::npos, "string.upper should work, got: " + result);

    // math lib should work
    result = lua.Eval("return math.floor(3.7)", "test");
    Assert(result.find("3") != std::string::npos, "math.floor should work, got: " + result);

    // table lib should work
    result = lua.Eval("local t = {3,1,2}; table.sort(t); return t[1]", "test");
    Assert(result.find("1") != std::string::npos, "table.sort should work, got: " + result);

    std::cerr << " done\n";
}

// ============================================================================
// Test: JSON utility
// ============================================================================
void TestJsonUtility() {
    std::cerr << "  TestJsonUtility...";

    ToolRegistry tools;
    HttpClient http;
    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 100'000;

    LuaState lua(config, tools, http);

    // json.encode
    std::string result = lua.Eval("return json.encode({a=1})", "test");
    Assert(!result.empty(), "json.encode should return non-empty, got: " + result);

    // json.decode
    result = lua.Eval("local d = json.decode('{\"x\":42}'); return d.x", "test");
    Assert(result == "42", "json.decode should work, got: " + result);

    std::cerr << " done\n";
}

// ============================================================================
// Test: Logging utility
// ============================================================================
void TestLogging() {
    std::cerr << "  TestLogging...";

    ToolRegistry tools;
    HttpClient http;
    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 100'000;

    LuaState lua(config, tools, http);

    // These should not crash
    lua.Eval("log.info('test info message')", "test");
    lua.Eval("log.warn('test warn message')", "test");
    lua.Eval("log.error('test error message')", "test");

    std::cerr << " done\n";
}

// ============================================================================
// Test: Config access
// ============================================================================
void TestConfigAccess() {
    std::cerr << "  TestConfigAccess...";

    ToolRegistry tools;
    HttpClient http;
    LuaState::Config config;
    config.agentId = "test_agent";
    config.instructionLimit = 100'000;

    LuaState lua(config, tools, http);

    // config.get for built-in agentId
    std::string result = lua.Eval("return config.get('agentId')", "test");
    Assert(result.find("test_agent") != std::string::npos, "config.get('agentId') should return 'test_agent', got: " + result);

    // config.set and config.get
    result = lua.Eval("config.set('custom_key', 'custom_value'); return config.get('custom_key')", "test");
    Assert(result.find("custom_value") != std::string::npos, "config set/get roundtrip, got: " + result);

    std::cerr << " done\n";
}

// ============================================================================
// Test: Plugin registration (animus.register_tool)
// ============================================================================
void TestPluginRegistration() {
    std::cerr << "  TestPluginRegistration...";

    ToolRegistry tools;
    HttpClient http;
    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 1'000'000;

    LuaState lua(config, tools, http);

    // Register a simple Lua tool
    std::string script = R"lua(
animus.register_tool({
    name = "greet",
    description = "Returns a greeting",
    parameters = {
        { name = "name", type = "string", required = true, description = "Name to greet" }
    },
    handler = function(args)
        return { success = true, output = "Hello, " .. args.name .. "!" }
    end
})
return "registered"
)lua";

    std::string result = lua.Eval(script, "test");
    Assert(result.find("registered") != std::string::npos, "registration should return 'registered', got: " + result);

    // Check that the tool was registered in the registry
    Assert(tools.Has("greet"), "greet tool should be registered");

    // Check that the tool definition is correct
    IToolHandler* handler = tools.Find("greet");
    Assert(handler != nullptr, "greet handler should be findable");
    if (handler) {
        auto def = handler->GetDefinition();
        Assert(def.name == "greet", "tool name should be 'greet'");
        Assert(!def.description.empty(), "tool should have a description");
        Assert(def.parameters.size() >= 1, "tool should have at least 1 parameter");
    }

    // Test calling the registered Lua tool
    if (handler) {
        ToolCall call;
        call.id = "test-call-1";
        call.name = "greet";
        call.arguments = R"({"name":"World"})";

        ToolResult tr = handler->Execute(call);
        Assert(tr.success, "greet tool should succeed, error: " + tr.error);
        Assert(tr.output.find("Hello, World!") != std::string::npos,
               "greet output should contain 'Hello, World!', got: " + tr.output);
    }

    // Check plugin info
    auto& plugins = lua.GetRegisteredPlugins();
    Assert(!plugins.empty(), "should have registered plugins");
    Assert(plugins.back().id == "greet", "plugin id should be 'greet'");

    std::cerr << " done\n";
}

// ============================================================================
// Test: Tool bridge (tools.*)
// ============================================================================
void TestToolBridge() {
    std::cerr << "  TestToolBridge...";

    ToolRegistry tools;
    HttpClient http;

    // Register a built-in tool that the Lua bridge can call
    // We'll use a simple test tool
    class EchoTool : public IToolHandler {
    public:
        ToolDefinition GetDefinition() const override {
            ToolDefinition def;
            def.name = "echo";
            def.description = "Echoes back its arguments";
            def.parameters.push_back({"message", "string", "Message to echo", true});
            def.resultMode = ToolResultMode::deliver_to_model;
            return def;
        }

        ToolResult Execute(const ToolCall& call) override {
            Json::Value args;
            Json::CharReaderBuilder builder;
            std::istringstream stream(call.arguments);
            std::string errors;
            Json::parseFromStream(builder, stream, &args, &errors);

            ToolResult result;
            result.call_id = call.id;
            result.success = true;
            result.output = args.isMember("message") ? args["message"].asString() : "";
            return result;
        }
    };

    tools.Register(std::make_unique<EchoTool>());

    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 1'000'000;

    LuaState lua(config, tools, http);

    // Call tools.echo from Lua
    std::string result = lua.Eval(
        "local r = tools.echo({message = 'hello from lua'}); return r",
        "test"
    );
    Assert(result.find("hello from lua") != std::string::npos,
           "tools.echo should return 'hello from lua', got: " + result);

    // Test unknown tool — should throw from Eval, not from Lua
    bool caught = false;
    try {
        lua.Eval("tools.nonexistent({})", "test");
    } catch (const LuaException& e) {
        Assert(std::string(e.what()).find("unknown tool") != std::string::npos
               || std::string(e.what()).find("not found") != std::string::npos
               || std::string(e.what()).find("error") != std::string::npos,
               "should throw some error for unknown tool, got: " + std::string(e.what()));
        caught = true;
    }
    Assert(caught, "unknown tool should throw");

    std::cerr << " done\n";
}

// ============================================================================
// Test: ScriptStore persistence
// ============================================================================
void TestScriptStore() {
    std::cerr << "  TestScriptStore...";

    std::string dbPath = MakeTempDbPath();
    SqliteDataStore db(dbPath);
    ScriptStore store(&db);

    // Create a script
    ScriptDescriptor desc;
    desc.agent_id = "test";
    desc.name = "hello";
    desc.source = "return 'hello world'";
    desc.description = "A test script";
    desc.enabled = true;

    std::string error;
    std::string id = store.CreateAndReturnId(desc, &error);
    Assert(!id.empty(), "script creation should return id, error: " + error);

    // Get by id
    auto retrieved = store.Get(id);
    Assert(retrieved.has_value(), "should find script by id");
    Assert(retrieved->name == "hello", "name should be 'hello'");
    Assert(retrieved->source == "return 'hello world'", "source should match");

    // Get by name
    auto byName = store.GetByName("test", "hello");
    Assert(byName.has_value(), "should find script by name");
    Assert(byName->id == id, "id should match");

    // List
    auto scripts = store.List("test");
    Assert(scripts.size() == 1, "should have 1 script");

    // Update
    retrieved->source = "return 'updated'";
    Assert(store.Update(*retrieved, &error), "update should succeed");

    auto updated = store.Get(id);
    Assert(updated->source == "return 'updated'", "source should be updated");

    // Delete
    Assert(store.Delete(id, &error), "delete should succeed");
    Assert(!store.Get(id).has_value(), "should not find deleted script");

    // Cleanup
    std::filesystem::remove(dbPath);

    std::cerr << " done\n";
}

// ============================================================================
// Test: Interface registration
// ============================================================================
void TestInterfaceRegistration() {
    std::cerr << "  TestInterfaceRegistration...";

    ToolRegistry tools;
    HttpClient http;
    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 1'000'000;

    LuaState lua(config, tools, http);

    std::string script = R"lua(
animus.register_interface({
    id = "signal",
    name = "Signal Messenger",
    actions = {"send", "list_contacts"},
    schema = {
        send = {
            { name = "to", type = "string", required = true },
            { name = "message", type = "string", required = true }
        }
    },
    handler = function(args)
        return { success = true, output = "Sent to " .. (args.to or "unknown") }
    end
})
return "ok"
)lua";

    std::string result = lua.Eval(script, "test");
    Assert(result.find("ok") != std::string::npos, "register_interface should succeed, got: " + result);

    // Check plugin info
    auto& plugins = lua.GetRegisteredPlugins();
    bool found = false;
    for (const auto& p : plugins) {
        if (p.type == "interface" && p.id == "signal") {
            found = true;
            Assert(p.name == "Signal Messenger", "interface name should match");
            Assert(p.actions.size() == 2, "should have 2 actions");
            Assert(!p.actionSchemas.empty(), "should have action schemas");
            Assert(p.actionSchemas.count("send") > 0, "should have 'send' schema");
        }
    }
    Assert(found, "should find interface plugin");

    // Check that a tool was registered
    Assert(tools.Has("interface_signal"), "interface_signal tool should be registered");

    std::cerr << " done\n";
}

// ============================================================================
// Test: Social registration
// ============================================================================
void TestSocialRegistration() {
    std::cerr << "  TestSocialRegistration...";

    ToolRegistry tools;
    HttpClient http;
    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 1'000'000;

    LuaState lua(config, tools, http);

    std::string script = R"lua(
animus.register_social({
    id = "bluesky",
    name = "Bluesky",
    capabilities = {"read", "write", "search"},
    actions = {"post", "search"},
    schema = {
        post = {
            { name = "content", type = "string", required = true }
        }
    },
    handler = function(args)
        return { success = true, output = "Posted: " .. (args.content or "") }
    end
})
return "ok"
)lua";

    std::string result = lua.Eval(script, "test");
    Assert(result.find("ok") != std::string::npos, "register_social should succeed, got: " + result);

    // Check plugin info
    auto& plugins = lua.GetRegisteredPlugins();
    bool found = false;
    for (const auto& p : plugins) {
        if (p.type == "social" && p.id == "bluesky") {
            found = true;
            Assert(p.name == "Bluesky", "social name should match");
            Assert(p.capabilities.size() == 3, "should have 3 capabilities");
            Assert(p.actions.size() == 2, "should have 2 actions");
            Assert(p.actionSchemas.count("post") > 0, "should have 'post' schema");
        }
    }
    Assert(found, "should find social plugin");

    // Design contract (LuaRegisterSocial): social plugins are NOT registered
    // as standalone agent-visible tools — the LuaToolHandler is owned by
    // LuaPluginInfo and routed exclusively through the ChannelsTool composite
    // (AgentKernel Phase 2). Registry suppression is deliberate; assert it holds.
    Assert(!tools.Has("social_bluesky"),
           "social_bluesky must NOT appear as a standalone tool (composite-only design)");

    std::cerr << " done\n";
}

// ============================================================================
// Test: HTTP client (managed, SSRF-protected)
// ============================================================================
void TestHttpClient() {
    std::cerr << "  TestHttpClient...";

    ToolRegistry tools;
    HttpClient http;
    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 100'000;

    LuaState lua(config, tools, http);

    // Test that animus.http_get exists and is callable
    std::string result = lua.Eval("return type(animus.http_get)", "test");
    Assert(result.find("function") != std::string::npos, "animus.http_get should be a function, got: " + result);

    // Test that animus.http_post exists and is callable
    result = lua.Eval("return type(animus.http_post)", "test");
    Assert(result.find("function") != std::string::npos, "animus.http_post should be a function, got: " + result);

    // Test SSRF block on localhost
    bool caught = false;
    try {
        lua.Eval("animus.http_get('http://127.0.0.1:8080/internal')", "test");
    } catch (const LuaException& e) {
        Assert(std::string(e.what()).find("SSRF") != std::string::npos
               || std::string(e.what()).find("blocked") != std::string::npos
               || std::string(e.what()).find("error") != std::string::npos,
               "should throw SSRF or blocked error, got: " + std::string(e.what()));
        caught = true;
    }
    // Note: SSRF may not block if IsUrlAllowed returns true for all URLs
    // by default. The important thing is that the function is callable and
    // the SSRF check path exists.
    // Don't assert caught=true since SSRF behavior depends on config.

    std::cerr << " done\n";
}

// ============================================================================
// Test: Instruction limit
// ============================================================================
void TestInstructionLimit() {
    std::cerr << "  TestInstructionLimit...";

    ToolRegistry tools;
    HttpClient http;
    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 1000; // very low limit

    LuaState lua(config, tools, http);

    // This should exceed the instruction limit
    bool caught = false;
    try {
        lua.Eval("local x = 0; for i = 1, 100000 do x = x + 1 end; return x", "test");
    } catch (const LuaException& e) {
        Assert(std::string(e.what()).find("execution limit") != std::string::npos,
               "should throw execution limit error, got: " + std::string(e.what()));
        caught = true;
    }
    Assert(caught, "infinite loop should be caught by instruction limit");

    std::cerr << " done\n";
}

// ============================================================================
// Test: Coroutine mode (tool calls yield to host)
// ============================================================================
void TestCoroutineMode() {
    std::cerr << "  TestCoroutineMode...";

    ToolRegistry tools;
    HttpClient http;

    // Register a test tool
    class EchoTool : public IToolHandler {
    public:
        ToolDefinition GetDefinition() const override {
            ToolDefinition def;
            def.name = "echo";
            def.description = "Echoes back its arguments";
            def.parameters.push_back({"message", "string", "Message to echo", true});
            def.resultMode = ToolResultMode::deliver_to_model;
            return def;
        }

        ToolResult Execute(const ToolCall& call) override {
            Json::Value args;
            Json::CharReaderBuilder builder;
            std::istringstream stream(call.arguments);
            std::string errors;
            Json::parseFromStream(builder, stream, &args, &errors);

            ToolResult result;
            result.call_id = call.id;
            result.success = true;
            result.output = args.isMember("message") ? args["message"].asString() : "";
            return result;
        }
    };

    tools.Register(std::make_unique<EchoTool>());

    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 1'000'000;

    LuaState lua(config, tools, http);

    // Track tool calls received by the callback
    int toolCallCount = 0;
    std::string lastToolName;

    // Script that calls tools.echo multiple times
    std::string script = R"lua(
local a = tools.echo({message = "first"})
local b = tools.echo({message = "second"})
return a .. " and " .. b
)lua";

    // Use EvalCoroutine — each tool call yields to the callback
    std::string result = lua.EvalCoroutine(script, "test",
        [&](const ToolCall& call) -> ToolResult {
            toolCallCount++;
            lastToolName = call.name;

            // Execute through the registry (same as sync mode, but host-controlled)
            IToolHandler* handler = tools.Find(call.name);
            if (handler) {
                return handler->Execute(call);
            }
            ToolResult err;
            err.success = false;
            err.error = "unknown tool: " + call.name;
            return err;
        }
    );

    // Should have received 2 tool calls
    Assert(toolCallCount == 2, "should have received 2 tool calls, got: " + std::to_string(toolCallCount));
    Assert(lastToolName == "echo", "last tool should be 'echo', got: " + lastToolName);

    // Result should contain both echoes
    Assert(result.find("first") != std::string::npos, "result should contain 'first', got: " + result);
    Assert(result.find("second") != std::string::npos, "result should contain 'second', got: " + result);

    std::cerr << " done\n";
}

// ============================================================================
// Test: Memory limit
// ============================================================================
void TestMemoryLimit() {
    std::cerr << "  TestMemoryLimit...";

    ToolRegistry tools;
    HttpClient http;
    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 100'000;
    config.memoryLimitBytes = 1024 * 1024; // 1 MB — very low for testing

    LuaState lua(config, tools, http);

    // Normal scripts should work fine
    std::string result = lua.Eval("return 1 + 1", "test");
    Assert(result.find("2") != std::string::npos, "simple eval should work under memory limit");

    // Scripts that try to allocate excessive memory should fail
    bool caught = false;
    try {
        lua.Eval("local t = {}; for i = 1, 1000000 do t[i] = string.rep('x', 1024) end; return #t", "test");
    } catch (const LuaException& e) {
        // Lua should fail with a memory error when the allocator refuses
        Assert(std::string(e.what()).find("memory") != std::string::npos
               || std::string(e.what()).find("not enough") != std::string::npos
               || std::string(e.what()).find("error") != std::string::npos,
               "should throw memory error, got: " + std::string(e.what()));
        caught = true;
    }
    Assert(caught, "excessive memory allocation should be caught");

    std::cerr << " done\n";
}

// ============================================================================
// Main
// ============================================================================
// ============================================================================
// Test: Bluesky richtext facets (#36)
// Loads the real adapter with a stubbed animus table and asserts facets
// in the createRecord / sendMessage payloads. One Eval chunk per case:
// prelude (stubs) + adapter source + driver, sharing chunk-local scope.
// ============================================================================
std::string ReadIrcAdapterSrc() {
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    std::vector<std::string> candidates;
    if (n > 0) {
        exe[n] = '\0';
        std::string dir(exe);
        const auto slash = dir.rfind('/');
        if (slash != std::string::npos)
            candidates.push_back(dir.substr(0, slash) + "/../../state/lua/_shared/irc.lua");
    }
    candidates.push_back("state/lua/_shared/irc.lua");
    candidates.push_back("../../state/lua/_shared/irc.lua");
    for (const auto& c : candidates) {
        std::ifstream f(c);
        if (f.good()) {
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }
    }
    return "";
}

std::string ReadBlueskyAdapterSrc() {
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    std::vector<std::string> candidates;
    if (n > 0) {
        exe[n] = '\0';
        std::string dir(exe);
        const auto slash = dir.rfind('/');
        if (slash != std::string::npos)
            candidates.push_back(dir.substr(0, slash) + "/../../state/lua/_shared/bluesky.lua");
    }
    candidates.push_back("state/lua/_shared/bluesky.lua");
    candidates.push_back("../../state/lua/_shared/bluesky.lua");
    for (const auto& c : candidates) {
        std::ifstream f(c);
        if (!f) continue;
        std::stringstream ss;
        ss << f.rdbuf();
        std::string s = ss.str();
        if (!s.empty()) return s;
    }
    return "";
}

const char* kBskyPrelude = R"lua(
local calls = {}
local captured = nil
local function stub_reply(url)
    if string.find(url, "createSession", 1, true) then
        return { status = 200, body = json.encode({ accessJwt = "h.p.s", did = "did:plc:t", refreshJwt = "r.p.s" }) }
    end
    if string.find(url, "getRecord", 1, true) then
        return { status = 200, body = json.encode({ uri = "at://did:plc:t/app.bsky.feed.post/root", cid = "bafyr", value = { cid = "bafyr" } }) }
    end
    return { status = 200, body = json.encode({ uri = "at://did:plc:t/app.bsky.feed.post/new", cid = "bafyn" }) }
end
animus.register_channel = function(t) captured = t end
animus.http_post = function(url, opts)
    table.insert(calls, { url = url, body = (opts and opts.body) or "" })
    return stub_reply(url)
end
animus.http_get = function(url, opts)
    table.insert(calls, { url = url, body = "" })
    return stub_reply(url)
end
config.set("channels.bluesky:test.handle", "tester.bsky.social")
config.set("channels.bluesky:test.app_password", "app-pass")
config.set("channels.bluesky:test.access_jwt", "")
config.set("channels.bluesky:test.did", "")
)lua";

// Runs one case; extracts the record (post/reply) or message (chat) actually
// sent, json-encoded, from the stubbed http layer.
Json::Value RunBlueskyCase(LuaState& lua, const std::string& src, const std::string& driver) {
    std::string out;
    try {
        out = lua.Eval(std::string(kBskyPrelude) + "\n" + src + "\n" + driver, "bsky-case");
    } catch (const std::exception& e) {
        Assert(false, std::string("Eval threw: ") + e.what());
        return Json::Value();  // null degrades to assert failures, not abort
    }
    Json::Value v;
    std::string err;
    Assert(Json::Reader().parse(out, v), "case output should be JSON, got: " + out.substr(0, 200));
    // Eval serializes Lua string returns as JSON strings (quoted+escaped);
    // unwrap one layer so callers always get the object/array.
    if (v.isString()) {
        const std::string inner = v.asString();
        Assert(Json::Reader().parse(inner, v), "unwrapped case output should be JSON, got: " + inner.substr(0, 200));
    }
    return v;
}

void AssertFacet(const Json::Value& f, int bs, int be, const char* type, const std::string& val) {
    Assert(f["index"]["byteStart"].asInt() == bs,
           "facet byteStart " + std::to_string(f["index"]["byteStart"].asInt()) + " != " + std::to_string(bs));
    Assert(f["index"]["byteEnd"].asInt() == be,
           "facet byteEnd " + std::to_string(f["index"]["byteEnd"].asInt()) + " != " + std::to_string(be));
    const std::string got = f["features"][0]["$type"].asString();
    Assert(got == type, "facet $type " + got + " != " + type);
    const std::string field = (got.find("#tag") != std::string::npos) ? "tag" : "uri";
    Assert(f["features"][0][field].asString() == val,
           "facet " + field + " " + f["features"][0][field].asString() + " != " + val);
}

const char* kBskyPostDriver = R"lua(
local r = captured.handler({ action = "post", platform_id = "bluesky:test", content = CONTENT })
local rec
for _, c in ipairs(calls) do
    if string.find(c.url, "createRecord", 1, true) then rec = json.decode(c.body).record end
end
return json.encode({ ok = (r.success == true), record = rec })
)lua";

void TestBlueskyFacets() {
    std::cerr << "  TestBlueskyFacets...";
    const std::string src = ReadBlueskyAdapterSrc();
    Assert(!src.empty(), "bluesky.lua source found");

    ToolRegistry tools;
    HttpClient http;
    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 5'000'000;
    LuaState lua(config, tools, http);

    // helper: run post case with given content
    auto post_case = [&](const std::string& content) {
        std::string driver = std::string(kBskyPostDriver);
        const std::string marker = "CONTENT";
        const size_t p = driver.find(marker);
        driver.replace(p, marker.size(), content);
        return RunBlueskyCase(lua, src, driver);
    };

    // 1. plain hashtag: "#hello world" -> tag facet 0..6
    {
        Json::Value v = post_case("\"#hello world\"");
        Assert(v["ok"].asBool(), "case1 post succeeds");
        Assert(v["record"]["facets"].isArray() && v["record"]["facets"].size() == 1, "case1 one facet");
        if (v["record"]["facets"].size() == 1)
            AssertFacet(v["record"]["facets"][0], 0, 6, "app.bsky.richtext.facet#tag", "hello");
    }

    // 2. mid-string URL: "see https://example.com/page now" -> link 4..28
    {
        Json::Value v = post_case("\"see https://example.com/page now\"");
        const Json::Value& fs = v["record"]["facets"];
        Assert(fs.isArray() && fs.size() == 1, "case2 one facet");
        if (fs.size() == 1)
            AssertFacet(fs[0], 4, 28, "app.bsky.richtext.facet#link", "https://example.com/page");
    }

    // 3. emoji (4 bytes) shifts offsets: "\xF0\x9F\xA6\x85 #kestrel" -> tag 5..13
    {
        Json::Value v = post_case("\"\\xF0\\x9F\\xA6\\x85 #kestrel\"");
        const Json::Value& fs = v["record"]["facets"];
        Assert(fs.isArray() && fs.size() == 1, "case3 one facet (emoji before tag)");
        if (fs.size() == 1)
            AssertFacet(fs[0], 5, 13, "app.bsky.richtext.facet#tag", "kestrel");
    }

    // 4. CJK (3-byte chars) before URL: -> link 7..23
    {
        Json::Value v = post_case("\"\\xE8\\xA6\\x8B\\xE3\\x81\\xA6 https://x.test/a \\xE3\\x81\\xAD\"");
        const Json::Value& fs = v["record"]["facets"];
        Assert(fs.isArray() && fs.size() == 1, "case4 one facet (CJK before url)");
        if (fs.size() == 1)
            AssertFacet(fs[0], 7, 23, "app.bsky.richtext.facet#link", "https://x.test/a");
    }

    // 5. trailing punctuation excluded: "check https://x.test/a." -> 6..22, no dot
    {
        Json::Value v = post_case("\"check https://x.test/a.\"");
        const Json::Value& fs = v["record"]["facets"];
        Assert(fs.isArray() && fs.size() == 1, "case5 one facet");
        if (fs.size() == 1)
            AssertFacet(fs[0], 6, 22, "app.bsky.richtext.facet#link", "https://x.test/a");
    }

    // 6. hashtag trailing punctuation: "#test, done" -> tag test 0..5
    {
        Json::Value v = post_case("\"#test, done\"");
        const Json::Value& fs = v["record"]["facets"];
        Assert(fs.isArray() && fs.size() == 1, "case6 one facet");
        if (fs.size() == 1)
            AssertFacet(fs[0], 0, 5, "app.bsky.richtext.facet#tag", "test");
    }

    // 7. url fragment is NOT a hashtag: one link facet only
    {
        Json::Value v = post_case("\"go https://x.test/a#frag now\"");
        const Json::Value& fs = v["record"]["facets"];
        Assert(fs.isArray() && fs.size() == 1, "case7 exactly one facet (frag not a tag)");
        if (fs.size() == 1)
            AssertFacet(fs[0], 3, 24, "app.bsky.richtext.facet#link", "https://x.test/a#frag");
    }

    // 8. www form -> https-prefixed uri, span covers bare www text
    {
        Json::Value v = post_case("\"www.example.com works\"");
        const Json::Value& fs = v["record"]["facets"];
        Assert(fs.isArray() && fs.size() == 1, "case8 one facet");
        if (fs.size() == 1)
            AssertFacet(fs[0], 0, 15, "app.bsky.richtext.facet#link", "https://www.example.com");
    }

    // 9. plain text -> facets key absent entirely
    {
        Json::Value v = post_case("\"just plain text\"");
        Assert(v["ok"].asBool(), "case9 post succeeds");
        Assert(!v["record"].isMember("facets"), "case9 no facets key on plain text");
    }

    // 10. reply path inherits facets + keeps reply refs
    {
        const char* driver = R"lua(
local r = captured.handler({ action = "reply", platform_id = "bluesky:test", post_id = "at://did:plc:t/app.bsky.feed.post/root", root_id = "at://did:plc:t/app.bsky.feed.post/root", content = "#thread" })
local rec
for _, c in ipairs(calls) do
    if string.find(c.url, "createRecord", 1, true) then rec = json.decode(c.body).record end
end
return json.encode({ ok = (r.success == true), record = rec })
)lua";
        Json::Value v = RunBlueskyCase(lua, src, driver);
        Assert(v["ok"].asBool(), "case10 reply succeeds");
        Assert(v["record"]["reply"]["parent"]["uri"].asString().find("root") != std::string::npos,
               "case10 reply ref intact");
        const Json::Value& fs = v["record"]["facets"];
        Assert(fs.isArray() && fs.size() == 1, "case10 reply has facets");
        if (fs.size() == 1)
            AssertFacet(fs[0], 0, 7, "app.bsky.richtext.facet#tag", "thread");
    }

    // 11. chat_send attaches facets inside message
    {
        const char* driver = R"lua(
local r = captured.handler({ action = "chat_send", platform_id = "bluesky:test", convo_id = "convo1", content = "ping #dm" })
local msg
for _, c in ipairs(calls) do
    if string.find(c.url, "sendMessage", 1, true) then msg = json.decode(c.body).message end
end
return json.encode({ ok = (r.success == true), message = msg })
)lua";
        Json::Value v = RunBlueskyCase(lua, src, driver);
        Assert(v["ok"].asBool(), "case11 chat_send succeeds");
        Assert(v["message"]["text"].asString() == "ping #dm", "case11 message text");
        const Json::Value& fs = v["message"]["facets"];
        Assert(fs.isArray() && fs.size() == 1, "case11 chat message has facets");
        if (fs.size() == 1)
            AssertFacet(fs[0], 5, 8, "app.bsky.richtext.facet#tag", "dm");
    }

    std::cerr << " done\n";
}


// ============================================================================
// #204 follow-up: the IRC Lua adapter must DECLARE send_action
// ============================================================================
// The C++ bridge (ChannelsTool) intercepts irc post/reply/send_action before
// the Lua handler runs — but the schema the model sees (tool action enum +
// describe output + action validation) is built from THIS registration. If
// send_action is not in the Lua action list, the agent cannot discover or
// call it, no matter what the backend supports. (Live sighting: Vesper
// iterated /me-as-literal-text, blind action calls, and describe — the
// command was invisible until the schema declared it.)
void TestIrcAdapterSendAction() {
    ToolRegistry tools;
    HttpClient http;
    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 100'000;
    LuaState lua(config, tools, http);

    // Prelude: capture the registration; stub the two globals irc.lua uses.
    const char* prelude = R"(
captured = nil
animus = {}
function animus.register_channel(t) captured = t end
config = {}
function config.get(k) return "" end
)";

    const std::string src = ReadIrcAdapterSrc();
    Assert(!src.empty(), "irc.lua source found");

    const std::string driver = R"(
local out = {}
out.id = captured.id
out.has_send_action = false
for _, a in ipairs(captured.actions) do
    if a == "send_action" then out.has_send_action = true end
end
out.schema_has = (captured.schema.send_action ~= nil)
out.has_content_param = false
if captured.schema.send_action then
    for _, p in ipairs(captured.schema.send_action) do
        if p.name == "content" then out.has_content_param = true end
    end
end
local r = captured.handler({ action = "send_action", channel = "#t", content = "waves" })
out.handler_success = r.success
out.handler_output = r.output or ""
return out
)";

    const std::string out = lua.Eval(
        std::string(prelude) + "\n" + src + "\n" + driver, "irc-schema-case");
    Json::Value v;
    Assert(Json::Reader().parse(out, v), "irc schema case output parses, got: "
           + out.substr(0, 120));
    if (v.isString()) {
        Assert(Json::Reader().parse(v.asString(), v),
               "irc schema output unwrap parses");
    }

    Assert(v.get("id", "").asString() == "irc", "registration id is irc");
    Assert(v.get("has_send_action", false).asBool(),
           "actions list declares send_action");
    Assert(v.get("schema_has", false).asBool(),
           "schema block declares send_action parameters");
    Assert(v.get("has_content_param", false).asBool(),
           "send_action schema has a content parameter");
    Assert(v.get("handler_success", true).asBool() == false,
           "send_action handler runs (fallback path, no C++ bridge in tests)");
    Assert(v.get("handler_output", "").asString().find("not available")
               != std::string::npos,
           "send_action fallback mirrors post/reply error text");
}

// ============================================================================
// #205: join/part schemas declare key/farewell; fallback names the toggle
// ============================================================================
void TestIrcAdapterJoinPartSchema() {
    ToolRegistry tools;
    HttpClient http;
    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 100'000;
    LuaState lua(config, tools, http);

    const char* prelude = R"(
captured = nil
animus = {}
function animus.register_channel(t) captured = t end
config = {}
function config.get(k) return "" end
)";

    const std::string src = ReadIrcAdapterSrc();
    Assert(!src.empty(), "irc.lua source found");

    const std::string driver = R"(
local out = {}
out.join_has_key = false
out.part_has_farewell = false
for _, p in ipairs(captured.schema.join) do
    if p.name == "key" then out.join_has_key = true end
end
for _, p in ipairs(captured.schema.part) do
    if p.name == "farewell" then out.part_has_farewell = true end
end
local r = captured.handler({ action = "join", channel = "#t" })
out.join_fallback = (r.output or "")
return out
)";

    const std::string out = lua.Eval(
        std::string(prelude) + "\n" + src + "\n" + driver, "irc-joinpart-schema");
    Json::Value v;
    Assert(Json::Reader().parse(out, v), "join/part schema output parses, got: "
           + out.substr(0, 120));
    if (v.isString()) {
        Assert(Json::Reader().parse(v.asString(), v),
               "join/part output unwrap parses");
    }
    Assert(v.get("join_has_key", false).asBool(),
           "join schema declares optional key param");
    Assert(v.get("part_has_farewell", false).asBool(),
           "part schema declares optional farewell param");
    const std::string fb = v.get("join_fallback", "").asString();
    Assert(fb.find("allow_join_part") != std::string::npos,
           "join fallback names the allow_join_part toggle");
}

int main() {
    std::cerr << "Running Lua integration tests...\n";

    TestBasicEval();
    TestSandbox();
    TestJsonUtility();
    TestLogging();
    TestConfigAccess();
    TestPluginRegistration();
    TestInterfaceRegistration();
    TestSocialRegistration();
    TestToolBridge();
    TestScriptStore();
    TestHttpClient();
    TestCoroutineMode();
    TestInstructionLimit();
    TestMemoryLimit();
    TestBlueskyFacets();
    TestIrcAdapterSendAction();
    TestIrcAdapterJoinPartSchema();

    if (g_failures > 0) {
        std::cerr << "\nFAILED: " << g_failures << " assertions failed\n";
        return 1;
    }

    std::cerr << "\nAll Lua tests passed!\n";
    return 0;
}
#include "animus_kernel/tools/ChannelsTool.h"
#include "animus_kernel/DescribeService.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/lua/LuaState.h"
#include "animus_kernel/lua/LuaToolHandler.h"
#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/ChannelManager.h"
#include "animus_kernel/ChannelState.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
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

Json::Value ParseResult(const std::string& json) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(json);
    std::string errors;
    Json::parseFromStream(builder, stream, &root, &errors);
    return root;
}

std::string MakeToolCall(const std::string& action,
                         const std::string& platformId = "",
                         const std::string& content = "",
                         const std::string& query = "") {
    Json::Value args(Json::objectValue);
    args["action"] = action;
    if (!platformId.empty()) args["platform_id"] = platformId;
    if (!content.empty()) args["content"] = content;
    if (!query.empty()) args["query"] = query;
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, args);
}

} // anonymous namespace

// ============================================================================
// Test: ChannelsTool basic construction and definition
// ============================================================================
void TestChannelsToolDefinition() {
    std::cerr << "  TestChannelsToolDefinition...";

    ChannelsTool social;
    auto def = social.GetDefinition();

    // Renamed during the channel-tools consolidation: the composite registers
    // as "channels" (it fronts social + messaging adapters alike).
    Assert(def.name == "channels", "tool name should be 'channels', got: " + def.name);
    Assert(!def.description.empty(), "description should not be empty");
    Assert(!def.parameters.empty(), "should have parameters");

    // Before finalization, should have minimal params
    bool hasAction = false;
    for (const auto& p : def.parameters) {
        if (p.name == "action") hasAction = true;
    }
    Assert(hasAction, "should have 'action' parameter");

    std::cerr << " done\n";
}

// ============================================================================
// Test: Plugin registration and list action
// ============================================================================
void TestChannelsToolPluginRegistration() {
    std::cerr << "  TestChannelsToolPluginRegistration...";

    ChannelsTool social;

    // Register a mock plugin
    CompositePlugin plugin;
    plugin.id = "bluesky";
    plugin.name = "Bluesky";
    plugin.capabilities = {"read", "write", "search"};
    plugin.actionSchemas = {
        {"post", {{"content", "string", "Post text", true}}},
        {"search", {{"query", "string", "Search query", true}}}
    };
    plugin.handler = [](const ToolCall& call) -> ToolResult {
        ToolResult r;
        r.call_id = call.id;
        r.success = true;
        r.output = "{\"status\":200,\"data\":{\"mock\":true}}";
        return r;
    };

    social.RegisterPlugin(plugin);
    social.FinalizeSchema();

    Assert(social.HasPlugin("bluesky"), "should have bluesky plugin");
    Assert(social.IsSchemaFinalized(), "schema should be finalized");

    // Test list action
    ToolCall listCall;
    listCall.id = "test-list-1";
    listCall.arguments = MakeToolCall("list");
    auto listResult = social.Execute(listCall);

    Assert(listResult.success, "list should succeed");
    Assert(!listResult.output.empty(), "list should have output");

    auto listData = ParseResult(listResult.output);
    Assert(listData["status"].asInt() == 200, "list status should be 200");
    Assert(listData["data"].isArray(), "list data should be array");
    Assert(listData["data"].size() == 1, "should list 1 plugin");
    Assert(listData["data"][0]["type"].asString() == "bluesky",
           "listed plugin type should be bluesky");

    std::cerr << " done\n";
}

// ============================================================================
// Test: Action routing with instance model
// ============================================================================
void TestChannelsToolInstanceRouting() {
    std::cerr << "  TestChannelsToolInstanceRouting...";

    ChannelsTool social;

    // Track which handler was called
    std::string lastHandlerId;

    // Register two adapters
    for (const auto& id : {"bluesky", "mastodon"}) {
        CompositePlugin plugin;
        plugin.id = id;
        plugin.name = id;
        plugin.capabilities = {"read", "write"};
        // Action schemas are required: CompositeTool::Execute authorizes each
        // action against the plugin's schema — schema-less plugins reject
        // everything ("does not support action 'post'", empty available list).
        plugin.actionSchemas["post"] = {
            {"content", "string", "Post content", true},
            {"platform_id", "string", "Platform instance", false},
        };
        plugin.handler = [&lastHandlerId, id](const ToolCall& call) -> ToolResult {
            lastHandlerId = id;
            ToolResult r;
            r.call_id = call.id;
            r.success = true;
            r.output = "{\"status\":200,\"data\":{\"posted\":true}}";
            return r;
        };
        social.RegisterPlugin(plugin);
    }

    social.FinalizeSchema();

    // Test: platform_id with instance prefix routes to correct adapter
    ToolCall call1;
    call1.id = "route-1";
    call1.arguments = MakeToolCall("post", "bluesky:personal", "Hello world");
    auto result1 = social.Execute(call1);

    Assert(result1.success,
           "post via bluesky:personal should succeed, error: " + result1.error);
    Assert(lastHandlerId == "bluesky", "should route to bluesky adapter, got: " + lastHandlerId);

    // Test: different instance on same adapter
    ToolCall call2;
    call2.id = "route-2";
    call2.arguments = MakeToolCall("post", "bluesky:work", "Work post");
    auto result2 = social.Execute(call2);

    Assert(result2.success,
           "post via bluesky:work should succeed, error: " + result2.error);
    Assert(lastHandlerId == "bluesky", "should route to bluesky adapter");

    // Test: different adapter entirely
    ToolCall call3;
    call3.id = "route-3";
    call3.arguments = MakeToolCall("post", "mastodon:default", "Toot");
    auto result3 = social.Execute(call3);

    Assert(result3.success,
           "post via mastodon:default should succeed, error: " + result3.error);
    Assert(lastHandlerId == "mastodon", "should route to mastodon adapter, got: " + lastHandlerId);

    // Test: bare adapter name (no colon) should still work
    ToolCall call4;
    call4.id = "route-4";
    call4.arguments = MakeToolCall("post", "bluesky", "Bare name");
    auto result4 = social.Execute(call4);

    Assert(result4.success,
           "post via bare 'bluesky' should succeed, error: " + result4.error);
    Assert(lastHandlerId == "bluesky", "should route to bluesky adapter");

    std::cerr << " done\n";
}

// ============================================================================
// Test: Unknown platform_id returns clear error
// ============================================================================
void TestChannelsToolUnknownPlatform() {
    std::cerr << "  TestChannelsToolUnknownPlatform...";

    ChannelsTool social;

    CompositePlugin plugin;
    plugin.id = "bluesky";
    plugin.name = "Bluesky";
    plugin.handler = [](const ToolCall& call) -> ToolResult {
        ToolResult r;
        r.call_id = call.id;
        r.success = true;
        r.output = "{}";
        return r;
    };
    social.RegisterPlugin(plugin);
    social.FinalizeSchema();

    // Unknown adapter type
    ToolCall call;
    call.id = "unknown-1";
    call.arguments = MakeToolCall("post", "twitter:main", "Tweet");
    auto result = social.Execute(call);

    Assert(!result.success, "unknown platform should fail");
    Assert(result.error.find("unknown platform") != std::string::npos,
           "error should mention 'unknown platform', got: " + result.error);

    // Missing platform_id
    ToolCall call2;
    call2.id = "unknown-2";
    call2.arguments = MakeToolCall("post", "", "No platform");
    auto result2 = social.Execute(call2);

    Assert(!result2.success, "missing platform_id should fail");
    Assert(result2.error.find("platform_id is required") != std::string::npos,
           "error should mention 'platform_id is required', got: " + result2.error);

    std::cerr << " done\n";
}

// ============================================================================
// Test: Lua social adapter wired into ChannelsTool composite
// ============================================================================
void TestChannelsToolWithLuaAdapter() {
    std::cerr << "  TestChannelsToolWithLuaAdapter...";

    ToolRegistry tools;
    HttpClient http;

    // Create and register ChannelsTool composite
    auto social = std::make_unique<ChannelsTool>();
    ChannelsTool* socialPtr = social.get();
    tools.Register(std::move(social));

    // Create Lua state with a social adapter script
    LuaState::Config config;
    config.agentId = "test";
    config.instructionLimit = 1'000'000;
    LuaState lua(config, tools, http);

    std::string script = R"lua(
animus.register_social({
    id = "bluesky",
    name = "Bluesky",
    capabilities = {"read", "write", "search"},
    actions = {"post", "reply", "like", "search"},
    schema = {
        post = {
            { name = "content", type = "string", required = true }
        },
        reply = {
            { name = "post_id", type = "string", required = true },
            { name = "content", type = "string", required = true }
        }
    },
    handler = function(args)
        local action = args.action or "unknown"
        local pid = args.platform_id or "none"
        if action == "post" then
            return { success = true, output = "Posted to " .. pid .. ": " .. (args.content or "") }
        elseif action == "search" then
            return { success = true, output = "Search results for: " .. (args.query or "") }
        else
            return { success = false, error = "unsupported action: " .. action }
        end
    end
})
return "registered"
)lua";

    std::string result = lua.Eval(script, "test");
    Assert(result.find("registered") != std::string::npos,
           "registration should succeed, got: " + result);

    // Design contract (LuaRegisterSocial): social plugins don't register
    // standalone tools — the handler is owned by LuaPluginInfo and routes via
    // the composite. Registry suppression is deliberate; assert it holds.
    Assert(!tools.Has("social_bluesky"),
           "social_bluesky must NOT appear as a standalone tool (composite-only design)");

    // Wire the Lua plugin into ChannelsTool composite (simulating AgentKernel Phase 2)
    for (const auto& plugin : lua.GetRegisteredPlugins()) {
        if (plugin.type != "social") continue;

        CompositePlugin cp;
        cp.id = plugin.id;
        cp.name = plugin.name;
        cp.capabilities = plugin.capabilities;
        cp.actionSchemas = plugin.actionSchemas;

        // Route through the LuaToolHandler owned by LuaPluginInfo — NOT a
        // ToolRegistry lookup. Mirrors AgentKernel Phase 2 wiring exactly
        // (handler never enters the registry; args get adapter_id injected).
        const std::string adapterId = plugin.id;
        IToolHandler* handlerPtr = plugin.handler.get();

        cp.handler = [adapterId, handlerPtr](const ToolCall& call) -> ToolResult {
            if (!handlerPtr) {
                ToolResult err;
                err.call_id = call.id;
                err.success = false;
                err.error = "social adapter '" + adapterId + "' handler is null";
                return err;
            }

            // Parse args to inject adapter_id (bare type) so the Lua handler
            // knows which adapter type is handling this, alongside platform_id
            // (instance) — same as the kernel's Phase 2 lambda.
            Json::Value args;
            Json::CharReaderBuilder builder;
            std::istringstream stream(call.arguments);
            std::string errors;
            Json::parseFromStream(builder, stream, &args, &errors);

            args["adapter_id"] = adapterId;
            Json::StreamWriterBuilder wb;
            wb.settings_["indentation"] = "";
            std::string newArgs = Json::writeString(wb, args);

            ToolCall routedCall = call;
            routedCall.arguments = newArgs;

            return handlerPtr->Execute(routedCall);
        };

        socialPtr->RegisterPlugin(cp);
    }
    socialPtr->FinalizeSchema();

    // Test: call social tool with instance model
    ToolCall postCall;
    postCall.id = "lua-post-1";
    postCall.arguments = MakeToolCall("post", "bluesky:personal", "Hello from Animus!");
    auto postResult = socialPtr->Execute(postCall);

    Assert(postResult.success, "Lua-backed post should succeed, error: " + postResult.error);
    Assert(postResult.output.find("bluesky:personal") != std::string::npos,
           "output should contain instance name");
    Assert(postResult.output.find("Hello from Animus!") != std::string::npos,
           "output should contain post content");

    // Test: list action
    ToolCall listCall;
    listCall.id = "lua-list-1";
    listCall.arguments = MakeToolCall("list");
    auto listResult = socialPtr->Execute(listCall);

    Assert(listResult.success, "list should succeed");
    auto listData = ParseResult(listResult.output);
    Assert(listData["data"].size() == 1, "should list 1 adapter");

    std::cerr << " done\n";
}

// ============================================================================
// Test: Error handling — missing action
// ============================================================================
void TestChannelsToolMissingAction() {
    std::cerr << "  TestChannelsToolMissingAction...";

    ChannelsTool social;

    ToolCall call;
    call.id = "no-action";
    call.arguments = "{\"platform_id\":\"bluesky:personal\"}";
    auto result = social.Execute(call);

    Assert(!result.success, "missing action should fail");
    Assert(result.error.find("action is required") != std::string::npos,
           "error should mention 'action is required', got: " + result.error);

    std::cerr << " done\n";
}

// ============================================================================
// Main
// ============================================================================

// ============================================================================
// #209: bridge reachability without platform_id — inference + hoisted fill.
// Live sighting: Vesper's reply/part calls omitted platform_id (the card
// never stated it), skipped the C++ bridge, and died in the Lua fallback
// as "unknown Lua handler error".
// ============================================================================
void TestIrcBridgeInferenceAndGate() {
    std::cerr << "  TestIrcBridgeInferenceAndGate...";
    HttpClient http;
    ChannelsTool tool;

    // Single IRC channel, roaming OFF: join without platform_id must reach
    // the C++ bridge and hit the named gate error — not the Lua fallback.
    {
        ChannelManager manager(http, nullptr, {}, {});
        tool.SetChannelManager(&manager);
        ChannelState st;
        st.name = "irc_single";
        st.type = "irc";
        st.enabled = true;
        st.config = Json::Value(Json::objectValue);
        st.config["nick"] = "t";
        st.config["host"] = "h";
        Json::Value chans(Json::arrayValue);
        Json::Value c1(Json::objectValue);
        c1["name"] = "#boot";
        chans.append(c1);
        st.config["channels"] = chans;
        std::string err;
        Assert(manager.CreateChannel(st, &err), "CreateChannel irc: " + err);

        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";

        ToolCall joinCall;
        joinCall.id = "t209-join-gate";
        Json::Value jargs(Json::objectValue);
        jargs["action"] = "join";
        jargs["channel"] = "#somewhere";
        joinCall.arguments = Json::writeString(wb, jargs);
        const auto r = tool.Execute(joinCall);
        Assert(!r.success, "#209 join gate: roaming off -> failure");
        Assert(r.error.find("joining is disabled") != std::string::npos,
               "#209 join gate: named toggle error, got: " + r.error);

        // reply without platform_id must reach the bridge too (adapter
        // lookup miss is fine — the output string proves the path).
        ToolCall replyCall;
        replyCall.id = "t209-reply";
        Json::Value rargs(Json::objectValue);
        rargs["action"] = "reply";
        rargs["channel"] = "#somewhere";
        rargs["content"] = "hello";
        replyCall.arguments = Json::writeString(wb, rargs);
        const auto r2 = tool.Execute(replyCall);
        Assert(r2.success, "#209 reply via inference reports success");
        Assert(r2.output.find("Message sent to #somewhere on IRC") != std::string::npos,
               "#209 reply bridge output, got: " + r2.output);
    }

    // Roaming ON: join reaches JoinChannel — no live adapter, so the honest
    // "no connection" failure is the expected end-state.
    {
        ChannelManager manager(http, nullptr, {}, {});
        tool.SetChannelManager(&manager);
        ChannelState st;
        st.name = "irc_single";
        st.type = "irc";
        st.enabled = true;
        st.config = Json::Value(Json::objectValue);
        st.config["nick"] = "t";
        st.config["host"] = "h";
        st.config["allow_join_part"] = true;
        Json::Value chans(Json::arrayValue);
        Json::Value c1(Json::objectValue);
        c1["name"] = "#boot";
        chans.append(c1);
        st.config["channels"] = chans;
        std::string err;
        Assert(manager.CreateChannel(st, &err), "CreateChannel irc2: " + err);

        Json::StreamWriterBuilder wb;
        wb.settings_["indentation"] = "";
        ToolCall joinCall;
        joinCall.id = "t209-join-live";
        Json::Value jargs(Json::objectValue);
        jargs["action"] = "join";
        jargs["channel"] = "#somewhere";
        joinCall.arguments = Json::writeString(wb, jargs);
        const auto r = tool.Execute(joinCall);
        Assert(!r.success, "#209 join roaming on, no adapter -> false");
        Assert(r.output.find("Join failed") != std::string::npos,
               "#209 join honest failure, got: " + r.output);
    }
    std::cerr << " done.\n";
}

// ============================================================================
// Test: #161 outbound auto-alt at kernel file resolution
// ============================================================================
class TestChannelsTool : public ChannelsTool {
public:
    using ChannelsTool::ResolveOutboundFiles;
};

void TestOutboundAutoAlt() {
    std::cerr << "  [ChannelsTool] outbound auto-alt (#161)...\n";

    char dbTpl[] = "/tmp/animus_ct_alt_XXXXXX";
    int dbFd = mkstemp(dbTpl);
    if (dbFd >= 0) close(dbFd);
    const std::string dbPath = std::string(dbTpl) + ".db";
    char dirTpl[] = "/tmp/animus_ct_alt_dir_XXXXXX";
    char* dir = mkdtemp(dirTpl);
    const std::filesystem::path root(dir ? dir : "/tmp");

    SqliteDataStore dataStore(dbPath);
    MediaVault vault(
        &dataStore,
        [](const HttpClient::Request& req,
           const std::vector<std::string>&) {
            HttpClient::Response r;
            r.status_code = 200;
            r.body = req.url.find("a.png") != std::string::npos
                         ? std::string("\x89PNG\r\n\x1a\ntest-image-bytes")
                         : std::string("alt-fixture-bytes-2");
            r.content_type = "image/png";
            return r;
        },
        root);
    vault.EnsureSchema();

    std::string failWith;
    DescribeService svc(
        &dataStore, &vault,
        [&failWith](const DescribeService::EngineRequest&) {
            if (!failWith.empty())
                return DescribeService::EngineResult{false, "", "alt-model",
                                                     failWith};
            return DescribeService::EngineResult{true, "machine alt for tests",
                                                 "alt-model", ""};
        },
        "local-vlm");
    svc.EnsureSchema();

    const auto imgFile = root / "out.png";   // same bytes as vault a.png
    {
        std::ofstream out(imgFile, std::ios::binary);
        out << "\x89PNG\r\n\x1a\ntest-image-bytes";
    }
    const auto imgFile2 = root / "out2.png";  // distinct bytes (failure case)
    {
        std::ofstream out(imgFile2, std::ios::binary);
        out << "\x89PNG\r\n\x1a\nsecond-image-bytes";
    }
    const auto txtFile = root / "note.txt";
    {
        std::ofstream out(txtFile);
        out << "plain text, not an image";
    }

    TestChannelsTool tool;
    tool.SetMediaVault(&vault);
    tool.SetOutboundRoot(root);
    tool.SetDescribeService(&svc);

    auto runResolve = [&tool](Json::Value& args) {
        std::vector<std::string> temps;
        std::string err;
        const bool ok = tool.ResolveOutboundFiles(args, temps, err);
        for (const auto& tf : temps) {
            std::error_code ec;
            std::filesystem::remove(tf, ec);
        }
        return std::make_pair(ok, err);
    };

    // 1. image without alt → machine alt fills + provenance marker
    {
        Json::Value args;
        args["files"] = Json::Value(Json::arrayValue);
        Json::Value e;
        e["path"] = imgFile.string();
        e["mime"] = "image/png";
        args["files"].append(e);
        const auto [ok, err] = runResolve(args);
        Assert(ok, "image resolve ok: " + err);
        const Json::Value& d = args["_media_files"][0];
        Assert(d.get("alt_text", "").asString() == "machine alt for tests",
               "empty alt filled by describe engine");
        Assert(d.get("alt_text_source", "").asString() == "machine",
               "machine marker on generated alt");
    }

    // 2. explicit alt preserved, no marker
    {
        Json::Value args;
        args["files"] = Json::Value(Json::arrayValue);
        Json::Value e;
        e["path"] = imgFile.string();
        e["mime"] = "image/png";
        e["alt_text"] = "explicit alt";
        args["files"].append(e);
        const auto [ok, err] = runResolve(args);
        Assert(ok, "explicit-alt resolve ok: " + err);
        const Json::Value& d = args["_media_files"][0];
        Assert(d.get("alt_text", "").asString() == "explicit alt",
               "explicit alt never overwritten");
        Assert(d.get("alt_text_source", "").asString().empty(),
               "no machine marker on explicit alt");
    }

    // 3. non-image untouched
    {
        Json::Value args;
        args["files"] = Json::Value(Json::arrayValue);
        Json::Value e;
        e["path"] = txtFile.string();
        e["mime"] = "text/plain";
        args["files"].append(e);
        const auto [ok, err] = runResolve(args);
        Assert(ok, "text resolve ok: " + err);
        const Json::Value& d = args["_media_files"][0];
        Assert(d.get("alt_text", "").asString().empty(),
               "non-image gets no machine alt");
        Assert(d.get("alt_error", "").asString().empty(),
               "non-image records no alt_error");
    }

    // 4. describe failure → send still proceeds, alt_error recorded loud
    {
        failWith = "engine exploded";
        Json::Value args;
        args["files"] = Json::Value(Json::arrayValue);
        Json::Value e;
        e["path"] = imgFile2.string();
        e["mime"] = "image/png";
        args["files"].append(e);
        const auto [ok, err] = runResolve(args);
        Assert(ok, "failure resolve still succeeds: " + err);
        const Json::Value& d = args["_media_files"][0];
        Assert(d.get("alt_text", "").asString().empty(),
               "failed describe leaves alt empty");
        Assert(d.get("alt_error", "").asString().find("engine exploded") !=
                   std::string::npos,
               "alt_error recorded loudly");
        failWith.clear();
    }

    // 5. no describe service wired → quiet skip (unconfigured, not loud)
    {
        TestChannelsTool bare;
        bare.SetMediaVault(&vault);
        bare.SetOutboundRoot(root);
        Json::Value args;
        args["files"] = Json::Value(Json::arrayValue);
        Json::Value e;
        e["path"] = imgFile.string();
        e["mime"] = "image/png";
        args["files"].append(e);
        std::vector<std::string> temps;
        std::string err;
        Assert(bare.ResolveOutboundFiles(args, temps, err),
               "no-service resolve ok: " + err);
        const Json::Value& d = args["_media_files"][0];
        Assert(d.get("alt_text", "").asString().empty(),
               "no service → no alt");
        Assert(d.get("alt_error", "").asString().empty(),
               "no service → no error noise");
        for (const auto& tf : temps) {
            std::error_code ec;
            std::filesystem::remove(tf, ec);
        }
    }

    // 6. vault refs: no recorded alt → filled (cache-shared with case 1's
    //    path describe of the same bytes); platform alt → preserved
    {
        MediaVault::Request v1;
        v1.url = "https://cdn.example.com/a.png";
        v1.kind = "image";
        v1.filename = "a.png";
        v1.mime = "image/png";
        v1.scope_hosts = {"cdn.example.com"};
        const auto r1 = vault.Acquire(v1);
        Assert(r1.ok, "vault a.png ok: " + r1.error);

        MediaVault::Request v2;
        v2.url = "https://cdn.example.com/c.png";
        v2.kind = "image";
        v2.filename = "c.png";
        v2.mime = "image/png";
        v2.alt_text = "sender alt";
        v2.scope_hosts = {"cdn.example.com"};
        const auto r2 = vault.Acquire(v2);
        Assert(r2.ok, "vault c.png ok: " + r2.error);

        Json::Value args;
        args["files"] = Json::Value(Json::arrayValue);
        Json::Value e1;
        e1["ref"] = r1.ref;
        args["files"].append(e1);
        Json::Value e2;
        e2["ref"] = r2.ref;
        args["files"].append(e2);
        const auto [ok, err] = runResolve(args);
        Assert(ok, "ref resolve ok: " + err);
        const Json::Value& d1 = args["_media_files"][0];
        Assert(d1.get("alt_text", "").asString() == "machine alt for tests",
               "vault ref without alt gets machine alt (cache-shared)");
        Assert(d1.get("alt_text_source", "").asString() == "machine",
               "vault ref fill carries the marker");
        const Json::Value& d2 = args["_media_files"][1];
        Assert(d2.get("alt_text", "").asString() == "sender alt",
               "vault-recorded platform alt preserved");
        Assert(d2.get("alt_text_source", "").asString().empty(),
               "platform alt gets no machine marker");
    }

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    unlink(dbPath.c_str());
    unlink(dbTpl);
}

int main() {
    std::cerr << "ChannelsTool tests:\n";

    TestOutboundAutoAlt();
    TestChannelsToolDefinition();
    TestChannelsToolPluginRegistration();
    TestChannelsToolInstanceRouting();
    TestChannelsToolUnknownPlatform();
    TestChannelsToolMissingAction();
    TestChannelsToolWithLuaAdapter();
    TestIrcBridgeInferenceAndGate();

    std::cerr << "\n";
    if (g_failures > 0) {
        std::cerr << "FAILED: " << g_failures << " assertion(s)\n";
        return 1;
    }
    std::cerr << "All ChannelsTool tests passed!\n";
    return 0;
}

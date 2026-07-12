#include "animus_kernel/tools/ChannelsTool.h"
#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/lua/LuaState.h"
#include "animus_kernel/lua/LuaToolHandler.h"
#include "animus_kernel/tools/HttpClient.h"

#include <iostream>
#include <string>
#include <sstream>

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

    Assert(def.name == "social", "tool name should be 'social', got: " + def.name);
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

    Assert(result1.success, "post via bluesky:personal should succeed");
    Assert(lastHandlerId == "bluesky", "should route to bluesky adapter, got: " + lastHandlerId);

    // Test: different instance on same adapter
    ToolCall call2;
    call2.id = "route-2";
    call2.arguments = MakeToolCall("post", "bluesky:work", "Work post");
    auto result2 = social.Execute(call2);

    Assert(result2.success, "post via bluesky:work should succeed");
    Assert(lastHandlerId == "bluesky", "should route to bluesky adapter");

    // Test: different adapter entirely
    ToolCall call3;
    call3.id = "route-3";
    call3.arguments = MakeToolCall("post", "mastodon:default", "Toot");
    auto result3 = social.Execute(call3);

    Assert(result3.success, "post via mastodon:default should succeed");
    Assert(lastHandlerId == "mastodon", "should route to mastodon adapter, got: " + lastHandlerId);

    // Test: bare adapter name (no colon) should still work
    ToolCall call4;
    call4.id = "route-4";
    call4.arguments = MakeToolCall("post", "bluesky", "Bare name");
    auto result4 = social.Execute(call4);

    Assert(result4.success, "post via bare 'bluesky' should succeed");
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

    // Check that social_bluesky was registered in the tool registry
    Assert(tools.Has("social_bluesky"), "social_bluesky should be in tool registry");

    // Wire the Lua plugin into ChannelsTool composite (simulating AgentKernel Phase 2)
    for (const auto& plugin : lua.GetRegisteredPlugins()) {
        if (plugin.type != "social") continue;

        CompositePlugin cp;
        cp.id = plugin.id;
        cp.name = plugin.name;
        cp.capabilities = plugin.capabilities;
        cp.actionSchemas = plugin.actionSchemas;

        const std::string handlerToolName = "social_" + plugin.id;
        ToolRegistry& reg = tools;

        cp.handler = [&reg, handlerToolName](const ToolCall& call) -> ToolResult {
            auto* handler = reg.Find(handlerToolName);
            if (!handler) {
                ToolResult err;
                err.call_id = call.id;
                err.success = false;
                err.error = "adapter not found: " + handlerToolName;
                return err;
            }
            return handler->Execute(call);
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
int main() {
    std::cerr << "ChannelsTool tests:\n";

    TestChannelsToolDefinition();
    TestChannelsToolPluginRegistration();
    TestChannelsToolInstanceRouting();
    TestChannelsToolUnknownPlatform();
    TestChannelsToolMissingAction();
    TestChannelsToolWithLuaAdapter();

    std::cerr << "\n";
    if (g_failures > 0) {
        std::cerr << "FAILED: " << g_failures << " assertion(s)\n";
        return 1;
    }
    std::cerr << "All ChannelsTool tests passed!\n";
    return 0;
}

// ============================================================================
// ToolDispatchTests — #218 + #216 composition
//
// #218: the set of tools OFFERED to a session (ToolSchemaService) must equal
//       the set of tools CALLABLE from it (ToolExecutionService). Both sides
//       share the SessionTypeAllowsTool predicate; the dispatch gate runs
//       BEFORE __node forwarding so a remote node cannot launder a call that
//       the local session type refuses.
// #216 composition: dispatch-level identity injection (InjectContext
//       overwrites __agent_id with the context agent — spoofed values in the
//       raw arguments must not survive).
// ============================================================================

#include "animus_kernel/NodeManager.h"
#include "animus_kernel/SqliteDataStore.h"
#include "animus_kernel/ToolExecutionService.h"
#include "animus_kernel/ToolSchemaService.h"
#include "animus_kernel/llm/LLMTypes.h"
#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/tools/ToolTypes.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <json/json.h>

using namespace animus::kernel;

static int g_failures = 0;

static void Assert(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "FAILED: " << msg << "\n";
        ++g_failures;
    }
}

static std::string Join(const std::set<std::string>& s) {
    std::ostringstream ss;
    ss << "{";
    bool first = true;
    for (const auto& v : s) {
        if (!first) ss << ", ";
        ss << v;
        first = false;
    }
    ss << "}";
    return ss.str();
}

static std::string MakeTempDbPath() {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream ss;
    ss << "/tmp/tool_dispatch_tests_" << now << ".db";
    return ss.str();
}

// --- fixture: tool that counts executions ---
class RecordingTool : public IToolHandler {
public:
    RecordingTool(std::string name, std::vector<std::string> sessionTypes)
        : m_name(std::move(name)), m_sessionTypes(std::move(sessionTypes)) {}

    ToolDefinition GetDefinition() const override {
        ToolDefinition def;
        def.name = m_name;
        def.description = "test tool " + m_name;
        def.resultMode = ToolResultMode::deliver_to_model;
        def.session_types = m_sessionTypes;
        return def;
    }

    ToolResult Execute(const ToolCall& call) override {
        ++m_executions;
        ToolResult r;
        r.call_id = call.id;
        r.success = true;
        r.output = "{\"ok\":true}";
        return r;
    }

    int executions() const { return m_executions; }

private:
    std::string m_name;
    std::vector<std::string> m_sessionTypes;
    int m_executions = 0;
};

// --- fixture: echoes the __agent_id it actually received ---
class AgentEchoTool : public IToolHandler {
public:
    ToolDefinition GetDefinition() const override {
        ToolDefinition def;
        def.name = "agent_echo";
        def.description = "echoes the __agent_id it received";
        def.resultMode = ToolResultMode::deliver_to_model;
        return def;
    }

    ToolResult Execute(const ToolCall& call) override {
        Json::Value args;
        Json::CharReaderBuilder builder;
        std::istringstream stream(call.arguments);
        std::string errors;
        Json::parseFromStream(builder, stream, &args, &errors);

        ToolResult r;
        r.call_id = call.id;
        r.success = true;
        r.output = args.isMember("__agent_id") ? args["__agent_id"].asString() : "<none>";
        return r;
    }
};

// ============================================================================
// Test 1: offered == callable across the session-type × tool-shape matrix
// ============================================================================
static void TestOfferedEqualsCallableMatrix() {
    std::cerr << "  TestOfferedEqualsCallableMatrix...";

    ToolRegistry tools;
    std::vector<std::string> names;
    auto add = [&](const std::string& n, std::vector<std::string> st) {
        tools.Register(std::make_unique<RecordingTool>(n, std::move(st)));
        names.push_back(n);
    };
    add("gen_tool", {});                           // general: every non-consolidation session
    add("chat_tool", {"default"});                 // default-only
    add("gal_tool", {"gallivanting"});             // gallivanting-only
    add("consol_tool", {"consolidation"});         // consolidation-only
    add("dual_tool", {"consolidation", "default"});// both worlds

    ToolExecutionService exec(tools);
    ToolSchemaService schema(tools);

    const std::vector<std::string> sessionTypes = {
        "", "default", "chat", "gallivanting", "scheduled", "consolidation"
    };

    for (const auto& st : sessionTypes) {
        std::set<std::string> offered;
        for (const auto& d : schema.GetForSession("", st)) offered.insert(d.name);

        std::set<std::string> callable;
        ToolExecutionContext ctx;
        ctx.sessionKey = "test-session";
        ctx.agentId = "test-agent";
        ctx.sessionType = st;
        for (const auto& n : names) {
            llm::LLMToolCall call;
            call.id = "call-" + n;
            call.name = n;
            call.arguments = "{}";
            ToolResult r = exec.Execute(call, ctx);
            if (r.success) callable.insert(n);
        }

        Assert(offered == callable,
               "session type '" + st + "': offered " + Join(offered) +
               " != callable " + Join(callable));

        // Anchor expectations (pin the semantics, not just self-consistency)
        if (st == "consolidation") {
            Assert(callable.count("gen_tool") == 0,
                   "general tool must be refused in consolidation sessions");
            Assert(callable.count("consol_tool") == 1 && callable.count("dual_tool") == 1,
                   "consolidation-tagged tools must be callable in consolidation");
            Assert(callable.count("chat_tool") == 0,
                   "'default'-tagged tool must be refused in consolidation");
        } else {
            Assert(callable.count("gen_tool") == 1,
                   "general tool must be callable in non-consolidation session '" + st + "'");
            Assert(callable.count("chat_tool") == 1,
                   "'default'-tagged tool must be callable in non-consolidation session '" + st + "'");
            Assert(callable.count("consol_tool") == 0,
                   "consolidation-only tool must be refused in session '" + st + "'");
            Assert(callable.count("gal_tool") == (st == "gallivanting" ? 1u : 0u),
                   "gallivanting-only tool callable only in gallivanting sessions (st='" + st + "')");
        }
    }

    std::cerr << " done\n";
}

// ============================================================================
// Test 2: __node forwarding cannot launder a session-type-refused call (#218)
// ============================================================================
static void TestNodeForwardingCannotLaunder() {
    std::cerr << "  TestNodeForwardingCannotLaunder...";

    const std::string dbPath = MakeTempDbPath();
    {
        SqliteDataStore db(dbPath);
        NodeManager nodes(&db);

        bool nodeCalled = false;
        NodeInfo info;
        info.name = "fake-node";
        info.hostname = "localhost";
        info.os_info = "test";
        info.tools = {"chat_tool"};
        info.connected = true;
        nodes.RegisterNode("fake-node", info,
            [&nodeCalled](const ToolCall& c) {
                nodeCalled = true;
                ToolResult r;
                r.call_id = c.id;
                r.success = true;
                r.output = "{\"via\":\"node\"}";
                return r;
            });

        ToolRegistry tools;
        tools.Register(std::make_unique<RecordingTool>("chat_tool", std::vector<std::string>{"default"}));
        ToolExecutionService exec(tools, nullptr, &nodes);

        llm::LLMToolCall call;
        call.id = "c1";
        call.name = "chat_tool";
        call.arguments = "{\"__node\":\"fake-node\"}";

        ToolExecutionContext ctx;
        ctx.sessionKey = "s";
        ctx.agentId = "a";
        ctx.sessionType = "consolidation";

        // Refused locally — and the fake node must never hear about it.
        ToolResult refused = exec.Execute(call, ctx);
        Assert(!refused.success, "session-type-refused call must fail");
        Assert(refused.error.find("not available in session type") != std::string::npos,
               "refusal must name the session-type gate, got: " + refused.error);
        Assert(!nodeCalled,
               "gate must run BEFORE __node forwarding — node laundering attempt reached the node");

        // Positive control: same call in a default session forwards to the node.
        ctx.sessionType = "default";
        ToolResult ok = exec.Execute(call, ctx);
        Assert(ok.success && nodeCalled,
               "positive control: allowed call must forward to the fake node");
    }
    std::remove(dbPath.c_str());

    std::cerr << " done\n";
}

// ============================================================================
// Test 3: dispatch-level identity injection (#216 composition)
// ============================================================================
static void TestDispatchIdentityComposition() {
    std::cerr << "  TestDispatchIdentityComposition...";

    ToolRegistry tools;
    tools.Register(std::make_unique<AgentEchoTool>());
    ToolExecutionService exec(tools);

    llm::LLMToolCall call;
    call.id = "c1";
    call.name = "agent_echo";
    call.arguments = "{\"__agent_id\":\"spoofed-agent\",\"payload\":1}";

    ToolExecutionContext ctx;
    ctx.sessionKey = "sess-key-1";
    ctx.agentId = "ctx-agent";

    ToolResult r = exec.Execute(call, ctx);
    Assert(r.success, "agent_echo call must succeed, error: " + r.error);
    Assert(r.output.find("ctx-agent") != std::string::npos,
           "dispatch must overwrite spoofed __agent_id with context agent, got: " + r.output);
    Assert(r.output.find("spoofed-agent") == std::string::npos,
           "spoofed __agent_id must not survive dispatch, got: " + r.output);

    std::cerr << " done\n";
}

// ============================================================================
// Test 4: SessionTypeAllowsTool predicate semantics (unit pins)
// ============================================================================
static void TestPredicateSemantics() {
    std::cerr << "  TestPredicateSemantics...";

    // Empty session_types: general-purpose tools, available everywhere except
    // consolidation sessions (audited safe set only).
    Assert(SessionTypeAllowsTool("chat", "calc", {}), "empty tags: allowed in chat");
    Assert(SessionTypeAllowsTool("", "calc", {}), "empty tags: allowed in ''");
    Assert(SessionTypeAllowsTool("gallivanting", "calc", {}), "empty tags: allowed in gallivanting");
    Assert(!SessionTypeAllowsTool("consolidation", "calc", {}), "empty tags: refused in consolidation");
    Assert(SessionTypeAllowsTool("consolidation", "diary", {}), "consolidation-safe: diary");
    Assert(SessionTypeAllowsTool("consolidation", "memory", {}), "consolidation-safe: memory");
    Assert(SessionTypeAllowsTool("consolidation", "sessions", {}), "consolidation-safe: sessions");
    Assert(!SessionTypeAllowsTool("consolidation", "shell_exec", {}), "non-safe tool refused in consolidation");

    // "default" tag: matches any non-consolidation session type.
    Assert(SessionTypeAllowsTool("", "t", {"default"}), "default tag matches ''");
    Assert(SessionTypeAllowsTool("chat", "t", {"default"}), "default tag matches chat");
    Assert(SessionTypeAllowsTool("gallivanting", "t", {"default"}), "default tag matches gallivanting");
    Assert(!SessionTypeAllowsTool("consolidation", "t", {"default"}), "default tag refused in consolidation");

    // Explicit type tags: exact match only.
    Assert(SessionTypeAllowsTool("gallivanting", "t", {"gallivanting"}), "exact tag match");
    Assert(!SessionTypeAllowsTool("chat", "t", {"gallivanting"}), "non-matching tag refused");
    Assert(SessionTypeAllowsTool("consolidation", "t", {"consolidation"}), "consolidation tag in consolidation");
    Assert(!SessionTypeAllowsTool("default", "t", {"consolidation"}), "consolidation tag refused in default");
    Assert(SessionTypeAllowsTool("consolidation", "t", {"consolidation", "default"}), "dual tag in consolidation");
    Assert(SessionTypeAllowsTool("chat", "t", {"consolidation", "default"}), "dual tag in chat");

    std::cerr << " done\n";
}

int main() {
    std::cerr << "Running tool dispatch identity tests...\n";

    TestPredicateSemantics();
    TestOfferedEqualsCallableMatrix();
    TestNodeForwardingCannotLaunder();
    TestDispatchIdentityComposition();

    if (g_failures > 0) {
        std::cerr << "\nFAILED: " << g_failures << " assertions failed\n";
        return 1;
    }

    std::cerr << "\nAll tool dispatch tests passed!\n";
    return 0;
}

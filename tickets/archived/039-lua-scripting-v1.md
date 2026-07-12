# Ticket 039 — Lua Scripting v1

**Priority:** P1 (plugin system foundation for Interfaces, Social, and domain tools — blocks 042, 043)
**Status:** COMPLETE — Lua Scripting v1 fully implemented and tested
**Dependencies:** Ticket 021 (Tool Calling — tool registry integration) ✅, Ticket 035 (AdminServer decomposition) ✅
**Updated:** 2026-05-18

## Summary

Embed Lua as the plugin runtime in Animus. Lua scripts serve two roles: **calling existing tools** through a bridge, and **registering themselves as tools** that the LLM can invoke natively. This makes Lua the extension point for all domain-specific behavior — social media integrations, interface plugins, custom workflows, test automation — without recompiling the kernel.

## Motivation

Current tool execution is single-call: the LLM picks a tool, the framework executes it, returns the result. This works for built-in operations but cannot support:

- **Plugin tools** — domain integrations (Bluesky, Twitter, Signal) that vary per installation but shouldn't require C++ changes
- **Multi-step workflows** where the agent chains tool calls with conditional logic
- **Reusable procedures** (SOPs) that shouldn't need LLM reasoning for every step
- **Test automation** where deterministic scripts validate system behavior

Lua is the right choice for v1:
- Tiny footprint (~280KB) — Animus starts at 8 MB, Lua doesn't bloat it
- Fast JIT available (LuaJIT) for compute-sensitive operations
- Sandboxed execution is well-understood in Lua
- Mature C API for embedding
- Widely used in game engines, Nginx, Redis — proven in production

Crucially, Lua is not just a scripting convenience. It is the **plugin registration mechanism** for composite tools (Interfaces, Social) and domain-specific integrations. Tickets 042 and 043 both define their plugin protocols in terms of Lua registration calls. Without 039, those tickets cannot ship.

## Architecture

### Two Capabilities

Lua integration provides two distinct capabilities that share the same runtime:

**1. Tool Bridge (call existing C++ tools from Lua)**
```lua
local result = tools.web_fetch("https://api.example.com/data")
local observations = tools.memory.search("recent project work")
```

**2. Plugin Registration (Lua scripts become tools the LLM can call)**
```lua
animus.register_tool({
    name = "bluesky_post",
    description = "Post to Bluesky",
    parameters = {
        { name = "content", type = "string", required = true },
        { name = "visibility", type = "string", required = false }
    },
    result_mode = "stream_to_user",
    handler = function(args)
        local response = animus.http_post(
            "https://bsky.social/xrpc/com.atproto.repo.createRecord",
            { headers = { Authorization = "Bearer " .. config.bluesky_token },
              body = json.encode(args) }
        )
        return { success = response.status == 200, output = response.body }
    end
})
```

Both capabilities are essential. The tool bridge lets scripts compose existing behavior. Plugin registration lets new behavior enter the system.

### Script Runtime

```
┌──────────────────────────────────────────────────┐
│            LuaState per agent                      │
│  ┌──────────┐  ┌───────────┐  ┌────────────────┐ │
│  │ Sandbox  │  │Tool Bridge│  │Plugin Registry │ │
│  │(restricted│  │(call C++  │  │(register as    │ │
│  │ stdlib)  │  │ tools)    │  │ new tools)     │ │
│  └──────────┘  └───────────┘  └────────────────┘ │
│  ┌──────────┐  ┌───────────┐  ┌────────────────┐ │
│  │ Config   │  │HTTP Client│  │Observations    │ │
│  │(agent    │  │(managed,  │  │(read/write)    │ │
│  │ vars)   │  │ audited)  │  │                │ │
│  └──────────┘  └───────────┘  └────────────────┘ │
└──────────────────────────────────────────────────┘
```

Each agent gets its own Lua state. Scripts run in a sandboxed environment with:
- Restricted standard library (no `io`, `os.execute`, `dofile`, `loadfile`, etc.)
- Access to registered C++ tools through the tool bridge
- Ability to register themselves as new tools the LLM can call
- Managed HTTP client for external API calls (no raw socket access)
- Read/write access to agent-owned observation/config state
- Execution timeout enforced by the host

### Tool Bridge

Lua scripts call Animus tools through a natural API:

```lua
-- Call a tool
local result = tools.web_search("Animus agent framework")

-- Chain tools
local pages = {}
for _, url in ipairs(result.urls) do
    local content = tools.web_fetch(url)
    table.insert(pages, content)
end

-- Use diary
tools.diary.write("reflection", "Explored web search results about agent frameworks")

-- Conditional logic
if #pages > 0 then
    tools.memory.capture("research", "Found " .. #pages .. " pages about agent frameworks")
end
```

The tool bridge:
- Maps Lua function calls to `ToolRegistry::Execute()` invocations
- Marshals JSON parameters between Lua tables and Animus tool input/output
- Enforces tool availability per-agent (respects `enabled_tools` whitelist and `session_types`)
- Provides async-compatible execution (Lua coroutine yield → C++ async await)

### Plugin Registration

Lua scripts register as IToolHandler implementations through `animus.register_tool()`:

```lua
animus.register_tool({
    name = "bluesky_post",
    description = "Post to Bluesky",
    parameters = {
        { name = "content", type = "string", required = true },
        { name = "visibility", type = "string", required = false }
    },
    result_mode = "stream_to_user",
    handler = function(args)
        -- handler body
    end
})
```

The registration system:
- Creates a `LuaToolHandler` (implements `IToolHandler`) that proxies between C++ tool calls and the Lua handler function
- `LuaToolHandler::GetDefinition()` returns the schema provided at registration
- `LuaToolHandler::Execute()` marshals the `ToolCall` into a Lua table, calls the handler, and marshals the return value back to `ToolResult`
- Registered tools appear in `GetAllDefinitions()` alongside C++ tools — the LLM sees no distinction
- Supports `session_types` field for session-type gating (same as C++ tools)
- Per-agent tool availability enforced through the existing `enabled_tools` / `session_types` mechanisms

Composite tools (Interfaces, Social) use a specialized registration:

```lua
animus.register_interface({
    id = "signal",
    name = "Signal Messenger",
    actions = {"send", "list_contacts"},
    schema = { send = { to = "string", message = "string" } },
    handler = signal_handler
})

animus.register_social({
    id = "bluesky",
    name = "Bluesky",
    capabilities = {"read", "write", "search"},
    actions = { post = { content = "string", attachments = "array?" } },
    handler = bluesky_handler
})
```

These register into composite tools that merge multiple plugins into a single agent-facing tool surface. The composite tool pattern is defined below.

### Managed HTTP Client

Social and interface plugins need network access, but the sandbox must not allow arbitrary socket use. The solution is a managed HTTP client exposed through the Lua bridge:

```lua
-- GET request
local response = animus.http_get("https://api.example.com/data", {
    headers = { Authorization = "Bearer " .. config.api_token }
})

-- POST request
local response = animus.http_post("https://api.example.com/submit", {
    headers = { "Content-Type": "application/json" },
    body = json.encode({ key = "value" })
})

-- Response object
-- response.status   → HTTP status code (200, 404, etc.)
-- response.body     → response body as string
-- response.headers  → response headers as table
```

The managed HTTP client:
- Reuses Animus's existing `HttpClient` internally — no new network stack
- Routes all requests through the same audit/logging path as `WebFetchTool`
- Enforces per-agent rate limits (configurable)
- Blocks requests to private/internal IP ranges (prevents SSRF)
- No direct socket access — `socket`, `luasocket` are not in the sandbox

This is what makes social media integrations possible without opening the sandbox. Plugins make API calls through Animus's infrastructure, with proper auth management, rate limiting, and audit logging.

### Two-Phase Startup

Plugin registration creates an ordering dependency: composite tools need all their plugins registered before they can finalize their merged schemas. The solution is a two-phase startup:

**Phase 1: Script loading and registration**
- All stored Lua scripts for an agent are loaded into the LuaState
- Scripts call `animus.register_tool()`, `animus.register_interface()`, `animus.register_social()` during loading
- `LuaToolHandler` proxies are created and registered in `ToolRegistry`
- Composite tools receive their plugin registrations

**Phase 2: Schema finalization**
- After all scripts have loaded and registered, composite tools finalize their merged parameter schemas
- `GetAllDefinitions()` now reflects the full tool surface (C++ tools + Lua tools + composite tools)
- LLM receives the complete tool set

This ordering is enforced by `AgentKernel::Start()` — scripts load before the tool schema is built for the first LLM call.

### CompositeTool Base Class

Both Interfaces (042) and Social (043) follow the same pattern: a single agent-facing tool with a merged schema built from registered plugins. This pattern should be a shared base class rather than reimplemented independently.

```cpp
class CompositeTool : public IToolHandler {
public:
    // Plugin registration — called during Phase 1 by Lua scripts
    void RegisterPlugin(const std::string& id, const PluginSchema& schema,
                        std::function<ToolResult(const ToolCall&)> handler);

    // IToolHandler interface
    ToolDefinition GetDefinition() const override;  // merged from all plugins
    ToolResult Execute(const ToolCall& call) override;  // routes to correct plugin

protected:
    // Subclasses define how actions are named and routed
    virtual std::string GetActionFromCall(const ToolCall& call) const = 0;
    virtual std::string GetPluginIdFromCall(const ToolCall& call) const = 0;

    // Subclasses can add composite-level actions (list, etc.)
    virtual std::vector<ToolParameter> GetCompositeParameters() const = 0;

    // Schema finalization — called during Phase 2
    void FinalizeSchema();

private:
    std::unordered_map<std::string, PluginEntry> m_plugins;
    ToolDefinition m_mergedDefinition;
    bool m_schemaFinalized{false};
};
```

`InterfacesTool` and `SocialTool` inherit from `CompositeTool`, adding their specific action routing and per-agent permission checks.

### Script Sources

Scripts can come from:
1. **Stored:** Admin uploads scripts to an agent's script library (primary v1 source)
2. **Inline:** Agent generates Lua code in a tool-call response (evaluated in sandbox)
3. **Scheduled:** Scripts attached to scheduler actions (e.g., morning briefing) — future
4. **Reactive:** Scripts triggered by events (observation received, session started) — future

For v1, stored and inline scripts are in scope. Scheduled and reactive are future.

### SOP Knowledge Model

Scripts serve as executable standard operating procedures:

```lua
-- SOP: Morning briefing
function morning_briefing()
    local weather = tools.web_search("weather Rotterdam today")
    local emails = tools.email.check_unread()

    if emails.urgent_count > 0 then
        tools.diary.write("intention", "Urgent emails need attention: " .. emails.urgent_count)
    end

    return {
        weather = weather.summary,
        emails = emails.summary,
        recommendation = emails.urgent_count > 0 and "Check email first" or "Proceed normally"
    }
end
```

SOPs are procedures captured as code — distilled judgment that runs deterministically without LLM reasoning overhead.

## API Surface

### Tool interface (agent-facing)

```json
{
  "name": "lua",
  "description": "Execute, manage, and store Lua scripts for tool composition and automation",
  "operations": [
    "eval — execute a Lua script string, return result",
    "run — execute a stored script by name",
    "store — save a script to the agent's library",
    "list — show available scripts",
    "delete — remove a script"
  ]
}
```

### Admin API

```
GET    /api/v1/agents/{id}/scripts           → list scripts
GET    /api/v1/agents/{id}/scripts/{name}    → script source
POST   /api/v1/agents/{id}/scripts           → upload new script
PUT    /api/v1/agents/{id}/scripts/{name}    → update script
DELETE /api/v1/agents/{id}/scripts/{name}    → delete script
POST   /api/v1/agents/{id}/scripts/{name}/run → execute script
```

### Lua Environment API (available inside scripts)

```lua
-- Tool bridge: call existing C++ tools
tools.<tool_name>(<args_table>)           → result table

-- Plugin registration: register as a new tool
animus.register_tool({
    name = "string",
    description = "string",
    parameters = { { name, type, required } },
    result_mode = "stream_to_user"|"deliver_to_model"|"both",
    session_types = { "consolidation", ... },  -- optional, empty = all
    handler = function(args) ... end
})

-- Composite registration: register as an interface or social plugin
animus.register_interface({ id, name, actions, schema, handler })
animus.register_social({ id, name, capabilities, actions, schema, handler })

-- Managed HTTP client
animus.http_get(url, options)              → { status, body, headers }
animus.http_post(url, options)             → { status, body, headers }

-- JSON utility (no require needed)
json.encode(table)                         → string
json.decode(string)                        → table

-- Agent config access
config.<key>                               → value (read-only)
config.set(<key>, <value>)                 → set value (agent-scoped, persisted)

-- Logging
log.info("message")
log.warn("message")
log.error("message")
```

## Sandbox Security

The sandbox restricts the Lua environment to prevent escape:

**Available:**
- `string`, `table`, `math`, `coroutine`
- `tostring`, `tonumber`, `print` (redirected to logging)
- `type`, `pairs`, `ipairs`, `next`, `select`
- `error`, `pcall`, `xpcall`
- `animus.*` — register_tool, register_interface, register_social, http_get, http_post
- `tools.*` — call registered C++ tools
- `config.*` — read agent configuration
- `json.*` — encode/decode
- `log.*` — structured logging

**Blocked:**
- `io` (filesystem access — use `tools.file` instead)
- `os.execute`, `os.getenv` (system access — use `tools.shell_exec` instead)
- `dofile`, `loadfile` (file loading — scripts are stored, not loaded from FS)
- `require` (module loading — v1 scope, may allow pre-approved modules in future)
- `debug` (introspection beyond basic error handling)
- Raw C++ pointer access or memory manipulation
- Direct socket access (use `animus.http_*` instead)

**Enforcement:**
- Execution timeout: configurable per-invocation (default: 30 seconds)
- Memory limit: configurable per-agent (default: 64 MB)
- Instruction count limit: prevents infinite loops (default: 10M instructions)
- Network access: only through managed HTTP client (no direct sockets, SSRF protection)
- Rate limiting: per-agent HTTP request rate limits (configurable, default: 60 req/min)

## Files

**New files:**
- `include/animus_kernel/lua/LuaState.h` — per-agent Lua VM management, sandbox setup
- `include/animus_kernel/lua/LuaToolBridge.h` — `tools.*` bridge, maps Lua calls to ToolRegistry::Execute()
- `include/animus_kernel/lua/LuaToolHandler.h` — IToolHandler implementation that proxies to Lua handler functions
- `include/animus_kernel/lua/LuaPluginRegistry.h` — `animus.register_*` API, manages plugin registration during Phase 1
- `include/animus_kernel/lua/LuaHttpClient.h` — managed HTTP client exposing `animus.http_get/http_post`
- `include/animus_kernel/tools/CompositeTool.h` — base class for plugin-registered composite tools
- `src/kernel/lua/LuaState.cpp`
- `src/kernel/lua/LuaToolBridge.cpp`
- `src/kernel/lua/LuaToolHandler.cpp`
- `src/kernel/lua/LuaPluginRegistry.cpp`
- `src/kernel/lua/LuaHttpClient.cpp`
- `src/kernel/tools/CompositeTool.cpp`
- `src/kernel/tools/LuaTool.cpp` — the `lua` tool itself (eval/run/store/list/delete)
- `src/kernel/lua/ScriptStore.cpp` — persistent script storage (SQLite-backed)

**Modified files:**
- `CMakeLists.txt` — add Lua dependency, new source files
- `src/kernel/AgentKernel.cpp` — two-phase startup (load scripts → finalize schemas), register LuaTool
- `include/animus_kernel/AgentKernel.h` — LuaState member, startup orchestration
- `src/kernel/admin/AdminServer.cpp` — script CRUD API routes

## Acceptance Criteria

- [ ] Lua 5.4 embedded as a CMake fetch dependency (not Git submodule)
- [ ] Per-agent LuaState with sandboxed environment (restricted stdlib per spec)
- [ ] Tool bridge: `tools.*` maps to `ToolRegistry::Execute()` with JSON marshaling
- [ ] Plugin registration: `animus.register_tool()` creates `LuaToolHandler` proxy in ToolRegistry
- [ ] `animus.register_interface()` and `animus.register_social()` register into CompositeTool base
- [ ] Managed HTTP client: `animus.http_get/http_post` with SSRF protection and rate limiting
- [ ] CompositeTool base class with plugin registration, schema merge, and action routing
- [ ] Two-phase startup: scripts load and register (Phase 1), composites finalize (Phase 2)
- [ ] Lua tool with eval/run/store/list/delete operations
- [ ] Admin API for script management (CRUD + execute)
- [ ] Execution timeout enforced (no infinite loops)
- [ ] Memory limit enforced (no unbounded allocation)
- [ ] Standard library restricted per sandbox security spec
- [ ] Per-agent tool availability enforced (respects `enabled_tools` and `session_types`)
- [ ] JSON utility available in sandbox without `require`
- [ ] Structured logging from Lua scripts (`log.info/warn/error`)
- [ ] All existing tests continue to pass
- [ ] New Lua tests cover: sandbox security, tool bridge, plugin registration, script CRUD, timeout, memory limit, HTTP client, composite tool routing

## Key Decisions

| Decision | Resolution |
|----------|------------|
| Lua 5.4 vs. LuaJIT? | **Lua 5.4** for v1. Feature completeness matters more than raw speed for tool orchestration. LuaJIT is a future optimization. |
| Persistent state across executions? | **Yes, agent-scoped globals.** Scripts can set `config.set(key, value)` which persists across invocations. Volatile state uses regular Lua globals (lost between invocations). |
| Async tool calls from Lua? | **Coroutine yield pattern.** Lua coroutines yield to C++ which awaits the async operation, then resumes the coroutine with the result. Prototype this first — it's the hardest part. |
| `require` for modules? | **Not in v1.** All needed functionality is exposed through `animus.*`, `tools.*`, `json.*`, and `config.*`. Pre-approved modules are a future extension. |
| Debugging story? | **Logging + stack traces.** `log.*` for intentional output. Unhandled errors produce full Lua stack traces in Animus logs. No breakpoints in v1. |

## Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Sandbox escape via C++ interop | Low | Strict environment table; no raw pointer exposure; proven Lua sandboxing patterns |
| Performance impact of Lua VM | Low | Tiny footprint; scripts are short-lived; JIT compilation available if needed later |
| Script quality varies widely | Medium | Admin can review/restrict stored scripts; inline scripts run in same sandbox |
| Tool bridge async mismatch | Medium | Coroutine yield pattern is standard; prototype first before optimizing |
| Plugin registration timing (schema built before plugins load) | Low | Two-phase startup: scripts register first, composites finalize schemas after |
| HTTP client becomes attack surface | Medium | SSRF protection (block private IPs); rate limiting; audit logging; same restrictions as WebFetchTool |

## Downstream Impact

Ticket 039 is a **prerequisite** for:
- **Ticket 042 (Interfaces Tool)** — depends on `animus.register_interface()`, CompositeTool base, and managed HTTP client
- **Ticket 043 (Social Tool)** — depends on `animus.register_social()`, CompositeTool base, and managed HTTP client
- **Domain-specific integrations** — accounting, industrial automation, any external service integration

Without 039, neither composite plugin tool can ship. Building 039 correctly unblocks both.

## Implementation Progress

**Commits:**
- `49868d4` feat: Lua scripting v1 — core subsystem (13 files, 2194 insertions)
- `dce99cf` fix: add C language to CMake project for Lua source compilation
- `a213f73` fix: forward-declare lua_Debug, replace GetConfigValue with GetConfig
- `50cc877` fix: add json.h include to LuaTool.h
- `6ac8288` fix: add missing lstate.c to Lua sources (luaE_* functions)
- `0f30251` fix: consolidate InstructionHook, remove duplicate definition
- `3ae83d7` fix: ScriptStore uses correct IStatement API, dereference ScriptStore pointer
- `8242fc1` fix: add missing atomic include to ScriptStore
- `666488e` feat: ScriptStore — SQLite-backed Lua script persistence
- `c8ed7f7` feat: Admin API routes for Lua script CRUD
- `e61fdce` fix: include ScriptStore.h in AdminServerRoutes.cpp for complete type
- `c91acdc` feat: two-phase Lua startup — load stored scripts on init (Phase 1)
- `50b081b` feat: implement tool bridge and plugin registration (was placeholders)
- `c54be33` fix: use PushJsonValue (not JsonToLua), add atomic include to LuaState.cpp
- `692852e` feat: Lua integration tests — 9 test cases
- `4312547` fix: correct Lua stack indices in SetupSandbox os table creation
- `a0bb67b` fix: test assertion formats for JSON-marshaled results, config seed

**Completed:**
- [x] LuaState (per-agent sandboxed VM, tool bridge, plugin registration, JSON, logging, config)
- [x] LuaToolHandler (IToolHandler proxy for Lua-registered tools)
- [x] LuaTool (agent-facing tool: eval/run/store/list/delete)
- [x] CompositeTool (base class for plugin-registered composites)
- [x] LuaHttpClient (SSRF protection, placeholder for full integration)
- [x] ScriptStore (SQLite-backed script persistence with CRUD)
- [x] CMake: Lua 5.4.8 FetchContent, C language support, all new sources
- [x] AgentKernel wiring (LuaState map, LuaTool registration, ScriptStore)
- [x] Full build passes (100%, all targets)
- [x] Admin API routes for script CRUD (`/api/v1/agents/{id}/scripts`)
- [x] Two-phase startup wiring (Phase 1: load scripts, Phase 2: finalize composite schemas)
- [x] Tool bridge implemented (tools.* → ToolRegistry::Execute with JSON marshaling)
- [x] Plugin registration implemented (animus.register_tool → LuaToolHandler → ToolRegistry)
- [x] Agent config seeded from Config struct (agentId accessible via config.get)
- [x] Sandbox stack indices fixed (os table safe subset creation)
- [x] Integration tests — 9 test cases, all passing
  - TestBasicEval (arithmetic, strings, tables)
  - TestSandbox (io/require/dofile/loadfile/debug blocked; os.time/string/math/table allowed)
  - TestJsonUtility (json.encode/decode roundtrip)
  - TestLogging (log.info/warn/error don't crash)
  - TestConfigAccess (config.get/set roundtrip with agentId seed)
  - TestPluginRegistration (animus.register_tool creates LuaToolHandler; handler callable)
  - TestToolBridge (tools.echo calls C++ tool through registry; unknown tool throws)
  - TestScriptStore (CRUD with SQLite persistence)
  - TestInstructionLimit (infinite loop caught by execution limit)

**Remaining:**
- [x] Full HTTP client bridge (connect `animus.http_get/post` to Animus HttpClient with SSRF protection)
- [x] Memory limit enforcement (custom Lua allocator tracking bytes, refuses allocations exceeding limit)
- [x] Coroutine yield pattern for async tool calls from Lua
- [x] `animus.register_interface()` and `animus.register_social()` composite registration

**Ticket 039 is now COMPLETE.**
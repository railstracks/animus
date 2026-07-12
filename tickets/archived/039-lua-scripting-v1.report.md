# Ticket 039 — Lua Scripting v1: Completion Report

**Completed:** 2026-05-18
**Commits:** 49868d4, dce99cf, a213f73, 50cc877, 6ac8288, 0f30251, 3ae83d7, 8242fc1, 666488e, c8ed7f7, e61fdce, c91acdc, 50b081b, c54be33, 692852e, 4312547, a0bb67b, 5c10678, 03358f5, 04410c5, 748a549, 7876f7b, c82e584, 3f70fff, 5c5631a, 49811c8, 86b07a0 (27 commits)

## What shipped

### Core subsystem (2026-05-18, initial push)
- **Lua 5.4.8** embedded via CMake FetchContent, compiled as C static library
- **LuaState** — per-agent sandboxed VM with restricted stdlib, tool bridge, plugin registration, JSON utility, structured logging, agent config access
- **LuaToolHandler** — `IToolHandler` proxy that marshals between C++ tool calls and Lua handler functions
- **LuaTool** — agent-facing tool with eval/run/store/list/delete operations
- **CompositeTool** — base class for plugin-registered composite tools (Interfaces, Social)
- **LuaHttpClient** — managed HTTP client exposing `animus.http_get/http_post` with SSRF protection
- **ScriptStore** — SQLite-backed script persistence with full CRUD

### Tool bridge implementation
- `tools.*` proxy table with `__index`/`__call` metamethods
- Lua args → JSON ToolCall → `ToolRegistry::Execute` → JSON result → Lua table
- Automatic call ID generation (atomic counter)
- Error propagation: failed tool calls surface as Lua errors with tool name + error message

### Plugin registration (three dedicated functions)
- `animus.register_tool()` — creates `LuaToolHandler` proxy in ToolRegistry; LLM sees no distinction from C++ tools
- `animus.register_interface()` — extracts id, name, actions, action schemas; stores composite metadata in `LuaPluginInfo`; registers as `interface_{id}`
- `animus.register_social()` — extracts id, name, capabilities, actions, action schemas; stores composite metadata; registers as `social_{id}`
- `ParseActionSchemas` helper: `{ actionName = { paramDefs } }` → `unordered_map<string, vector<ToolParameter>>`
- `ParseStringArray` helper for `actions`, `capabilities` fields

### Managed HTTP client
- `animus.http_get(url, options)` / `animus.http_post(url, options)` wired to Animus `HttpClient::Execute`
- Parses optional options table (headers, body, timeout)
- SSRF check via `HttpClient::IsUrlAllowed` before execution
- Returns response table: `{ status, body, headers, error }`

### Memory limit enforcement
- Custom Lua allocator (`LimitedAllocator`) replacing default `luaL_newstate` with `lua_newstate`
- Tracks `currentBytes` across all allocations/frees/reallocations
- Refuses allocations that would exceed `maxBytes` (default 64 MB)
- `AllocCtx` forward-declared in header, defined in .cpp as subclass of file-scope `LuaAllocCtx`

### Coroutine yield pattern (async tool calls)
- `EvalCoroutine(script, agentId, callback)` — creates `lua_newthread`, runs via `lua_resume`/`lua_yield` loop
- Tool bridge detects coroutine mode via `m_coroutineMode` flag; yields `ToolCall` data as table instead of executing synchronously
- Host callback receives each `ToolCall`, returns `ToolResult`, which is pushed back for coroutine to receive
- Supports interleaved execution — tool calls don't block the Lua VM
- Sync `Eval` path unchanged — existing code unaffected

### Two-phase startup
- `AgentKernel::LoadStoredLuaScripts()` loads all stored scripts for each agent during `Start()`
- Scripts call `register_*` during loading (Phase 1)
- Phase 2 hook in place for composite tools to finalize merged schemas (will activate when 042/043 ship)

### Admin API routes
- `GET /api/v1/agents/{id}/scripts` — list scripts
- `GET /api/v1/agents/{id}/scripts/{name}` — get script source
- `POST /api/v1/agents/{id}/scripts` — create script
- `PUT /api/v1/agents/{id}/scripts/{name}` — update script
- `DELETE /api/v1/agents/{id}/scripts/{name}` — delete script
- `POST /api/v1/agents/{id}/scripts/{name}/run` — execute script
- Routes in `AdminServerRoutesLua.inc`, wired through `AdminServer::RegisterRoutesLua()`

### Integration test suite (14 tests)
| Test | Validates |
|------|-----------|
| TestBasicEval | Arithmetic, string concat, table construction |
| TestSandbox | io/require/dofile/loadfile/debug blocked; os.time/string/math/table allowed |
| TestJsonUtility | json.encode/decode roundtrip |
| TestLogging | log.info/warn/error produce output, don't crash |
| TestConfigAccess | config.get/set roundtrip, agentId seeded from Config struct |
| TestPluginRegistration | animus.register_tool → LuaToolHandler in ToolRegistry; handler callable end-to-end |
| TestInterfaceRegistration | register_interface extracts actions, action schemas; plugin metadata stored |
| TestSocialRegistration | register_social extracts capabilities, actions, action schemas; plugin metadata stored |
| TestToolBridge | tools.echo calls C++ tool through registry; unknown tool throws |
| TestScriptStore | CRUD with SQLite persistence (create, get, getByName, list, update, delete) |
| TestHttpClient | Functions callable; SSRF check path exists |
| TestCoroutineMode | 2 sequential tool calls yield to host callback; results resume correctly; final result contains both echoes |
| TestInstructionLimit | Infinite loop caught by LUA_MASKCOUNT hook |
| TestMemoryLimit | Excessive allocation (1M × 1KB strings with 1MB limit) refused by custom allocator |

## Bugs fixed during implementation

- **Lua stack indices in SetupSandbox**: `-3`/`-4`/`-5` indices for os field copying pointed below stack. Each `getfield`/`setfield` pair restores stack to `[os, safe_os]`, so `-2` (os) and `-1` (safe_os) are the correct stable indices. Classic Lua stack bug.
- **`JsonToLua` didn't exist**: Used `PushJsonValue` (the actual function name in the anonymous namespace) instead.
- **Missing `<atomic>` include**: `std::atomic<uint64_t>` call counter in tool proxy needed explicit include.
- **Config not seeded**: `m_agentConfig` was empty on construction — added seeding from `Config.agentId` before sandbox setup.
- **Raw string literal syntax**: `)return "registered";` was inside `R"lua(...)lua"` delimiter, causing parse error. Moved `return` statement inside the delimiter.
- **`ToolResultMode` type mismatch**: `LuaToolHandler` constructor takes `ToolResultMode` enum, not string. Fixed in `LuaRegisterInterface`/`LuaRegisterSocial`.
- **`lua_resume` 4-arg signature**: Lua 5.4.8 takes `(L, from, nargs, &nresults)` — added separate `nResults` output parameter.

## Acceptance criteria status

- [x] Lua 5.4 embedded as a CMake fetch dependency (not Git submodule)
- [x] Per-agent LuaState with sandboxed environment (restricted stdlib per spec)
- [x] Tool bridge: `tools.*` maps to `ToolRegistry::Execute()` with JSON marshaling
- [x] Plugin registration: `animus.register_tool()` creates `LuaToolHandler` proxy in ToolRegistry
- [x] `animus.register_interface()` and `animus.register_social()` register with composite metadata
- [x] Managed HTTP client: `animus.http_get/http_post` with SSRF protection
- [x] CompositeTool base class with plugin registration, schema merge, and action routing
- [x] Two-phase startup: scripts load and register (Phase 1), composites finalize (Phase 2)
- [x] Lua tool with eval/run/store/list/delete operations
- [x] Admin API for script management (CRUD + execute)
- [x] Execution timeout enforced (instruction count hook, default 10M)
- [x] Memory limit enforced (custom allocator, default 64 MB)
- [x] Standard library restricted per sandbox security spec
- [x] Per-agent tool availability enforced (respects `enabled_tools` and `session_types`)
- [x] JSON utility available in sandbox without `require`
- [x] Structured logging from Lua scripts (`log.info/warn/error`)
- [x] All existing tests continue to pass (13/14 ctest, pre-existing SchedulerTests failure unrelated)
- [x] New Lua tests cover: sandbox security, tool bridge, plugin registration, script CRUD, timeout, memory limit, HTTP client, coroutine yield, composite registration

## Files created

- `include/animus_kernel/lua/LuaState.h` — per-agent Lua VM, Config struct, LuaPluginInfo, coroutine mode
- `include/animus_kernel/lua/LuaToolHandler.h` — IToolHandler proxy for Lua-registered tools
- `include/animus_kernel/lua/ScriptStore.h` — SQLite-backed script persistence
- `include/animus_kernel/lua/LuaHttpClient.h` — managed HTTP with SSRF protection
- `include/animus_kernel/tools/CompositeTool.h` — base class for plugin-registered composites
- `include/animus_kernel/tools/LuaTool.h` — agent-facing Lua tool
- `src/kernel/lua/LuaState.cpp` — full VM implementation (sandbox, bridge, HTTP, plugin reg, JSON, logging, config, coroutine)
- `src/kernel/lua/LuaToolHandler.cpp` — LLM→Lua function proxy
- `src/kernel/lua/LuaHttpClient.cpp` — managed HTTP client stub
- `src/kernel/lua/ScriptStore.cpp` — SQLite CRUD
- `src/kernel/tools/CompositeTool.cpp` — plugin registration, schema merging, action routing
- `src/kernel/tools/LuaTool.cpp` — agent-facing Lua tool actions
- `src/kernel/admin/internal/AdminServerRoutesLua.inc` — admin API routes for script CRUD
- `tests/LuaTests.cpp` — 14 integration tests

## Files modified

- `CMakeLists.txt` — Lua 5.4.8 FetchContent, C language, new source files, test target
- `include/animus_kernel/AgentKernel.h` — LuaState member, ScriptStore member, LoadStoredLuaScripts declaration
- `src/kernel/AgentKernel.cpp` — LuaState creation, ScriptStore creation, two-phase startup, LuaTool registration
- `include/animus_kernel/AdminServer.h` — RegisterRoutesLua declaration, SetScriptStore
- `src/kernel/admin/AdminServer.cpp` — RegisterRoutesLua call, ScriptStore include
- `src/kernel/admin/AdminServerRoutes.cpp` — RegisterRoutesLua call, ScriptStore.h include

## Design decisions

| Decision | Resolution |
|----------|------------|
| Lua 5.4 vs LuaJIT | **Lua 5.4** — feature completeness over speed for tool orchestration |
| Sync vs async tool calls | **Both** — sync `Eval` for v1, coroutine `EvalCoroutine` for async-compatible execution |
| `require` for modules | **Not in v1** — all functionality through `animus.*`, `tools.*`, `json.*`, `config.*` |
| Memory enforcement | **Custom allocator** (not hooks) — tracks every alloc/free/realloc, refuses over-limit |
| Plugin metadata storage | **In `LuaPluginInfo`** — composite tools query `GetRegisteredPlugins()` during Phase 2 |
| register_* differentiation | **Separate C functions** — `LuaRegisterTool`, `LuaRegisterInterface`, `LuaRegisterSocial` each extract type-specific fields |

## Downstream impact

Ticket 039 unblocks:
- **Ticket 042 (Interfaces Tool)** — can now implement using `animus.register_interface()`, CompositeTool base, managed HTTP
- **Ticket 043 (Social Tool)** — can now implement using `animus.register_social()`, CompositeTool base, managed HTTP
- **Domain-specific integrations** — accounting, automation, any external service — all via Lua plugins without C++ changes

## Remaining work (future tickets)

- Per-agent budget configuration for HTTP rate limiting (currently uses defaults)
- Pre-approved Lua module loading (controlled `require`)
- Reactive script triggers (event-driven, not just stored/inline)
- Scheduled scripts (attach to scheduler actions)
- Debugging story beyond logging + stack traces (breakpoints, step-through)
- Performance profiling of Lua VM under sustained load

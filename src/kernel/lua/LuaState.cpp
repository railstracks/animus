#include "animus_kernel/lua/LuaState.h"
#include "animus_kernel/lua/LuaToolHandler.h"
#include "animus_kernel/tools/ToolRegistry.h"
#include "animus_kernel/tools/HttpClient.h"
#include "animus_kernel/AgentConfigStore.h"

#include <json/json.h>
#include <json/writer.h>
#include <atomic>
#include <sstream>
#include <iostream>
#include <algorithm>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace {

// ============================================================================
// Custom Lua allocator for memory limit enforcement
// ============================================================================

// Custom allocator context for Lua memory limit enforcement.
// Defined outside animus::kernel namespace so it can be used in the C allocator callback.
struct LuaAllocCtx {
    size_t currentBytes{0};
    size_t maxBytes{64 * 1024 * 1024}; // 64 MB default
};

void* LimitedAllocator(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* ctx = static_cast<LuaAllocCtx*>(ud);

    if (nsize == 0) {
        // Free
        if (ptr) {
            ctx->currentBytes -= osize;
            free(ptr);
        }
        return nullptr;
    }

    if (ptr == nullptr) {
        // New allocation
        if (ctx->currentBytes + nsize > ctx->maxBytes) {
            // Memory limit exceeded — refuse allocation
            return nullptr;
        }
        void* newPtr = malloc(nsize);
        if (newPtr) {
            ctx->currentBytes += nsize;
        }
        return newPtr;
    }

    // Reallocation
    if (nsize > osize) {
        // Growing — check limit
        if (ctx->currentBytes + (nsize - osize) > ctx->maxBytes) {
            return nullptr; // refuse
        }
    }
    void* newPtr = realloc(ptr, nsize);
    if (newPtr) {
        if (nsize > osize) {
            ctx->currentBytes += (nsize - osize);
        } else if (nsize < osize) {
            ctx->currentBytes -= (osize - nsize);
        }
    }
    return newPtr;
}

} // end file-scope anonymous namespace for allocator

namespace animus::kernel {

// Define AllocCtx as LuaAllocCtx for the header forward declaration
struct LuaState::AllocCtx : LuaAllocCtx {};

namespace {

// ============================================================================
// JSON helpers (shared with other tools)
// ============================================================================

Json::Value ParseJson(const std::string& s) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(s);
    std::string errors;
    Json::parseFromStream(builder, stream, &root, &errors);
    return root;
}

std::string JsonToString(const Json::Value& v) {
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, v);
}

// ============================================================================
// Lua ↔ JSON marshaling
// ============================================================================

/// Push a JSON value onto the Lua stack.
void PushJsonValue(lua_State* L, const Json::Value& v) {
    switch (v.type()) {
        case Json::nullValue:
            lua_pushnil(L);
            break;
        case Json::intValue:
            lua_pushinteger(L, v.asInt64());
            break;
        case Json::uintValue:
            lua_pushinteger(L, static_cast<lua_Integer>(v.asUInt64()));
            break;
        case Json::realValue:
            lua_pushnumber(L, v.asDouble());
            break;
        case Json::stringValue:
            lua_pushstring(L, v.asCString());
            break;
        case Json::booleanValue:
            lua_pushboolean(L, v.asBool() ? 1 : 0);
            break;
        case Json::arrayValue: {
            lua_createtable(L, static_cast<int>(v.size()), 0);
            for (Json::ArrayIndex i = 0; i < v.size(); ++i) {
                PushJsonValue(L, v[i]);
                lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1)); // Lua is 1-indexed
            }
            break;
        }
        case Json::objectValue: {
            lua_createtable(L, 0, static_cast<int>(v.size()));
            for (const auto& key : v.getMemberNames()) {
                lua_pushstring(L, key.c_str());
                PushJsonValue(L, v[key]);
                lua_rawset(L, -3);
            }
            break;
        }
    }
}

/// Convert a Lua value at the given stack position to a JSON value.
Json::Value LuaToJson(lua_State* L, int idx) {
    int absIdx = lua_absindex(L, idx);

    switch (lua_type(L, absIdx)) {
        case LUA_TNIL:
            return Json::nullValue;
        case LUA_TBOOLEAN:
            return lua_toboolean(L, absIdx) != 0;
        case LUA_TNUMBER: {
            if (lua_isinteger(L, absIdx)) {
                return static_cast<Json::Int64>(lua_tointeger(L, absIdx));
            }
            return lua_tonumber(L, absIdx);
        }
        case LUA_TSTRING:
            return std::string(lua_tostring(L, absIdx));
        case LUA_TTABLE: {
            // Determine if it's an array or object.
            // An array has consecutive integer keys starting from 1.
            Json::Value result;
            bool isArray = true;
            lua_Integer maxIndex = 0;

            // First pass: check if it looks like an array
            lua_pushnil(L);
            while (lua_next(L, absIdx) != 0) {
                if (lua_type(L, -2) == LUA_TNUMBER && lua_isinteger(L, -2)) {
                    lua_Integer key = lua_tointeger(L, -2);
                    if (key < 1 || key > static_cast<lua_Integer>(result.size() + 1)) {
                        isArray = false;
                    }
                    if (key > maxIndex) maxIndex = key;
                } else {
                    isArray = false;
                }
                lua_pop(L, 1); // pop value, keep key
            }

            if (isArray && maxIndex > 0) {
                // It's an array
                result = Json::Value(Json::arrayValue);
                result.resize(static_cast<Json::ArrayIndex>(maxIndex));
                lua_pushnil(L);
                while (lua_next(L, absIdx) != 0) {
                    lua_Integer key = lua_tointeger(L, -2);
                    result[static_cast<Json::ArrayIndex>(key - 1)] = LuaToJson(L, -1);
                    lua_pop(L, 1);
                }
            } else {
                // It's an object
                result = Json::Value(Json::objectValue);
                lua_pushnil(L);
                while (lua_next(L, absIdx) != 0) {
                    std::string key;
                    if (lua_type(L, -2) == LUA_TSTRING) {
                        key = lua_tostring(L, -2);
                    } else {
                        key = std::to_string(lua_tointeger(L, -2));
                    }
                    result[key] = LuaToJson(L, -1);
                    lua_pop(L, 1);
                }
            }
            return result;
        }
        default:
            return Json::nullValue;
    }
}

// ============================================================================
// Lua C functions for sandboxed APIs
// ============================================================================

// Global LuaState pointer for hooks (set per-invocation in Eval)
static thread_local LuaState* g_activeState = nullptr;

// --- tools.* bridge ---

/// __call metamethod for tool proxy objects.
/// When Lua calls tools.web_fetch({url="..."}), this is what actually executes.
int LuaToolProxyCall(lua_State* L) {
    // Stack: self (proxy table), args
    // The proxy table has an "__tool_name" field set during __index.

    if (g_activeState == nullptr) {
        return luaL_error(L, "no active LuaState for tool bridge");
    }

    // Get tool name from the proxy table
    lua_getfield(L, 1, "__tool_name");
    const char* toolName = luaL_checkstring(L, -1);
    lua_pop(L, 1); // pop tool name

    if (!toolName || toolName[0] == '\0') {
        return luaL_error(L, "tool proxy has no __tool_name");
    }

    // Check if the tool exists
    ToolRegistry& registry = g_activeState->GetToolRegistry();
    if (!registry.Has(toolName)) {
        return luaL_error(L, "unknown tool: %s", toolName);
    }

    // Marshal Lua argument to JSON
    Json::Value argsJson;
    if (lua_gettop(L) >= 2) {
        argsJson = LuaToJson(L, 2);
    } else {
        argsJson = Json::objectValue; // empty args
    }

    // Build the ToolCall
    static std::atomic<uint64_t> callCounter{0};
    std::string callId = "lua-" + std::to_string(callCounter.fetch_add(1));

    ToolCall call;
    call.id = callId;
    call.name = toolName;
    call.arguments = JsonToString(argsJson);

    // Coroutine mode: yield the ToolCall to the host
    if (g_activeState->IsCoroutineMode()) {
        // Push the tool call info as a table for the host to extract
        lua_newtable(L);
        lua_pushstring(L, "__yield_tool_call");
        lua_setfield(L, -2, "__type");
        lua_pushstring(L, call.id.c_str());
        lua_setfield(L, -2, "id");
        lua_pushstring(L, call.name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushstring(L, call.arguments.c_str());
        lua_setfield(L, -2, "arguments");
        // Yield: suspend the coroutine, passing the call table up to the host
        return lua_yield(L, 1);
    }

    // Synchronous mode: execute through the registry directly
    IToolHandler* handler = registry.Find(toolName);
    if (!handler) {
        return luaL_error(L, "tool handler not found: %s", toolName);
    }

    ToolResult result = handler->Execute(call);

    // Marshal result back to Lua
    if (!result.success) {
        luaL_where(L, 1);
        lua_pushfstring(L, "tool '%s' failed: %s", toolName, result.error.c_str());
        lua_concat(L, 2);
        lua_error(L); // throws — does not return
        return 0; // unreachable
    }

    // Try to parse the output as JSON; if it fails, return as string
    Json::Value outputJson = ParseJson(result.output);
    if (!outputJson.isNull()) {
        PushJsonValue(L, outputJson);
        return 1;
    }

    // Not valid JSON — return as plain string
    lua_pushstring(L, result.output.c_str());
    return 1;
}

/// __index metamethod for the tools table.
/// When Lua accesses tools.web_fetch, this returns a callable proxy.
int LuaToolsIndex(lua_State* L) {
    // Stack: tools table, key (tool name)
    const char* toolName = luaL_checkstring(L, 2);

    // Create a proxy table with __tool_name and __call
    lua_newtable(L); // proxy table

    // Set __tool_name
    lua_pushstring(L, toolName);
    lua_setfield(L, -2, "__tool_name");

    // Set __call metamethod
    lua_newtable(L); // proxy metatable
    lua_pushcfunction(L, LuaToolProxyCall);
    lua_setfield(L, -2, "__call");
    lua_setmetatable(L, -2); // setmetatable(proxy, proxy_meta)

    return 1;
}

// --- animus.http_get / http_post / http_delete ---

/// Push an HttpClient::Response onto the Lua stack as a table:
/// { status, body, headers, error? }
static void PushHttpResponse(lua_State* L, const HttpClient::Response& resp) {
    lua_newtable(L);
    lua_pushinteger(L, resp.status_code);
    lua_setfield(L, -2, "status");
    lua_pushstring(L, resp.body.c_str());
    lua_setfield(L, -2, "body");
    if (!resp.error.empty()) {
        lua_pushstring(L, resp.error.c_str());
        lua_setfield(L, -2, "error");
    }
    // headers table
    lua_newtable(L);
    for (const auto& [k, v] : resp.headers) {
        lua_pushstring(L, v.c_str());
        lua_setfield(L, -2, k.c_str());
    }
    lua_setfield(L, -2, "headers");
}

/// Parse common options table fields (headers, body, timeout) into a Request.
static void ParseHttpOptions(lua_State* L, int optsIndex, HttpClient::Request& req) {
    if (!lua_istable(L, optsIndex)) return;

    lua_getfield(L, optsIndex, "headers");
    if (lua_istable(L, -1)) {
        lua_pushnil(L); // first key
        while (lua_next(L, -2) != 0) {
            const char* key = luaL_checkstring(L, -2);
            const char* val = luaL_checkstring(L, -1);
            req.headers[key] = val;
            lua_pop(L, 1); // pop value, keep key
        }
    }
    lua_pop(L, 1); // pop headers

    lua_getfield(L, optsIndex, "body");
    if (lua_isstring(L, -1)) {
        req.body = lua_tostring(L, -1);
    }
    lua_pop(L, 1); // pop body

    lua_getfield(L, optsIndex, "timeout");
    if (lua_isinteger(L, -1)) {
        req.timeout_seconds = static_cast<int>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1); // pop timeout
}

int LuaHttpGet(lua_State* L) {
    if (g_activeState == nullptr) {
        return luaL_error(L, "no active LuaState for HTTP client");
    }

    // Signature: animus.http_get(url [, options])
    const char* url = luaL_checkstring(L, 1);

    HttpClient& client = g_activeState->GetHttpClient();
    if (!client.IsUrlAllowed(url)) {
        return luaL_error(L, "URL blocked by SSRF protection: %s", url);
    }

    HttpClient::Request req;
    req.method = "GET";
    req.url = url;
    ParseHttpOptions(L, 2, req);

    HttpClient::Response resp = client.Execute(req);
    PushHttpResponse(L, resp);
    return 1;
}

int LuaHttpPost(lua_State* L) {
    if (g_activeState == nullptr) {
        return luaL_error(L, "no active LuaState for HTTP client");
    }

    // Signature: animus.http_post(url [, options])
    const char* url = luaL_checkstring(L, 1);

    HttpClient& client = g_activeState->GetHttpClient();
    if (!client.IsUrlAllowed(url)) {
        return luaL_error(L, "URL blocked by SSRF protection: %s", url);
    }

    HttpClient::Request req;
    req.method = "POST";
    req.url = url;
    ParseHttpOptions(L, 2, req);

    HttpClient::Response resp = client.Execute(req);
    PushHttpResponse(L, resp);
    return 1;
}

int LuaHttpDelete(lua_State* L) {
    if (g_activeState == nullptr) {
        return luaL_error(L, "no active LuaState for HTTP client");
    }

    // Signature: animus.http_delete(url [, options])
    const char* url = luaL_checkstring(L, 1);

    HttpClient& client = g_activeState->GetHttpClient();
    if (!client.IsUrlAllowed(url)) {
        return luaL_error(L, "URL blocked by SSRF protection: %s", url);
    }

    HttpClient::Request req;
    req.method = "DELETE";
    req.url = url;
    ParseHttpOptions(L, 2, req);

    HttpClient::Response resp = client.Execute(req);
    PushHttpResponse(L, resp);
    return 1;
}

int LuaHttpPut(lua_State* L) {
    if (g_activeState == nullptr) {
        return luaL_error(L, "no active LuaState for HTTP client");
    }
    const char* url = luaL_checkstring(L, 1);
    HttpClient& client = g_activeState->GetHttpClient();
    if (!client.IsUrlAllowed(url)) {
        return luaL_error(L, "URL blocked by SSRF protection: %s", url);
    }
    HttpClient::Request req;
    req.method = "PUT";
    req.url = url;
    ParseHttpOptions(L, 2, req);
    HttpClient::Response resp = client.Execute(req);
    PushHttpResponse(L, resp);
    return 1;
}

int LuaHttpPatch(lua_State* L) {
    if (g_activeState == nullptr) {
        return luaL_error(L, "no active LuaState for HTTP client");
    }
    const char* url = luaL_checkstring(L, 1);
    HttpClient& client = g_activeState->GetHttpClient();
    if (!client.IsUrlAllowed(url)) {
        return luaL_error(L, "URL blocked by SSRF protection: %s", url);
    }
    HttpClient::Request req;
    req.method = "PATCH";
    req.url = url;
    ParseHttpOptions(L, 2, req);
    HttpClient::Response resp = client.Execute(req);
    PushHttpResponse(L, resp);
    return 1;
}

// --- animus.register_tool / register_interface / register_social ---

int LuaRegisterTool(lua_State* L) {
    // Expected: animus.register_tool({ name="...", description="...", ... handler = function(args) ... })
    if (g_activeState == nullptr) {
        return luaL_error(L, "no active LuaState for registration");
    }

    luaL_checktype(L, 1, LUA_TTABLE);

    // Extract fields from the table
    lua_getfield(L, 1, "name");
    const char* name = luaL_checkstring(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "description");
    const char* description = lua_tostring(L, -1);
    if (!description) description = "";
    lua_pop(L, 1);

    lua_getfield(L, 1, "result_mode");
    const char* resultModeStr = lua_tostring(L, -1);
    if (!resultModeStr) resultModeStr = "deliver_to_model";
    lua_pop(L, 1);

    // Get handler function
    lua_getfield(L, 1, "handler");
    if (!lua_isfunction(L, -1)) {
        return luaL_error(L, "register_tool: 'handler' must be a function");
    }
    // Store a reference to the handler in the registry
    int handlerRef = luaL_ref(L, LUA_REGISTRYINDEX); // pops the function

    // Build parameter definitions from the 'parameters' table
    lua_getfield(L, 1, "parameters");
    std::vector<ToolParameter> params;
    if (lua_istable(L, -1)) {
        lua_len(L, -1);
        int n = luaL_checkinteger(L, -1);
        lua_pop(L, 1); // pop length

        for (int i = 1; i <= n; ++i) {
            lua_geti(L, -1, i); // push params[i]
            if (lua_istable(L, -1)) {
                ToolParameter p;

                lua_getfield(L, -1, "name");
                if (lua_isstring(L, -1)) p.name = lua_tostring(L, -1);
                lua_pop(L, 1);

                lua_getfield(L, -1, "type");
                if (lua_isstring(L, -1)) p.type = lua_tostring(L, -1);
                else p.type = "string";
                lua_pop(L, 1);

                lua_getfield(L, -1, "description");
                if (lua_isstring(L, -1)) p.description = lua_tostring(L, -1);
                lua_pop(L, 1);

                lua_getfield(L, -1, "required");
                p.required = lua_toboolean(L, -1) != 0;
                lua_pop(L, 1);

                params.push_back(std::move(p));
            }
            lua_pop(L, 1); // pop params[i]
        }
    }
    lua_pop(L, 1); // pop parameters table

    // Parse result mode
    ToolResultMode resultMode = ToolResultMode::deliver_to_model;
    std::string modeStr(resultModeStr);
    if (modeStr == "stream_to_user") resultMode = ToolResultMode::stream_to_user;
    else if (modeStr == "deliver_to_model") resultMode = ToolResultMode::deliver_to_model;

    // Get session_types if provided
    lua_getfield(L, 1, "session_types");
    std::vector<std::string> sessionTypes;
    if (lua_istable(L, -1)) {
        lua_len(L, -1);
        int n = luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        for (int i = 1; i <= n; ++i) {
            lua_geti(L, -1, i);
            if (lua_isstring(L, -1)) {
                sessionTypes.push_back(lua_tostring(L, -1));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1); // pop session_types

    // Determine registration type — always "tool" for LuaRegisterTool
    std::string pluginType = "tool";

    // Create the LuaToolHandler and register it
    auto handler = std::make_unique<LuaToolHandler>(
        name, description, params, resultMode, sessionTypes,
        handlerRef, g_activeState
    );

    ToolRegistry& registry = g_activeState->GetToolRegistry();
    registry.Register(std::move(handler));

    // Record the plugin info
    LuaPluginInfo info;
    info.id = name;
    info.name = description;
    info.type = pluginType;
    info.handlerRef = handlerRef;
    info.agentId = g_activeState->GetConfig("agentId");
    g_activeState->GetRegisteredPluginsMutable().push_back(std::move(info));

    // Return the tool name as confirmation
    lua_pushstring(L, name);
    return 1;
}

// Helper: parse action schemas from a Lua table.
// Schema format: { actionName = { { name=, type=, required= }, ... }, ... }
std::unordered_map<std::string, std::vector<ToolParameter>> ParseActionSchemas(lua_State* L, int schemaIdx) {
    std::unordered_map<std::string, std::vector<ToolParameter>> schemas;
    if (!lua_istable(L, schemaIdx)) return schemas;

    lua_pushnil(L);
    while (lua_next(L, schemaIdx) != 0) {
        if (lua_isstring(L, -2) && lua_istable(L, -1)) {
            std::string action = lua_tostring(L, -2);
            std::vector<ToolParameter> params;
            lua_len(L, -1);
            int n = luaL_checkinteger(L, -1);
            lua_pop(L, 1);
            for (int i = 1; i <= n; ++i) {
                lua_geti(L, -1, i);
                if (lua_istable(L, -1)) {
                    ToolParameter p;
                    lua_getfield(L, -1, "name");
                    if (lua_isstring(L, -1)) p.name = lua_tostring(L, -1);
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "type");
                    if (lua_isstring(L, -1)) p.type = lua_tostring(L, -1);
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "required");
                    p.required = lua_toboolean(L, -1);
                    lua_pop(L, 1);
                    lua_getfield(L, -1, "description");
                    if (lua_isstring(L, -1)) p.description = lua_tostring(L, -1);
                    lua_pop(L, 1);
                    params.push_back(p);
                }
                lua_pop(L, 1); // pop params[i]
            }
            schemas[action] = params;
        }
        lua_pop(L, 1); // pop value, keep key
    }
    return schemas;
}

// Helper: parse a string array from a Lua table field.
std::vector<std::string> ParseStringArray(lua_State* L, int tableIdx, const char* fieldName) {
    std::vector<std::string> result;
    lua_getfield(L, tableIdx, fieldName);
    if (lua_istable(L, -1)) {
        lua_len(L, -1);
        int n = luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        for (int i = 1; i <= n; ++i) {
            lua_geti(L, -1, i);
            if (lua_isstring(L, -1)) {
                result.push_back(lua_tostring(L, -1));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1); // pop field
    return result;
}

// --- animus.register_interface ---
// Expected: animus.register_interface({
//     id = "signal",
//     name = "Signal Messenger",
//     actions = {"send", "list_contacts"},
//     schema = { send = { { name="to", type="string", required=true } } },
//     handler = function(args) ... end
// })
int LuaRegisterInterface(lua_State* L) {
    if (g_activeState == nullptr) {
        return luaL_error(L, "no active LuaState for registration");
    }
    luaL_checktype(L, 1, LUA_TTABLE);

    // Extract required fields
    lua_getfield(L, 1, "id");
    const char* pluginId = luaL_checkstring(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "name");
    const char* pluginName = lua_tostring(L, -1);
    if (!pluginName) pluginName = pluginId; // default to id
    lua_pop(L, 1);

    lua_getfield(L, 1, "handler");
    if (!lua_isfunction(L, -1)) {
        return luaL_error(L, "register_interface: 'handler' must be a function");
    }
    int handlerRef = luaL_ref(L, LUA_REGISTRYINDEX);

    // Parse actions and schema
    auto actions = ParseStringArray(L, 1, "actions");

    lua_getfield(L, 1, "schema");
    auto actionSchemas = ParseActionSchemas(L, lua_absindex(L, -1));
    lua_pop(L, 1); // pop schema

    // Build a flat parameter list for the tool definition
    std::vector<ToolParameter> params;
    params.push_back({"action", "string", "Action to perform", true});
    params.push_back({"interface_id", "string", "Interface plugin ID", false});

    // Build the tool name from the plugin id
    std::string toolName = std::string("interface_") + pluginId;

    // Register as a LuaToolHandler (individual tool)
    auto handler = std::make_unique<LuaToolHandler>(
        toolName,
        std::string("Interface: ") + pluginName,
        params,
        ToolResultMode::deliver_to_model,
        std::vector<std::string>{},
        handlerRef,
        g_activeState
    );
    g_activeState->GetToolRegistry().Register(std::move(handler));

    // Record the plugin info with composite metadata
    LuaPluginInfo info;
    info.id = pluginId;
    info.name = pluginName;
    info.type = "interface";
    info.handlerRef = handlerRef;
    info.agentId = g_activeState->GetConfig("agentId");
    info.actions = actions;
    info.actionSchemas = actionSchemas;
    info.handler = std::move(handler);
    g_activeState->GetRegisteredPluginsMutable().push_back(std::move(info));

    lua_pushstring(L, pluginId);
    return 1;
}

// --- animus.register_social ---
// Expected: animus.register_social({
//     id = "bluesky",
//     name = "Bluesky",
//     capabilities = {"read", "write", "search"},
//     actions = { post = { { name="content", type="string", required=true } } },
//     handler = function(args) ... end
// })
int LuaRegisterSocial(lua_State* L) {
    if (g_activeState == nullptr) {
        return luaL_error(L, "no active LuaState for registration");
    }
    luaL_checktype(L, 1, LUA_TTABLE);

    // Extract required fields
    lua_getfield(L, 1, "id");
    const char* pluginId = luaL_checkstring(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "name");
    const char* pluginName = lua_tostring(L, -1);
    if (!pluginName) pluginName = pluginId;
    lua_pop(L, 1);

    lua_getfield(L, 1, "handler");
    if (!lua_isfunction(L, -1)) {
        return luaL_error(L, "register_social: 'handler' must be a function");
    }
    int handlerRef = luaL_ref(L, LUA_REGISTRYINDEX);

    // Parse capabilities, actions, and schema
    auto capabilities = ParseStringArray(L, 1, "capabilities");
    auto actions = ParseStringArray(L, 1, "actions");

    lua_getfield(L, 1, "schema");
    auto actionSchemas = ParseActionSchemas(L, lua_absindex(L, -1));
    lua_pop(L, 1);

    // Build parameter list
    std::vector<ToolParameter> params;
    params.push_back({"action", "string", "Action to perform", true});
    params.push_back({"social_id", "string", "Social plugin ID", false});

    // Build the tool name (for internal reference, NOT registered in ToolRegistry)
    std::string toolName = std::string("social_") + pluginId;

    // Create LuaToolHandler but store it in plugin info — NOT in ToolRegistry.
    // The SocialTool composite will route calls through it directly.
    // This prevents "social_bluesky", "social_vk" etc. from appearing as
    // separate agent-visible tools.
    auto handler = std::make_unique<LuaToolHandler>(
        toolName,
        std::string("Social: ") + pluginName,
        params,
        ToolResultMode::deliver_to_model,
        std::vector<std::string>{},
        handlerRef,
        g_activeState
    );

    // Record plugin info with composite metadata
    LuaPluginInfo info;
    info.id = pluginId;
    info.name = pluginName;
    info.type = "social";
    info.handlerRef = handlerRef;
    info.agentId = g_activeState->GetConfig("agentId");
    info.actions = actions;
    info.capabilities = capabilities;
    info.actionSchemas = actionSchemas;
    info.handler = std::move(handler);
    g_activeState->GetRegisteredPluginsMutable().push_back(std::move(info));

    lua_pushstring(L, pluginId);
    return 1;
}

// --- json.encode / json.decode ---

int LuaJsonEncode(lua_State* L) {
    if (lua_gettop(L) != 1) {
        return luaL_error(L, "json.encode expects exactly 1 argument");
    }
    Json::Value v = LuaToJson(L, 1);
    std::string s = JsonToString(v);
    lua_pushstring(L, s.c_str());
    return 1;
}

int LuaJsonDecode(lua_State* L) {
    if (lua_gettop(L) != 1 || lua_type(L, 1) != LUA_TSTRING) {
        return luaL_error(L, "json.decode expects a string argument");
    }
    std::string s = lua_tostring(L, 1);
    Json::Value v = ParseJson(s);
    if (v.isNull()) {
        return luaL_error(L, "json.decode: invalid JSON");
    }
    PushJsonValue(L, v);
    return 1;
}

// --- log.info / log.warn / log.error ---

int LuaLogInfo(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    std::cerr << "[lua:info] " << (g_activeState ? "agent=" + g_activeState->GetConfig("agentId") + " " : "") << msg << "\n";
    return 0;
}

int LuaLogWarn(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    std::cerr << "[lua:warn] " << (g_activeState ? "agent=" + g_activeState->GetConfig("agentId") + " " : "") << msg << "\n";
    return 0;
}

int LuaLogError(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    std::cerr << "[lua:error] " << (g_activeState ? "agent=" + g_activeState->GetConfig("agentId") + " " : "") << msg << "\n";
    return 0;
}

// --- config.get / config.set ---

int LuaConfigGet(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    std::string value = g_activeState ? g_activeState->GetConfig(key) : "";
    lua_pushstring(L, value.c_str());
    return 1;
}

int LuaConfigSet(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    const char* value = luaL_checkstring(L, 2);
    if (g_activeState) {
        g_activeState->SetConfig(key, value);
    }
    return 0;
}

// --- Instruction count hook ---

void InstructionHook(lua_State* L, lua_Debug* /*ar*/) {
    // This hook fires when the instruction count limit is reached.
    // We set it via lua_sethook with LUA_MASKCOUNT, so when this fires,
    // the script has exceeded its instruction budget.
    luaL_error(L, "execution limit exceeded");
}

} // anonymous namespace

// ============================================================================
// LuaState implementation
// ============================================================================

LuaState::LuaState(const Config& config, ToolRegistry& tools, HttpClient& http)
    : m_config(config)
    , m_tools(tools)
    , m_http(http)
{
    // Allocate the allocator context on the heap so it outlives the lua_State
    m_allocCtx = new AllocCtx{};
    m_allocCtx->maxBytes = config.memoryLimitBytes;

    m_L = lua_newstate(LimitedAllocator, m_allocCtx);
    if (!m_L) {
        delete m_allocCtx;
        m_allocCtx = nullptr;
        throw LuaException("failed to create Lua state (memory limit too low?)");
    }

    // Load safe standard libraries (no io, os, debug, etc.)
    // We load the base libraries then remove unsafe functions.
    luaL_openlibs(m_L);

    // Seed agent config from the Config struct
    m_agentConfig["agentId"] = config.agentId;
    m_configStore = config.configStore;

    // Warm the persistent store cache for this agent
    if (m_configStore) {
        m_configStore->WarmCache(config.agentId);
    }

    SetupSandbox();
    SetupToolBridge();
    SetupHttpClient();
    SetupPluginRegistration();
    SetupConfigAccess();
    SetupJsonUtil();
    SetupLogging();

    // Set instruction count limit
    if (m_config.instructionLimit > 0) {
        lua_sethook(m_L, InstructionHook, LUA_MASKCOUNT,
                    static_cast<int>(m_config.instructionLimit));
    }
}

LuaState::~LuaState() {
    if (m_L) {
        // Clean up registry references
        for (int ref : m_registryRefs) {
            luaL_unref(m_L, LUA_REGISTRYINDEX, ref);
        }
        lua_close(m_L);
    }
    delete m_allocCtx;
    m_allocCtx = nullptr;
}

std::string LuaState::Eval(const std::string& script, const std::string& agentId) {
    g_activeState = this;
    m_coroutineMode = false;

    // Set instruction count hook for timeout
    if (m_config.instructionLimit > 0) {
        lua_sethook(m_L, InstructionHook, LUA_MASKCOUNT,
                    static_cast<int>(m_config.instructionLimit));
    }

    int status = luaL_loadstring(m_L, script.c_str());
    if (status != LUA_OK) {
        std::string err = lua_tostring(m_L, -1);
        lua_pop(m_L, 1);
        g_activeState = nullptr;
        throw LuaException("Lua load error: " + err);
    }

    status = lua_pcall(m_L, 0, 1, 0);
    if (status != LUA_OK) {
        std::string err = lua_tostring(m_L, -1);
        lua_pop(m_L, 1);
        g_activeState = nullptr;
        throw LuaException("Lua runtime error: " + err);
    }

    // Convert result to JSON string
    std::string result;
    if (lua_gettop(m_L) > 0 && !lua_isnil(m_L, -1)) {
        Json::Value jsonResult = LuaToJson(m_L, -1);
        result = JsonToString(jsonResult);
    } else {
        result = "{\"status\":200,\"data\":{\"result\":\"ok\"}}";
    }
    lua_pop(m_L, lua_gettop(m_L));

    g_activeState = nullptr;
    return result;
}

std::string LuaState::EvalCoroutine(const std::string& script,
                                      const std::string& agentId,
                                      ToolCallCallback onToolCall) {
    g_activeState = this;
    m_coroutineMode = true;

    // Create a new coroutine (Lua thread)
    lua_State* co = lua_newthread(m_L);
    if (!co) {
        m_coroutineMode = false;
        g_activeState = nullptr;
        throw LuaException("failed to create Lua coroutine");
    }

    // Set instruction count hook on the coroutine
    if (m_config.instructionLimit > 0) {
        lua_sethook(co, InstructionHook, LUA_MASKCOUNT,
                    static_cast<int>(m_config.instructionLimit));
    }

    // Load the script onto the coroutine's stack
    int status = luaL_loadstring(co, script.c_str());
    if (status != LUA_OK) {
        std::string err = lua_tostring(co, -1);
        lua_pop(co, 1);
        lua_pop(m_L, 1); // pop the thread
        m_coroutineMode = false;
        g_activeState = nullptr;
        throw LuaException("Lua load error: " + err);
    }

    // Run the coroutine — initial resume with 0 arguments
    int nArgs = 0;
    int nResults = 0;
    while (true) {
        status = lua_resume(co, nullptr, nArgs, &nResults);

        if (status == LUA_OK) {
            // Coroutine completed normally
            break;
        }

        if (status == LUA_YIELD) {
            // Coroutine yielded — extract the tool call info
            if (lua_gettop(co) < 1) {
                lua_pop(m_L, 1); // pop the thread
                m_coroutineMode = false;
                g_activeState = nullptr;
                throw LuaException("coroutine yielded with no value");
            }

            // Check if this is a tool call yield
            lua_getfield(co, -1, "__type");
            std::string yieldType = lua_isstring(co, -1) ? lua_tostring(co, -1) : "";
            lua_pop(co, 1);

            if (yieldType == "__yield_tool_call") {
                // Extract tool call fields
                lua_getfield(co, -1, "id");
                std::string callId = lua_tostring(co, -1);
                lua_pop(co, 1);

                lua_getfield(co, -1, "name");
                std::string toolName = lua_tostring(co, -1);
                lua_pop(co, 1);

                lua_getfield(co, -1, "arguments");
                std::string args = lua_tostring(co, -1);
                lua_pop(co, 1);

                lua_pop(co, 1); // pop the yield table

                // Invoke the callback
                ToolCall call;
                call.id = callId;
                call.name = toolName;
                call.arguments = args;

                ToolResult result = onToolCall(call);

                // Push the result back for the coroutine to receive
                if (!result.success) {
                    lua_pushnil(co);
                    lua_pushstring(co, result.error.c_str());
                    nArgs = 2; // return nil, error
                } else {
                    Json::Value outputJson = ParseJson(result.output);
                    if (!outputJson.isNull()) {
                        PushJsonValue(co, outputJson);
                    } else {
                        lua_pushstring(co, result.output.c_str());
                    }
                    nArgs = 1;
                }
            } else {
                // Unknown yield — treat as final result
                break;
            }
        } else {
            // Error during coroutine execution
            std::string err = lua_tostring(co, -1);
            lua_pop(co, 1);
            lua_pop(m_L, 1); // pop the thread
            m_coroutineMode = false;
            g_activeState = nullptr;
            throw LuaException("Lua coroutine error: " + err);
        }
    }

    // Extract the final result from the coroutine
    std::string result;
    if (lua_gettop(co) > 0 && !lua_isnil(co, -1)) {
        Json::Value jsonResult = LuaToJson(co, -1);
        result = JsonToString(jsonResult);
    } else {
        result = "{\"status\":200,\"data\":{\"result\":\"ok\"}}";
    }

    lua_pop(m_L, 1); // pop the thread
    m_coroutineMode = false;
    g_activeState = nullptr;
    return result;
}

std::string LuaState::RunStored(const std::string& name, const std::string& agentId) {
    auto it = m_storedScripts.find(name);
    if (it == m_storedScripts.end()) {
        throw LuaException("script not found: " + name);
    }
    return Eval(it->second, agentId);
}

const std::vector<LuaPluginInfo>& LuaState::GetRegisteredPlugins() const {
    return m_plugins;
}

const LuaPluginInfo* LuaState::FindPlugin(const std::string& id, const std::string& type) const {
    for (const auto& p : m_plugins) {
        if (p.id == id && p.type == type) return &p;
    }
    return nullptr;
}

void LuaState::StoreScript(const std::string& name, const std::string& source) {
    m_storedScripts[name] = source;
}

std::string LuaState::GetScript(const std::string& name) const {
    auto it = m_storedScripts.find(name);
    return (it != m_storedScripts.end()) ? it->second : "";
}

std::vector<std::string> LuaState::ListScripts() const {
    std::vector<std::string> names;
    names.reserve(m_storedScripts.size());
    for (const auto& [name, _] : m_storedScripts) {
        names.push_back(name);
    }
    return names;
}

bool LuaState::DeleteScript(const std::string& name) {
    return m_storedScripts.erase(name) > 0;
}

bool LuaState::PushRegistryRef(int ref) {
    if (ref == LUA_NOREF || ref == LUA_REFNIL) return false;
    lua_rawgeti(m_L, LUA_REGISTRYINDEX, ref);
    if (lua_isnil(m_L, -1)) {
        lua_pop(m_L, 1);
        return false;
    }
    return true;
}

std::string LuaState::GetConfig(const std::string& key) const {
    if (m_configStore) {
        return m_configStore->Get(m_config.agentId, key);
    }
    auto it = m_agentConfig.find(key);
    return (it != m_agentConfig.end()) ? it->second : "";
}

void LuaState::SetConfig(const std::string& key, const std::string& value) {
    if (m_configStore) {
        m_configStore->Set(m_config.agentId, key, value);
    } else {
        m_agentConfig[key] = value;
    }
}

std::vector<std::string> LuaState::ListConfigKeys() const {
    if (m_configStore) {
        return m_configStore->ListKeys(m_config.agentId);
    }
    std::vector<std::string> keys;
    keys.reserve(m_agentConfig.size());
    for (const auto& [k, _] : m_agentConfig) {
        keys.push_back(k);
    }
    return keys;
}

// ============================================================================
// Sandbox setup
// ============================================================================

void LuaState::SetupSandbox() {
    // Remove unsafe globals
    // The following are removed from the sandbox:
    // io, os (except os.clock and os.time), dofile, loadfile, require, debug,
    // load (with chunk argument), print (replaced with logging)

    lua_pushnil(m_L);
    lua_setglobal(m_L, "io");

    // Remove os.execute, os.getenv, os.exit, os.setlocale, os.rename, os.remove,
    // os.tmpname, os.clock (allow os.time and os.date for timestamps)
    lua_getglobal(m_L, "os");
    if (lua_istable(m_L, -1)) {
        // Create a safe os table with only time, date, clock, difftime
        // Stack after lua_newtable: [os, safe_os]  -2=os, -1=safe_os
        lua_newtable(m_L); // safe os

        lua_getfield(m_L, -2, "time");
        lua_setfield(m_L, -2, "time");      // safe_os.time = os.time

        lua_getfield(m_L, -2, "date");
        lua_setfield(m_L, -2, "date");      // safe_os.date = os.date

        lua_getfield(m_L, -2, "clock");
        lua_setfield(m_L, -2, "clock");     // safe_os.clock = os.clock

        lua_getfield(m_L, -2, "difftime");
        lua_setfield(m_L, -2, "difftime");  // safe_os.difftime = os.difftime

        // Replace global os with safe version
        lua_setglobal(m_L, "os");           // pops safe_os, sets as global 'os'
        lua_pop(m_L, 1);                    // pop original os table
    } else {
        lua_pop(m_L, 1);
    }

    // Remove dangerous functions
    lua_pushnil(m_L); lua_setglobal(m_L, "dofile");
    lua_pushnil(m_L); lua_setglobal(m_L, "loadfile");
    lua_pushnil(m_L); lua_setglobal(m_L, "require");
    lua_pushnil(m_L); lua_setglobal(m_L, "debug");

    // Remove load function (dangerous — allows arbitrary code construction)
    lua_pushnil(m_L); lua_setglobal(m_L, "load");

    // Override print to redirect to logging
    lua_pushcfunction(m_L, LuaLogInfo);
    lua_setglobal(m_L, "print");
}

void LuaState::SetupToolBridge() {
    // Create the 'tools' proxy table with a __index metamethod.
    // When Lua accesses tools.X, __index returns a callable proxy for tool X.
    // When Lua calls that proxy (tools.X{...}), __call marshals the call
    // through ToolRegistry::Execute.

    lua_newtable(m_L); // tools table

    // Create metatable with __index
    lua_newtable(m_L); // metatable
    lua_pushcfunction(m_L, LuaToolsIndex);
    lua_setfield(m_L, -2, "__index"); // meta.__index = LuaToolsIndex
    lua_setmetatable(m_L, -2); // setmetatable(tools, meta)

    lua_setglobal(m_L, "tools");
}

void LuaState::SetupHttpClient() {
    // animus.http_get and animus.http_post
    // These will use HttpClient internally with SSRF protection

    lua_newtable(m_L); // animus table (will also hold register_*)

    // http_get
    lua_pushcfunction(m_L, LuaHttpGet);
    lua_setfield(m_L, -2, "http_get");

    // http_post
    lua_pushcfunction(m_L, LuaHttpPost);
    lua_setfield(m_L, -2, "http_post");

    // http_delete
    lua_pushcfunction(m_L, LuaHttpDelete);
    lua_setfield(m_L, -2, "http_delete");

    // http_put
    lua_pushcfunction(m_L, LuaHttpPut);
    lua_setfield(m_L, -2, "http_put");

    // http_patch
    lua_pushcfunction(m_L, LuaHttpPatch);
    lua_setfield(m_L, -2, "http_patch");

    // Store the animus table on the registry for later addition of register_*
    int animusRef = luaL_ref(m_L, LUA_REGISTRYINDEX);
    m_registryRefs.push_back(animusRef);

    // Push back and set as global
    lua_rawgeti(m_L, LUA_REGISTRYINDEX, animusRef);
    lua_setglobal(m_L, "animus");
}

void LuaState::SetupPluginRegistration() {
    // animus.register_tool, animus.register_interface, animus.register_social
    // These are C functions that create LuaToolHandler entries and register
    // them in ToolRegistry.

    lua_getglobal(m_L, "animus");

    lua_pushcfunction(m_L, LuaRegisterTool);
    lua_setfield(m_L, -2, "register_tool");

    lua_pushcfunction(m_L, LuaRegisterInterface);
    lua_setfield(m_L, -2, "register_interface");

    lua_pushcfunction(m_L, LuaRegisterSocial);
    lua_setfield(m_L, -2, "register_social");
    lua_pushcfunction(m_L, LuaRegisterSocial);
    lua_setfield(m_L, -2, "register_channel");

    lua_pop(m_L, 1); // pop animus table
}

void LuaState::SetupConfigAccess() {
    // config.get and config.set are already registered as C functions
    lua_newtable(m_L); // config table

    lua_pushcfunction(m_L, LuaConfigGet);
    lua_setfield(m_L, -2, "get");

    lua_pushcfunction(m_L, LuaConfigSet);
    lua_setfield(m_L, -2, "set");

    lua_setglobal(m_L, "config");
}

void LuaState::SetupJsonUtil() {
    // json.encode and json.decode
    lua_newtable(m_L); // json table

    lua_pushcfunction(m_L, LuaJsonEncode);
    lua_setfield(m_L, -2, "encode");

    lua_pushcfunction(m_L, LuaJsonDecode);
    lua_setfield(m_L, -2, "decode");

    lua_setglobal(m_L, "json");
}

void LuaState::SetupLogging() {
    // log.info, log.warn, log.error
    lua_newtable(m_L); // log table

    lua_pushcfunction(m_L, LuaLogInfo);
    lua_setfield(m_L, -2, "info");

    lua_pushcfunction(m_L, LuaLogWarn);
    lua_setfield(m_L, -2, "warn");

    lua_pushcfunction(m_L, LuaLogError);
    lua_setfield(m_L, -2, "error");

    lua_setglobal(m_L, "log");
}

void LuaState::SetActiveState() const {
    g_activeState = const_cast<LuaState*>(this);
}

void LuaState::ClearActiveState() {
    g_activeState = nullptr;
}

} // namespace animus::kernel
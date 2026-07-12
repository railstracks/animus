#include "animus_kernel/lua/LuaToolHandler.h"

#include <json/json.h>
#include <json/writer.h>

#include <sstream>
#include <iostream>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace animus::kernel {

namespace {

Json::Value ParseArgs(const std::string& args) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::istringstream stream(args);
    std::string errors;
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        return {};
    }
    return root;
}

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
                lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
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
            Json::Value result;
            bool isArray = true;
            lua_Integer maxIndex = 0;

            lua_pushnil(L);
            while (lua_next(L, absIdx) != 0) {
                if (lua_type(L, -2) == LUA_TNUMBER && lua_isinteger(L, -2)) {
                    lua_Integer key = lua_tointeger(L, -2);
                    if (key < 1 || key > static_cast<lua_Integer>(1000000)) {
                        isArray = false;
                    }
                    if (key > maxIndex) maxIndex = key;
                } else {
                    isArray = false;
                }
                lua_pop(L, 1);
            }

            if (isArray && maxIndex > 0) {
                result = Json::Value(Json::arrayValue);
                result.resize(static_cast<Json::ArrayIndex>(maxIndex));
                lua_pushnil(L);
                while (lua_next(L, absIdx) != 0) {
                    lua_Integer key = lua_tointeger(L, -2);
                    result[static_cast<Json::ArrayIndex>(key - 1)] = LuaToJson(L, -1);
                    lua_pop(L, 1);
                }
            } else {
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

std::string JsonToString(const Json::Value& v) {
    Json::StreamWriterBuilder wb;
    wb.settings_["indentation"] = "";
    return Json::writeString(wb, v);
}

} // anonymous namespace

// ============================================================================
// LuaToolHandler
// ============================================================================

LuaToolHandler::LuaToolHandler(const std::string& name,
                               const std::string& description,
                               const std::vector<ToolParameter>& parameters,
                               ToolResultMode resultMode,
                               const std::vector<std::string>& sessionTypes,
                               int handlerRef,
                               LuaState* luaState)
    : m_handlerRef(handlerRef)
    , m_luaState(luaState)
{
    m_definition.name = name;
    m_definition.description = description;
    m_definition.parameters = parameters;
    m_definition.resultMode = resultMode;
    m_definition.session_types = sessionTypes;
}

ToolDefinition LuaToolHandler::GetDefinition() const {
    return m_definition;
}

ToolResult LuaToolHandler::Execute(const ToolCall& call) {
    ToolResult result;
    result.call_id = call.id;

    if (!m_luaState) {
        result.success = false;
        result.error = "Lua state not available";
        return result;
    }

    lua_State* L = m_luaState->GetRawState();
    if (!L) {
        result.success = false;
        result.error = "Lua VM not available";
        return result;
    }

    // Push the handler function from the registry
    lua_rawgeti(L, LUA_REGISTRYINDEX, m_handlerRef);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        result.success = false;
        result.error = "Lua handler is not a function";
        return result;
    }

    // Parse the call arguments and push as a Lua table
    Json::Value args = ParseArgs(call.arguments);
    if (args.isNull()) {
        lua_pop(L, 1);
        result.success = false;
        result.error = "failed to parse tool call arguments";
        return result;
    }

    PushJsonValue(L, args);

    // Set g_activeState so config.get, animus.http_*, log.* work
    m_luaState->SetActiveState();

    // Call the Lua handler (1 arg, 1 return)
    int status = lua_pcall(L, 1, 1, 0);

    LuaState::ClearActiveState();
    if (status != LUA_OK) {
        std::string err = lua_tostring(L, -1);
        lua_pop(L, 1);
        result.success = false;
        result.error = "Lua handler error: " + err;
        return result;
    }

    // Convert Lua return value to JSON
    Json::Value returnVal = LuaToJson(L, -1);
    lua_pop(L, 1);

    if (returnVal.isObject() && returnVal.isMember("success")) {
        result.success = returnVal["success"].asBool();
        if (result.success) {
            result.output = returnVal.isMember("output")
                ? (returnVal["output"].isString()
                    ? returnVal["output"].asString()
                    : JsonToString(returnVal["output"]))
                : JsonToString(returnVal);
        } else {
            result.error = returnVal.isMember("error")
                ? returnVal["error"].asString()
                : "unknown Lua handler error";
        }
    } else {
        // Handler returned a plain value — treat as success
        result.success = true;
        result.output = JsonToString(returnVal);
    }

    return result;
}

ToolResultMode LuaToolHandler::GetResultMode() const {
    return m_definition.resultMode;
}

} // namespace animus::kernel
#include "animus_kernel/lua/LuaState.h"
#include "animus_kernel/tools/HttpClient.h"

#include <json/json.h>
#include <json/writer.h>
#include <json/reader.h>

#include <sstream>
#include <iostream>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace animus::kernel {

namespace {

// Thread-local pointer to the active LuaState for C callbacks
static thread_local LuaState* g_luaHttpState = nullptr;

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

/// Check if a URL points to a private/internal IP range (SSRF protection)
bool IsPrivateAddress(const std::string& host) {
    // Block common private IP ranges
    if (host == "localhost" || host == "127.0.0.1" || host == "::1") return true;
    if (host.substr(0, 8) == "192.168.") return true;
    if (host.substr(0, 3) == "10.") return true;
    if (host.substr(0, 7) == "172.16." ||
        host.substr(0, 7) == "172.17." ||
        host.substr(0, 7) == "172.18." ||
        host.substr(0, 7) == "172.19." ||
        host.substr(0, 7) == "172.20." ||
        host.substr(0, 7) == "172.21." ||
        host.substr(0, 7) == "172.22." ||
        host.substr(0, 7) == "172.23." ||
        host.substr(0, 7) == "172.24." ||
        host.substr(0, 7) == "172.25." ||
        host.substr(0, 7) == "172.26." ||
        host.substr(0, 7) == "172.27." ||
        host.substr(0, 7) == "172.28." ||
        host.substr(0, 7) == "172.29." ||
        host.substr(0, 7) == "172.30." ||
        host.substr(0, 7) == "172.31.") return true;
    if (host.substr(0, 5) == "169.2") return true; // 169.254.0.0/16 link-local
    return false;
}

/// Extract host from URL
std::string ExtractHost(const std::string& url) {
    // Simple extraction: find "://" then take up to first '/' or ':'
    auto protoEnd = url.find("://");
    if (protoEnd == std::string::npos) return "";

    std::string rest = url.substr(protoEnd + 3);
    auto hostEnd = rest.find('/');
    auto portEnd = rest.find(':');
    size_t end = std::min(hostEnd, portEnd);

    if (end == std::string::npos) {
        end = rest.size();
    }
    return rest.substr(0, end);
}

/// Lua C function: animus.http_get(url [, options_table])
int LuaHttpGet(lua_State* L) {
    if (lua_gettop(L) < 1) {
        return luaL_error(L, "animus.http_get expects at least 1 argument (url)");
    }

    const char* url = luaL_checkstring(L, 1);
    if (!url) {
        return luaL_error(L, "url must be a string");
    }

    // SSRF protection
    std::string host = ExtractHost(url);
    if (IsPrivateAddress(host)) {
        lua_pushstring(L, "SSRF protection: private addresses are blocked");
        lua_error(L);
        return 0; // unreachable
    }

    // For now, return a placeholder response.
    // Full HttpClient integration requires async HTTP which will be
    // implemented in a subsequent commit.
    lua_newtable(L);
    lua_pushinteger(L, 501);  // 501 Not Implemented
    lua_setfield(L, -2, "status");
    lua_pushstring(L, "HTTP client integration pending");
    lua_setfield(L, -2, "body");

    return 1;
}

/// Lua C function: animus.http_post(url, options_table)
int LuaHttpPost(lua_State* L) {
    if (lua_gettop(L) < 2) {
        return luaL_error(L, "animus.http_post expects 2 arguments (url, options)");
    }

    const char* url = luaL_checkstring(L, 1);
    if (!url) {
        return luaL_error(L, "url must be a string");
    }

    // SSRF protection
    std::string host = ExtractHost(url);
    if (IsPrivateAddress(host)) {
        lua_pushstring(L, "SSRF protection: private addresses are blocked");
        lua_error(L);
        return 0; // unreachable
    }

    lua_newtable(L);
    lua_pushinteger(L, 501);  // 501 Not Implemented
    lua_setfield(L, -2, "status");
    lua_pushstring(L, "HTTP client integration pending");
    lua_setfield(L, -2, "body");

    return 1;
}

} // anonymous namespace

// This file is intentionally minimal — the full HTTP integration
// will be connected to HttpClient in a follow-up. The SSRF protection
// and URL parsing logic is already in place.

} // namespace animus::kernel
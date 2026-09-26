#include "animus_kernel/api/ApiRuntime.h"

#include "animus_kernel/api/SecretsVault.h"

#include "animus_kernel/ApiPackageStore.h"
#include "animus_kernel/Log.h"
#include "animus_kernel/tools/HttpClient.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <functional>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <sys/stat.h>

namespace animus::kernel {

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

std::string JsonWrite(const Json::Value& v) {
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    b["commentStyle"] = "None";
    return Json::writeString(b, v);
}

bool ParseJsonText(const std::string& text, Json::Value& out, std::string& err) {
    Json::CharReaderBuilder b;
    std::istringstream iss(text);
    return Json::parseFromStream(b, iss, &out, &err);
}

signed char B64Table(unsigned char c) {
    static const std::array<signed char, 256> table = [] {
        std::array<signed char, 256> t{};
        t.fill(-1);
        const char* alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i)
            t[static_cast<unsigned char>(alpha[i])] = static_cast<signed char>(i);
        return t;
    }();
    return table[c];
}

std::string Base64DecodeStr(const std::string& in, bool& ok) {
    ok = false;
    std::string out;
    out.reserve(in.size() / 4 * 3);
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        signed char d = B64Table(c);
        if (d < 0) return {};
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    ok = true;
    return out;
}

std::string JsonNames(const std::vector<std::string>& names) {
    Json::Value arr(Json::arrayValue);
    for (const auto& n : names) arr.append(n);
    return JsonWrite(arr);
}

bool IsValidFileSlug(const std::string& name) {
    if (name.empty() || name.size() > 128) return false;
    if (name.front() == '.') return false;
    for (char c : name)
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-'))
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// Args validation (TOOL.md: type + required, unknown rejected)
// ---------------------------------------------------------------------------

bool ValidateArgs(const Json::Value& parameters,
                  const Json::Value& args,
                  std::vector<std::string>& secretArgNames,
                  std::string& error) {
    if (!args.isObject()) {
        error = "arguments must be a JSON object";
        return false;
    }
    for (const std::string& key : args.getMemberNames()) {
        if (!parameters.isMember(key)) {
            error = "unknown argument '" + key + "' (allowed: " +
                    JsonNames(parameters.getMemberNames()) + ")";
            return false;
        }
    }
    for (const std::string& key : parameters.getMemberNames()) {
        const Json::Value& def = parameters[key];
        const std::string type = def.get("type", Json::Value::nullSingleton()).asString();
        const bool required = def.get("required", Json::Value(false)).asBool();
        if (def.get("secret", Json::Value(false)).asBool()) secretArgNames.push_back(key);
        if (!args.isMember(key)) {
            if (required) {
                error = "missing required argument '" + key + "' (" + type + ") — parameters: " +
                        JsonWrite(parameters);
                return false;
            }
            continue;
        }
        const Json::Value& v = args[key];
        bool ok = false;
        if (type == "string") ok = v.isString();
        else if (type == "number") ok = v.isNumeric();
        else if (type == "integer") ok = v.isIntegral();
        else if (type == "boolean") ok = v.isBool();
        else if (type == "object") ok = v.isObject();
        else if (type == "array") ok = v.isArray();
        else ok = true;  // unknown declared type: pass through
        if (!ok) {
            error = "argument '" + key + "' must be of type " + type;
            return false;
        }
    }
    return true;
}

// State writes (SANDBOX.md: validated against state_schema, unknown keys and
// framework-reserved `_`-prefixed keys rejected from scripts).
bool ValidateStateWrite(const Json::Value& stateSchema, const std::string& key,
                        const Json::Value& value, std::string& error) {
    if (!key.empty() && key[0] == '_') {
        error = "key '" + key + "' is framework-reserved (leading underscore)";
        return false;
    }
    if (!stateSchema.isMember(key)) {
        error = "unknown state key '" + key + "' (declared: " +
                JsonNames(stateSchema.getMemberNames()) + ")";
        return false;
    }
    const std::string type =
        stateSchema[key].get("type", Json::Value::nullSingleton()).asString();
    bool ok = type == "string" ? value.isString()
             : type == "number" ? value.isNumeric()
             : type == "boolean" ? value.isBool()
             : true;
    if (!ok) {
        error = "state key '" + key + "' must be of type " + type;
        return false;
    }
    return true;
}

void CollectSecrets(const Json::Value& stateSchema, const Json::Value& state,
                    const std::vector<std::string>& secretArgNames, const Json::Value& args,
                    std::vector<std::string>& secretValues, std::set<std::string>& secretKeys) {
    if (stateSchema.isObject()) {
        for (const std::string& k : stateSchema.getMemberNames()) {
            if (stateSchema[k].get("secret", Json::Value(false)).asBool()) {
                secretKeys.insert("state." + k);
                if (state.isMember(k) && state[k].isString())
                    secretValues.push_back(state[k].asString());
            }
        }
    }
    for (const auto& k : secretArgNames) {
        if (args.isMember(k) && args[k].isString()) secretValues.push_back(args[k].asString());
    }
    // Drop empty and very short secrets — masking "" would corrupt output.
    secretValues.erase(std::remove_if(secretValues.begin(), secretValues.end(),
                                      [](const std::string& s) { return s.size() < 4; }),
                       secretValues.end());
}

std::string MaskedTemplate(const std::string& tmpl, const std::set<std::string>& secretKeys) {
    std::string out = tmpl;
    // Best-effort: replace resolved secret positions in the ORIGINAL template
    // are already literals like "{{state.token}}" — mask the path itself.
    for (const auto& k : secretKeys) {
        std::string from = "{{" + k + "}}";
        size_t pos;
        while ((pos = out.find(from)) != std::string::npos)
            out.replace(pos, from.size(), "{{***}}");
    }
    return out;
}

// ---------------------------------------------------------------------------
// Lua bridge — per-invocation context
// ---------------------------------------------------------------------------

struct BridgeContext {
    std::string packageName;
    std::string commandName;
    std::string packageId;
    int64_t filesQuotaBytes{256LL * 1024 * 1024};
    Json::Value state;         // live values (real; vault-resolved in memory)
    Json::Value persistState;  // storage view: secrets stripped (vault owns them)
    Json::Value stateSchema;
    SecretsVault* vault{nullptr};
    Json::Value args;
    std::vector<std::string> secretValues;
    std::set<std::string> secretKeys;
    std::vector<std::string> egressHosts;  // #25: package egress scope
    ApiPackageStore* store{nullptr};
    HttpClient* http{nullptr};
    ApiRuntime* runtime{nullptr};  // owns ExecuteScoped (per-hop redirect gate)
    std::string filesRoot;
    size_t fsReadCap{1024 * 1024};
    size_t stringTruncate{16 * 1024};
    int httpBudget{5};
    int httpUsed{0};
    std::string logPrefix;
};

BridgeContext* GetBridge(lua_State* L, int upvalueIndex) {
    return static_cast<BridgeContext*>(lua_touserdata(L, lua_upvalueindex(upvalueIndex)));
}

// --- Lua ⇄ JSON --------------------------------------------------------------

void PushJson(lua_State* L, const Json::Value& v);

Json::Value LuaToJson(lua_State* L, int idx) {
    idx = lua_absindex(L, idx);  // pushes below shift relative indices
    switch (lua_type(L, idx)) {
        case LUA_TNIL: return Json::Value();
        case LUA_TBOOLEAN: return Json::Value(lua_toboolean(L, idx) != 0);
        case LUA_TNUMBER: {
            double d = lua_tonumber(L, idx);
            if (std::floor(d) == d && std::abs(d) < 9.0e15)
                return Json::Value(static_cast<Json::Int64>(d));
            return Json::Value(d);
        }
        case LUA_TSTRING: return Json::Value(std::string(lua_tostring(L, idx)));
        case LUA_TTABLE: {
            Json::Value out;
            const bool hasMeta = (lua_getmetatable(L, idx) != 0);
            if (hasMeta) lua_pop(L, 1);  // metatable (or nil) must not leak
            const bool asArray = !hasMeta && [&] {
                // heuristic: table with only 1..n integer keys -> array
                int maxKey = 0, count = 0, nonInteger = 0;
                lua_pushnil(L);
                while (lua_next(L, idx) != 0) {
                    if (lua_isinteger(L, -2)) {
                        int k = static_cast<int>(lua_tointeger(L, -2));
                        if (k > maxKey) maxKey = k;
                    } else {
                        nonInteger++;
                    }
                    count++;
                    lua_pop(L, 1);
                }
                (void)count;
                return nonInteger == 0 && maxKey > 0;
            }();
            if (asArray) {
                out = Json::Value(Json::arrayValue);
                const int n = static_cast<int>(lua_rawlen(L, idx));
                for (int i = 1; i <= n; ++i) {
                    lua_rawgeti(L, idx, i);
                    out.append(LuaToJson(L, -1));
                    lua_pop(L, 1);
                }
            } else {
                out = Json::Value(Json::objectValue);
                lua_pushnil(L);
                while (lua_next(L, idx) != 0) {
                    std::string key;
                    if (lua_isnumber(L, -2))
                        key = JsonWrite(Json::Value(lua_tonumber(L, -2)));
                    else
                        key = lua_tostring(L, -2);
                    out[key] = LuaToJson(L, -1);
                    lua_pop(L, 1);
                }
            }
            return out;
        }
        default: return Json::Value();
    }
}

void PushJson(lua_State* L, const Json::Value& v) {
    switch (v.type()) {
        case Json::nullValue: lua_pushnil(L); break;
        case Json::booleanValue: lua_pushboolean(L, v.asBool()); break;
        case Json::intValue:
        case Json::uintValue:
            lua_pushinteger(L, static_cast<lua_Integer>(v.asInt64()));
            break;
        case Json::realValue: lua_pushnumber(L, v.asDouble()); break;
        case Json::stringValue: lua_pushstring(L, v.asCString()); break;
        case Json::arrayValue: {
            lua_newtable(L);
            int i = 1;
            for (const auto& e : v) {
                PushJson(L, e);
                lua_rawseti(L, -2, i++);
            }
            break;
        }
        case Json::objectValue: {
            lua_newtable(L);
            for (const std::string& k : v.getMemberNames()) {
                PushJson(L, v[k]);
                lua_setfield(L, -2, k.c_str());
            }
            break;
        }
        default: lua_pushnil(L);
    }
}

// --- closures -----------------------------------------------------------------

int CtxLog(lua_State* L) {
    BridgeContext* bc = GetBridge(L, 1);
    std::ostringstream oss;
    int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        if (i > 1) oss << " ";
        if (lua_isstring(L, i)) oss << lua_tostring(L, i);
        else oss << JsonWrite(LuaToJson(L, i));
    }
    ALOG_INFO("api", "[" << bc->logPrefix << "] " << ApiRuntime::MaskSecrets(oss.str(), bc->secretValues));
    return 0;
}

int CtxGetState(lua_State* L) {
    BridgeContext* bc = GetBridge(L, 1);
    const char* k = luaL_checkstring(L, 1);
    if (!bc->state.isMember(k)) { lua_pushnil(L); return 1; }
    PushJson(L, bc->state[k]);
    return 1;
}

int CtxSetState(lua_State* L) {
    BridgeContext* bc = GetBridge(L, 1);
    const char* k = luaL_checkstring(L, 1);
    Json::Value v = LuaToJson(L, 2);
    // #23: a {secret_ref = name} table is the indirection form for secret-typed
    // keys — ValidateStateWrite only knows scalars, so vet it here instead:
    // key must be declared AND secret-typed, ref name must be a valid vault name.
    std::string refName;
    const bool secretKey = bc->stateSchema.isObject() && bc->stateSchema.isMember(k) &&
                           bc->stateSchema[k].get("secret", false).asBool();
    const bool isRef = secretKey && SecretsVault::IsSecretRef(v, refName) &&
                       SecretsVault::IsValidSecretName(refName);
    std::string err;
    if (isRef) {
        if (!k[0] || k[0] == '_') {
            lua_pushnil(L);
            lua_pushstring(L, "key is framework-reserved (leading underscore)");
            return 2;
        }
    } else if (!ValidateStateWrite(bc->stateSchema, k, v, err)) {
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    if (secretKey) {
        if (isRef) {
            // Explicit indirection is config — persist the ref object itself.
            bc->persistState[k] = v;
            auto resolved = bc->vault ? bc->vault->Get(bc->packageId, refName) : std::nullopt;
            if (resolved) {
                bc->state[k] = *resolved;
                // Refs resolve to secret bytes — they join this invocation's
                // redaction set so they can't leak via results.
                bc->secretValues.push_back(*resolved);
            } else {
                bc->state.removeMember(k);
            }
        } else {
            std::string setErr;
            if (!bc->vault || !bc->vault->Set(bc->packageId, k, v.asString(), setErr)) {
                lua_pushnil(L);
                lua_pushstring(L, ("secret vault write failed: " + setErr).c_str());
                return 2;
            }
            bc->state[k] = v;               // resolved in-memory view
            bc->persistState.removeMember(k);  // storage view stays secret-free
            // Values written during THIS invocation join the redaction set —
            // a script must not be able to set a secret and echo it back out.
            bc->secretValues.push_back(v.asString());
        }
        // Ref objects persist; plain secret values never do. Only persist when
        // the storage view actually changed (indirection case).
        if (bc->store && bc->store->SetPackageState(bc->packageId, JsonWrite(bc->persistState))) {
            lua_pushboolean(L, 1);
            return 1;
        }
        lua_pushnil(L);
        lua_pushstring(L, "state persist failed");
        return 2;
    }
    bc->state[k] = v;
    bc->persistState[k] = v;
    if (bc->store && bc->store->SetPackageState(bc->packageId, JsonWrite(bc->persistState))) {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushnil(L);
    lua_pushstring(L, "state persist failed");
    return 2;
}

Json::Value DoHttp(BridgeContext* bc, const std::string& method, lua_State* L, int urlIdx,
                   int optsIdx) {
    Json::Value out(Json::objectValue);
    if (bc->httpUsed >= bc->httpBudget) {
        out["status"] = 0;
        out["error"] = "secondary http budget exhausted (" + std::to_string(bc->httpBudget) +
                       " calls per invocation)";
        return out;
    }
    const char* url = luaL_checkstring(L, urlIdx);
    // #25 egress gate for script-initiated fetches: same package scope as the
    // transport path. Returned as a normal http-result table (status 0 +
    // error) so scripts can handle it — but always audited.
    {
        std::string egressHost;
        if (!ApiRuntime::EgressAllowed(bc->egressHosts, url, egressHost)) {
            ALOG_WARNING("api", "[egress] DENIED " << bc->logPrefix << " -> "
                         << (egressHost.empty() ? "<malformed url>" : egressHost)
                         << " (sandbox fetch outside package scope)");
            Json::Value denied(Json::objectValue);
            denied["status"] = 0;
            denied["error"] = "egress denied: host '" +
                              (egressHost.empty() ? "<malformed>" : egressHost) +
                              "' is not in this package's egress allowlist";
            return denied;
        }
    }
    HttpClient::Request req;
    req.method = method;
    req.url = url;
    req.timeout_seconds = 30;
    if (lua_istable(L, optsIdx)) {
        // #119 opts.timeout_s — long-request APIs (image generation routinely
        // takes 20-45s; found field-testing the pixellab package). Clamp 1-300.
        lua_getfield(L, optsIdx, "timeout_s");
        if (lua_isnumber(L, -1)) {
            int t = static_cast<int>(lua_tonumber(L, -1));
            if (t >= 1 && t <= 300) req.timeout_seconds = t;
        }
        lua_pop(L, 1);
        lua_getfield(L, optsIdx, "headers");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                if (lua_isstring(L, -2) && lua_isstring(L, -1))
                    req.headers[lua_tostring(L, -2)] = lua_tostring(L, -1);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        lua_getfield(L, optsIdx, "body");
        if (lua_isstring(L, -1)) req.body = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    bc->httpUsed++;
    // #25: sandbox fetches follow redirects under the same per-hop scope
    // checks as the primary transport.
    HttpClient::Response resp =
        bc->http ? ApiRuntime::ExecuteScoped(bc->http, bc->egressHosts, req,
                                             bc->logPrefix + " (sandbox)")
                 : HttpClient::Response{};
    out["status"] = resp.status_code;
    {
        Json::Value headers(Json::objectValue);
        for (const auto& [k, v] : resp.headers) headers[k] = v;
        out["headers"] = headers;
    }
    out["body"] = resp.body;
    if (!resp.error.empty()) {
        out["error"] = resp.error;
    } else {
        Json::Value parsed;
        std::string jerr;
        std::istringstream bs(resp.body);
        Json::CharReaderBuilder rb;
        if (Json::parseFromStream(rb, bs, &parsed, &jerr)) out["json"] = parsed;
    }
    return out;
}

int CtxHttpGet(lua_State* L) { PushJson(L, DoHttp(GetBridge(L, 1), "GET", L, 1, 2)); return 1; }
int CtxHttpPost(lua_State* L) { PushJson(L, DoHttp(GetBridge(L, 1), "POST", L, 1, 2)); return 1; }
int CtxHttpPut(lua_State* L) { PushJson(L, DoHttp(GetBridge(L, 1), "PUT", L, 1, 2)); return 1; }
int CtxHttpDelete(lua_State* L) { PushJson(L, DoHttp(GetBridge(L, 1), "DELETE", L, 1, 2)); return 1; }

fs::path PackageRoot(const BridgeContext* bc) { return fs::path(bc->filesRoot) / bc->packageName; }

int64_t DirUsageBytes(const fs::path& dir) {
    int64_t total = 0;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
        if (it->is_regular_file(ec)) total += static_cast<int64_t>(it->file_size(ec));
    }
    return total;
}

int CtxFsWrite(lua_State* L) {
    BridgeContext* bc = GetBridge(L, 1);
    const char* name = luaL_checkstring(L, 1);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    if (!IsValidFileSlug(name)) {
        lua_pushnil(L);
        lua_pushstring(L, "invalid file name (slug chars only, no leading dot)");
        return 2;
    }
    std::error_code ec;
    fs::path root = PackageRoot(bc);
    fs::create_directories(root, ec);
    fs::path target = root / name;
    int64_t existing = fs::exists(target, ec) ? static_cast<int64_t>(fs::file_size(target, ec)) : 0;
    if (DirUsageBytes(root) - existing + static_cast<int64_t>(len) > bc->filesQuotaBytes) {
        lua_pushnil(L);
        lua_pushstring(L, "quota exceeded");
        return 2;
    }
    std::ofstream f(target, std::ios::binary | std::ios::trunc);
    if (!f) { lua_pushnil(L); lua_pushstring(L, "open failed"); return 2; }
    f.write(data, static_cast<std::streamsize>(len));
    if (!f.good()) { lua_pushnil(L); lua_pushstring(L, "write failed"); return 2; }
    lua_pushstring(L, fs::absolute(target).string().c_str());
    return 1;
}

int CtxFsRead(lua_State* L) {
    BridgeContext* bc = GetBridge(L, 1);
    const char* name = luaL_checkstring(L, 1);
    if (!IsValidFileSlug(name)) { lua_pushnil(L); return 1; }
    fs::path target = PackageRoot(bc) / name;
    std::error_code ec;
    if (!fs::is_regular_file(target, ec)) { lua_pushnil(L); return 1; }
    auto size = fs::file_size(target, ec);
    std::ifstream f(target, std::ios::binary);
    std::string out(std::min<size_t>(size, bc->fsReadCap), '\0');
    f.read(out.data(), static_cast<std::streamsize>(out.size()));
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int CtxFsDelete(lua_State* L) {
    BridgeContext* bc = GetBridge(L, 1);
    const char* name = luaL_checkstring(L, 1);
    if (!IsValidFileSlug(name)) { lua_pushboolean(L, 0); return 1; }
    std::error_code ec;
    lua_pushboolean(L, fs::remove(PackageRoot(bc) / name, ec) ? 1 : 0);
    return 1;
}

int CtxFsStat(lua_State* L) {
    BridgeContext* bc = GetBridge(L, 1);
    const char* name = luaL_checkstring(L, 1);
    if (!IsValidFileSlug(name)) { lua_pushnil(L); return 1; }
    struct stat st{};
    if (::stat((PackageRoot(bc) / name).c_str(), &st) != 0) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 2);
    lua_pushinteger(L, static_cast<lua_Integer>(st.st_size));
    lua_setfield(L, -2, "size");
    lua_pushinteger(L, static_cast<lua_Integer>(st.st_mtim.tv_sec * 1000));
    lua_setfield(L, -2, "mtime");
    return 1;
}

int CtxFsList(lua_State* L) {
    BridgeContext* bc = GetBridge(L, 1);
    lua_newtable(L);
    std::error_code ec;
    fs::path root = PackageRoot(bc);
    int i = 1;
    for (fs::directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        struct stat st{};
        if (::stat(it->path().c_str(), &st) != 0) continue;
        lua_createtable(L, 0, 3);
        lua_pushstring(L, it->path().filename().c_str());
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, static_cast<lua_Integer>(st.st_size));
        lua_setfield(L, -2, "size");
        lua_pushinteger(L, static_cast<lua_Integer>(st.st_mtim.tv_sec * 1000));
        lua_setfield(L, -2, "mtime");
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

int JsonEncode(lua_State* L) {
    Json::Value v = LuaToJson(L, 1);
    std::string s = JsonWrite(v);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int JsonDecodeSafe(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    Json::Value v;
    std::string err;
    if (!ParseJsonText(std::string(s, len), v, err)) {
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    PushJson(L, v);
    return 1;
}

int B64Encode(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    // inline base64 (same table as ChannelHelpers; kept local for binary safety)
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint8_t>(s[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint8_t>(s[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint8_t>(s[i + 2]);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += (i + 1 < len) ? table[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? table[n & 0x3F] : '=';
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int B64Decode(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    bool ok = false;
    std::string out = Base64DecodeStr(std::string(s, len), ok);
    if (!ok) { lua_pushnil(L); lua_pushstring(L, "invalid base64"); return 2; }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int OsTime(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(::time(nullptr)));
    return 1;
}

int OsDate(lua_State* L) {
    time_t t = ::time(nullptr);
    struct tm tmv{};
    gmtime_r(&t, &tmv);
    char buf[64];
    const char* fmt = luaL_optstring(L, 1, "%Y-%m-%dT%H:%M:%SZ");
    // support the common subset; anything else falls back to ISO
    if (std::strpbrk(fmt, "%aAbBcCxXeFgGHIjklmnOpPrRsStTuUvVwWyYzZ+") != nullptr &&
        std::strcmp(fmt, "%Y-%m-%dT%H:%M:%SZ") != 0 &&
        std::strcmp(fmt, "!%Y-%m-%dT%H:%M:%SZ") != 0) {
        fmt = "%Y-%m-%dT%H:%M:%SZ";
    }
    strftime(buf, sizeof(buf), fmt, &tmv);
    lua_pushstring(L, buf);
    return 1;
}

void HookInstructionCount(lua_State* L, lua_Debug*) {
    luaL_error(L, "instruction limit exceeded");
}

// Sandbox globals per SANDBOX.md. No io/require/loadfile/load/dofile/print.
void BuildSandboxGlobals(lua_State* L, BridgeContext* bc) {
    luaL_requiref(L, "_G", luaopen_base, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L, 1);

    // strip dangerous base entries
    const char* strip[] = {"dofile", "loadfile", "load", "require", "print", "collectgarbage"};
    for (const char* fn : strip) lua_pushnil(L), lua_setglobal(L, fn);

    // os: time/date only
    lua_newtable(L);
    lua_pushcfunction(L, OsTime);
    lua_setfield(L, -2, "time");
    lua_pushcfunction(L, OsDate);
    lua_setfield(L, -2, "date");
    lua_setglobal(L, "os");

    // json
    lua_newtable(L);
    lua_pushcfunction(L, JsonEncode);
    lua_setfield(L, -2, "encode");
    lua_pushcfunction(L, JsonDecodeSafe);
    lua_setfield(L, -2, "decode_safe");
    lua_setglobal(L, "json");

    // b64
    lua_newtable(L);
    lua_pushcfunction(L, B64Encode);
    lua_setfield(L, -2, "encode");
    lua_pushcfunction(L, B64Decode);
    lua_setfield(L, -2, "decode");
    lua_setglobal(L, "b64");

    (void)bc;
}

void BuildCtx(lua_State* L, BridgeContext* bc, const Json::Value& request, bool isHook,
              const Json::Value& eventJson) {
    lua_newtable(L);  // ctx

    // masked state copy (display/branching only; get_state returns real values).
    // Masks ANY present secret value regardless of JSON type — a stale or
    // hand-edited non-string under a secret key never reaches scripts raw.
    Json::Value masked = bc->state;
    for (const std::string& k : bc->state.getMemberNames()) {
        if (bc->secretKeys.count("state." + k) && !masked[k].isNull())
            masked[k] = "***";
    }

    // ctx.package (single build: name, masked state, get_state, set_state)
    lua_createtable(L, 0, 4);
    lua_pushstring(L, bc->packageName.c_str());
    lua_setfield(L, -2, "name");
    PushJson(L, masked);
    lua_setfield(L, -2, "state");
    lua_pushlightuserdata(L, bc);
    lua_pushcclosure(L, CtxGetState, 1);
    lua_setfield(L, -2, "get_state");
    lua_pushlightuserdata(L, bc);
    lua_pushcclosure(L, CtxSetState, 1);
    lua_setfield(L, -2, "set_state");
    lua_setfield(L, -2, "package");

    // ctx.args (actions only)
    if (!isHook) {
        PushJson(L, bc->args);
        lua_setfield(L, -2, "args");
    }

    // ctx.request (actions only, present iff declared)
    if (!isHook && !request.isNull()) {
        PushJson(L, request);
        lua_setfield(L, -2, "request");
    }

    // ctx.http
    lua_createtable(L, 0, 4);
    lua_pushlightuserdata(L, bc);
    lua_pushcclosure(L, CtxHttpGet, 1);
    lua_setfield(L, -2, "get");
    lua_pushlightuserdata(L, bc);
    lua_pushcclosure(L, CtxHttpPost, 1);
    lua_setfield(L, -2, "post");
    lua_pushlightuserdata(L, bc);
    lua_pushcclosure(L, CtxHttpPut, 1);
    lua_setfield(L, -2, "put");
    lua_pushlightuserdata(L, bc);
    lua_pushcclosure(L, CtxHttpDelete, 1);
    lua_setfield(L, -2, "delete");
    lua_setfield(L, -2, "http");

    // ctx.fs
    lua_createtable(L, 0, 5);
    lua_pushlightuserdata(L, bc);
    lua_pushcclosure(L, CtxFsWrite, 1);
    lua_setfield(L, -2, "write");
    lua_pushlightuserdata(L, bc);
    lua_pushcclosure(L, CtxFsRead, 1);
    lua_setfield(L, -2, "read");
    lua_pushlightuserdata(L, bc);
    lua_pushcclosure(L, CtxFsDelete, 1);
    lua_setfield(L, -2, "delete");
    lua_pushlightuserdata(L, bc);
    lua_pushcclosure(L, CtxFsStat, 1);
    lua_setfield(L, -2, "stat");
    lua_pushlightuserdata(L, bc);
    lua_pushcclosure(L, CtxFsList, 1);
    lua_setfield(L, -2, "list");
    lua_setfield(L, -2, "fs");

    // ctx.log
    lua_pushlightuserdata(L, bc);
    lua_pushcclosure(L, CtxLog, 1);
    lua_setfield(L, -2, "log");

    // ctx.now
    lua_pushinteger(
        L, static_cast<lua_Integer>(
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count()));
    lua_setfield(L, -2, "now");

    // ctx.event (hooks only)
    if (isHook) {
        PushJson(L, eventJson);
        lua_setfield(L, -2, "event");
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// ApiRuntime
// ---------------------------------------------------------------------------

ApiRuntime::ApiRuntime(ApiPackageStore* store, HttpClient* http, Config cfg,
                       SecretsVault* vault)
    : m_store(store), m_http(http), m_cfg(std::move(cfg)), m_vault(vault) {}

std::string ApiRuntime::Interpolate(const std::string& tmpl, const Json::Value& state,
                                    const Json::Value& args,
                                    const std::set<std::string>& secretStateKeys,
                                    std::string& error) {
    std::string out;
    out.reserve(tmpl.size());
    size_t i = 0;
    while (i < tmpl.size()) {
        size_t start = tmpl.find("{{", i);
        if (start == std::string::npos) {
            out.append(tmpl, i, std::string::npos);
            break;
        }
        out.append(tmpl, i, start - i);
        size_t end = tmpl.find("}}", start + 2);
        if (end == std::string::npos) {
            error = "unterminated {{ in template: " + MaskedTemplate(tmpl, secretStateKeys);
            return "";
        }
        std::string path = tmpl.substr(start + 2, end - start - 2);
        // trim
        while (!path.empty() && (path.front() == ' ')) path.erase(0, 1);
        while (!path.empty() && (path.back() == ' ')) path.pop_back();

        const Json::Value* root = nullptr;
        if (path.rfind("state.", 0) == 0) root = &state;
        else if (path.rfind("args.", 0) == 0) root = &args;
        if (!root) {
            error = "interpolation path '" + path + "' must start with state. or args. (template: " +
                    MaskedTemplate(tmpl, secretStateKeys) + ")";
            return "";
        }
        std::string rest = path.substr(path.find('.') + 1);
        const Json::Value* v = root;
        bool missing = false;
        while (!rest.empty()) {
            size_t dot = rest.find('.');
            std::string seg = (dot == std::string::npos) ? rest : rest.substr(0, dot);
            if (seg.empty() || !v->isObject() || !v->isMember(seg)) { missing = true; break; }
            v = &(*v)[seg];
            if (dot == std::string::npos) rest.clear();
            else rest.erase(0, dot + 1);
        }
        if (missing) {
            error = "missing key: " + path + " (template: " + MaskedTemplate(tmpl, secretStateKeys) + ")";
            return "";
        }
        if (v->isString()) out += v->asString();
        else if (v->isNumeric()) out += JsonWrite(*v);
        else if (v->isBool()) out += v->asBool() ? "true" : "false";
        else {
            error = "non-scalar value at " + path + " (template: " +
                    MaskedTemplate(tmpl, secretStateKeys) + ")";
            return "";
        }
        i = end + 2;
    }
    return out;
}

std::vector<std::string> ApiRuntime::ParseEgressHosts(const std::string& json) {
    std::vector<std::string> out;
    Json::Value arr;
    std::string err;
    if (!ParseJsonText(json.empty() ? "[]" : json, arr, err) || !arr.isArray()) return out;
    for (const auto& h : arr)
        if (h.isString() && !h.asString().empty()) out.push_back(h.asString());
    return out;
}

bool ApiRuntime::EgressAllowed(const std::vector<std::string>& hostPatterns,
                               const std::string& resolvedUrl, std::string& hostOut) {
    // Extract host from the resolved URL (no templating left here).
    const size_t sep = resolvedUrl.find("://");
    if (sep == std::string::npos) {
        hostOut = "";
        return false;  // not an absolute URL — deny
    }
    size_t hostStart = sep + 3;
    size_t hostEnd = hostStart;
    while (hostEnd < resolvedUrl.size() && resolvedUrl[hostEnd] != '/' &&
           resolvedUrl[hostEnd] != '?' && resolvedUrl[hostEnd] != '#')
        ++hostEnd;
    std::string host = resolvedUrl.substr(hostStart, hostEnd - hostStart);
    const size_t at = host.rfind('@');
    if (at != std::string::npos) host = host.substr(at + 1);
    const size_t colon = host.rfind(':');
    if (!host.empty() && host[0] != '[' && colon != std::string::npos)
        host = host.substr(0, colon);
    std::transform(host.begin(), host.end(), host.begin(), ::tolower);
    hostOut = host;
    // Empty scope = deny-all (script-only packages stay network-silent).
    for (const auto& pat : hostPatterns) {
        if (pat == host) return true;
        if (pat.substr(0, 2) == "*.") {
            const std::string suffix = pat.substr(1);  // ".example.com"
            if (host.size() > suffix.size() &&
                host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0)
                return true;
        }
    }
    return false;
}

std::string ApiRuntime::ResolveRedirectUrl(const std::string& requestUrl,
                                           const std::string& location) {
    if (location.empty()) return "";
    if (location.find("://") != std::string::npos) return location;
    const size_t sep = requestUrl.find("://");
    if (sep == std::string::npos) return "";
    const size_t pathStart = requestUrl.find('/', sep + 3);
    const std::string origin = pathStart == std::string::npos
                                   ? requestUrl
                                   : requestUrl.substr(0, pathStart);
    if (location[0] == '/') return origin + location;
    const std::string basePath = pathStart == std::string::npos
                                     ? "/"
                                     : requestUrl.substr(pathStart);
    if (location[0] == '?') {
        std::string base = basePath;
        const size_t q = base.find('?');
        if (q != std::string::npos) base = base.substr(0, q);
        return origin + base + location;
    }
    const size_t slash = basePath.rfind('/');
    return origin + basePath.substr(0, slash + 1) + location;
}

HttpClient::Response ApiRuntime::ExecuteScoped(HttpClient* http,
                                               const std::vector<std::string>& hostPatterns,
                                               HttpClient::Request req,
                                               const std::string& auditPrefix) {
    std::string auditChain;
    for (int hop = 0;; ++hop) {
        std::string host;
        if (!EgressAllowed(hostPatterns, req.url, host)) {
            ALOG_WARNING("api", "[egress] DENIED " << auditPrefix << " -> "
                         << (host.empty() ? "<malformed url>" : host) << auditChain
                         << " (outside package scope)");
            HttpClient::Response denied;
            denied.status_code = 0;
            denied.error = "egress denied: host '" +
                           (host.empty() ? "<malformed>" : host) +
                           "' is not in this package's egress allowlist" +
                           (hop > 0 ? " (redirect target)" : "");
            return denied;
        }
        auditChain += " -> " + host;
        HttpClient::Request hopReq = req;
        hopReq.follow_redirects = false;  // every hop is decided HERE, not by curl
        HttpClient::Response resp = http->Execute(hopReq);
        const int code = resp.status_code;
        const bool isRedirect = code == 301 || code == 302 || code == 303 ||
                                code == 307 || code == 308;
        if (resp.error.empty() && isRedirect) {
            std::string loc;
            for (const auto& [k, v] : resp.headers) {
                if (k == "Location" || k == "location") { loc = v; break; }
            }
            if (loc.empty()) return resp;  // malformed redirect: surface raw
            const std::string next = ResolveRedirectUrl(req.url, loc);
            if (next.empty()) return resp;
            if (hop + 1 > kMaxRedirectHops) {
                HttpClient::Response looped;
                looped.status_code = 0;
                looped.error = "redirect chain exceeded " +
                               std::to_string(kMaxRedirectHops) + " hops";
                return looped;
            }
            std::string nextHost;
            EgressAllowed(hostPatterns, next, nextHost);  // parse only; loop re-checks
            if (!nextHost.empty() && nextHost != host) {
                // never forward credentials across hosts
                req.headers.erase("Authorization");
                req.headers.erase("authorization");
                req.headers.erase("Proxy-Authorization");
                req.headers.erase("proxy-authorization");
                req.headers.erase("Cookie");
                req.headers.erase("cookie");
            }
            if (code == 303 ||
                ((code == 301 || code == 302) &&
                 (req.method == "POST" || req.method == "PUT" ||
                  req.method == "PATCH" || req.method == "DELETE"))) {
                req.method = "GET";
                req.body.clear();
            }
            req.url = next;
            continue;
        }
        return resp;
    }
}

std::string ApiRuntime::MaskSecrets(const std::string& text,
                                    const std::vector<std::string>& secretValues) {
    std::string out = text;
    for (const auto& s : secretValues) {
        if (s.size() < 4) continue;
        size_t pos = 0;
        while ((pos = out.find(s, pos)) != std::string::npos) {
            out.replace(pos, s.size(), "***");
            pos += 3;
        }
    }
    return out;
}

std::vector<ApiRuntime::CommandSummary> ApiRuntime::ListActions(
    const std::string& packageName, const std::string& agentId) const {
    std::vector<CommandSummary> out;
    auto pkg = m_store->GetPackageByName(packageName);
    if (!pkg || !m_store->EffectiveEnabled(pkg->id, agentId)) return out;
    for (const auto& c : m_store->ListCommands(pkg->id)) {
        if (c.kind != "action") continue;
        out.push_back({c.name, c.description, !c.request.empty()});
    }
    return out;
}

Json::Value ApiRuntime::ExecuteAction(const std::string& packageName,
                                      const std::string& commandName, const std::string& agentId,
                                      const Json::Value& args) {
    return ExecuteInternal(packageName, commandName, agentId, args, false, Json::Value());
}

Json::Value ApiRuntime::RunHook(const std::string& packageName, const std::string& commandName,
                                const std::string& agentId, const Json::Value& eventJson) {
    return ExecuteInternal(packageName, commandName, agentId, Json::Value(), true, eventJson);
}

Json::Value ApiRuntime::ExecuteInternal(const std::string& packageName,
                                        const std::string& commandName,
                                        const std::string& agentId, Json::Value args,
                                        bool isHook, const Json::Value& eventJson) {
    Json::Value fail(Json::objectValue);
    fail["success"] = false;

    auto err = [&](const std::string& msg) {
        fail["error"] = msg;
        ALOG_WARNING("api", "[" << packageName << "] " << msg);
        return fail;
    };

    auto pkg = m_store->GetPackageByName(packageName);
    if (!pkg) {
        std::string names;
        for (const auto& p : m_store->ListPackages()) names += (names.empty() ? "" : ", ") + p.name;
        return err("unknown package '" + packageName + "' (installed: " +
                   (names.empty() ? "none" : names) + ")");
    }
    if (!m_store->EffectiveEnabled(pkg->id, agentId))
        return err("package '" + packageName + "' is not enabled for this agent (api enable " +
                   packageName + ")");
    // #25 approval gate — the runtime backstop: unapproved packages execute
    // nothing (actions, hooks, sandbox), regardless of the enable flag. The
    // admin/tool enable refusals are UX; THIS check is the boundary.
    if (pkg->approval_status != "approved") {
        const std::string state = pkg->approval_status == "rejected"
                                      ? "was rejected by the owner"
                                      : (pkg->approval_status.empty()
                                             ? "is awaiting owner approval"
                                             : "is " + pkg->approval_status +
                                                   " (awaiting owner approval)");
        ALOG_WARNING("api", "[approval] DENIED execute " << packageName << ":"
                     << commandName << " (" << state << ")");
        return err("package '" + packageName + "' " + state +
                   " — execution blocked until the owner approves it via the admin API");
    }
    // #106 audit backstop: approval binds to content — independently verify
    // the stored content still matches approved_hash. Catches any mutation
    // path that skipped RefreshApproval (including direct DB writes);
    // mismatches self-heal to pending via RefreshApproval inside.
    if (!m_store->VerifyApprovalBinding(pkg->id))
        return err("package '" + packageName +
                   "' content changed since approval — returned to pending; the owner "
                   "must re-approve it (or the content must be restored to the "
                   "approved bytes)");
    const std::vector<std::string> egressScope = ParseEgressHosts(pkg->egress_hosts);
    auto cmd = m_store->GetCommand(pkg->id, commandName);
    if (!cmd) {
        std::string avail;
        for (const auto& c : m_store->ListCommands(pkg->id))
            if (!isHook || c.kind == "hook")
                avail += (avail.empty() ? "" : ", ") + c.name;
        return err("unknown command '" + commandName + "' in package '" + packageName +
                   "' (available: " + (avail.empty() ? "none" : avail) + ")");
    }
    if (isHook != (cmd->kind == "hook"))
        return err(std::string("command '") + commandName + "' is a " + cmd->kind +
                   ", not invocable on this path");

    // --- state, secrets, args ------------------------------------------------
    Json::Value stateSchema, liveState, persistState;
    {
        std::string e;
        ParseJsonText(pkg->state_schema.empty() ? "{}" : pkg->state_schema, stateSchema, e);
        ParseJsonText(pkg->state.empty() ? "{}" : pkg->state, liveState, e);
        persistState = liveState;  // storage view: pre-defaults, pre-resolution
        // Overlay schema defaults for unset keys (values win over defaults).
        if (stateSchema.isObject()) {
            for (const std::string& k : stateSchema.getMemberNames()) {
                if (stateSchema[k].isMember("default") && !liveState.isMember(k))
                    liveState[k] = stateSchema[k]["default"];
            }
        }
        // Vault resolution (#23): secret-typed keys resolve from the encrypted
        // vault (or {"secret_ref": ...} indirection) into this in-memory copy
        // only — stored state never holds secret bytes. Unset secrets resolve
        // absent, so the missing-key machinery below names them at first use.
        if (m_vault) m_vault->ResolveState(pkg->id, stateSchema, liveState);
    }

    if (args.isNull()) args = Json::Value(Json::objectValue);
    std::vector<std::string> secretArgNames;
    if (!isHook) {
        Json::Value params;
        std::string e;
        ParseJsonText(cmd->parameters.empty() ? "{}" : cmd->parameters, params, e);
        std::string verr;
        if (!ValidateArgs(params, args, secretArgNames, verr))
            return err(verr);
    }

    std::vector<std::string> secretValues;
    std::set<std::string> secretKeys;
    CollectSecrets(stateSchema, liveState, secretArgNames, isHook ? Json::Value() : args,
                   secretValues, secretKeys);

    // --- primary transport -----------------------------------------------------
    Json::Value request(Json::nullValue);
    if (!isHook && !cmd->request.empty()) {
        Json::Value reqTmpl;
        std::string e;
        if (!ParseJsonText(cmd->request, reqTmpl, e))
            return err("stored request template does not parse: " + e);
        const std::string method = reqTmpl.get("method", Json::Value("GET")).asString();
        const std::string urlT = reqTmpl.get("url", Json::Value("")).asString();
        std::string ierr;
        std::string url = Interpolate(urlT, liveState, args, secretKeys, ierr);
        if (!ierr.empty()) return err(ierr);
        HttpClient::Request hreq;
        hreq.method = method;
        hreq.url = url;
        // connection templates: same long-request allowance as ctx.http (#119)
        {
            const int tt = reqTmpl.get("timeout_s", 30).asInt();
            hreq.timeout_seconds = (tt >= 1 && tt <= 300) ? tt : 30;
        }
        if (reqTmpl.isMember("headers") && reqTmpl["headers"].isObject()) {
            for (const std::string& h : reqTmpl["headers"].getMemberNames()) {
                std::string hv = Interpolate(reqTmpl["headers"][h].asString(), liveState, args,
                                             secretKeys, ierr);
                if (!ierr.empty()) return err(ierr);
                hreq.headers[h] = hv;
            }
        }
        if (reqTmpl.isMember("body") && reqTmpl["body"].isString()) {
            std::string body = Interpolate(reqTmpl["body"].asString(), liveState, args, secretKeys,
                                           ierr);
            if (!ierr.empty()) return err(ierr);
            hreq.body = body;
        }
        // #25 egress gate: the resolved URL's host must be in this package's
        // scope. Audited loudly — every denial names package, command, host.
        {
            std::string egressHost;
            if (!EgressAllowed(egressScope, url, egressHost)) {
                ALOG_WARNING("api", "[egress] DENIED " << packageName << ":" << commandName
                             << " -> " << (egressHost.empty() ? "<malformed url>" : egressHost)
                             << " (package scope has " << egressScope.size()
                             << " pattern(s); declare egress_hosts in the manifest)");
                return err("egress denied: host '" +
                           (egressHost.empty() ? "<malformed>" : egressHost) +
                           "' is not in this package's egress allowlist");
            }
        }
        ALOG_INFO("api", "[" << packageName << ":" << commandName << "] "
                             << method << " " << MaskSecrets(url, secretValues) << " (body "
                             << hreq.body.size() << " B)");
        // redirects followed under the same scope (#25 audit: per-hop checks)
        HttpClient::Response resp =
            ExecuteScoped(m_http, egressScope, hreq,
                          packageName + ":" + commandName + " (transport)");
        if (resp.status_code == 0 && resp.error.rfind("egress denied", 0) == 0)
            return err(resp.error);  // scope violation, not a status the script sees
        request = Json::Value(Json::objectValue);
        request["status"] = resp.status_code;
        Json::Value headers(Json::objectValue);
        for (const auto& [k, v] : resp.headers) headers[k] = v;
        request["headers"] = headers;
        request["body"] = resp.body;
        if (!resp.error.empty()) {
            request["error"] = resp.error;
        } else {
            Json::Value parsed;
            std::istringstream bs(resp.body);
            Json::CharReaderBuilder rb;
            if (Json::parseFromStream(rb, bs, &parsed, &e)) request["json"] = parsed;
        }
        ALOG_INFO("api", "[" << packageName << ":" << commandName << "] transport status "
                             << resp.status_code << " (body " << resp.body.size() << " B)");
    }

    // --- sandbox -----------------------------------------------------------------
    BridgeContext bc;
    bc.packageName = pkg->name;
    bc.commandName = cmd->name;
    bc.packageId = pkg->id;
    bc.filesQuotaBytes = pkg->files_quota_mb * 1024LL * 1024LL;
    bc.state = liveState;
    bc.persistState = persistState;
    bc.stateSchema = stateSchema;
    bc.vault = m_vault;
    bc.args = args;
    bc.secretValues = secretValues;
    bc.secretKeys = secretKeys;
    bc.egressHosts = egressScope;
    bc.runtime = this;
    bc.store = m_store;
    bc.http = m_http;
    bc.filesRoot = m_cfg.filesRoot;
    bc.fsReadCap = m_cfg.fsReadCapBytes;
    bc.stringTruncate = m_cfg.stringTruncateBytes;
    bc.httpBudget = m_cfg.httpBudgetPerCall;
    bc.logPrefix = pkg->name + ":" + cmd->name;

    lua_State* L = luaL_newstate();
    if (!L) return err("failed to create sandbox VM");
    BuildSandboxGlobals(L, &bc);
    lua_sethook(L, HookInstructionCount, LUA_MASKCOUNT,
                static_cast<int>(m_cfg.instructionLimit));

    Json::Value result(Json::objectValue);
    if (luaL_loadbuffer(L, cmd->script.data(), cmd->script.size(), cmd->name.c_str()) ||
        lua_pcall(L, 0, 0, 0)) {
        const char* msg = lua_tostring(L, -1);
        result["success"] = false;
        result["error"] = MaskSecrets(
            std::string("script load error: ") + (msg ? msg : "?"), secretValues);
        lua_close(L);
        return result;
    }
    lua_getglobal(L, "run");
    if (!lua_isfunction(L, -1)) {
        lua_close(L);
        return err("script must define run(ctx)");
    }
    BuildCtx(L, &bc, request, isHook, eventJson);
    // Redaction basis for script-produced text: the pre-execution set PLUS
    // anything the script wrote/resolved into the vault while running — a
    // set_state followed by error('...' .. get_state(...)) must not smuggle
    // the new value out through the error path. `bc.secretValues` stays live
    // until the VM closes, so refresh from it per branch.
    auto masked = [&](const std::string& s) {
        std::vector<std::string> all = secretValues;
        all.insert(all.end(), bc.secretValues.begin(), bc.secretValues.end());
        return MaskSecrets(s, all);
    };
    if (lua_pcall(L, 1, 1, 0)) {
        const char* msg = lua_tostring(L, -1);
        result["success"] = false;
        result["error"] = masked(std::string("run error: ") + (msg ? msg : "?"));
        lua_close(L);
        return result;
    }
    Json::Value returned = lua_isnil(L, -1) ? Json::Value(Json::objectValue)
                                            : LuaToJson(L, -1);
    lua_close(L);
    // Fold execution-written secrets into the redaction basis for the success
    // path (truncate + final mask below); the error path handled its own via
    // `masked` above.
    secretValues.insert(secretValues.end(), bc.secretValues.begin(), bc.secretValues.end());

    if (!returned.isObject()) {
        result["success"] = false;
        result["error"] = "run(ctx) must return a table";
        return result;
    }
    result = returned;
    // Transport status mapping: an HTTP >=400 with no explicit script verdict
    // is a failure. Scripts can still override (e.g. deliberate probes that
    // expect 4xx) by returning their own success/error.
    if (!result.isMember("success")) {
        const bool httpFailed = request.isObject() &&
                               request.get("status", 0).asInt() >= 400;
        if (httpFailed) {
            result["success"] = false;
            result["http_status"] = request["status"];
            if (!result.isMember("error")) {
                result["error"] = "HTTP " +
                    request.get("status", Json::Value(0)).asString() + " from " +
                    MaskSecrets(pkg->name + ":" + cmd->name, secretValues);
            }
        } else {
            result["success"] = true;
        }
    }

    // --- files verification + truncation --------------------------------------
    if (result.isMember("files") && result["files"].isArray()) {
        Json::Value verified(Json::arrayValue);
        fs::path root = fs::path(m_cfg.filesRoot) / pkg->name;
        std::error_code ec;
        for (const auto& f : result["files"]) {
            if (!f.isObject() || !f.isMember("path")) continue;
            fs::path p(f["path"].asString());
            // Scripts declare files relative to the package filespace (the
            // same namespace ctx.fs.write takes). Canonicalizing the raw
            // declaration resolves it against the daemon CWD instead — every
            // relative entry then reads as an escape. Join first; absolute
            // declarations still face the containment check below.
            if (p.is_relative()) p = root / p;
            std::error_code nec;
            auto canon = fs::weakly_canonical(p, nec);
            auto rootCanon = fs::weakly_canonical(root, nec);
            // Component-aware containment: a string-prefix check would accept
            // sibling dirs (/…/pkg-escape passes for root /…/pkg). Relative
            // form must be non-escaping (not absolute, not leading "..");
            // canonicalization errors reject — boundary unprovable = outside.
            std::error_code rec;
            const fs::path rel = fs::relative(canon, rootCanon, rec);
            const bool escapes = nec || rec || rel.empty() || rel.is_absolute() ||
                                 rel.native() == ".." ||
                                 rel.native().rfind("../", 0) == 0;
            if (escapes) {
                result["success"] = false;
                // Masked through the full redaction basis: a script can smuggle a
                // secret (get_state or one it just wrote) into an escaping file
                // path. The rejected files array is stripped entirely — error
                // responses never echo the offending artifact.
                result["error"] = masked(
                    std::string("file path escapes package filespace: ") + p.string());
                result.removeMember("files");
                return result;
            }
            Json::Value vf = f;
            if (fs::is_regular_file(canon, ec)) {
                vf["bytes"] = static_cast<Json::Int64>(fs::file_size(canon, ec));
                verified.append(vf);
            }
        }
        result["files"] = verified;
    }

    // Truncation rule: any string field over the cap gets truncated with marker.
    std::function<void(Json::Value&)> truncate = [&](Json::Value& v) {
        if (v.isString() && v.asString().size() > m_cfg.stringTruncateBytes) {
            v = v.asString().substr(0, m_cfg.stringTruncateBytes) + "... [truncated " +
                std::to_string(v.asString().size()) + " B — bulk payloads go via files]";
        } else if (v.isArray()) {
            for (Json::ArrayIndex i = 0; i < v.size(); ++i) truncate(v[i]);
        } else if (v.isObject()) {
            for (const std::string& k : v.getMemberNames()) truncate(v[k]);
        }
    };
    truncate(result);

    // Egress mask: secrets never leave in output/data strings (SANDBOX.md).
    std::function<void(Json::Value&)> mask = [&](Json::Value& v) {
        if (v.isString()) v = MaskSecrets(v.asString(), secretValues);
        else if (v.isArray()) {
            for (Json::ArrayIndex i = 0; i < v.size(); ++i) mask(v[i]);
        } else if (v.isObject()) {
            for (const std::string& k : v.getMemberNames()) mask(v[k]);
        }
    };
    mask(result);

    return result;
}

}  // namespace animus::kernel

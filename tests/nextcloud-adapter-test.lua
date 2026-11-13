-- ============================================================================
-- nextcloud-adapter-test.lua — fixture test harness for scripts/nextcloud.lua
-- Issue #300 Phase B. Run from repo root: lua tests/nextcloud-adapter-test.lua
--
-- Mocks the kernel contract (animus.http_get/post, config, json, log) the
-- same way LuaState provides them, loads the REAL adapter, and drives:
--   * registration shape (id, actions, schema)
--   * send: wire shape (OCS headers, Basic auth vector, trailing-slash
--     base URL normalization), message-id output, 401 loud, missing
--     token/text loud, unknown-instance loud
--   * rooms: token/type/displayName rows, empty-list case
--
-- NO live account dependency: all HTTP calls hit mocks.
-- ============================================================================

local PASS, FAIL = 0, 0
local failures = {}

local function check(name, cond, detail)
    if cond then
        PASS = PASS + 1
        print("  ok - " .. name)
    else
        FAIL = FAIL + 1
        failures[#failures + 1] = name .. (detail and (" [" .. tostring(detail) .. "]") or "")
        print("  FAIL - " .. name .. (detail and (" [" .. tostring(detail) .. "]") or ""))
    end
end

-- ---------------------------------------------------------------------------
-- Mock kernel environment (mirrors LuaState globals)
-- ---------------------------------------------------------------------------

-- config store with the KERNEL contract: unset keys return "" (never nil)
local config_store = {}
config = {
    get = function(k)
        local v = config_store[k]
        if v == nil then return "" end
        return v
    end,
    set = function(k, v) config_store[k] = v end,
}

json = {}
local function json_escape(s)
    return (tostring(s):gsub('[%c\\"\\]', {
        ['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n',
        ['\r'] = '\\r', ['\t'] = '\\t'
    }))
end
local decode_pos, decode_str
local function decode_value()
    local s = decode_str
    local function skipws()
        while decode_pos <= #s and s:sub(decode_pos, decode_pos):match("%s") do
            decode_pos = decode_pos + 1
        end
    end
    skipws()
    local c = s:sub(decode_pos, decode_pos)
    if c == "{" then
        decode_pos = decode_pos + 1
        local t = {}
        skipws()
        if s:sub(decode_pos, decode_pos) == "}" then decode_pos = decode_pos + 1; return t end
        while true do
            skipws()
            local key = decode_value()
            skipws()
            decode_pos = decode_pos + 1 -- colon
            t[key] = decode_value()
            skipws()
            local d = s:sub(decode_pos, decode_pos)
            decode_pos = decode_pos + 1
            if d == "}" then break end
        end
        return t
    elseif c == "[" then
        decode_pos = decode_pos + 1
        local a = {}
        skipws()
        if s:sub(decode_pos, decode_pos) == "]" then decode_pos = decode_pos + 1; return a end
        while true do
            a[#a + 1] = decode_value()
            skipws()
            local d = s:sub(decode_pos, decode_pos)
            decode_pos = decode_pos + 1
            if d == "]" then break end
        end
        return a
    elseif c == '"' then
        decode_pos = decode_pos + 1
        local out = {}
        while true do
            local ch = s:sub(decode_pos, decode_pos)
            if ch == '"' then decode_pos = decode_pos + 1; break end
            if ch == "\\" then
                local nxt = s:sub(decode_pos + 1, decode_pos + 1)
                local esc = { n = "\n", r = "\r", t = "\t", ['"'] = '"', ["\\"] = "\\" }
                out[#out + 1] = esc[nxt] or nxt
                decode_pos = decode_pos + 2
            else
                out[#out + 1] = ch
                decode_pos = decode_pos + 1
            end
        end
        return table.concat(out)
    else
        -- number
        local num = s:match("^[%-%d%.eE+]+", decode_pos)
        if num then
            decode_pos = decode_pos + #num
            return tonumber(num)
        elseif s:sub(decode_pos, decode_pos + 3) == "null" then
            decode_pos = decode_pos + 4
            return nil -- NOTE: Lua table keys skip nil; null → absent (matches kernel json lib behavior for our use)
        elseif s:sub(decode_pos, decode_pos + 3) == "true" then
            decode_pos = decode_pos + 4
            return true
        elseif s:sub(decode_pos, decode_pos + 4) == "false" then
            decode_pos = decode_pos + 5
            return false
        end
        decode_pos = decode_pos + 1
        return nil
    end
end
function json.decode(s)
    decode_pos, decode_str = 1, s
    return decode_value()
end
function json.encode(v)
    if type(v) == "table" then
        -- Heuristic: array if all keys are sequential integers
        local is_array = true
        local n = 0
        for k in pairs(v) do
            if type(k) ~= "number" or k ~= n + 1 then is_array = false; break end
            n = n + 1
        end
        local parts = {}
        if is_array and n > 0 then
            for i = 1, n do parts[i] = json.encode(v[i]) end
            return "[" .. table.concat(parts, ",") .. "]"
        elseif next(v) == nil then
            return (n == 0) and "[]" or "{}"
        end
        for k, val in pairs(v) do
            parts[#parts + 1] = '"' .. json_escape(k) .. '":' .. json.encode(val)
        end
        return "{" .. table.concat(parts, ",") .. "}"
    elseif type(v) == "string" then
        return '"' .. json_escape(v) .. '"'
    elseif type(v) == "number" then
        return string.format("%.14g", v)
    elseif type(v) == "boolean" then
        return tostring(v)
    else
        return "null"
    end
end

-- log: capture for assertions
local log_lines = {}

log = {
    info = function() end,
    warn = function() end,
    error = function() end,
}

-- ---------------------------------------------------------------------------
-- animus mocks: capture wire calls; register_channel captures the spec
-- ---------------------------------------------------------------------------

animus = {}
local post_cap, get_cap
local post_mode = "ok"

function animus.http_post(url, opts)
    post_cap = { url = url, opts = opts }
    if post_mode == "401" then
        return {
            status = 401,
            body = '{"ocs":{"meta":{"status":"failure","statuscode":401,"message":"Unauthorised"}}}',
        }
    end
    return {
        status = 200,
        body = '{"ocs":{"meta":{"status":"ok"},"data":{"id":4242}}}',
    }
end

function animus.http_get(url, opts)
    get_cap = { url = url, opts = opts }
    if post_mode == "empty_rooms" then
        return { status = 200, body = '{"ocs":{"meta":{"status":"ok"},"data":[]}}' }
    end
    return {
        status = 200,
        body = '{"ocs":{"meta":{"status":"ok"},"data":[' ..
            '{"token":"abc123","type":1,"displayName":"Melvin Sommer"},' ..
            '{"token":"grp777","type":2,"displayName":"Team"}]}}',
    }
end

local captured
function animus.register_channel(spec) captured = spec end

-- channel config: instance "nextcloud:talk" with a trailing slash to prove
-- normalization; creds chosen so the Basic header equals a known vector
config_store["channels.nextcloud:talk.server_url"] = "https://nc.example.org/"
config_store["channels.nextcloud:talk.username"] = "animus-agent"
config_store["channels.nextcloud:talk.app_password"] = "secret"

dofile("scripts/nextcloud.lua")

-- ---------------------------------------------------------------------------
-- Registration shape
-- ---------------------------------------------------------------------------

check("registration captured", captured ~= nil)
check("id is nextcloud", captured.id == "nextcloud")
check("actions are send + rooms", captured.actions ~= nil and #captured.actions == 2
    and captured.actions[1] == "send" and captured.actions[2] == "rooms")
check("schema send present", captured.schema ~= nil and captured.schema.send ~= nil)
check("schema send token required", (function()
    for _, p in ipairs(captured.schema.send) do
        if p.name == "token" and p.required then return true end
    end
    return false
end)())
check("schema send text required", (function()
    for _, p in ipairs(captured.schema.send) do
        if p.name == "text" and p.required then return true end
    end
    return false
end)())

-- ---------------------------------------------------------------------------
-- send: wire shape
-- ---------------------------------------------------------------------------

local r = captured.handler({
    action = "send", platform_id = "nextcloud:talk",
    token = "abc123", text = "hello world",
})
check("send succeeds", r.success == true, r.error)
check("send output names room + message id",
    r.output ~= nil and r.output:find("abc123", 1, true) ~= nil
    and r.output:find("4242", 1, true) ~= nil, r.output)
check("send url normalized + v1 chat path",
    post_cap.url == "https://nc.example.org/ocs/v2.php/apps/spreed/api/v1/chat/abc123",
    post_cap.url)
check("basic auth vector",
    post_cap.opts.headers["Authorization"] == "Basic YW5pbXVzLWFnZW50OnNlY3JldA==",
    post_cap.opts.headers["Authorization"])
check("ocs headers", post_cap.opts.headers["OCS-APIREQUEST"] == "true"
    and post_cap.opts.headers["Accept"] == "application/json")
check("body carries message + silent flag",
    post_cap.opts.body:find("hello world", 1, true) ~= nil
    and post_cap.opts.body:find("silent", 1, true) ~= nil, post_cap.opts.body)

-- 401 path: loud, carries status + server message
post_mode = "401"
r = captured.handler({
    action = "send", platform_id = "nextcloud:talk",
    token = "abc123", text = "hi",
})
post_mode = "ok"
check("send 401 fails loud", r.success == false
    and r.error:find("401", 1, true) ~= nil
    and r.error:find("Unauthorised", 1, true) ~= nil, r.error)

-- missing args
r = captured.handler({ action = "send", platform_id = "nextcloud:talk", text = "hi" })
check("missing token fails loud", r.success == false
    and r.error:find("token", 1, true) ~= nil, r.error)
r = captured.handler({ action = "send", platform_id = "nextcloud:talk", token = "abc123" })
check("missing text fails loud", r.success == false
    and r.error:find("text", 1, true) ~= nil, r.error)

-- unknown instance: config.get returns "" — guidance, not a nil crash
r = captured.handler({
    action = "send", platform_id = "nextcloud:other",
    token = "abc123", text = "hi",
})
check("unknown instance fails with guidance", r.success == false
    and r.error:find("platform_id", 1, true) ~= nil, r.error)

r = captured.handler({ action = "send", token = "abc123", text = "hi" })
check("absent platform_id fails with guidance", r.success == false
    and r.error:find("platform_id", 1, true) ~= nil, r.error)

-- ---------------------------------------------------------------------------
-- rooms
-- ---------------------------------------------------------------------------

r = captured.handler({ action = "rooms", platform_id = "nextcloud:talk" })
check("rooms succeeds", r.success == true, r.error)
check("rooms url is v4 room list",
    get_cap.url == "https://nc.example.org/ocs/v2.php/apps/spreed/api/v4/room?format=json",
    get_cap.url)
check("rooms rows carry token/type/display",
    r.output:find("abc123", 1, true) ~= nil
    and r.output:find("dm", 1, true) ~= nil
    and r.output:find("grp777", 1, true) ~= nil
    and r.output:find("group", 1, true) ~= nil
    and r.output:find("Team", 1, true) ~= nil, r.output)

post_mode = "empty_rooms"
r = captured.handler({ action = "rooms", platform_id = "nextcloud:talk" })
post_mode = "ok"
check("empty room list is a clean success", r.success == true
    and r.output:find("no rooms", 1, true) ~= nil, r.output)

-- ---------------------------------------------------------------------------
-- handler contract
-- ---------------------------------------------------------------------------

r = captured.handler({ token = "abc123" })
check("missing action fails loud", r.success == false
    and r.error:find("action", 1, true) ~= nil, r.error)
r = captured.handler({ action = "dance" })
check("unknown action fails loud", r.success == false
    and r.error:find("Unknown Nextcloud action", 1, true) ~= nil, r.error)

-- ---------------------------------------------------------------------------
print(string.format("\n%d passed, %d failed", PASS, FAIL))
if FAIL > 0 then
    os.exit(1)
end

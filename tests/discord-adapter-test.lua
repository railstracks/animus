-- ============================================================================
-- discord-adapter-test.lua — fixture test harness for scripts/discord.lua
-- Issue #159 (send_media lane) — focused harness.
--
-- Mocks the kernel contract (animus.http_* incl. http_upload, config, json,
-- log) the same way LuaState provides them, loads the REAL adapter, and
-- drives the send_media action: single-phase multipart (payload_json +
-- files[0..n]) on the message endpoint, chunk-splitting with files riding
-- chunk 1, validation, and loud failure paths.
--
-- NO live account dependency: all HTTP calls hit a mock server table.
-- Run from repo root: lua tests/discord-adapter-test.lua
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

local config_store = {}
config = {
    get = function(k) return config_store[k] end,
    set = function(k, v) config_store[k] = v end
}

json = {}
local function json_escape(s)
    return (tostring(s):gsub('[%c\\"]', {
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
            decode_pos = decode_pos + 1
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
        local num = s:match("^[%-%d%.eE+]+", decode_pos)
        if num then
            decode_pos = decode_pos + #num
            return tonumber(num)
        elseif s:sub(decode_pos, decode_pos + 3) == "null" then
            decode_pos = decode_pos + 4
            return nil
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

local log_lines = {}
log = {
    info = function(msg) log_lines[#log_lines + 1] = "INFO: " .. msg end,
    warn = function(msg) log_lines[#log_lines + 1] = "WARN: " .. msg end,
    error = function(msg) log_lines[#log_lines + 1] = "ERROR: " .. msg end
}

-- Mock routes: LAST MATCH WINS.
local routes = {}
local function mock_add(route) routes[#routes + 1] = route end
local function mock_dispatch(url, headers, body)
    local matched = nil
    for _, r in ipairs(routes) do
        if r.matcher(url, headers, body) then matched = r end
    end
    if matched then return matched.respond(url, headers, body) end
    return { status = 404, body = '{"message": "Not Found"}', headers = {} }
end

animus = {}
animus.http_get = function(url, opts)
    return mock_dispatch(url, (opts and opts.headers) or {}, nil)
end
animus.http_post = function(url, opts)
    return mock_dispatch(url, (opts and opts.headers) or {}, (opts and opts.body) or "")
end
animus.http_delete = function(url, opts)
    return mock_dispatch(url, (opts and opts.headers) or {}, nil)
end
animus.http_patch = function(url, opts)
    return mock_dispatch(url, (opts and opts.headers) or {}, (opts and opts.body) or "")
end

-- #159 upload seam mock: captures multipart sends, scripted via responder.
local uploads = {}
local mock_upload_responder = nil
animus.http_upload = function(url, opts)
    opts = opts or {}
    uploads[#uploads + 1] = {
        url = url, headers = opts.headers or {},
        fields = opts.fields or {}, files = opts.files or {},
        timeout = opts.timeout
    }
    if mock_upload_responder then
        return mock_upload_responder(#uploads, url, opts)
    end
    return {
        status = 200, headers = {},
        body = json.encode({ id = "9001", channel_id = "123", content = "" })
    }
end

-- follow-up chunk posts (auth_post → http_post) capture
local followup_posts = {}
mock_add({
    matcher = function(url) return url:find("/channels/123/messages", 1, true) ~= nil end,
    respond = function(url, headers, body)
        followup_posts[#followup_posts + 1] = { url = url, headers = headers, body = body }
        return { status = 200, headers = {}, body = json.encode({ id = "9002", channel_id = "123" }) }
    end
})

-- ---------------------------------------------------------------------------
-- Load the REAL adapter
-- ---------------------------------------------------------------------------
local registration = nil
animus.register_channel = function(spec) registration = spec end
dofile("scripts/discord.lua")

local PID = "discord:main"
local API = "https://discord.com/api/v10"

check("adapter registered", registration ~= nil)
check("registration id", registration.id == "discord")
check("actions declared", #registration.actions == 14, table.concat(registration.actions, ","))
local has_send_media = false
for _, a in ipairs(registration.actions) do
    if a == "send_media" then has_send_media = true end
end
check("send_media declared", has_send_media)

local sm_schema = registration.schema.send_media
check("send_media schema present", type(sm_schema) == "table")
local files_required = false
for _, f in ipairs(sm_schema or {}) do
    if f.name == "files" and f.required then files_required = true end
end
check("send_media requires files[]", files_required)

local function call(action, args)
    args = args or {}
    args.action = action
    args.platform_id = args.platform_id or PID
    return registration.handler(args)
end

-- ---------------------------------------------------------------------------
print("== handler contract ==")
local r = registration.handler({})
check("missing action rejected", r.success == false and r.error:find("action is required") ~= nil)
r = call("bogus", {})
check("unknown action rejected", r.success == false and r.error:find("unknown action") ~= nil)

-- ---------------------------------------------------------------------------
print("== send_media: single-phase multipart ==")
config.set("channels." .. PID .. ".bot_token", "bot-token-1")

r = call("send_media", {
    channel_id = "123",
    _media_files = {
        { path = "/data/media/tmp/a.png", filename = "a.png", mime = "image/png", alt_text = "A chart" },
        { path = "/data/media/tmp/b.jpg", filename = "b.jpg", mime = "image/jpeg" }
    },
    text = "Hello with media"
})
check("send_media success", r.success == true, r.error)
check("message id returned", r.message_id == "9001", tostring(r.message_id))
check("one multipart send", #uploads == 1, tostring(#uploads))
check("send url is message endpoint", uploads[1].url == API .. "/channels/123/messages", uploads[1].url)
check("send carries bot auth", uploads[1].headers["Authorization"] == "Bot bot-token-1",
    tostring(uploads[1].headers["Authorization"]))
local payload = json.decode(uploads[1].fields.payload_json)
check("payload_json carries content", payload.content == "Hello with media", uploads[1].fields.payload_json)
check("file field names indexed from 0", uploads[1].files[1].field == "files[0]"
    and uploads[1].files[2].field == "files[1]")
check("filename passthrough", uploads[1].files[1].filename == "a.png")
check("mime passthrough", uploads[1].files[2].mime == "image/jpeg")
check("path passthrough", uploads[1].files[1].path == "/data/media/tmp/a.png")
check("no follow-up posts for short text", #followup_posts == 0, tostring(#followup_posts))

-- empty text is fine (media-only message)
uploads = {}
r = call("send_media", {
    channel_id = "123",
    _media_files = { { path = "/x/a.png", filename = "a.png", mime = "image/png" } }
})
check("media-only (no text) accepted", r.success == true, r.error)
payload = json.decode(uploads[1].fields.payload_json)
check("media-only payload content empty", payload.content == "", tostring(payload.content))

-- ---------------------------------------------------------------------------
print("== send_media: validation ==")
r = call("send_media", { channel_id = "123", text = "no files" })
check("missing _media_files rejected", r.success == false
    and r.error:find("no resolved attachments") ~= nil, r.error)

r = call("send_media", { _media_files = { { path = "/x/a.png" } }, text = "no channel" })
check("missing channel_id rejected", r.success == false
    and r.error:find("channel_id is required") ~= nil, r.error)

local eleven = {}
for i = 1, 11 do eleven[#eleven + 1] = { path = "/x/" .. i .. ".png", mime = "image/png" } end
r = call("send_media", { channel_id = "123", _media_files = eleven })
check("11 files rejected", r.success == false
    and r.error:find("too many attachments: 11 > 10") ~= nil, r.error)

-- ---------------------------------------------------------------------------
print("== send_media: loud failures + chunk split ==")
mock_upload_responder = function()
    return { status = 422, headers = {}, body = '{"message": "Unsupported file"}' }
end
uploads = {}
r = call("send_media", {
    channel_id = "123",
    _media_files = { { path = "/x/a.png", mime = "image/png" } },
    text = "will fail"
})
check("upload 422 fails loudly", r.success == false
    and r.error:find("send_media failed %(422%)") ~= nil, r.error)
mock_upload_responder = nil

-- long text: multipart chunk 1 + JSON follow-ups
uploads = {}
followup_posts = {}
local long_text = string.rep("word ", 600)  -- 3000 chars
r = call("send_media", {
    channel_id = "123",
    _media_files = { { path = "/x/a.png", filename = "a.png", mime = "image/png" } },
    text = long_text
})
check("long text success", r.success == true, r.error)
check("files ride only chunk 1 (one multipart)", #uploads == 1, tostring(#uploads))
check("follow-up chunks posted as JSON", #followup_posts >= 1, tostring(#followup_posts))
local total = #json.decode(uploads[1].fields.payload_json).content
for _, p in ipairs(followup_posts) do
    total = total + #json.decode(p.body).content
end
check("no text lost across chunks", math.abs(total - #long_text) < 60,
    "total=" .. total .. " orig=" .. #long_text)

-- ---------------------------------------------------------------------------
print()
print("RESULT: " .. PASS .. " passed, " .. FAIL .. " failed")
if FAIL > 0 then
    print("Failures:")
    for _, f in ipairs(failures) do print("  - " .. f) end
    os.exit(1)
end

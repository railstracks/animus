-- ============================================================================
-- slack-adapter-test.lua — fixture test harness for scripts/slack.lua
-- Issue #159 (send_media lane) — focused harness.
--
-- Mocks the kernel contract (animus.http_post/upload, config, json, log)
-- the same way LuaState provides them, loads the REAL adapter, and drives
-- the send_media action: files.getUploadURLExternal (exact length from
-- _media_files[].size) → raw byte POST to the presigned URL (no auth
-- header) → files.completeUploadExternal (alt text as title, text as
-- initial_comment). Validation + loud failure paths.
--
-- NO live account dependency: all HTTP calls hit a mock server table.
-- Run from repo root: lua tests/slack-adapter-test.lua
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
    return { status = 404, body = '{"ok":false,"error":"not_found"}', headers = {} }
end

animus = {}
animus.http_get = function(url, opts)
    return mock_dispatch(url, (opts and opts.headers) or {}, nil)
end
animus.http_post = function(url, opts)
    return mock_dispatch(url, (opts and opts.headers) or {}, (opts and opts.body) or "")
end

-- #159 upload seam mock (raw mode — presigned byte uploads)
local uploads = {}
local mock_upload_responder = nil
animus.http_upload = function(url, opts)
    opts = opts or {}
    uploads[#uploads + 1] = {
        url = url, headers = opts.headers or {}, file = opts.file,
        timeout = opts.timeout
    }
    if mock_upload_responder then
        return mock_upload_responder(#uploads, url, opts)
    end
    return { status = 200, headers = {}, body = "" }
end

-- form-params helper (api_post bodies are url-encoded k=v pairs)
local function url_decode(s)
    s = s:gsub("%+", " ")
    return (s:gsub("%%(%x%x)", function(h) return string.char(tonumber(h, 16)) end))
end
local function form_params(body)
    local t = {}
    for k, v in tostring(body):gmatch("([^&=]+)=([^&]*)") do
        t[url_decode(k)] = url_decode(v)
    end
    return t
end

-- ---------------------------------------------------------------------------
-- Slack API method mocks
-- ---------------------------------------------------------------------------
local PID = "slack:personal"
local API = "https://slack.com/api"

local geturl_calls = {}
mock_add({
    matcher = function(url) return url:find("files.getUploadURLExternal", 1, true) ~= nil end,
    respond = function(url, headers, body)
        local n = #geturl_calls + 1
        geturl_calls[n] = form_params(body)
        return {
            status = 200, headers = {},
            body = json.encode({
                ok = true,
                upload_url = "https://files.slack.com/upload/v1/UP" .. tostring(n),
                file_id = "F" .. tostring(n)
            })
        }
    end
})

local complete_calls = {}
mock_add({
    matcher = function(url) return url:find("files.completeUploadExternal", 1, true) ~= nil end,
    respond = function(url, headers, body)
        complete_calls[#complete_calls + 1] = form_params(body)
        return { status = 200, headers = {}, body = json.encode({ ok = true }) }
    end
})

-- ---------------------------------------------------------------------------
-- Load the REAL adapter
-- ---------------------------------------------------------------------------
local registration = nil
animus.register_channel = function(spec) registration = spec end
dofile("scripts/slack.lua")

check("adapter registered", registration ~= nil)
check("registration id", registration.id == "slack")
check("actions declared", #registration.actions == 12, table.concat(registration.actions, ","))
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
print("== send_media: getUploadURLExternal → bytes → complete ==")
config.set("channels." .. PID .. ".bot_token", "xoxb-test-1")

r = call("send_media", {
    channel_id = "C123",
    _media_files = {
        { path = "/data/media/tmp/a.png", filename = "a.png", mime = "image/png", size = 1024, alt_text = "A chart" },
        { path = "/data/media/tmp/b.jpg", filename = "b.jpg", mime = "image/jpeg", size = 2048 }
    },
    text = "Hello with media"
})
check("send_media success", r.success == true, r.error)
check("two getUploadURLExternal calls", #geturl_calls == 2, tostring(#geturl_calls))
check("length is exact size (1st)", geturl_calls[1].length == "1024", tostring(geturl_calls[1].length))
check("length is exact size (2nd)", geturl_calls[2].length == "2048", tostring(geturl_calls[2].length))
check("filename param (1st)", geturl_calls[1].filename == "a.png", tostring(geturl_calls[1].filename))
check("two presigned byte uploads", #uploads == 2, tostring(#uploads))
check("upload hits returned url (1st)", uploads[1].url == "https://files.slack.com/upload/v1/UP1", uploads[1].url)
check("upload hits returned url (2nd)", uploads[2].url == "https://files.slack.com/upload/v1/UP2", uploads[2].url)
check("presigned upload carries no auth", uploads[1].headers["Authorization"] == nil,
    tostring(uploads[1].headers["Authorization"]))
check("raw file path + mime (1st)", uploads[1].file.path == "/data/media/tmp/a.png"
    and uploads[1].file.mime == "image/png")

check("one complete call", #complete_calls == 1, tostring(#complete_calls))
local comp = complete_calls[1]
check("complete targets channel", comp.channel_id == "C123", tostring(comp.channel_id))
check("initial comment is text", comp.initial_comment == "Hello with media", tostring(comp.initial_comment))
local files_arr = json.decode(comp.files)
check("complete carries both file ids", #files_arr == 2
    and files_arr[1].file_id == "F1" and files_arr[2].file_id == "F2", comp.files)
check("alt text rides title (1st)", files_arr[1].title == "A chart", tostring(files_arr[1].title))
check("no alt text -> filename as title", files_arr[2].title == "b.jpg", tostring(files_arr[2].title))

-- ---------------------------------------------------------------------------
print("== send_media: validation ==")
uploads = {}
geturl_calls = {}
complete_calls = {}

r = call("send_media", { channel_id = "C123", text = "no files" })
check("missing _media_files rejected", r.success == false
    and r.error:find("no resolved attachments") ~= nil, r.error)

r = call("send_media", { _media_files = { { path = "/x/a.png", size = 10 } }, text = "no channel" })
check("missing channel_id rejected", r.success == false
    and r.error:find("channel_id is required") ~= nil, r.error)

local eleven = {}
for i = 1, 11 do eleven[#eleven + 1] = { path = "/x/" .. i .. ".png", size = 10 } end
r = call("send_media", { channel_id = "C123", _media_files = eleven })
check("11 files rejected", r.success == false
    and r.error:find("too many attachments: 11 > 10") ~= nil, r.error)
check("limit rejection calls nothing", #geturl_calls == 0, tostring(#geturl_calls))

r = call("send_media", {
    channel_id = "C123",
    _media_files = { { path = "/x/a.png", mime = "image/png", filename = "a.png" } },
    text = "no size"
})
check("missing size rejected loudly", r.success == false
    and r.error:find("carries no size") ~= nil, r.error)

-- empty text → no initial_comment key
r = call("send_media", {
    channel_id = "C123",
    _media_files = { { path = "/x/a.png", filename = "a.png", mime = "image/png", size = 10 } }
})
check("media-only accepted", r.success == true, r.error)
check("no initial_comment without text", complete_calls[#complete_calls].initial_comment == nil)

-- ---------------------------------------------------------------------------
print("== send_media: loud failures ==")
mock_add({
    matcher = function(url) return url:find("files.getUploadURLExternal", 1, true) ~= nil end,
    respond = function(url, headers, body)
        return { status = 200, headers = {}, body = json.encode({ ok = false, error = "invalid_length" }) }
    end
})
geturl_calls = {}
uploads = {}
r = call("send_media", {
    channel_id = "C123",
    _media_files = { { path = "/x/a.png", filename = "a.png", size = 10 } },
    text = "will fail"
})
check("getURL ok=false fails loudly", r.success == false
    and r.error:find("getUploadURLExternal error %(file 1%): invalid_length") ~= nil, r.error)
check("failed getURL uploads nothing", #uploads == 0, tostring(#uploads))

-- byte upload 500 → loud, names file position
mock_add({
    matcher = function(url) return url:find("files.getUploadURLExternal", 1, true) ~= nil end,
    respond = function(url, headers, body)
        local n = #geturl_calls + 1
        geturl_calls[n] = form_params(body)
        return {
            status = 200, headers = {},
            body = json.encode({ ok = true, upload_url = "https://files.slack.com/upload/v1/UP" .. tostring(n), file_id = "F" .. tostring(n) })
        }
    end
})
mock_upload_responder = function()
    return { status = 500, headers = {}, body = "InternalError" }
end
r = call("send_media", {
    channel_id = "C123",
    _media_files = {
        { path = "/x/a.png", filename = "a.png", size = 10 },
        { path = "/x/b.png", filename = "b.png", size = 20 }
    },
    text = "fail on 1st upload"
})
check("byte upload 500 fails loudly", r.success == false
    and r.error:find("byte upload failed %(HTTP 500%, file 1%)") ~= nil, r.error)
mock_upload_responder = nil

-- completeUploadExternal ok=false → loud
mock_add({
    matcher = function(url) return url:find("files.completeUploadExternal", 1, true) ~= nil end,
    respond = function()
        return { status = 200, headers = {}, body = json.encode({ ok = false, error = "channel_not_found" }) }
    end
})
r = call("send_media", {
    channel_id = "C999",
    _media_files = { { path = "/x/a.png", filename = "a.png", size = 10 } },
    text = "complete fails"
})
check("complete ok=false fails loudly", r.success == false
    and r.error:find("completeUploadExternal: channel_not_found") ~= nil, r.error)

-- ---------------------------------------------------------------------------
print()
print("RESULT: " .. PASS .. " passed, " .. FAIL .. " failed")
if FAIL > 0 then
    print("Failures:")
    for _, f in ipairs(failures) do print("  - " .. f) end
    os.exit(1)
end

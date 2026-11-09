-- ============================================================================
-- bluesky-adapter-test.lua — fixture test harness for scripts/bluesky.lua
-- Issue #159 (send_media lane) — first bluesky fixture harness.
--
-- Mocks the kernel contract (animus.http_get/post/upload, config, json, log)
-- the same way LuaState provides them, loads the REAL adapter, and drives:
--   * registration shape (id, capabilities, actions incl. send_media, schema)
--   * send_media: two-phase upload (uploadBlob per file → createRecord with
--     app.bsky.embed.images), alt-text passthrough, blob refs by reference
--   * validation: missing/over-limit _media_files
--   * loud failures: upload 422 naming file position, blob-less 200 body,
--     401 → reauth-once → retry, createRecord 500
--
-- NO live account dependency: all HTTP calls hit a mock server table.
-- Run from repo root: lua tests/bluesky-adapter-test.lua
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

-- config store: plain table
local config_store = {}
config = {
    get = function(k) return config_store[k] end,
    set = function(k, v) config_store[k] = v end
}

-- json: compact pure-Lua encode/decode (same fixture-grade implementation
-- as the mastodon harness; sufficient here since fixtures are controlled)
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

-- log: capture for assertions
local log_lines = {}
log = {
    info = function(msg) log_lines[#log_lines + 1] = "INFO: " .. msg end,
    warn = function(msg) log_lines[#log_lines + 1] = "WARN: " .. msg end,
    error = function(msg) log_lines[#log_lines + 1] = "ERROR: " .. msg end
}

-- ---------------------------------------------------------------------------
-- Mock HTTP layer. Routes: { matcher = fn(url, headers, body) -> bool,
-- respond = fn(url, headers, body) -> resp }. LAST MATCH WINS.
-- ---------------------------------------------------------------------------
local routes = {}
local function mock_add(route) routes[#routes + 1] = route end

local function mock_dispatch(url, headers, body)
    local matched = nil
    for _, r in ipairs(routes) do
        if r.matcher(url, headers, body) then matched = r end
    end
    if matched then return matched.respond(url, headers, body) end
    return { status = 404, body = '{"error":"Record not found"}', headers = {} }
end

animus = {}

animus.http_get = function(url, opts)
    return mock_dispatch(url, (opts and opts.headers) or {}, nil)
end
animus.http_post = function(url, opts)
    return mock_dispatch(url, (opts and opts.headers) or {}, (opts and opts.body) or "")
end

-- #159 upload seam mock: captures raw-mode uploads (url + headers + file),
-- scripted responses via mock_upload_responder(n, url, opts).
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
    local n = #uploads
    return {
        status = 200, headers = {},
        body = json.encode({
            blob = {
                ["$type"] = "blob",
                ref = { ["$link"] = "bafkrei:test-" .. tostring(n) },
                mimeType = "image/png",
                size = 100 + n
            }
        })
    }
end

-- ---------------------------------------------------------------------------
-- Mock auth + createRecord endpoints
--
-- The access token must look like a JWT with a far-future exp so
-- is_jwt_expired() accepts it from cache (payload below is unpadded
-- base64url for {"exp":9999999999}); otherwise every authenticate() would
-- re-run createSession and session counts would be non-deterministic.
-- ---------------------------------------------------------------------------
local JWT = "h.eyJleHAiOiA5OTk5OTk5OTk5OTl9O.s"
local PID = "bluesky:personal"
local PDS = "https://bsky.social"

local sessions = 0
mock_add({
    matcher = function(url) return url:find("com.atproto.server.createSession", 1, true) ~= nil end,
    respond = function(url)
        sessions = sessions + 1
        return {
            status = 200, headers = {},
            body = json.encode({
                accessJwt = JWT,
                refreshJwt = "r.eyJleHAiOiA5OTk5OTk5OTk5OTl9O.s",
                did = "did:plc:test123",
                handle = "tester.bsky.social"
            })
        }
    end
})

local created = 0
local created_bodies = {}
mock_add({
    matcher = function(url) return url:find("com.atproto.repo.createRecord", 1, true) ~= nil end,
    respond = function(url, headers, body)
        created = created + 1
        created_bodies[#created_bodies + 1] = body
        return {
            status = 200, headers = {},
            body = json.encode({
                uri = "at://did:plc:test123/app.bsky.feed.post/3ktest" .. tostring(created),
                cid = "bafytest" .. tostring(created)
            })
        }
    end
})

-- ---------------------------------------------------------------------------
-- Load the REAL adapter (capture its registration)
-- ---------------------------------------------------------------------------
local registration = nil
animus.register_channel = function(spec) registration = spec end
dofile("scripts/bluesky.lua")

check("adapter registered", registration ~= nil)
check("registration id", registration.id == "bluesky")
check("capabilities read+write+search", #registration.capabilities == 3)
check("actions declared", #registration.actions == 14,
    table.concat(registration.actions, ","))
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
print("== send_media: uploadBlob → embed by reference ==")
config.set("channels." .. PID .. ".handle", "tester.bsky.social")
config.set("channels." .. PID .. ".app_password", "app-pass-1")

r = call("send_media", {
    _media_files = {
        { path = "/data/media/tmp/a.png", filename = "a.png", mime = "image/png", alt_text = "A sunset" },
        { path = "/data/media/tmp/b.jpg", filename = "b.jpg", mime = "image/jpeg" }
    },
    text = "Hello media"
})
check("send_media success", r.success == true, r.error)
check("uri returned", r.output and json.decode(r.output).uri ~= nil, tostring(r.output))
check("two blobs uploaded", #uploads == 2, tostring(#uploads))
check("upload url is uploadBlob", uploads[1].url == PDS .. "/xrpc/com.atproto.repo.uploadBlob", uploads[1].url)
check("upload carries bearer", uploads[1].headers["Authorization"] == "Bearer " .. JWT,
    tostring(uploads[1].headers["Authorization"]))
check("raw mode: no manual content-type (kernel sets from mime)",
    uploads[1].headers["Content-Type"] == nil)
check("mime passthrough", uploads[1].file.mime == "image/png"
    and uploads[2].file.mime == "image/jpeg")
check("file path passthrough", uploads[2].file.path == "/data/media/tmp/b.jpg")

check("createRecord called once", created == 1, tostring(created))
local body = json.decode(created_bodies[1])
check("collection is feed.post", body.collection == "app.bsky.feed.post")
check("repo is did", body.repo == "did:plc:test123")
check("text on record", body.record.text == "Hello media")
check("embed type images", body.record.embed["$type"] == "app.bsky.embed.images")
check("two images embedded", #body.record.embed.images == 2)
check("blob ref by $link (1st)", body.record.embed.images[1].image.ref["$link"] == "bafkrei:test-1",
    tostring(body.record.embed.images[1].image.ref["$link"]))
check("blob ref by $link (2nd)", body.record.embed.images[2].image.ref["$link"] == "bafkrei:test-2")
check("alt text rides image entry", body.record.embed.images[1].alt == "A sunset")
check("no alt text -> no alt key", body.record.embed.images[2].alt == nil)
check("blob mimeType preserved", body.record.embed.images[1].image.mimeType == "image/png")
check("one createSession so far", sessions == 1, tostring(sessions))

-- ---------------------------------------------------------------------------
print("== send_media: validation ==")
r = call("send_media", { text = "no files" })
check("missing _media_files rejected", r.success == false
    and r.error:find("no resolved attachments") ~= nil, r.error)

local five = {}
for i = 1, 5 do five[#five + 1] = { path = "/x/" .. i .. ".png", mime = "image/png" } end
r = call("send_media", { _media_files = five, text = "too many" })
check("5 files rejected", r.success == false
    and r.error:find("too many attachments: 5 > 4") ~= nil, r.error)
check("limit rejection uploads nothing", #uploads == 2, tostring(#uploads))

-- ---------------------------------------------------------------------------
print("== send_media: loud failures ==")
mock_upload_responder = function(n)
    if n == 2 then
        return { status = 422, headers = {}, body = '{"error":"InvalidEncoding"}' }
    end
    return { status = 200, headers = {}, body = json.encode({
        blob = { ["$type"] = "blob", ref = { ["$link"] = "bafkrei:x" .. tostring(n) }, mimeType = "image/png", size = 1 }
    }) }
end
uploads = {}
r = call("send_media", {
    _media_files = {
        { path = "/x/a.png", mime = "image/png" },
        { path = "/x/b.png", mime = "image/png" }
    },
    text = "will fail on 2nd"
})
check("upload 422 fails loudly", r.success == false
    and r.error:find("blob upload failed %(422%)") ~= nil, r.error)
check("422 names file position", r.error:find("file 2/2") ~= nil, r.error)
check("no post after upload failure", created == 1, tostring(created))

mock_upload_responder = function()
    return { status = 200, headers = {}, body = '{"ok":true}' }
end
uploads = {}
r = call("send_media", { _media_files = { { path = "/x/a.png", mime = "image/png" } }, text = "no blob" })
check("200-without-blob-ref rejected", r.success == false
    and r.error:find("no blob ref %(file 1%)") ~= nil, r.error)

-- 401 on first upload → reauth-once → retry succeeds
mock_upload_responder = function(n)
    if n == 1 then
        return { status = 401, headers = {}, body = '{"error":"ExpiredToken"}' }
    end
    return { status = 200, headers = {}, body = json.encode({
        blob = { ["$type"] = "blob", ref = { ["$link"] = "bafkrei:retry" }, mimeType = "image/png", size = 1 }
    }) }
end
uploads = {}
local sessions_before = sessions
r = call("send_media", { _media_files = { { path = "/x/a.png", mime = "image/png" } }, text = "retry after 401" })
check("401 -> reauth retry succeeds", r.success == true, r.error)
check("retry re-uploaded", #uploads == 2, tostring(#uploads))
check("reauth ran createSession again", sessions == sessions_before + 1,
    tostring(sessions) .. " vs " .. tostring(sessions_before))
check("retry posted with retry blob", json.decode(created_bodies[#created_bodies])
    .record.embed.images[1].image.ref["$link"] == "bafkrei:retry")

-- createRecord 500 → loud failure (late route wins: LAST MATCH WINS)
mock_upload_responder = nil
mock_add({
    matcher = function(url) return url:find("com.atproto.repo.createRecord", 1, true) ~= nil end,
    respond = function() return { status = 500, headers = {}, body = '{"error":"Internal"}' } end
})
r = call("send_media", { _media_files = { { path = "/x/a.png", mime = "image/png" } }, text = "post fails" })
check("createRecord 500 fails loudly", r.success == false
    and r.error:find("post failed %(500%)") ~= nil, r.error)

-- ---------------------------------------------------------------------------
print()
print("RESULT: " .. PASS .. " passed, " .. FAIL .. " failed")
if FAIL > 0 then
    print("Failures:")
    for _, f in ipairs(failures) do print("  - " .. f) end
    os.exit(1)
end

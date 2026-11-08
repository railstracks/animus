-- ============================================================================
-- link-card-test.lua — #157 golden/degrade/regression tests for the uniform
-- outbound link card across all four social adapters.
--
--   lua tests/link-card-test.lua            run (compare goldens)
--   lua tests/link-card-test.lua --regen    regenerate goldens
--
-- Mocks the kernel contract (animus.http_get/post, config, json, log) the
-- same way tests/mastodon-adapter-test.lua does, loads the shared markdown
-- module FIRST (the adapters' card helpers are markdown-guarded), then each
-- real adapter. Drives post with a canonical card, captures the outgoing
-- HTTP body, and compares a CANONICAL (sorted-key) re-encoding against
-- tests/link-card-golden/<platform>.txt — the mock codec's pairs() order is
-- nondeterministic, so goldens must never pin key order.
-- ============================================================================

local REGEN = arg and arg[1] == "--regen"

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

--- Deterministic encoding: sorted object keys, arrays in order. Golden
--- files are written/read ONLY through this — never json.encode directly.
local function canonical_encode(v)
    if type(v) ~= "table" then return json.encode(v) end
    local is_array = true
    local n = 0
    for k in pairs(v) do
        if type(k) ~= "number" or k ~= n + 1 then is_array = false; break end
        n = n + 1
    end
    if is_array and n > 0 then
        local parts = {}
        for i = 1, n do parts[i] = canonical_encode(v[i]) end
        return "[" .. table.concat(parts, ",") .. "]"
    elseif next(v) == nil then
        return (n == 0) and "[]" or "{}"
    end
    local keys = {}
    for k in pairs(v) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    local parts = {}
    for _, k in ipairs(keys) do
        parts[#parts + 1] = '"' .. json_escape(tostring(k)) .. '":' .. canonical_encode(v[k])
    end
    return "{" .. table.concat(parts, ",") .. "}"
end

log = {
    info = function() end,
    warn = function() end,
    error = function() end
}

-- Mock HTTP: routes with LAST-match-wins; every call captured.
local routes = {}
local posted = {}   -- { {url=..., headers=..., body=...} }
local function mock_add(route) routes[#routes + 1] = route end
local function mock_clear() routes = {} end
local function reset_posts() posted = {} end

local function mock_dispatch(url, headers, body)
    local matched = nil
    for _, r in ipairs(routes) do
        if r.matcher(url, headers, body) then matched = r end
    end
    if matched then return matched.respond(url, headers, body) end
    return { status = 404, body = '{"error":"no route"}', headers = {} }
end

animus = {}
function animus.http_get(url, opts)
    return mock_dispatch(url, (opts and opts.headers) or {}, nil)
end
function animus.http_post(url, opts)
    return mock_dispatch(url, (opts and opts.headers) or {}, (opts and opts.body) or nil)
end

local registrations = {}
animus.register_channel = function(spec) registrations[spec.id] = spec end

--- Replace timestamp-shaped strings ("<date>T<time>...") with a fixed
--- marker so goldens stay byte-stable across runs (bluesky createdAt
--- mints a live clock on every post).
local function scrub_ts(v)
    if type(v) == "string" then
        return (v:gsub("^%d%d%d%d%-%d%d%-%d%dT%d%d:%d%d:%d%d.*$", "<TS>"))
    elseif type(v) == "table" then
        local t = {}
        for k, val in pairs(v) do t[k] = scrub_ts(val) end
        return t
    end
    return v
end

-- ---------------------------------------------------------------------------
-- Load shared markdown FIRST, then the real adapters
-- ---------------------------------------------------------------------------

dofile("scripts/markdown.lua")
check("markdown module loaded", type(markdown) == "table" and type(markdown.normalize_card) == "function")

dofile("scripts/slack.lua")
dofile("scripts/discord.lua")
dofile("scripts/mastodon.lua")
dofile("scripts/bluesky.lua")
check("all four adapters registered",
    registrations.slack and registrations.discord
    and registrations.mastodon and registrations.bluesky)

local PID = { slack = "slack:TEST", discord = "discord:TEST",
              mastodon = "mastodon:TEST", bluesky = "bluesky:TEST" }

config_store["channels." .. PID.slack .. ".bot_token"] = "xoxb-test"
config_store["channels." .. PID.discord .. ".bot_token"] = "discord-test"
config_store["channels." .. PID.mastodon .. ".access_token"] = "mast-test"
config_store["channels." .. PID.mastodon .. ".instance_url"] = "https://mastodon.social"
config_store["channels." .. PID.mastodon .. ".char_limit"] = "500"
config_store["channels." .. PID.bluesky .. ".handle"] = "test.bsky.social"
config_store["channels." .. PID.bluesky .. ".app_password"] = "app-pass-test"

local function call(platform, action, args)
    args = args or {}
    args.action = action
    args.platform_id = args.platform_id or PID[platform]
    return registrations[platform].handler(args)
end

-- Canonical card used by the goldens.
local CARD = {
    url = "https://example.com/animus",
    title = "Animus",
    description = "Agent kernel with native channels",
    image = "https://example.com/og.png"
}
local CONTENT = "Hello from the link card test"

-- ---------------------------------------------------------------------------
-- Platform mock routes (capture every outgoing POST/GET body)
-- ---------------------------------------------------------------------------

mock_add({
    matcher = function(url) return url:find("chat.postMessage", 1, true) ~= nil end,
    respond = function(url, headers, body)
        posted[#posted + 1] = { url = url, headers = headers, body = body }
        return { status = 200, headers = {}, body = json.encode({ ok = true, ts = "1234.5678", channel = "C123" }) }
    end
})
mock_add({
    matcher = function(url) return url:find("/channels/", 1, true) ~= nil
        and url:find("/messages", 1, true) ~= nil end,
    respond = function(url, headers, body)
        posted[#posted + 1] = { url = url, headers = headers, body = body }
        return { status = 200, headers = {}, body = json.encode({ id = "999" }) }
    end
})
mock_add({
    matcher = function(url) return url:find("/api/v1/statuses", 1, true) ~= nil end,
    respond = function(url, headers, body)
        posted[#posted + 1] = { url = url, headers = headers, body = body }
        return { status = 200, headers = {}, body = json.encode({
            id = "8001", url = "https://mastodon.social/@agent/8001" }) }
    end
})
mock_add({
    matcher = function(url) return url:find("com.atproto.repo.getRecord", 1, true) ~= nil end,
    respond = function(url, headers, body)
        posted[#posted + 1] = { url = url, headers = headers, body = body }
        return { status = 200, headers = {}, body = json.encode({
            uri = "at://did:plc:test123/app.bsky.feed.post/3kabc", cid = "bafytest" }) }
    end
})
mock_add({
    matcher = function(url) return url:find("com.atproto.repo.createRecord", 1, true) ~= nil end,
    respond = function(url, headers, body)
        posted[#posted + 1] = { url = url, headers = headers, body = body }
        return { status = 200, headers = {}, body = json.encode({
            uri = "at://did:plc:test123/app.bsky.feed.post/3knew", cid = "bafynew" }) }
    end
})
mock_add({
    matcher = function(url) return url:find("chat.bsky.convo.sendMessage", 1, true) ~= nil end,
    respond = function(url, headers, body)
        posted[#posted + 1] = { url = url, headers = headers, body = body }
        return { status = 200, headers = {}, body = json.encode({ id = "msg1", rev = "r1" }) }
    end
})
-- Bluesky auth endpoints: the #48 lineage guard drops seeded tokens, so the
-- harness seeds handle/app_password and lets refresh_auth mint the cache
-- through this createSession mock (far-future exp keeps it valid for the
-- whole run).
local LONG_JWT = "h.eyJleHAiOjk5OTk5OTk5OTl9.s"
mock_add({
    matcher = function(url) return url:find("com.atproto.server.createSession", 1, true) ~= nil end,
    respond = function(url, headers, body)
        posted[#posted + 1] = { url = url, headers = headers, body = body }
        return { status = 200, headers = {}, body = json.encode({
            accessJwt = LONG_JWT, refreshJwt = LONG_JWT,
            did = "did:plc:test123", handle = "test.bsky.social" }) }
    end
})
mock_add({
    matcher = function(url) return url:find("com.atproto.server.refreshSession", 1, true) ~= nil end,
    respond = function(url, headers, body)
        posted[#posted + 1] = { url = url, headers = headers, body = body }
        return { status = 200, headers = {}, body = json.encode({
            accessJwt = LONG_JWT, refreshJwt = LONG_JWT, did = "did:plc:test123" }) }
    end
})

local function first_posted_body()
    if #posted == 0 then return nil end
    local ok, v = pcall(json.decode, posted[1].body)
    if ok then return v end
    return nil
end

--- Decoded body of the LAST captured wire call (e.g. bluesky reply: parent
--- CID getRecord first, record creation last).
local function last_posted_body()
    if #posted == 0 then return nil end
    local ok, v = pcall(json.decode, posted[#posted].body)
    if ok then return v end
    return nil
end

-- Warm bluesky auth once through the mocked createSession so later golden
-- captures hold exactly the record-creation call (lineage stamp + token
-- cache are minted here and stay valid for the whole run).
do
    local wr = call("bluesky", "post", { content = "auth warmup" })
    check("bluesky: auth warmup succeeds", wr ~= nil and wr.success == true, wr and wr.error)
    reset_posts()
end

-- ---------------------------------------------------------------------------
print("== golden payloads (full card) ==")
-- ---------------------------------------------------------------------------

local GOLDEN_DIR = "tests/link-card-golden"
for _, platform in ipairs({ "slack", "discord", "mastodon", "bluesky" }) do
    reset_posts()
    local r = call(platform, "post", { content = CONTENT, link_card = CARD,
        channel_id = platform == "slack" and "C123" or "999" })
    check(platform .. ": post with card succeeds", r ~= nil and r.success == true, r and r.error)
    check(platform .. ": exactly one wire call", #posted == 1, #posted)
    local body = first_posted_body()
    check(platform .. ": body decodes", body ~= nil)
    if body then
        local canon = canonical_encode(scrub_ts(body))
        local path = GOLDEN_DIR .. "/" .. platform .. ".txt"
        if REGEN then
            local f = assert(io.open(path, "w"))
            f:write(canon .. "\n")
            f:close()
            print("  regen - " .. path)
        else
            local f = io.open(path, "r")
            local expected = f and f:read("*a"):gsub("%s+$", "")
            if f then f:close() end
            check(platform .. ": golden payload matches", expected ~= nil and expected == canon,
                "canonical=" .. tostring(canon))
        end
    end
end

-- ---------------------------------------------------------------------------
print("== url-only degrade (no title/description → plain link) ==")
-- ---------------------------------------------------------------------------

local URLONLY = { url = "https://example.com/bare" }

reset_posts()
local r = call("slack", "post", { content = "degrade test", link_card = URLONLY, channel_id = "C123" })
local b = first_posted_body()
check("slack degrade: no blocks", r.success and b and b.blocks == nil, b and b.blocks)
check("slack degrade: url rides text", b and b.text and b.text:find(URLONLY.url, 1, true) ~= nil, b and b.text)

reset_posts()
r = call("discord", "post", { content = "degrade test", link_card = URLONLY, channel_id = "999" })
b = first_posted_body()
check("discord degrade: no embeds", r.success and b and b.embeds == nil, b and b.embeds)
check("discord degrade: url rides content", b and b.content and b.content:find(URLONLY.url, 1, true) ~= nil, b and b.content)

reset_posts()
r = call("mastodon", "post", { content = "degrade test", link_card = URLONLY })
b = first_posted_body()
check("mastodon degrade: url anchor in status",
    r.success and b and b.status and b.status:find('<a href="https://example.com/bare"', 1, true) ~= nil,
    b and b.status)

reset_posts()
r = call("bluesky", "post", { content = "degrade test", link_card = URLONLY })
b = first_posted_body()
check("bluesky degrade: embed with empty title/desc",
    r.success and b and b.record and b.record.embed
        and b.record.embed.external.title == ""
        and b.record.embed.external.uri == URLONLY.url,
    b and b.record and b.record.embed and b.record.embed.external)

-- ---------------------------------------------------------------------------
print("== invalid card (non-http scheme) → loud error, zero wire calls ==")
-- ---------------------------------------------------------------------------

for _, platform in ipairs({ "slack", "discord", "mastodon", "bluesky" }) do
    reset_posts()
    local rr = call(platform, "post", { content = "x", channel_id = "999",
        link_card = { url = "javascript:alert(1)", title = "Evil" } })
    check(platform .. ": rejects non-http scheme",
        rr and rr.success == false and rr.error and rr.error:find("invalid link_card", 1, true) ~= nil,
        rr and rr.error)
    check(platform .. ": no wire call on rejection", #posted == 0, #posted)
end

-- ---------------------------------------------------------------------------
print("== no-card regression (payloads unchanged) ==")
-- ---------------------------------------------------------------------------

reset_posts()
r = call("slack", "post", { content = "plain", channel_id = "C123" })
b = first_posted_body()
check("slack no-card: no blocks/unfurl keys", r.success and b and b.blocks == nil and b.unfurl_links == nil)

reset_posts()
r = call("discord", "post", { content = "plain", channel_id = "999" })
b = first_posted_body()
check("discord no-card: no embeds", r.success and b and b.embeds == nil)

reset_posts()
r = call("mastodon", "post", { content = "plain" })
b = first_posted_body()
check("mastodon no-card: status is rendered plain", r.success and b and b.status ~= nil)

-- ---------------------------------------------------------------------------
print("== precedence + merge ==")
-- ---------------------------------------------------------------------------

reset_posts()
r = call("slack", "post", {
    content = "blocks win", channel_id = "C123", link_card = CARD,
    blocks = { { type = "section", text = { type = "mrkdwn", text = "user blocks" } } }
})
b = first_posted_body()
check("slack: explicit blocks win over card",
    r.success and b and b.blocks and b.blocks[1] and b.blocks[1].text and b.blocks[1].text.text == "user blocks",
    b and b.blocks)
check("slack: no unfurl override when blocks win", b and b.unfurl_links == nil)

reset_posts()
r = call("discord", "post", {
    content = "merge test", channel_id = "999", link_card = CARD,
    embeds = { { title = "User embed" } }
})
b = first_posted_body()
check("discord: card embed first, user embeds follow",
    r.success and b and b.embeds and #b.embeds == 2
        and b.embeds[1].url == CARD.url and b.embeds[2].title == "User embed",
    b and b.embeds)

-- ---------------------------------------------------------------------------
print("== mastodon weighted link counting (#157 acceptance) ==")
-- ---------------------------------------------------------------------------

-- Three 120-char-URL links + ~300 chars: raw bytes far exceed 500, but the
-- weighted count (23 per link) fits — must be ONE status.
local long_url = "https://example.com/" .. string.rep("a", 100) .. "/end"
local weighted_content = ""
for i = 1, 3 do
    weighted_content = weighted_content .. "See [ref " .. i .. "](" .. long_url .. ") for detail.\n\n"
end
weighted_content = weighted_content .. string.rep("word ", 55)
reset_posts()
r = call("mastodon", "post", { content = weighted_content })
check("mastodon weighted: single toot for link-heavy text",
    r.success and #posted == 1, "posts=" .. #posted .. " err=" .. tostring(r.error))
local mbody = first_posted_body()
check("mastodon weighted: all three anchors ride the toot",
    mbody and mbody.status and mbody.status:find("ref 1") and mbody.status:find("ref 3")
        and select(2, mbody.status:gsub("<a ", "")) == 3,
    mbody and mbody.status)
check("mastodon weighted: no mask debris on the wire",
    mbody and mbody.status and mbody.status:find("\1", 1, true) == nil and mbody.status:find("\2", 1, true) == nil)
check("mastodon weighted: anchor labels not duplicated after restore",
    mbody and mbody.status and mbody.status:find("</a>ref", 1, true) == nil,
    mbody and mbody.status)

-- Control: same volume of PLAIN text (no links) still splits.
local plain_long = string.rep("word ", 130)
reset_posts()
r = call("mastodon", "post", { content = plain_long })
check("mastodon control: plain over-limit text still threads",
    r.success and #posted >= 2, "posts=" .. #posted)

-- ---------------------------------------------------------------------------
print("== bluesky reply embed (the do_reply seam) ==")
-- ---------------------------------------------------------------------------

reset_posts()
r = call("bluesky", "reply", {
    content = "replying with a card",
    post_id = "at://did:plc:peer/app.bsky.feed.post/3kparent",
    root_id = "at://did:plc:peer/app.bsky.feed.post/3kparent",
    link_card = CARD
})
b = last_posted_body()
check("bluesky reply: succeeds", r ~= nil and r.success == true, r and r.error)
check("bluesky reply: external embed on record",
    b and b.record and b.record.embed and b.record.embed.external
        and b.record.embed.external.uri == CARD.url,
    b and b.record and b.record.embed)
check("bluesky reply: card url rides text", b and b.record and b.record.text
    and b.record.text:find(CARD.url, 1, true) ~= nil, b and b.record and b.record.text)

-- ---------------------------------------------------------------------------
print("== bluesky chat degrade (url-in-text, no embed) ==")
-- ---------------------------------------------------------------------------

reset_posts()
r = call("bluesky", "chat_send", { convo_id = "convo1", content = "chat card", link_card = CARD })
b = first_posted_body()
check("bluesky chat: succeeds", r ~= nil and r.success == true, r and r.error)
check("bluesky chat: no embed on message", b and b.message and b.message.embed == nil,
    b and b.message)
check("bluesky chat: url rides message text", b and b.message and b.message.text
    and b.message.text:find(CARD.url, 1, true) ~= nil, b and b.message and b.message.text)

-- ---------------------------------------------------------------------------
print(("RESULT: %d passed, %d failed"):format(PASS, FAIL))
for _, f in ipairs(failures) do print("  failed: " .. f) end
os.exit(FAIL > 0 and 1 or 0)

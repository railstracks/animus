-- ============================================================================
-- markdown-renderer-test.lua — golden/degrade/hostile tests for the shared
-- Markdown renderer (scripts/markdown.lua, issue #156) plus one adapter
-- integration case proving rendered output reaches the wire.
--
-- Run from the repo root:
--   lua tests/markdown-renderer-test.lua             run tests
--   lua tests/markdown-renderer-test.lua --regen     regenerate goldens
--
-- Structure:
--   1. Golden files  — tests/markdown-golden/{slack,discord,mastodon,bluesky}.txt
--                      over the shared corpus (tests/markdown-corpus.md)
--   2. Degrade       — platform-invalid syntax must never appear in output
--   3. Hostile input — Mastodon sanitizer: tags, javascript:, entity
--                      smuggling, attribute injection
--   4. Lock-ins      — regressions for the bugs found while building this
--   5. Facet safety  — Bluesky URLs stay contiguous (the #36 facet pass
--                      byte-indexes the final text)
--   6. Integration   — the REAL mastodon adapter, markdown present, posts
--                      rendered HTML through a mocked kernel
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
        failures[#failures + 1] = name
        print("  FAIL - " .. name .. (detail and ("  [" .. tostring(detail) .. "]") or ""))
    end
end

local function read_file(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local s = f:read("*a")
    f:close()
    return s
end

local function write_file(path, s)
    local f = assert(io.open(path, "wb"))
    f:write(s)
    f:close()
end

-- ---------------------------------------------------------------------------
-- Load the library (sets the `markdown` global the way the kernel's Eval
-- does; the returned table is the same object).
-- ---------------------------------------------------------------------------
dofile("scripts/markdown.lua")
local md = markdown
check("library loads and exports render fns",
    type(md) == "table"
    and type(md.render_slack) == "function"
    and type(md.render_discord) == "function"
    and type(md.render_mastodon) == "function"
    and type(md.render_bluesky) == "function")

local platforms = { "slack", "discord", "mastodon", "bluesky" }

local corpus = read_file("tests/markdown-corpus.md")
check("corpus present", corpus ~= nil and #corpus > 0)

local rendered = {}
for _, p in ipairs(platforms) do rendered[p] = md["render_" .. p](corpus) end

-- ---------------------------------------------------------------------------
-- 1. Golden files
-- ---------------------------------------------------------------------------
print("== golden files ==")
for _, p in ipairs(platforms) do
    local path = "tests/markdown-golden/" .. p .. ".txt"
    if REGEN then
        write_file(path, rendered[p])
        print("  regen - " .. path)
    else
        local golden = read_file(path)
        if golden == nil then
            check(p .. " golden present (run --regen)", false, path)
        else
            local ok = golden == rendered[p]
            check(p .. " output matches golden", ok)
            if not ok then
                local gl, rl = golden:match("([^\n]*)"), rendered[p]:match("([^\n]*)")
                for i = 1, math.min(#golden, #rendered[p]) do
                    if golden:sub(i, i) ~= rendered[p]:sub(i, i) then
                        print(string.format("    first diff at byte %d: golden %q vs got %q",
                            i, golden:sub(math.max(1, i - 20), i + 20), rendered[p]:sub(math.max(1, i - 20), i + 20)))
                        break
                    end
                end
                _ = gl _ = rl
            end
        end
    end
end

-- ---------------------------------------------------------------------------
-- 2. Degrade: syntax the platform cannot express must never appear
-- ---------------------------------------------------------------------------
print("== degrade invariants ==")
-- Patterns are chosen per platform: `](` is NATIVE discord link syntax,
-- `**`/backticks are native discord emphasis — only leak what the platform
-- cannot render.
local leak_patterns = {
    slack    = { "](", "**", "<a ", "<p>", "<b>" },
    discord  = { "<a ", "<p>", "<script", "<img" },
    mastodon = { "](", "**", "`", "<script", "<img", "<i></i>" },
    bluesky  = { "](", "**", "`", "<a ", "<p>", "<b>", "<i>" },
}
for _, p in ipairs(platforms) do
    for _, pat in ipairs(leak_patterns[p]) do
        check(p .. ": no leaked '" .. pat .. "'",
            rendered[p]:find(pat, 1, true) == nil)
    end
end

-- ---------------------------------------------------------------------------
-- 3. Hostile input (Mastodon is the HTML surface; everything else is plain)
-- ---------------------------------------------------------------------------
print("== hostile input (mastodon) ==")
local function hostile(name, input, forbid)
    local out = md.render_mastodon(input)
    for _, f in ipairs(forbid) do
        check(name .. ": no '" .. f .. "'", out:find(f, 1, true) == nil, out)
    end
    return out
end
hostile("script tag", "<script>alert(1)</script>", { "<script", "<img" })
hostile("img onerror", "<img src=x onerror=alert(1)>", { "<img" })
check("img onerror: arrives as escaped text, never a live tag",
    md.render_mastodon("<img src=x onerror=alert(1)>"):find("&lt;img", 1, true) ~= nil)
hostile("javascript link", "[x](javascript:alert(1))", { "javascript:", "alert" })

local ent = md.render_mastodon("plain &lt;script&gt; text")
check("entity smuggling double-escapes (no live tag)", ent:find("&amp;lt;script&amp;gt;", 1, true) ~= nil, ent)
check("entity smuggling: no raw <script", ent:find("<script", 1, true) == nil)

local attr = hostile("attr injection via label",
    '[x" onmouseover="alert(1)](https://e.com)', { '" onmouseover="' })
check("attr injection: link still emitted safely",
    attr:find("https://e.com", 1, true) ~= nil, attr)

-- Non-http URLs are dropped on EVERY platform, label survives
for _, p in ipairs(platforms) do
    local out = md["render_" .. p]("[docs](ftp://evil.example/x)")
    check(p .. ": non-http URL dropped, label kept",
        out:find("ftp://", 1, true) == nil and out:find("docs", 1, true) ~= nil, out)
end

-- ---------------------------------------------------------------------------
-- 4. Lock-ins: bugs found while building this library
-- ---------------------------------------------------------------------------
print("== lock-ins ==")
-- Lua patterns have no {n,m}: headings once parsed as literal "# text".
for _, p in ipairs({ "slack", "mastodon", "bluesky" }) do
    check(p .. ": heading markers strip (no literal '# ')",
        rendered[p]:find("\n# ", 1, true) == nil and not rendered[p]:match("^# "))
end
check("table cells trimmed", rendered.slack:find("Phase | Gate", 1, true) ~= nil, rendered.slack)
check("nested code in bold (mastodon): bold wraps, no empty italics",
    md.render_mastodon("**bold `code` inside**") == "<p><b>bold code inside</b></p>",
    md.render_mastodon("**bold `code` inside**"))
check("nested code in bold (slack): both markers survive",
    md.render_slack("**bold `code` inside**") == "*bold `code` inside*")
check("emphasis in link label strips to plain",
    md.render_bluesky("[the *fine* manual](https://example.com/rtfm)")
        == "the fine manual (https://example.com/rtfm)")
check("snake_case is not italic",
    md.render_slack("my_var_name stays") == "my_var_name stays",
    md.render_slack("my_var_name stays"))

-- ---------------------------------------------------------------------------
-- 5. Facet safety (bluesky): URLs contiguous in the FINAL text
-- ---------------------------------------------------------------------------
print("== facet safety (bluesky) ==")
local bs = rendered.bluesky
check("labeled link URL contiguous", bs:find("https://example.com/changelog", 1, true) ~= nil)
check("bare URL contiguous", bs:find("https://mirror.example.com/notes", 1, true) ~= nil)
check("rtfm URL contiguous", bs:find("https://example.com/rtfm", 1, true) ~= nil)

-- ---------------------------------------------------------------------------
-- 6. Integration: the REAL mastodon adapter sends RENDERED output
-- ---------------------------------------------------------------------------
print("== adapter integration (mastodon) ==")

local function jenc(v)
    local t = type(v)
    if t == "string" then
        return '"' .. (v:gsub('[%c"\\]', {
            ['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n',
            ['\r'] = '\\r', ['\t'] = '\\t'
        })) .. '"'
    elseif t == "number" or t == "boolean" then
        return tostring(v)
    elseif t == "table" then
        local n = #v
        if n > 0 then
            local parts = {}
            for i = 1, n do parts[i] = jenc(v[i]) end
            return "[" .. table.concat(parts, ",") .. "]"
        end
        local parts = {}
        for k, val in pairs(v) do parts[#parts + 1] = jenc(tostring(k)) .. ":" .. jenc(val) end
        return "{" .. table.concat(parts, ",") .. "}"
    end
    return "null"
end

local function jdec(s)
    local obj = {}
    local body = s and s:match("^%s*{(.*)}%s*$")
    if not body then return nil end
    for k, v in body:gmatch('"([^"]+)"%s*:%s*"((\\.|[^"\\])*)"') do
        obj[k] = v:gsub("\\n", "\n"):gsub('\\"', '"'):gsub("\\\\", "\\")
    end
    for k, v in body:gmatch('"([^"]+)"%s*:%s*(-?%d+%.?%d*)%s*[,}]') do
        if obj[k] == nil then obj[k] = tonumber(v) end
    end
    return obj
end

local captured = {}
config_store = {}
config = {
    get = function(k) return config_store[k] end,
    set = function(k, v) config_store[k] = v end,
}
json = { encode = jenc, decode = jdec }
log = { info = function() end, warn = function() end, error = function() end, debug = function() end }

local PID = "mastodon:INT"
config_store["channels." .. PID .. ".access_token"] = "test-token"
config_store["channels." .. PID .. ".instance_url"] = "https://inst.example"

animus = {}
function animus.http_get() return { status = 404, body = "{}", headers = {} } end
function animus.http_post(url, opts)
    captured[#captured + 1] = { url = url, body = opts and opts.body }
    return {
        status = 200, headers = {},
        body = jenc({ id = "1", url = "https://inst.example/@agent/1" })
    }
end

local registration
animus.register_channel = function(spec) registration = spec end

-- markdown global is already set (dofile above) — the same per-agent VM
-- shape the kernel produces.
dofile("scripts/mastodon.lua")
check("adapter registered", registration ~= nil and registration.id == "mastodon")

local res = registration.handler({
    action = "post",
    platform_id = PID,
    content = "# Heading\n\nSome **bold** and `code` with a [link](https://example.com/x)."
})
check("integration: post succeeds", res ~= nil and res.success == true, res and res.error)
check("integration: one wire call", #captured == 1, #captured)
local wire = captured[1] and captured[1].body or ""
check("integration: wire carries rendered HTML",
    wire:find("<p>", 1, true) ~= nil and wire:find("<b>bold</b>", 1, true) ~= nil, wire)
check("integration: no raw markdown on the wire",
    wire:find("%*%*", 1) == nil and wire:find("`", 1, true) == nil, wire)
check("integration: link rendered native",
    wire:find("https://example.com/x", 1, true) ~= nil, wire)

-- ---------------------------------------------------------------------------
print(("RESULT: %d passed, %d failed"):format(PASS, FAIL))
for _, f in ipairs(failures) do print("  failed: " .. f) end
os.exit(FAIL > 0 and 1 or 0)

-- ===========================================================================
-- Markdown renderer library (#156) — canonical Markdown in, platform-native
-- out. Loaded as a shared library into every agent VM: defines the global
-- `markdown` table. Adapters reference it at SEND time (not load time) and
-- fall back to raw content when absent — standalone harnesses that load
-- only the adapter keep working.
--
-- Contract (issue #156, settled with Melvin Oct 12):
--   markdown.render_slack(src)    → mrkdwn (*bold*, _italic_, `code`)
--   markdown.render_discord(src)  → Markdown-with-quirks
--   markdown.render_mastodon(src) → strict-allowlist HTML (p, br, b, i, a,
--                                   span.h-card); ALL text escaped at
--                                   construction; sanitize_mastodon_html()
--                                   is defense-in-depth. Outbound HTML is
--                                   an injection surface — every text
--                                   segment passes esc_html before it can
--                                   become markup.
--   markdown.render_bluesky(src)  → plain text; formatting constructs
--                                   strip, links keep visible URL text so
--                                   the adapter's #36 facet pass can
--                                   byte-index them.
--   markdown.parse_blocks(src)   → block AST (exposed for tests)
--   markdown.parse_inline(s)      → inline segment array (exposed for tests)
--   markdown.sanitize_mastodon_html(s) → allowlist sanitizer
--
-- Degrade invariant: a construct a platform can't express strips to plain
-- text — it never leaks literal syntax (`*this*` arriving as asterisks is
-- a bug). Platform-native constructs (Discord's # headers, --- rules) stay.
--
-- Deliberate limits (documented, tested):
--   * One nesting level inside inline styles (bold text renders plainly).
--   * Link text renders unstyled (one level, flat).
--   * Link URLs must be http(s):// — non-web schemes are unexpressible
--     everywhere and an injection risk in HTML; they degrade to plain
--     text at parse time.
--   * `_italic_` honors the intraword rule (no emphasis inside words —
--     snake_case identifiers survive intact); `*` has no such rule
--     (CommonMark behavior).
--
-- Pure Lua string/table only — no kernel globals, no require. Same bytes
-- in scripts/ and state/lua/_shared/ (LuaAdapterSyncGuard enforces parity).
-- ===========================================================================

local M = {}

-- ---------------------------------------------------------------------------
-- HTML escaping — applied to every text segment at CONSTRUCTION time, so
-- nothing agent/user-authored can reach output as live markup.
-- ---------------------------------------------------------------------------
local function esc_html(s)
    s = s:gsub("&", "&amp;")
    s = s:gsub("<", "&lt;")
    s = s:gsub(">", "&gt;")
    s = s:gsub('"', "&quot;")
    return s
end

local function is_blank(line) return line:match("^%s*$") ~= nil end

-- ---------------------------------------------------------------------------
-- Block parser: source → array of blocks.
--   {type="heading",   level=n, text=s}
--   {type="paragraph", text=s}
--   {type="code",      lang=s, text=s}
--   {type="list",      ordered=b, items={s,...}}
--   {type="quote",     lines={s,...}}
--   {type="table",     rows={{cell,...},...}}
--   {type="hr"}
-- ---------------------------------------------------------------------------
local function parse_blocks(src)
    local blocks = {}
    local lines = {}
    for line in src:gmatch("([^\n]*)\n?") do lines[#lines + 1] = line end
    local i, n = 1, #lines
    local para = {}

    local function flush_para()
        if #para > 0 then
            blocks[#blocks + 1] = { type = "paragraph", text = table.concat(para, " ") }
            para = {}
        end
    end

    while i <= n do
        local line = lines[i]

        if is_blank(line) then
            flush_para()
            i = i + 1

        elseif line:match("^%s*```") then                      -- fenced code
            flush_para()
            local lang = line:match("^%s*```%s*(%S*)") or ""
            local body = {}
            i = i + 1
            while i <= n and not lines[i]:match("^%s*```") do
                body[#body + 1] = lines[i]
                i = i + 1
            end
            i = i + 1                                          -- closing fence / EOF
            blocks[#blocks + 1] = { type = "code", lang = lang, text = table.concat(body, "\n") }

        elseif line:match("^%s*#+%s+") then                -- heading (Lua patterns
            flush_para()                                     -- have no {n,m}; #+ is it)
            local level = #line:match("^%s*(#+)")
            local text = line:match("^%s*#+%s+(.-)%s*$") or ""
            blocks[#blocks + 1] = { type = "heading", level = level, text = text }
            i = i + 1

        elseif line:match("^%s*([-*_])%s*%1%s*%1[%s%-*_]*$") then      -- hr (--- *** ___)
            flush_para()
            blocks[#blocks + 1] = { type = "hr" }
            i = i + 1

        elseif line:match("^%s*>") then                       -- blockquote
            flush_para()
            local qlines = {}
            while i <= n and lines[i]:match("^%s*>") do
                qlines[#qlines + 1] = lines[i]:match("^%s*>%s?(.*)$") or ""
                i = i + 1
            end
            blocks[#blocks + 1] = { type = "quote", lines = qlines }

        elseif line:match("^%s*[-*]%s+") then                 -- unordered list
            flush_para()
            local items = {}
            while i <= n and lines[i]:match("^%s*[-*]%s+") do
                items[#items + 1] = lines[i]:match("^%s*[-*]%s+(.-)%s*$") or ""
                i = i + 1
            end
            blocks[#blocks + 1] = { type = "list", ordered = false, items = items }

        elseif line:match("^%s*%d+[.)]%s+") then              -- ordered list
            flush_para()
            local items = {}
            while i <= n and lines[i]:match("^%s*%d+[.)]%s+") do
                items[#items + 1] = lines[i]:match("^%s*%d+[.)]%s+(.-)%s*$") or ""
                i = i + 1
            end
            blocks[#blocks + 1] = { type = "list", ordered = true, items = items }

        elseif line:match("^%s*|") and lines[i + 1]
            and lines[i + 1]:match("^%s*|[%s:|-]*$") then     -- table
            flush_para()
            local rows = {}
            local function split_row(l)
                local cells = {}
                for cell in l:gmatch("|%s*([^|]*)%s*") do
                    cells[#cells + 1] = cell:gsub("^%s*(.-)%s*$", "%1")  -- trim
                end
                if #cells > 0 and cells[#cells] == "" then cells[#cells] = nil end
                return cells
            end
            rows[#rows + 1] = split_row(line)                  -- header
            i = i + 2                                          -- skip separator
            while i <= n and lines[i]:match("^%s*|") do
                rows[#rows + 1] = split_row(lines[i])
                i = i + 1
            end
            blocks[#blocks + 1] = { type = "table", rows = rows }

        else                                                  -- paragraph line
            para[#para + 1] = line:gsub("^%s+", ""):gsub("%s+$", "")
            i = i + 1
        end
    end
    flush_para()
    return blocks
end

-- ---------------------------------------------------------------------------
-- Inline parser: text → segment array. Code spans protected first, then
-- links/bold/italic by earliest match (priority link > bold > *italic* >
-- _italic_ on ties). URLs are http(s) only; intraword `_` never emphasizes.
--   {type="text"|"bold"|"italic"|"code"|"link", text=s, url=s?}
-- ---------------------------------------------------------------------------
local function parse_inline(s)
    -- Pass 1: extract code spans and MASK them with \1N\2 placeholders.
    -- Emphasis pairing runs on the masked string: `**bold `code`**` must
    -- pair its markers ACROSS the code span. (Extracting code spans as
    -- segments first used to split the bold pair in half; each orphaned
    -- `**` then matched the single-star italic pattern as empty emphasis.)
    local codes = {}
    local masked = {}
    local pos, n = 1, 0
    while true do
        local tick = s:find("`", pos, true)
        if not tick then
            masked[#masked + 1] = s:sub(pos)
            break
        end
        masked[#masked + 1] = s:sub(pos, tick - 1)
        local close = s:find("`", tick + 1, true)
        if not close then
            masked[#masked + 1] = s:sub(tick)
            break
        end
        n = n + 1
        codes["\1" .. n .. "\2"] = s:sub(tick + 1, close - 1)
        masked[#masked + 1] = "\1" .. n .. "\2"
        pos = close + 1
    end
    local m = table.concat(masked)

    -- Expand masked text into text/code sub-segments (styled spans that
    -- contain code spans carry .subs; renderers walk them).
    local function expand(mt)
        local subs, last, f = {}, 1, mt:find("\1", 1, true)
        while f do
            local e = mt:find("\2", f, true)
            if not e then break end
            local ph = mt:sub(f, e)
            if codes[ph] then
                if f > last then subs[#subs + 1] = { type = "text", text = mt:sub(last, f - 1) } end
                subs[#subs + 1] = { type = "code", text = codes[ph] }
                last = e + 1
            end
            f = mt:find("\1", e + 1, true)
        end
        if last <= #mt then subs[#subs + 1] = { type = "text", text = mt:sub(last) } end
        return subs
    end

    -- Plain label for links: code spans keep their content verbatim,
    -- paired star markers strip (no platform can nest inside a link
    -- label; markers arriving literally would be syntax leakage).
    local function plain_label(mt)
        local parts = {}
        for _, seg in ipairs(expand(mt)) do
            if seg.type == "code" then
                parts[#parts + 1] = seg.text
            else
                parts[#parts + 1] = seg.text:gsub("%*%*(.-)%*%*", "%1"):gsub("%*(.-)%*", "%1")
            end
        end
        return table.concat(parts)
    end

    -- Pass 2 output accumulator (declared before emit_text closes over it —
    -- a local defined after the closure would be invisible to it).
    local out = {}

    -- Emit a top-level text run; if it carries code placeholders the run
    -- expands into text/code segments (top-level code spans must reach
    -- renderers as code segments, not placeholder bytes).
    local function emit_text(t)
        if t:find("\1", 1, true) then
            for _, seg in ipairs(expand(t)) do out[#out + 1] = seg end
        else
            out[#out + 1] = { type = "text", text = t }
        end
    end

    -- Pass 2: scan the masked string; earliest candidate wins (strict <
    -- keeps priority on ties: bold beats italic on `**`).
    local p = 1
    while p <= #m do
        local chunk = m:sub(p)
        local bs, be, btype, btext, burl

        -- Markdown link, any scheme. https?:// renders as a link; every
        -- other scheme degrades to the plain label — the URL itself is
        -- NEVER emitted on any platform (javascript:, data:, ...).
        local s1, e1, ltext, lurl = chunk:find("%[([^%]]+)%]%((%S-)%)")
        if s1 and (not bs or s1 < bs) then
            if lurl:match("^https?://") then
                bs, be, btype, btext, burl = s1, e1, "link", ltext, lurl
            else
                bs, be, btype, btext = s1, e1, "droplink", ltext
            end
        end

        -- Bare URL (trailing sentence punctuation is not part of it)
        local s2, e2, uurl = chunk:find("(https?://%S+)")
        if s2 and (not bs or s2 < bs) then
            uurl = uurl:gsub("[.,;:!?]+$", "")
            e2 = s2 + #uurl - 1
            bs, be, btype, btext, burl = s2, e2, "link", uurl, uurl
        end

        -- Bold: content is masked; a placeholder inside means the span
        -- carries code — sub-segments attach at emission.
        local s3, e3, bbold = chunk:find("%*%*(.-)%*%*")
        if s3 and (not bs or s3 < bs) then bs, be, btype, btext = s3, e3, "bold", bbold end

        -- Italic (single star). Content excludes stars and edge spaces:
        -- `**` can never match here, `2 * 3 * 4` is not emphasis.
        local s4, e4, ital = chunk:find("%*([^%*]+)%*")
        if s4 and (not bs or s4 < bs) and not ital:match("^%s") and not ital:match("%s$") then
            bs, be, btype, btext = s4, e4, "italic", ital
        end

        -- `_italic_` with the intraword guard: no emphasis when the
        -- opening `_` follows a word char or the closing `_` precedes
        -- one (snake_case survives untouched).
        local s5, e5, ital5 = chunk:find("_(.-)_")
        if s5 and (not bs or s5 < bs) then
            local prev_ok = (s5 == 1) or (not chunk:sub(s5 - 1, s5 - 1):match("[%w_]"))
            local next_ok = (e5 >= #chunk) or (not chunk:sub(e5 + 1, e5 + 1):match("[%w_]"))
            if prev_ok and next_ok then
                bs, be, btype, btext = s5, e5, "italic", ital5
            end
        end

        if not bs then
            emit_text(chunk)
            break
        end
        if bs > 1 then emit_text(chunk:sub(1, bs - 1)) end
        if btype == "link" then
            out[#out + 1] = { type = "link", text = plain_label(btext), url = burl }
        elseif btype == "droplink" then
            out[#out + 1] = { type = "text", text = plain_label(btext) }
        else
            local seg = { type = btype, text = btext }
            if btext:find("\1", 1, true) then seg.subs = expand(btext) end
            out[#out + 1] = seg
        end
        p = p + be
    end
    return out
end

-- ---------------------------------------------------------------------------
-- Inline renderers (segments → platform string). Every renderer receives
-- already-parsed segments; each decides what constructs it can express.
-- ---------------------------------------------------------------------------
-- Styled spans may carry sub-segments when they contain code spans
-- (`**bold `code`**`): plain_fn renders text parts, code_fn code parts,
-- inside the span's own markers.
local function inner(s, plain_fn, code_fn)
    if s.subs then
        local parts = {}
        for _, seg in ipairs(s.subs) do
            if seg.type == "code" then parts[#parts + 1] = code_fn(seg.text)
            else parts[#parts + 1] = plain_fn(seg.text) end
        end
        return table.concat(parts)
    end
    return plain_fn(s.text)
end

local function idf(t) return t end
local function tick(t) return "`" .. t .. "`" end

local function render_inline_slack(segs)
    local parts = {}
    for _, s in ipairs(segs) do
        if s.type == "bold" then parts[#parts + 1] = "*" .. inner(s, idf, tick) .. "*"
        elseif s.type == "italic" then parts[#parts + 1] = "_" .. inner(s, idf, tick) .. "_"
        elseif s.type == "code" then parts[#parts + 1] = "`" .. s.text .. "`"
        elseif s.type == "link" then
            if s.text == s.url then parts[#parts + 1] = "<" .. s.url .. ">"
            else parts[#parts + 1] = "<" .. s.url .. "|" .. s.text .. ">" end
        else parts[#parts + 1] = s.text end
    end
    return table.concat(parts)
end

local function render_inline_discord(segs)
    local parts = {}
    for _, s in ipairs(segs) do
        if s.type == "bold" then parts[#parts + 1] = "**" .. inner(s, idf, tick) .. "**"
        elseif s.type == "italic" then parts[#parts + 1] = "*" .. inner(s, idf, tick) .. "*"
        elseif s.type == "code" then parts[#parts + 1] = "`" .. s.text .. "`"
        elseif s.type == "link" then parts[#parts + 1] = "[" .. s.text .. "](" .. s.url .. ")"
        else parts[#parts + 1] = s.text end
    end
    return table.concat(parts)
end

-- Mastodon: construction-time escaping; <code> is NOT in the allowlist
-- (p, br, b, i, a, span.h-card), so code spans degrade to escaped plain.
local function render_inline_mastodon(segs)
    local parts = {}
    for _, s in ipairs(segs) do
        if s.type == "bold" then parts[#parts + 1] = "<b>" .. inner(s, esc_html, esc_html) .. "</b>"
        elseif s.type == "italic" then parts[#parts + 1] = "<i>" .. inner(s, esc_html, esc_html) .. "</i>"
        elseif s.type == "code" then parts[#parts + 1] = esc_html(s.text)
        elseif s.type == "link" then
            parts[#parts + 1] = '<a href="' .. esc_html(s.url) ..
                '" rel="nofollow noopener noreferrer" target="_blank">' ..
                esc_html(s.text) .. "</a>"
        else parts[#parts + 1] = esc_html(s.text) end
    end
    return table.concat(parts)
end

-- Bluesky: plain text. Formatting strips; links keep the URL visible so the
-- adapter's #36 facet pass byte-indexes the FINAL text.
local function render_inline_bluesky(segs)
    local parts = {}
    for _, s in ipairs(segs) do
        if s.type == "link" then
            if s.text == s.url then parts[#parts + 1] = s.url
            else parts[#parts + 1] = s.text .. " (" .. s.url .. ")" end
        elseif s.type == "bold" or s.type == "italic" then
            parts[#parts + 1] = inner(s, idf, idf)
        else parts[#parts + 1] = s.text end
    end
    return table.concat(parts)
end

-- ---------------------------------------------------------------------------
-- Block renderers: one per platform.
-- ---------------------------------------------------------------------------
local function render_blocks(blocks, ri)
    -- Render each block to a string first: blocks that render to nothing
    -- (hr on platforms without a divider) emit no separator either — but
    -- blocks that DO render always get separated from what came before.
    local out = {}
    for _, b in ipairs(blocks) do
        local t
        if b.type == "heading" then t = ri.heading(b)
        elseif b.type == "paragraph" then t = ri.paragraph(b)
        elseif b.type == "code" then t = ri.code(b)
        elseif b.type == "list" then t = ri.list(b)
        elseif b.type == "quote" then t = ri.quote(b)
        elseif b.type == "table" then t = ri.table(b)
        elseif b.type == "hr" then t = ri.hr and ri.hr(b)
        end
        if t and t ~= "" then
            if #out > 0 then out[#out + 1] = "\n\n" end
            out[#out + 1] = t
        end
    end
    return table.concat(out)
end

local function renderers(inline_fn)
    local ri = {}
    -- Shared plain-row degrade for platforms without tables.
    local plain_rows = function(b)
        local rows = {}
        for _, r in ipairs(b.rows) do rows[#rows + 1] = table.concat(r, " | ") end
        return table.concat(rows, "\n")
    end
    ri.paragraph = function(b) return inline_fn(parse_inline(b.text)) end
    ri.table = plain_rows
    return ri
end

-- Slack (mrkdwn) -------------------------------------------------------------
local M_ri_slack = renderers(render_inline_slack)
M_ri_slack.heading = function(b) return "*" .. render_inline_slack(parse_inline(b.text)) .. "*" end
M_ri_slack.code = function(b)
    local fence = "```"
    if b.lang ~= "" then fence = "```" .. b.lang end
    return fence .. "\n" .. b.text .. "\n```"
end
M_ri_slack.list = function(b)
    local lines = {}
    for idx, item in ipairs(b.items) do
        local bullet = b.ordered and (idx .. ". ") or "• "
        lines[#lines + 1] = bullet .. render_inline_slack(parse_inline(item))
    end
    return table.concat(lines, "\n")
end
M_ri_slack.quote = function(b)
    local lines = {}
    for _, l in ipairs(b.lines) do lines[#lines + 1] = "> " .. render_inline_slack(parse_inline(l)) end
    return table.concat(lines, "\n")
end

-- Discord (Markdown-with-quirks) ----------------------------------------------
local M_ri_discord = renderers(render_inline_discord)
M_ri_discord.heading = function(b)
    return string.rep("#", b.level) .. " " .. render_inline_discord(parse_inline(b.text))
end
M_ri_discord.code = function(b)
    local fence = "```"
    if b.lang ~= "" then fence = "```" .. b.lang end
    return fence .. "\n" .. b.text .. "\n```"
end
M_ri_discord.list = function(b)
    local lines = {}
    for idx, item in ipairs(b.items) do
        local bullet = b.ordered and (idx .. ". ") or "- "
        lines[#lines + 1] = bullet .. render_inline_discord(parse_inline(item))
    end
    return table.concat(lines, "\n")
end
M_ri_discord.quote = function(b)
    local lines = {}
    for _, l in ipairs(b.lines) do lines[#lines + 1] = "> " .. render_inline_discord(parse_inline(l)) end
    return table.concat(lines, "\n")
end
M_ri_discord.hr = function() return "---" end   -- Discord renders natively

-- Mastodon (strict-allowlist HTML) ---------------------------------------------
local M_ri_mastodon = renderers(render_inline_mastodon)
M_ri_mastodon.heading = function(b) return "<p><b>" .. render_inline_mastodon(parse_inline(b.text)) .. "</b></p>" end
M_ri_mastodon.paragraph = function(b) return "<p>" .. render_inline_mastodon(parse_inline(b.text)) .. "</p>" end
M_ri_mastodon.code = function(b)
    -- No <pre>/<code> in the allowlist: escaped plain, <br>-joined lines.
    local lines = {}
    for l in b.text:gmatch("([^\n]*)\n?") do lines[#lines + 1] = esc_html(l) end
    if #lines > 0 and lines[#lines] == "" then lines[#lines] = nil end
    return "<p>" .. table.concat(lines, "<br>") .. "</p>"
end
M_ri_mastodon.list = function(b)
    local lines = {}
    for idx, item in ipairs(b.items) do
        local bullet = b.ordered and (idx .. ". ") or "• "
        lines[#lines + 1] = bullet .. render_inline_mastodon(parse_inline(item))
    end
    return "<p>" .. table.concat(lines, "<br>") .. "</p>"
end
M_ri_mastodon.quote = function(b)
    -- Degrade invariant: quote markers are syntax; they strip to plain
    -- text lines (the `> ` prefix would leak literal syntax).
    local lines = {}
    for _, l in ipairs(b.lines) do lines[#lines + 1] = render_inline_mastodon(parse_inline(l)) end
    return "<p>" .. table.concat(lines, "<br>") .. "</p>"
end
M_ri_mastodon.table = function(b)
    -- Tables degrade to plain rows — but cells are agent content: they
    -- reach the wire escaped and <p>-wrapped, never raw HTML surface.
    local rows = {}
    for _, r in ipairs(b.rows) do
        rows[#rows + 1] = render_inline_mastodon(parse_inline(table.concat(r, " | ")))
    end
    return "<p>" .. table.concat(rows, "<br>") .. "</p>"
end

-- Bluesky (plain text) ---------------------------------------------------------
local M_ri_bluesky = renderers(render_inline_bluesky)
M_ri_bluesky.heading = function(b) return render_inline_bluesky(parse_inline(b.text)) end
M_ri_bluesky.code = function(b)
    -- Indent as plain text: no fence syntax leaks into the post.
    local lines = {}
    for l in b.text:gmatch("([^\n]*)\n?") do lines[#lines + 1] = "    " .. l end
    if #lines > 0 and lines[#lines]:match("^%s*$") then lines[#lines] = nil end
    return table.concat(lines, "\n")
end
M_ri_bluesky.list = function(b)
    local lines = {}
    for idx, item in ipairs(b.items) do
        local bullet = b.ordered and (idx .. ". ") or "• "
        lines[#lines + 1] = bullet .. render_inline_bluesky(parse_inline(item))
    end
    return table.concat(lines, "\n")
end
M_ri_bluesky.quote = function(b)
    local lines = {}
    for _, l in ipairs(b.lines) do lines[#lines + 1] = render_inline_bluesky(parse_inline(l)) end
    return table.concat(lines, "\n")
end

-- ---------------------------------------------------------------------------
-- Public render API.
-- ---------------------------------------------------------------------------
function M.render_slack(src)    return render_blocks(parse_blocks(src), M_ri_slack)    end
function M.render_discord(src) return render_blocks(parse_blocks(src), M_ri_discord) end
function M.render_mastodon(src)
    return M.sanitize_mastodon_html(render_blocks(parse_blocks(src), M_ri_mastodon))
end
function M.render_bluesky(src) return render_blocks(parse_blocks(src), M_ri_bluesky) end

M.parse_blocks = parse_blocks
M.parse_inline = parse_inline
M.esc_html = esc_html

-- ---------------------------------------------------------------------------
-- sanitize_mastodon_html — defense-in-depth allowlist walker. The render
-- path already constructs escaped, allowlisted HTML; this re-validates any
-- tag-shaped sequence that survived, so a hostile string can never smuggle
-- markup, a non-http scheme, or an off-allowlist attribute through.
-- ---------------------------------------------------------------------------
local ALLOWED_TAGS = { p = true, br = true, b = true, i = true, a = true, span = true }
local ALLOWED_ATTRS = {
    a = { href = true, rel = true, target = true },
    span = { class = true },
}

function M.sanitize_mastodon_html(s)
    local out = s:gsub("<(/?)(%a+)([^>]*)>", function(slash, name, attrs)
        name = name:lower()
        if not ALLOWED_TAGS[name] then return "" end          -- drop tag, keep content
        if slash == "/" then return "</" .. name .. ">" end
        if name == "br" then return "<br>" end               -- br is bare
        local clean = {}
        for key, val in attrs:gmatch('(%a+)%s*=%s*"([^"]*)"') do
            if ALLOWED_ATTRS[name] and ALLOWED_ATTRS[name][key] then
                if key == "href" and not val:match("^https?://") then
                    -- non-http scheme: drop the href; the link degrades to text
                else
                    clean[#clean + 1] = key .. '="' .. val .. '"'
                end
            elseif key == "href" or key == "class" then
                -- allowlist attribute with off-allowlist value: skip it
            end
            -- everything else: dropped
        end
        -- span is allowlisted for .h-card only; sanitize enforces class too
        if name == "span" then
            local has_hcard = false
            for _, kv in ipairs(clean) do if kv == 'class="h-card"' then has_hcard = true end end
            if not has_hcard then return "<span>" end
        end
        if #clean == 0 then return "<" .. name .. ">" end
        return "<" .. name .. " " .. table.concat(clean, " ") .. ">"
    end)
    return out
end

-- Global assignment: the kernel Evals this source into the per-agent VM
-- and DISCARDS top-level returns — the global is the integration seam.
-- The `return M` keeps standalone dofile() harnesses working.
markdown = M

return M

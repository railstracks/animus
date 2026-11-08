-- ============================================================================
-- Mastodon Social Adapter for Animus
-- Issue #135 — Phase 1: Lua REST + polling
-- Issue #145 — Tier 1 discovery: search (v2, WebFinger resolve for remote
-- handles), account_statuses (per-account reads), public_timeline with
-- typed-422 handling (nodes that reject timelines get a classified error
-- with account_statuses as the guided fallback — never a generic failure).
--
-- Registers via animus.register_channel() as a stateless adapter.
-- Auth: static user access token (Mastodon "user token", scope read+write).
--
-- Kestrel-direct (not Devex-dispatched). Scope fence: NEW FILES ONLY —
-- no kernel edits; polling is caller-driven via the `poll` action
-- (the kernel's generic RestPollLoop is a stub; Bluesky-style dedicated
-- kernel loops are out of scope for phase 1).
--
-- Config keys (per-instance, platform_id like "mastodon:PERSONAL"):
--   config.get("channels.mastodon:PERSONAL.access_token")     -- user token
--   config.get("channels.mastodon:PERSONAL.instance_url")     -- e.g. https://mastodon.social
--   config.get("channels.mastodon:PERSONAL.poll_interval")    -- seconds, default 60, min 15
--
-- Watermark (dedupe, survives restarts):
--   config.get("channels.mastodon:PERSONAL.last_notif_id")   -- max notification id seen
--
-- Backoff state (persisted; 429/5xx handling):
--   config.get("channels.mastodon:PERSONAL.backoff_until")   -- ISO timestamp, empty = clear
--   config.get("channels.mastodon:PERSONAL.backoff_seconds") -- last computed backoff
--
-- Attribution spine (#133): surfaced mentions carry author + timestamp
-- first-class: author, author_display, author_url, created_at.
--
-- Chunk discipline (#131 family guard): outgoing posts auto-split at
-- natural boundaries; NO byte-trim sends. Mastodon default toot limit
-- is 500 chars — fetched from /api/v1/instance and cached.
-- ============================================================================

-- ---------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------

--- Build a config key for this instance.
local function cfg_key(platform_id, key)
    return "channels." .. platform_id .. "." .. key
end

--- Extract the instance ID from a platform_id like "mastodon:PERSONAL".
local function instance_id(platform_id)
    local _, sep = platform_id:find(":")
    return sep and platform_id:sub(sep + 1) or "default"
end

--- Safely decode JSON. Returns table or nil + error_string.
local function json_decode_safe(str)
    if not str or str == "" then return nil, "empty body" end
    local ok, data = pcall(json.decode, str)
    if ok then return data end
    return nil, "json decode failed: " .. tostring(data)
end

--- Truncate a string to max_len, appending "..." if truncated.
-- FOR ERROR MESSAGES ONLY. Never used on outgoing message content
-- (chunk discipline: split, don't trim).
local function truncate(str, max_len)
    if not str then return "" end
    if #str <= max_len then return str end
    return str:sub(1, max_len - 3) .. "..."
end

--- ISO 8601 UTC timestamp now.
local function iso_now()
    return os.date("!%Y-%m-%dT%H:%M:%SZ")
end

--- Parse an ISO 8601 timestamp to a unix epoch (best-effort, UTC assumed
--- when no offset; numeric offsets honored).
local function iso_to_epoch(ts)
    if not ts or ts == "" then return nil end
    local y, mo, d, h, mi, s, off_h, off_m, off_sign =
        ts:match("^(%d%d%d%d)%-(%d%d)%-(%d%d)T(%d%d):(%d%d):(%d%d)[%.%d]*([%+%-]?)(%d?%d?):?(%d?%d?)")
    if not y then return nil end
    -- Convert to epoch via os.time on a UTC-normalized table (os.time is
    -- local-tz; the standard trick is time() + difftime vs a gmtime anchor).
    local t = os.time({
        year = tonumber(y), month = tonumber(mo), day = tonumber(d),
        hour = tonumber(h), min = tonumber(mi), sec = tonumber(s),
        isdst = false
    })
    if not t then return nil end
    -- Normalize local→UTC
    local utc = t + (t - os.time(os.date("!*t", t)))
    -- Apply the offset encoded in the string (e.g. +02:00)
    if off_sign ~= "" and (off_h ~= "" or off_m ~= "") then
        local offs = (tonumber(off_h) or 0) * 3600 + (tonumber(off_m) or 0) * 60
        if off_sign == "+" then utc = utc - offs elseif off_sign == "-" then utc = utc + offs end
    end
    return utc
end

-- ---------------------------------------------------------------------------
-- Auth
-- ---------------------------------------------------------------------------

--- Get the access token for this instance. Loud on missing config.
local function get_token(platform_id)
    local token = config.get(cfg_key(platform_id, "access_token"))
    if not token or token == "" then
        return nil, "no access_token configured for " .. platform_id
            .. " (set channels." .. platform_id .. ".access_token)"
    end
    return token
end

--- Get the instance base URL; default to mastodon.social but warn loudly
--- if unset so the misconfiguration is visible in logs.
local function get_instance_url(platform_id)
    local url = config.get(cfg_key(platform_id, "instance_url"))
    if not url or url == "" then
        log.warn("[mastodon] no instance_url configured for " .. platform_id
            .. " — defaulting to https://mastodon.social")
        url = "https://mastodon.social"
    end
    -- Strip trailing slash
    if url:sub(-1) == "/" then url = url:sub(1, -2) end
    return url
end

-- ---------------------------------------------------------------------------
-- Rate Limit / Backoff
-- ---------------------------------------------------------------------------

-- Mastodon returns per-request headers on 429/503 responses:
--   X-RateLimit-Limit / X-RateLimit-Remaining / X-RateLimit-Reset (ISO ts)
-- plus Retry-After (seconds) on 503. State is persisted so backoff
-- survives script re-invocation and kernel restart.

local function load_backoff(platform_id)
    local until_ts = config.get(cfg_key(platform_id, "backoff_until"))
    if not until_ts or until_ts == "" then return 0 end
    local until_epoch = iso_to_epoch(until_ts)
    if not until_epoch then return 0 end
    local now = os.time()
    if now >= until_epoch then
        -- Backoff expired — clear it
        config.set(cfg_key(platform_id, "backoff_until"), "")
        return 0
    end
    return until_epoch - now
end

local function save_backoff(platform_id, seconds)
    config.set(cfg_key(platform_id, "backoff_seconds"), tostring(seconds))
    if seconds <= 0 then
        config.set(cfg_key(platform_id, "backoff_until"), "")
        return
    end
    config.set(cfg_key(platform_id, "backoff_until"), os.date("!%Y-%m-%dT%H:%M:%SZ", os.time() + seconds))
end

--- Compute backoff from a 429/503 response. Escalating on consecutive hits.
local function compute_backoff(platform_id, resp, consecutive)
    -- Retry-After header wins if present (seconds)
    local retry_after = tonumber(resp.headers and resp.headers["retry-after"] or "")
    local seconds
    if retry_after and retry_after > 0 then
        seconds = retry_after
    else
        -- Escalating ladder, capped at 10 minutes: 30 * consecutive
        consecutive = consecutive or 1
        seconds = math.min(600, 30 * consecutive)
    end
    save_backoff(platform_id, seconds)
    return seconds
end

--- Track Mastodon's proactive rate-limit headers (X-RateLimit-Limit /
--- Remaining / Reset, present on every REST response). Persists the snapshot
--- and warns loudly when the bucket is nearly exhausted — same pattern as
--- discord.lua. Mastodon REST limits are generous; this is observability,
--- not gating (the 429 path gates).
local function update_rate_limits(platform_id, resp)
    local h = resp.headers
    if not h then return end
    local remaining = tonumber(h["x-ratelimit-remaining"] or "")
    local limit = tonumber(h["x-ratelimit-limit"] or "")
    if remaining and limit then
        config.set(cfg_key(platform_id, "rate_limit_remaining"), tostring(remaining))
        config.set(cfg_key(platform_id, "rate_limit_limit"), tostring(limit))
        if remaining <= 2 then
            log.warn("[mastodon] rate limit bucket nearly exhausted for "
                .. platform_id .. ": " .. tostring(remaining) .. "/" .. tostring(limit)
                .. " remaining")
        end
    end
end

-- ---------------------------------------------------------------------------
-- HTTP primitives
-- ---------------------------------------------------------------------------

--- Percent-encode a query VALUE. Keys are adapter-controlled (safe
--- charset); values may carry arbitrary text — search q especially —
--- and must not leak spaces/&/# raw into the URL.
local function url_encode(v)
    return (tostring(v):gsub("[^%w%-._~]", function(c)
        return string.format("%%%02X", string.byte(c))
    end))
end

-- Typed 422 classification (#145). Fediverse servers diverge on surface
-- availability (live case: Buffett's node 422s public timelines while
-- per-account reads work — Akkoma/GoToSocial-family divergence or
-- instance policy). auth_get stamps machine-readable error_type/advice
-- on 422s so actions surface a capability divergence as classified
-- guidance, never as a generic failure string.
local TYPED_422 = {
    {
        pattern = "^/api/v1/timelines/",
        error_type = "timeline_unavailable",
        advice = "public timelines are unavailable on this node — commonly "
            .. "the endpoint is auth-gated (flagship Mastodon 422s "
            .. "unauthenticated reads; an invalid token looks the same), or "
            .. "the server family/policy rejects it (Akkoma/GoToSocial); "
            .. "use account_statuses for per-account reads"
    }
}

local function classify_422(endpoint)
    for _, spec in ipairs(TYPED_422) do
        if endpoint:match(spec.pattern) then return spec end
    end
    return {
        error_type = "unsupported_query",
        advice = "the server rejected this query as unprocessable — "
            .. "it may be unsupported on this node"
    }
end

--- Uniform action-failure shaping: 422-classified responses carry
--- error_type + advice (plus http_status); everything else keeps the
--- classic loud (status + body excerpt) format.
local function fail_for(action, resp)
    if resp.error_type then
        return {
            success = false,
            error = action .. " failed (" .. tostring(resp.status) .. ", "
                .. resp.error_type .. "): " .. tostring(resp.error_advice),
            http_status = resp.status,
            error_type = resp.error_type
        }
    end
    return {
        success = false,
        error = action .. " failed (" .. tostring(resp.status) .. "): "
            .. truncate(tostring(resp.body), 500),
        http_status = resp.status
    }
end

--- Authenticated GET. Returns resp table with status, body, headers.
--- Returns { status = 0, body = backoff message } while in backoff.
local function auth_get(platform_id, endpoint, query)
    local wait = load_backoff(platform_id)
    if wait > 0 then
        return { status = 0, body = "in backoff for " .. tostring(wait) .. "s (rate limited earlier)" }
    end

    local token, err = get_token(platform_id)
    if not token then return { status = 0, body = err } end

    local url = get_instance_url(platform_id) .. endpoint
    if query and next(query) then
        local parts = {}
        for k, v in pairs(query) do
            parts[#parts + 1] = tostring(k) .. "=" .. url_encode(v)
        end
        url = url .. "?" .. table.concat(parts, "&")
    end

    local resp = animus.http_get(url, {
        headers = {
            ["Authorization"] = "Bearer " .. token,
            ["Accept"] = "application/json"
        }
    })

    update_rate_limits(platform_id, resp)

    -- Typed classification (read path only: a POST 422 is caller error)
    if resp.status == 422 then
        local cls = classify_422(endpoint)
        resp.error_type = cls.error_type
        resp.error_advice = cls.advice
    end

    if resp.status == 429 or resp.status == 503 then
        -- #255: nil-floored form (see twitter.lua) — "" from config.get
        -- defeated the old `or "0"` guard on fresh instances.
        local consecutive = (tonumber(config.get(cfg_key(platform_id, "consecutive_errors"))) or 0) + 1
        config.set(cfg_key(platform_id, "consecutive_errors"), tostring(consecutive))
        local seconds = compute_backoff(platform_id, resp, consecutive)
        log.warn("[mastodon] rate limited (" .. tostring(resp.status)
            .. ") on " .. endpoint .. " — backing off " .. tostring(seconds) .. "s")
    elseif resp.status == 200 then
        config.set(cfg_key(platform_id, "consecutive_errors"), "0")
    end
    return resp
end

--- Authenticated POST with JSON body.
-- idem_suffix distinguishes same-second chunk posts (Idempotency-Key must
-- be unique per request or Mastodon dedupes thread chunks against each other).
local function auth_post(platform_id, endpoint, body_table, idem_suffix)
    local wait = load_backoff(platform_id)
    if wait > 0 then
        return { status = 0, body = "in backoff for " .. tostring(wait) .. "s (rate limited earlier)" }
    end

    local token, err = get_token(platform_id)
    if not token then return { status = 0, body = err } end

    local url = get_instance_url(platform_id) .. endpoint
    local resp = animus.http_post(url, {
        headers = {
            ["Authorization"] = "Bearer " .. token,
            ["Content-Type"] = "application/json",
            ["Idempotency-Key"] = platform_id .. "-" .. tostring(os.time())
                .. "-" .. tostring(idem_suffix or os.clock())
        },
        body = json.encode(body_table)
    })

    update_rate_limits(platform_id, resp)

    if resp.status == 429 or resp.status == 503 then
        local consecutive = (tonumber(config.get(cfg_key(platform_id, "consecutive_errors"))) or 0) + 1
        config.set(cfg_key(platform_id, "consecutive_errors"), tostring(consecutive))
        local seconds = compute_backoff(platform_id, resp, consecutive)
        log.warn("[mastodon] rate limited (" .. tostring(resp.status)
            .. ") on POST " .. endpoint .. " — backing off " .. tostring(seconds) .. "s")
    elseif resp.status == 200 then
        config.set(cfg_key(platform_id, "consecutive_errors"), "0")
    end
    return resp
end

-- ---------------------------------------------------------------------------
-- Chunk discipline (split, never trim — #131 family guard)
-- ---------------------------------------------------------------------------

--- Split text into chunks of at most `limit` bytes at natural boundaries
--- (paragraph > line > space), preserving code-fence pairing across chunks.
--- Port of discord.lua split_for_limit (#30): the yielded text is the
--- contract with the reader; NO truncation of message content.
local function split_for_limit(text, limit)
    limit = limit or 500
    if limit < 16 then limit = 16 end
    if #text <= limit then return { text } end

    local budget = limit - 8  -- reserve ``` close+reopen headroom
    local chunks, pos, fence_open = {}, 1, false

    local function count_fences(s)
        local n, i = 0, 1
        while true do
            local f = s:find("```", i, true)
            if not f then break end
            n = n + 1
            i = f + 3
        end
        return n
    end

    while pos <= #text do
        local cap = fence_open and (budget - 4) or budget
        local endp = pos + cap - 1
        if endp > #text then endp = #text end

        local brk
        if endp >= #text then
            brk = #text + 1
        else
            -- +2 so a boundary straddling the window edge is found
            local window = text:sub(pos, endp + 2)
            local m = window:match(".*\n\n") or window:match(".*\n") or window:match(".* ")
            if m and #m > 0 then
                brk = pos + #m
            else
                brk = endp + 1
            end
            if brk > endp + 3 then brk = endp + 1 end
        end
        if brk <= pos then brk = pos + 1 end

        local body = text:sub(pos, brk - 1)
        local fences = count_fences(body)
        local open_after = (fences % 2 == 1) ~= fence_open
        local is_last = brk > #text

        -- Opener-straddle guard: if the body ends with a fence opener line,
        -- pull the break back so the fence starts whole in the next chunk; if
        -- the opener sits at the body start, extend to the full cap instead.
        local function opener_tail(s)
            local f = s:match("```([^`]*)$")
            if f == nil then return false end
            return f == "" or (f:sub(-1) == "\n" and f:sub(1, -2):find("\n") == nil)
        end

        if open_after and not is_last and opener_tail(body) then
            local ms, me = body:find("```[^`]*$")
            if ms then
                if ms > 1 then
                    local before = body:sub(1, ms - 1)
                    local nl = before:match(".*\n")
                    local cutpos = nl and #nl or 0
                    if cutpos > 0 then
                        brk = pos + cutpos
                        body = text:sub(pos, brk - 1)
                    else
                        brk = endp + 1
                        body = text:sub(pos, brk - 1)
                    end
                else
                    brk = endp + 1
                    body = text:sub(pos, brk - 1)
                end
                fences = count_fences(body)
                open_after = (fences % 2 == 1) ~= fence_open
            end
        end
        pos = brk

        local chunk = fence_open and ("```\n" .. body) or body
        is_last = pos > #text
        if open_after and not is_last then
            chunk = chunk .. "\n```"
            fence_open = true
        else
            fence_open = open_after
        end
        chunks[#chunks + 1] = chunk
    end
    return chunks
end

--- Fetch the instance's max toot length. Cached; default 500.
-- Mastodon moved this from /api/v1/instance (max_toot_chars) to
-- /api/v2/instance (status_config.max_characters) — try both.
local function get_char_limit(platform_id)
    local cached = config.get(cfg_key(platform_id, "char_limit"))
    if cached and cached ~= "" then return tonumber(cached) or 500 end

    local resp = auth_get(platform_id, "/api/v1/instance", nil)
    if resp.status == 200 then
        local data = json_decode_safe(resp.body)
        if data and data.max_toot_chars then
            local n = tonumber(data.max_toot_chars)
            if n and n >= 16 then
                config.set(cfg_key(platform_id, "char_limit"), tostring(n))
                return n
            end
        end
    end

    -- v2 endpoint (current Mastodon)
    resp = auth_get(platform_id, "/api/v2/instance", nil)
    if resp.status == 200 then
        local data = json_decode_safe(resp.body)
        if data and data.status_config and data.status_config.max_characters then
            local n = tonumber(data.status_config.max_characters)
            if n and n >= 16 then
                config.set(cfg_key(platform_id, "char_limit"), tostring(n))
                return n
            end
        end
    end

    -- Both endpoints failed or unparseable: stay on the safe default. NOT
    -- fatal — posting still works, just conservative. Logged for visibility.
    log.warn("[mastodon] could not fetch instance char limit for "
        .. platform_id .. " — defaulting to 500")
    return 500
end

--- Convert Mastodon's HTML status content to plain text for agent context.
--- Mastodon serves content as HTML (<p>, <br>, mention spans); the #133
--- attribution spine wants the agent reading text, not markup. Raw HTML is
--- preserved alongside in content_html.
local function html_to_text(html)
    if not html or html == "" then return "" end
    local s = html
    s = s:gsub("<br%s*/?>", "\n")
    s = s:gsub("</p>", "\n\n")
    s = s:gsub("<[^>]+>", "")  -- strip remaining tags; mention spans keep inner text
    s = s:gsub("&amp;", "&"):gsub("&lt;", "<"):gsub("&gt;", ">")
    s = s:gsub("&quot;", '"'):gsub("&#39;", "'"):gsub("&nbsp;", " ")
    s = s:gsub("\n\n\n+", "\n\n")
    return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

--- Attribution-spine (#133) mapper for status entities in discovery
--- surfaces — same field contract as poll mentions, so agents read one
--- shape everywhere (status_url matches the poll contract).
local function shape_status(s)
    local account = s.account or {}
    return {
        id = s.id or "",
        author = account.acct or "",
        author_display = account.display_name or "",
        author_url = account.url or "",
        created_at = s.created_at or "",
        text = html_to_text(s.content),
        content_html = s.content or "",
        status_url = s.url or "",
        in_reply_to_id = s.in_reply_to_id or "",
        visibility = s.visibility or ""
    }
end

--- Parse a Mastodon Link pagination header → the next-page max_id.
--- Only an actual rel="next" segment counts; a prev-only header (last
--- page) must NOT yield a phantom cursor.
local function link_next_max_id(link_header)
    if not link_header then return nil end
    local next_url = link_header:match("<([^>]+)>%s*;%s*rel=\"next\"")
    if not next_url then return nil end
    return next_url:match("max_id=(%d+)")
end

--- Shared tail for status-page reads: shape every status onto the spine
--- and extract the next-page cursor from the response.
local function statuses_result(data, resp)
    local statuses = {}
    for _, s in ipairs(data) do
        statuses[#statuses + 1] = shape_status(s)
    end
    local headers = resp.headers or {}
    local link = headers["link"] or headers["Link"]
    local next_max_id = link and link_next_max_id(link) or nil
    return statuses, next_max_id
end

-- ---------------------------------------------------------------------------
-- Actions
-- ---------------------------------------------------------------------------

--- Poll notifications for new mentions. Dedupes against a persisted
--- watermark (last_notif_id), surfaces each new mention with full
--- attribution fields (#133): author, author_display, created_at,
--- status text, status url. Caller-driven: agent or a cron job invokes
--- this at poll_interval cadence.
local function do_poll(args)
    local platform_id = args.platform_id

    -- Watermark read once: drives BOTH the server-side cursor and the
    -- client-side dedupe (belt-and-suspenders — never trust one dedupe
    -- mechanism to be the only one).
    local watermark = tonumber(config.get(cfg_key(platform_id, "last_notif_id")) or "0") or 0

    -- Server-side cursor: min_id asks for items strictly newer than the
    -- watermark (protocol-native; Mastodon honors it on notifications).
    local query = {
        ["types[]"] = "mention",
        limit = "40"
    }
    if watermark > 0 then
        query.min_id = tostring(watermark)
    end

    local resp = auth_get(platform_id, "/api/v1/notifications", query)

    if resp.status ~= 200 then
        return fail_for("poll", resp)
    end

    local data, err = json_decode_safe(resp.body)
    if not data then
        return { success = false, error = "poll: " .. tostring(err) }
    end
    if type(data) ~= "table" then
        return { success = true, output = "no notifications (empty or non-array body)", mentions = {} }
    end

    local max_id = watermark
    local mentions = {}

    -- Notifications come newest-first; walk to oldest-first for stable output
    for i = #data, 1, -1 do
        local n = data[i]
        local nid = tonumber(n.id) or 0
        if nid > watermark then
            -- Watermark advances over EVERY notification above it (mentions
            -- and not) so nothing is ever re-examined after a poll cycle.
            max_id = math.max(max_id, nid)

            -- Defensive type filter: the request carries types[]=mention, but
            -- never trust the server to honor it — only surface actual
            -- mentions, never favourites/boosts as message content.
            if n.type == "mention" then
                local status = n.status or {}
                local account = status.account or {}
                local entry = {
                    id = n.id,
                    author = account.acct or "",
                    author_display = account.display_name or "",
                    author_url = account.url or "",
                    created_at = status.created_at or n.created_at or "",
                    -- Plain text for agent context; raw HTML preserved alongside
                    text = html_to_text(status.content),
                    content_html = status.content or "",
                    status_id = status.id or "",
                    status_url = status.url or "",
                    -- Reply targets (in_reply_to_id may be null for top-level mentions)
                    in_reply_to_id = status.in_reply_to_id or "",
                    visibility = status.visibility or "public"
                }
                mentions[#mentions + 1] = entry
            end
        end
    end

    -- Advance watermark only after successful surface
    if max_id > watermark then
        config.set(cfg_key(platform_id, "last_notif_id"), tostring(max_id))
    end

    return {
        success = true,
        output = #mentions .. " new mention(s); watermark=" .. tostring(max_id),
        mentions = mentions,
        count = #mentions
    }
end

--- Post a new toot. Auto-splits over-limit content into a thread
--- (first toot, then replies to it). NO byte-trim sends (#131 family).
-- Canonical Markdown -> Mastodon allowlist HTML (#156). scripts/markdown.lua
-- is Eval'd into the same per-agent VM and sets the `markdown` global;
-- referenced at SEND time so standalone harnesses (which load only this
-- adapter) keep working through the raw fallback. Rendered BEFORE chunk
-- splitting so every chunk stays <= limit on the FINAL text.
local function render_content(content)
    if type(markdown) == "table" and type(markdown.render_mastodon) == "function" then
        local ok, out = pcall(markdown.render_mastodon, content)
        if ok and type(out) == "string" and out ~= "" then return out end
        if log and log.warn then log.warn("[mastodon] markdown render failed; sending raw content") end
    end
    return content
end

local function do_post(args)
    local platform_id = args.platform_id
    local content = args.content

    if not content or content == "" then
        return { success = false, error = "content is required" }
    end

    local visibility = args.visibility or "public"
    if visibility ~= "public" and visibility ~= "unlisted"
        and visibility ~= "private" and visibility ~= "direct" then
        return { success = false, error = "invalid visibility: " .. tostring(visibility)
            .. " (public|unlisted|private|direct)" }
    end

    content = render_content(content)
    local limit = get_char_limit(platform_id)
    local chunks = split_for_limit(content, limit)

    local first_id, first_url
    for idx, chunk in ipairs(chunks) do
        local body = { status = chunk, visibility = visibility }
        if idx > 1 and first_id then
            body.in_reply_to_id = first_id
        end

        local resp = auth_post(platform_id, "/api/v1/statuses", body, tostring(idx))

        if resp.status ~= 200 then
            return {
                success = false,
                error = "post failed (" .. tostring(resp.status) .. ") on chunk "
                    .. tostring(idx) .. "/" .. tostring(#chunks) .. ": "
                    .. truncate(tostring(resp.body), 500)
                    .. (idx > 1 and (" (" .. tostring(idx - 1) .. " chunk(s) already delivered)") or "")
            }
        end

        local data = json_decode_safe(resp.body)
        if not first_id and data and data.id then
            first_id = data.id
            first_url = data.url
        end
    end

    if #chunks > 1 then
        log.info("[mastodon] status auto-split into " .. tostring(#chunks) .. " toots (thread) for " .. platform_id)
    end

    return {
        success = true,
        status_id = first_id,
        status_url = first_url,
        output = "Posted status " .. tostring(first_id)
            .. (#chunks > 1 and (" (" .. tostring(#chunks) .. " toots threaded)") or "")
    }
end

--- Reply to a specific status. Same chunk discipline as post: first
--- chunk carries in_reply_to_id, the rest thread onto the reply.
local function do_reply(args)
    local platform_id = args.platform_id
    local status_id = args.status_id
    local content = args.content

    if not status_id or status_id == "" then
        return { success = false, error = "status_id is required" }
    end
    if not content or content == "" then
        return { success = false, error = "content is required" }
    end

    content = render_content(content)
    local limit = get_char_limit(platform_id)
    local chunks = split_for_limit(content, limit)

    local reply_id, reply_url
    for idx, chunk in ipairs(chunks) do
        local body = {
            status = chunk,
            in_reply_to_id = (idx == 1) and status_id or reply_id,
            visibility = args.visibility or "public"
        }

        local resp = auth_post(platform_id, "/api/v1/statuses", body, tostring(idx))

        if resp.status ~= 200 then
            return {
                success = false,
                error = "reply failed (" .. tostring(resp.status) .. ") on chunk "
                    .. tostring(idx) .. "/" .. tostring(#chunks) .. ": "
                    .. truncate(tostring(resp.body), 500)
                    .. (idx > 1 and (" (" .. tostring(idx - 1) .. " chunk(s) already delivered)") or "")
            }
        end

        local data = json_decode_safe(resp.body)
        if not reply_id and data and data.id then reply_id = data.id end
        if idx == 1 and data and data.url then
            reply_url = data.url
        end
    end

    return {
        success = true,
        status_id = reply_id,
        status_url = reply_url,
        output = "Posted reply " .. tostring(reply_id) .. " to status " .. status_id
            .. (#chunks > 1 and (" (" .. tostring(#chunks) .. " toots threaded)") or "")
    }
end

--- Fetch a single status with full context (useful after a poll).
local function do_get_status(args)
    local platform_id = args.platform_id
    local status_id = args.status_id

    if not status_id or status_id == "" then
        return { success = false, error = "status_id is required" }
    end

    local resp = auth_get(platform_id, "/api/v1/statuses/" .. tostring(status_id), nil)
    if resp.status ~= 200 then
        return fail_for("get_status", resp)
    end

    local data = json_decode_safe(resp.body)
    if not data then
        return { success = false, error = "get_status: json decode failed" }
    end

    local account = data.account or {}
    return {
        success = true,
        output = json.encode({
            id = data.id,
            author = account.acct or "",
            author_display = account.display_name or "",
            author_url = account.url or "",
            created_at = data.created_at or "",
            text = html_to_text(data.content),
            content_html = data.content or "",
            url = data.url or "",
            visibility = data.visibility or "",
            in_reply_to_id = data.in_reply_to_id or ""
        })
    }
end

--- Fetch the context (ancestors + descendants) around a status.
local function do_get_context(args)
    local platform_id = args.platform_id
    local status_id = args.status_id

    if not status_id or status_id == "" then
        return { success = false, error = "status_id is required" }
    end

    local resp = auth_get(platform_id, "/api/v1/statuses/" .. tostring(status_id) .. "/context", nil)
    if resp.status ~= 200 then
        return fail_for("get_context", resp)
    end

    local data = json_decode_safe(resp.body)
    if not data then
        return { success = false, error = "get_context: json decode failed" }
    end

    local function shape(s)
        local acct = s.account and s.account.acct or ""
        local disp = s.account and s.account.display_name or ""
        return {
            id = s.id, author = acct, author_display = disp,
            created_at = s.created_at or "", text = html_to_text(s.content),
            content_html = s.content or "", url = s.url or ""
        }
    end

    local ancestors, descendants = {}, {}
    if data.ancestors then
        for _, s in ipairs(data.ancestors) do ancestors[#ancestors + 1] = shape(s) end
    end
    if data.descendants then
        for _, s in ipairs(data.descendants) do descendants[#descendants + 1] = shape(s) end
    end

    return {
        success = true,
        output = json.encode({ ancestors = ancestors, descendants = descendants })
    }
end

--- Search accounts/statuses/hashtags (GET /api/v2/search). With a user
--- token: offset pagination works and resolve=true WebFingers remote
--- handles / status URLs into numeric IDs — the entry primitive for
--- account_statuses. Honest-capability contract (#145): full-text STATUS
--- search requires server-side ElasticSearch; accounts/hashtags always
--- search. An empty statuses result carries an explicit caveat instead
--- of a confident empty.
local function do_search(args)
    local platform_id = args.platform_id
    local q = args.q
    if not q or q == "" or tostring(q):match("^%s*$") then
        return { success = false, error = "q is required (query text, or a "
            .. "user@instance handle / status URL with resolve=true)" }
    end
    q = tostring(q)

    local stype = args.type
    if stype and stype ~= "accounts" and stype ~= "statuses" and stype ~= "hashtags" then
        return { success = false, error = "invalid type: " .. tostring(stype)
            .. " (accounts|statuses|hashtags, or omit for all)" }
    end

    local limit = tonumber(args.limit) or 20
    if limit < 1 then limit = 1 elseif limit > 40 then limit = 40 end

    local query = { q = q, limit = tostring(limit) }
    if stype then query.type = stype end
    if args.resolve == true or args.resolve == "true" then query.resolve = "true" end
    if args.following == true or args.following == "true" then query.following = "true" end
    if args.account_id and tostring(args.account_id) ~= "" then
        query.account_id = tostring(args.account_id)
    end
    local offset = tonumber(args.offset)
    if offset and offset > 0 then query.offset = tostring(math.floor(offset)) end

    local resp = auth_get(platform_id, "/api/v2/search", query)
    if resp.status ~= 200 then
        return fail_for("search", resp)
    end

    local data, err = json_decode_safe(resp.body)
    if not data or type(data) ~= "table" then
        return { success = false, error = "search: " .. tostring(err or "non-object body") }
    end

    local accounts, statuses, hashtags = {}, {}, {}
    if data.accounts then
        for _, a in ipairs(data.accounts) do
            accounts[#accounts + 1] = {
                id = a.id or "", acct = a.acct or "",
                display_name = a.display_name or "",
                url = a.url or "",
                followers_count = a.followers_count,
                note_text = html_to_text(a.note)
            }
        end
    end
    if data.statuses then
        for _, s in ipairs(data.statuses) do
            statuses[#statuses + 1] = shape_status(s)
        end
    end
    if data.hashtags then
        for _, h in ipairs(data.hashtags) do
            hashtags[#hashtags + 1] = { name = h.name or "", url = h.url or "" }
        end
    end

    local statuses_note
    if (not stype or stype == "statuses") and #statuses == 0 then
        statuses_note = "no status results — this node may not serve full-text "
            .. "status search (server-side ElasticSearch required, and often "
            .. "auth-gated even where present); account and hashtag "
            .. "search always work"
        if args.resolve ~= true and args.resolve ~= "true"
            and (q:find("@", 1, true) or q:find("http", 1, true)) then
            statuses_note = statuses_note
                .. "; q looks like a handle or URL — retry with resolve=true"
        end
    end

    return {
        success = true,
        output = "search '" .. truncate(q, 60) .. "': " .. #accounts
            .. " account(s), " .. #statuses .. " status(es), " .. #hashtags
            .. " hashtag(s)" .. (statuses_note and (" — " .. statuses_note) or ""),
        accounts = accounts,
        statuses = statuses,
        hashtags = hashtags,
        statuses_note = statuses_note
    }
end

--- Read one account's statuses (GET /api/v1/accounts/:id/statuses).
--- Confirmed working from nodes where public timelines 422 — this is the
--- discovery workhorse. Requires the NUMERIC id; resolve handles via
--- search first. Link-header pagination surfaced as next_max_id.
local function do_account_statuses(args)
    local platform_id = args.platform_id
    local account_id = args.account_id
    if not account_id or tostring(account_id) == "" then
        return { success = false, error = "account_id is required (NUMERIC id — "
            .. "resolve a user@instance handle via search with resolve=true first)" }
    end
    if not tostring(account_id):match("^%d+$") then
        return { success = false, error = "account_id must be the NUMERIC account id, got '"
            .. tostring(account_id) .. "' — run search with q=<handle> and "
            .. "resolve=true to get the numeric id" }
    end

    local limit = tonumber(args.limit) or 20
    if limit < 1 then limit = 1 elseif limit > 40 then limit = 40 end

    local query = { limit = tostring(limit) }
    if args.exclude_replies == true or args.exclude_replies == "true" then
        query.exclude_replies = "true"
    end
    if args.only_media == true or args.only_media == "true" then
        query.only_media = "true"
    end
    if args.pinned == true or args.pinned == "true" then
        query.pinned = "true"
    end
    if args.tagged and tostring(args.tagged) ~= "" then
        local tag = tostring(args.tagged)
        if tag:sub(1, 1) == "#" then tag = tag:sub(2) end
        query.tagged = tag
    end
    for _, cursor in ipairs({ "max_id", "since_id", "min_id" }) do
        local v = args[cursor]
        if v and tostring(v) ~= "" then query[cursor] = tostring(v) end
    end

    local endpoint = "/api/v1/accounts/" .. tostring(account_id) .. "/statuses"
    local resp = auth_get(platform_id, endpoint, query)
    if resp.status ~= 200 then
        return fail_for("account_statuses", resp)
    end

    local data, err = json_decode_safe(resp.body)
    if not data or type(data) ~= "table" then
        return { success = false, error = "account_statuses: "
            .. tostring(err or "non-array body") }
    end

    local statuses, next_max_id = statuses_result(data, resp)
    return {
        success = true,
        output = #statuses .. " status(es) for account " .. tostring(account_id)
            .. (next_max_id and (" (more available — pass max_id=" .. next_max_id .. ")")
                or " (end of timeline)"),
        statuses = statuses,
        count = #statuses,
        next_max_id = next_max_id,
        has_more = next_max_id ~= nil
    }
end

--- Read the public timeline (local or federated). This is the surface
--- some nodes reject (live case: 422 on Buffett's node) — those get a
--- typed timeline_unavailable error with account_statuses as the guided
--- fallback, exercising the #145 classification end-to-end.
local function do_public_timeline(args)
    local platform_id = args.platform_id

    local limit = tonumber(args.limit) or 20
    if limit < 1 then limit = 1 elseif limit > 40 then limit = 40 end

    local query = { limit = tostring(limit) }
    if args["local"] == true or args["local"] == "true" then
        query["local"] = "true"
    end
    if args.remote == true or args.remote == "true" then
        query.remote = "true"
    end
    if args.only_media == true or args.only_media == "true" then
        query.only_media = "true"
    end
    for _, cursor in ipairs({ "max_id", "since_id", "min_id" }) do
        local v = args[cursor]
        if v and tostring(v) ~= "" then query[cursor] = tostring(v) end
    end

    local endpoint = "/api/v1/timelines/public"
    local resp = auth_get(platform_id, endpoint, query)
    if resp.status ~= 200 then
        return fail_for("public_timeline", resp)
    end

    local data, err = json_decode_safe(resp.body)
    if not data or type(data) ~= "table" then
        return { success = false, error = "public_timeline: "
            .. tostring(err or "non-array body") }
    end

    local statuses, next_max_id = statuses_result(data, resp)
    return {
        success = true,
        output = #statuses .. " status(es) on the public timeline"
            .. (next_max_id and (" (more available — pass max_id=" .. next_max_id .. ")")
                or " (end of timeline)"),
        statuses = statuses,
        count = #statuses,
        next_max_id = next_max_id,
        has_more = next_max_id ~= nil
    }
end

--- Get the configured poll interval for this instance (min 15s).
local function get_poll_interval(platform_id)
    local n = tonumber(config.get(cfg_key(platform_id, "poll_interval")))
    if not n or n < 15 then return 60 end
    return n
end

--- Verify credentials + connectivity. Returns account info.
local function do_verify(args)
    local platform_id = args.platform_id

    local resp = auth_get(platform_id, "/api/v1/accounts/verify_credentials", nil)
    if resp.status ~= 200 then
        return fail_for("verify", resp)
    end

    local data = json_decode_safe(resp.body)
    if not data then
        return { success = false, error = "verify: json decode failed" }
    end

    return {
        success = true,
        output = json.encode({
            id = data.id, acct = data.acct, display_name = data.display_name,
            url = data.url, created_at = data.created_at,
            poll_interval = get_poll_interval(platform_id),
            char_limit = get_char_limit(platform_id)
        })
    }
end

-- ---------------------------------------------------------------------------
-- Registration
-- ---------------------------------------------------------------------------

animus.register_channel({
    id = "mastodon",
    name = "Mastodon",
    capabilities = {"read", "write"},
    actions = {
        "poll", "post", "reply", "get_status",
        "get_context", "verify",
        "search", "account_statuses", "public_timeline"
    },
    schema = {
        poll = {
            -- No required params; platform_id is implicit per-instance
        },
        post = {
            { name = "content", type = "string", required = true, description = "Toot text (auto-split into a thread if over the instance limit)" },
            { name = "visibility", type = "string", required = false, description = "public (default), unlisted, private, direct" }
        },
        reply = {
            { name = "status_id", type = "string", required = true, description = "Status ID to reply to" },
            { name = "content", type = "string", required = true, description = "Reply text (auto-split if over limit)" },
            { name = "visibility", type = "string", required = false, description = "public (default), unlisted, private, direct" }
        },
        get_status = {
            { name = "status_id", type = "string", required = true, description = "Status ID to fetch" }
        },
        get_context = {
            { name = "status_id", type = "string", required = true, description = "Status ID to fetch thread context around" }
        },
        search = {
            { name = "q", type = "string", required = true, description = "Query: text, user@instance handle, or status URL (use resolve for the latter two)" },
            { name = "type", type = "string", required = false, description = "accounts | statuses | hashtags (omit for all). NOTE: full-text status search needs server-side ElasticSearch" },
            { name = "resolve", type = "boolean", required = false, description = "WebFinger-resolve remote handles/URLs to numeric IDs" },
            { name = "following", type = "boolean", required = false, description = "Limit account results to accounts you follow" },
            { name = "account_id", type = "string", required = false, description = "Restrict status search to this account (numeric id)" },
            { name = "limit", type = "number", required = false, description = "1-40 per type (default 20)" },
            { name = "offset", type = "number", required = false, description = "Skip results (user-token pagination)" }
        },
        account_statuses = {
            { name = "account_id", type = "string", required = true, description = "NUMERIC account id (resolve handles via search first)" },
            { name = "limit", type = "number", required = false, description = "1-40 (default 20)" },
            { name = "exclude_replies", type = "boolean", required = false, description = "Skip replies" },
            { name = "only_media", type = "boolean", required = false, description = "Only statuses with media" },
            { name = "pinned", type = "boolean", required = false, description = "Only pinned statuses" },
            { name = "tagged", type = "string", required = false, description = "Only statuses with this hashtag (leading # stripped)" },
            { name = "max_id", type = "string", required = false, description = "Pagination cursor from next_max_id" }
        },
        public_timeline = {
            { name = "local", type = "boolean", required = false, description = "This instance only (default: federated)" },
            { name = "remote", type = "boolean", required = false, description = "Remote-origin statuses only" },
            { name = "only_media", type = "boolean", required = false, description = "Only statuses with media" },
            { name = "limit", type = "number", required = false, description = "1-40 (default 20)" },
            { name = "max_id", type = "string", required = false, description = "Pagination cursor from next_max_id" }
        },
        verify = {
            -- No required params
        }
    },
    handler = function(args)
        local action = args.action
        if not action or action == "" then
            return { success = false, error = "action is required" }
        end

        if action == "poll" then return do_poll(args)
        elseif action == "post" then return do_post(args)
        elseif action == "reply" then return do_reply(args)
        elseif action == "get_status" then return do_get_status(args)
        elseif action == "get_context" then return do_get_context(args)
        elseif action == "search" then return do_search(args)
        elseif action == "account_statuses" then return do_account_statuses(args)
        elseif action == "public_timeline" then return do_public_timeline(args)
        elseif action == "verify" then return do_verify(args)
        else return { success = false, error = "unknown action: " .. tostring(action) }
        end
    end
})
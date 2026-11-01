-- ============================================================================
-- Mastodon Social Adapter for Animus
-- Issue #135 — Phase 1: Lua REST + polling
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
            parts[#parts + 1] = tostring(k) .. "=" .. tostring(v)
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
        return {
            success = false,
            error = "poll failed (" .. tostring(resp.status) .. "): "
                .. truncate(tostring(resp.body), 500)
        }
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
        return { success = false, error = "get_status failed (" .. tostring(resp.status) .. "): "
            .. truncate(tostring(resp.body), 500) }
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
        return { success = false, error = "get_context failed (" .. tostring(resp.status) .. "): "
            .. truncate(tostring(resp.body), 500) }
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
        return { success = false, error = "verify failed (" .. tostring(resp.status) .. "): "
            .. truncate(tostring(resp.body), 500) }
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
        "get_context", "verify"
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
        elseif action == "verify" then return do_verify(args)
        else return { success = false, error = "unknown action: " .. tostring(action) }
        end
    end
})
-- ============================================================================
-- Discord Social Adapter for Animus
-- Ticket 060 — Phase 1: Lua REST + polling
--
-- Registers via animus.register_channel() as a stateless adapter.
-- Auth: static bot token (no OAuth, no refresh).
--
-- Config keys (per-instance):
--   config.get("channels.discord:PERSONAL.bot_token")
--   config.get("channels.discord:PERSONAL.application_id")  -- for slash commands (Phase 3)
--   config.get("channels.discord:PERSONAL.monitored_channels") -- JSON array of channel IDs
--   config.get("channels.discord:PERSONAL.respond_to_dm")       -- default true
--   config.get("channels.discord:PERSONAL.respond_to_mentions") -- default true
--
-- Internal cached state:
--   config.get("channels.discord:PERSONAL.bot_user_id")  -- cached from /users/@me
--   config.get("channels.discord:PERSONAL.rate_limits")  -- JSON: bucket → {remaining, reset}
-- ============================================================================

local API_BASE = "https://discord.com/api/v10"

-- ---------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------

--- URL-encode a string (percent-encoding for query parameters).
local function url_encode(str)
    if not str then return "" end
    return (tostring(str):gsub("[^%w%-%._~]", function(c)
        return string.format("%%%02X", string.byte(c))
    end))
end

--- Build a config key for this instance.
local function cfg_key(platform_id, key)
    return "channels." .. platform_id .. "." .. key
end

--- Extract the instance ID from a platform_id like "discord:PERSONAL".
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
local function truncate(str, max_len)
    if not str then return "" end
    if #str <= max_len then return str end
    return str:sub(1, max_len - 3) .. "..."
end

-- ---------------------------------------------------------------------------
-- Rate Limit Tracking
-- ---------------------------------------------------------------------------

-- Per-bucket rate limit state, persisted in config for cross-invocation continuity.
-- Structure: { bucket_id = { remaining=N, reset=TS, reset_after=S } }

local rate_limits = {}

local function load_rate_limits(platform_id)
    local stored = config.get(cfg_key(platform_id, "rate_limits"))
    if stored and stored ~= "" then
        local data, err = json_decode_safe(stored)
        if data then rate_limits = data end
    end
end

local function save_rate_limits(platform_id)
    config.set(cfg_key(platform_id, "rate_limits"), json.encode(rate_limits))
end

--- Parse X-RateLimit-* headers from a response and update state.
local function update_rate_limits(resp, platform_id)
    if not resp.headers then return end

    local bucket = resp.headers["x-ratelimit-bucket"]
    if not bucket or bucket == "" then return end

    local remaining = tonumber(resp.headers["x-ratelimit-remaining"])
    local reset_after = tonumber(resp.headers["x-ratelimit-reset-after"])
    local reset_ts = tonumber(resp.headers["x-ratelimit-reset"])
    local limit = tonumber(resp.headers["x-ratelimit-limit"])

    rate_limits[bucket] = {
        remaining = remaining,
        reset_after = reset_after,
        reset = reset_ts,
        limit = limit
    }

    -- Log warnings when approaching limits
    if remaining and limit and remaining <= 2 then
        log.warn("[discord] rate limit bucket " .. bucket .. " nearly exhausted: "
            .. tostring(remaining) .. "/" .. tostring(limit) .. " remaining")
    end

    save_rate_limits(platform_id)
end

--- Check if we should wait before making a request to a known bucket.
-- Returns seconds to wait (0 if OK to proceed).
local function check_wait(bucket)
    if not bucket or not rate_limits[bucket] then return 0 end
    local state = rate_limits[bucket]
    if state.remaining and state.remaining > 0 then return 0 end
    if state.reset_after and state.reset_after > 0 then
        return state.reset_after
    end
    return 0
end

-- ---------------------------------------------------------------------------
-- Auth
-- ---------------------------------------------------------------------------

--- Get the bot token for this instance.
local function get_token(platform_id)
    local token = config.get(cfg_key(platform_id, "bot_token"))
    if not token or token == "" then
        return nil, "no bot_token configured for " .. platform_id
    end
    return token
end

--- Get the bot's own user ID (cached from /users/@me).
local function get_bot_user_id(platform_id)
    local cached = config.get(cfg_key(platform_id, "bot_user_id"))
    if cached and cached ~= "" then return cached end

    -- Fetch from API
    local token = get_token(platform_id)
    if not token then return nil end

    local resp = auth_get(platform_id, "/users/@me")
    if resp.status ~= 200 then return nil end

    local data = json_decode_safe(resp.body)
    if data and data.id then
        config.set(cfg_key(platform_id, "bot_user_id"), tostring(data.id))
        return tostring(data.id)
    end
    return nil
end

-- ---------------------------------------------------------------------------
-- HTTP primitives
-- ---------------------------------------------------------------------------

--- Authenticated GET. Returns resp table with status, body, headers.
local function auth_get(platform_id, endpoint, extra_headers)
    local token, err = get_token(platform_id)
    if not token then return { status = 0, body = err or "no token" } end

    local headers = { ["Authorization"] = "Bot " .. token }
    if extra_headers then
        for k, v in pairs(extra_headers) do headers[k] = v end
    end

    local url = API_BASE .. endpoint
    local resp = animus.http_get(url, { headers = headers })

    -- Handle global rate limit (429)
    if resp.status == 429 then
        local data = json_decode_safe(resp.body)
        local retry_after = (data and data.retry_after) or 5
        log.warn("[discord] rate limited, retrying after " .. tostring(retry_after) .. "s")
        -- Note: Lua has no sleep. We return the 429 and let the caller decide.
        -- For now, log and return the error.
    end

    update_rate_limits(resp, platform_id)
    return resp
end

--- Authenticated POST with JSON body.
local function auth_post(platform_id, endpoint, body_table)
    local token, err = get_token(platform_id)
    if not token then return { status = 0, body = err or "no token" } end

    local url = API_BASE .. endpoint
    local resp = animus.http_post(url, {
        headers = {
            ["Authorization"] = "Bot " .. token,
            ["Content-Type"] = "application/json"
        },
        body = json.encode(body_table)
    })

    if resp.status == 429 then
        local data = json_decode_safe(resp.body)
        local retry_after = (data and data.retry_after) or 5
        log.warn("[discord] rate limited on POST " .. endpoint .. ", retry_after=" .. tostring(retry_after))
    end

    update_rate_limits(resp, platform_id)
    return resp
end

--- Authenticated DELETE.
local function auth_delete(platform_id, endpoint)
    local token, err = get_token(platform_id)
    if not token then return { status = 0, body = err or "no token" } end

    local url = API_BASE .. endpoint
    local resp = animus.http_delete(url, {
        headers = { ["Authorization"] = "Bot " .. token }
    })

    if resp.status == 429 then
        local data = json_decode_safe(resp.body)
        local retry_after = (data and data.retry_after) or 5
        log.warn("[discord] rate limited on DELETE " .. endpoint .. ", retry_after=" .. tostring(retry_after))
    end

    update_rate_limits(resp, platform_id)
    return resp
end

--- Authenticated PATCH with JSON body.
local function auth_patch(platform_id, endpoint, body_table)
    local token, err = get_token(platform_id)
    if not token then return { status = 0, body = err or "no token" } end

    local url = API_BASE .. endpoint
    local resp = animus.http_patch(url, {
        headers = {
            ["Authorization"] = "Bot " .. token,
            ["Content-Type"] = "application/json"
        },
        body = json.encode(body_table)
    })

    if resp.status == 429 then
        local data = json_decode_safe(resp.body)
        local retry_after = (data and data.retry_after) or 5
        log.warn("[discord] rate limited on PATCH " .. endpoint .. ", retry_after=" .. tostring(retry_after))
    end

    update_rate_limits(resp, platform_id)
    return resp
end

--- Authenticated PUT (for reactions).
local function auth_put(platform_id, endpoint)
    local token, err = get_token(platform_id)
    if not token then return { status = 0, body = err or "no token" } end

    local url = API_BASE .. endpoint
    local resp = animus.http_put(url, {
        headers = {
            ["Authorization"] = "Bot " .. token
        }
    })

    if resp.status == 429 then
        local data = json_decode_safe(resp.body)
        local retry_after = (data and data.retry_after) or 5
        log.warn("[discord] rate limited on PUT " .. endpoint .. ", retry_after=" .. tostring(retry_after))
    end

    update_rate_limits(resp, platform_id)
    return resp
end

-- ---------------------------------------------------------------------------
-- Snowflake helpers
-- ---------------------------------------------------------------------------

--- Extract timestamp from a Discord snowflake ID.
local function snowflake_timestamp(id)
    if not id then return 0 end
    local n = tonumber(id)
    if not n then return 0 end
    -- Discord epoch: 2015-01-01 00:00:00 UTC = 1420070400000
    return math.floor((n / 4194304) + 1420070400000)
end

--- Format a millisecond timestamp as ISO-ish string.
local function format_timestamp(ms)
    if not ms or ms == 0 then return "unknown" end
    return os.date("!%Y-%m-%dT%H:%M:%S", math.floor(ms / 1000))
end

-- ---------------------------------------------------------------------------
-- Message formatting
-- ---------------------------------------------------------------------------

--- Format a Discord message for display.
local function format_message(msg)
    if not msg then return "(nil message)" end

    local author = msg.author and msg.author.username or "unknown"
    local content = msg.content or ""
    local ts = format_timestamp(snowflake_timestamp(msg.id))

    local parts = {}
    parts[#parts+1] = "[" .. ts .. "] " .. author .. ": " .. content

    -- Attachments
    if msg.attachments and #msg.attachments > 0 then
        for _, att in ipairs(msg.attachments) do
            parts[#parts+1] = "  📎 " .. (att.filename or "file") .. ": " .. (att.url or "")
        end
    end

    -- Embeds (show title only)
    if msg.embeds and #msg.embeds > 0 then
        for _, emb in ipairs(msg.embeds) do
            parts[#parts+1] = "  📋 " .. (emb.title or "(embed)")
        end
    end

    return table.concat(parts, "\n")
end

--- Format a channel object for display.
local function format_channel(ch)
    if not ch then return "(nil channel)" end
    local ch_type = ch.type or 0
    local type_names = {
        [0] = "text", [1] = "dm", [2] = "voice", [3] = "group_dm",
        [4] = "category", [5] = "announcement", [10] = "thread_announcement",
        [11] = "thread_public", [12] = "thread_private", [13] = "stage",
        [15] = "forum", [16] = "media"
    }
    local name = ch.name or ("DM with " .. (ch.recipients and ch.recipients[1] and ch.recipients[1].username or "unknown"))
    return "#" .. name .. " (type=" .. (type_names[ch_type] or ch_type) .. ", id=" .. (ch.id or "?") .. ")"
end

--- Format a guild for display.
local function format_guild(guild)
    if not guild then return "(nil guild)" end
    return guild.name or "unnamed" .. " (id=" .. (guild.id or "?") .. ", members=" .. tostring(guild.member_count or "?") .. ")"
end

-- ---------------------------------------------------------------------------
-- Action implementations
-- ---------------------------------------------------------------------------

--- Split text into chunks of at most `limit` bytes, breaking at natural
--- boundaries (paragraph > line > word > hard cut). When a break lands
--- inside an open ``` fence, the chunk closes it and the next chunk reopens
--- it, so rendered code blocks survive reassembly (#30).
local function split_for_limit(text, limit)
    limit = limit or 2000
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
            -- +2 so a paragraph boundary straddling the window edge is found
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

        -- Opener-straddle guard: if the body ends with a fence opener line
        -- (``` plus optional language tag plus newline, no content after it),
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

-- Canonical Markdown -> Discord-native (#156). scripts/markdown.lua is
-- Eval'd into the same per-agent VM and sets the `markdown` global;
-- referenced at SEND time so standalone harnesses (which load only this
-- adapter) keep working through the raw fallback.
--- #157: link card helpers — markdown module guarded the same way as
--- render_content. Degrade path: plain URL append.
local function get_link_card(card_arg)
    if not card_arg then return nil end
    if type(markdown) == "table" and type(markdown.normalize_card) == "function" then
        local ok, card, reason = pcall(markdown.normalize_card, card_arg)
        if not ok then return nil, "link_card validation failed" end
        if not card then return nil, reason end
        return card
    end
    if type(card_arg) ~= "table" or type(card_arg.url) ~= "string"
        or not card_arg.url:match("^https?://") then
        return nil, "link_card.url is required and must be http(s)"
    end
    return card_arg
end

local function append_card_url(content, card)
    if content:find(card.url, 1, true) then return content end
    if type(markdown) == "table" and type(markdown.append_card) == "function" then
        local ok, out = pcall(markdown.append_card, content, card)
        if ok and type(out) == "string" and out ~= "" then return out end
    end
    return content .. "\n\n" .. card.url
end

local function render_content(content)
    if type(markdown) == "table" and type(markdown.render_discord) == "function" then
        local ok, out = pcall(markdown.render_discord, content)
        if ok and type(out) == "string" and out ~= "" then return out end
        if log and log.warn then log.warn("[discord] markdown render failed; sending raw content") end
    end
    return content
end

--- POST /channels/{channel_id}/messages
local function do_post(args)
    local platform_id = args.platform_id
    local channel_id = args.channel_id
    local content = args.content

    if not channel_id or channel_id == "" then
        return { success = false, error = "channel_id is required" }
    end
    if not content or content == "" then
        return { success = false, error = "content is required" }
    end

    local card, cerr = get_link_card(args.link_card)
    if args.link_card and not card then
        return { success = false, error = "invalid link_card: " .. tostring(cerr) }
    end
    -- #157: a card with no title AND no description has nothing visible to
    -- embed — degrade to a plain link appended to the text.
    if card and not card.title and not card.description then
        content = append_card_url(content, card)
        card = nil
    end

    -- Auto-split at natural boundaries instead of truncating: the yielded
    -- text is the contract with the reader (#30). Render canonical
    -- Markdown to Discord-native BEFORE splitting so chunks stay <= 2000 (#156).
    content = render_content(content)
    local chunks = split_for_limit(content, 2000)
    local first_id
    for idx, chunk in ipairs(chunks) do
        local body = { content = chunk }
        if idx == 1 then
            -- #157: link card → native embed first; user-provided embeds follow.
            local embeds = {}
            if card then
                local e = { url = card.url }
                if card.title then e.title = card.title end
                if card.description then e.description = card.description end
                if card.image then e.image = { url = card.image } end
                embeds[#embeds + 1] = e
            end
            if args.embeds then
                for _, ue in ipairs(args.embeds) do embeds[#embeds + 1] = ue end
            end
            if #embeds > 0 then body.embeds = embeds end
        end

        local resp = auth_post(platform_id, "/channels/" .. channel_id .. "/messages", body)

        if resp.status ~= 200 and resp.status ~= 201 then
            return {
                success = false,
                error = "post failed (" .. tostring(resp.status) .. ") on chunk "
                    .. tostring(idx) .. "/" .. tostring(#chunks) .. ": "
                    .. truncate(tostring(resp.body), 500)
                    .. (idx > 1 and (" (" .. tostring(idx - 1) .. " chunk(s) already delivered)") or "")
            }
        end

        local data = json_decode_safe(resp.body)
        if not first_id and data and data.id then first_id = data.id end
    end

    if #chunks > 1 then
        log.info("[discord] message auto-split into " .. tostring(#chunks) .. " chunks for channel " .. channel_id)
    end

    return {
        success = true,
        message_id = first_id,
        channel_id = channel_id,
        output = "Message sent to channel " .. channel_id
            .. (#chunks > 1 and (" (" .. tostring(#chunks) .. " chunks)") or "")
    }
end

--- POST /channels/{channel_id}/messages with message_reference
local function do_reply(args)
    local platform_id = args.platform_id
    local channel_id = args.channel_id
    local message_id = args.message_id
    local content = args.content

    if not channel_id or channel_id == "" then
        return { success = false, error = "channel_id is required" }
    end
    if not message_id or message_id == "" then
        return { success = false, error = "message_id is required" }
    end
    if not content or content == "" then
        return { success = false, error = "content is required" }
    end

    local card, cerr = get_link_card(args.link_card)
    if args.link_card and not card then
        return { success = false, error = "invalid link_card: " .. tostring(cerr) }
    end
    if card and not card.title and not card.description then
        content = append_card_url(content, card)
        card = nil
    end

    -- Render canonical Markdown to Discord-native BEFORE splitting — the
    -- same seam as do_post (#156); #157 needed it here so card links render
    -- in replies too (previously replies sent raw canonical Markdown).
    content = render_content(content)

    -- Auto-split at natural boundaries instead of truncating (#30). The
    -- first chunk carries the reply reference; the rest follow in-channel.
    local chunks = split_for_limit(content, 2000)
    local first_id
    for idx, chunk in ipairs(chunks) do
        local body = { content = chunk }
        if idx == 1 then
            body.message_reference = {
                message_id = message_id,
                channel_id = channel_id
            }
            -- #157: card embed rides the first reply chunk.
            local embeds = {}
            if card then
                local e = { url = card.url }
                if card.title then e.title = card.title end
                if card.description then e.description = card.description end
                if card.image then e.image = { url = card.image } end
                embeds[#embeds + 1] = e
            end
            if args.embeds then
                for _, ue in ipairs(args.embeds) do embeds[#embeds + 1] = ue end
            end
            if #embeds > 0 then body.embeds = embeds end
        end

        local resp = auth_post(platform_id, "/channels/" .. channel_id .. "/messages", body)

        if resp.status ~= 200 and resp.status ~= 201 then
            return {
                success = false,
                error = "reply failed (" .. tostring(resp.status) .. ") on chunk "
                    .. tostring(idx) .. "/" .. tostring(#chunks) .. ": "
                    .. truncate(tostring(resp.body), 500)
                    .. (idx > 1 and (" (" .. tostring(idx - 1) .. " chunk(s) already delivered)") or "")
            }
        end

        local data = json_decode_safe(resp.body)
        if not first_id and data and data.id then first_id = data.id end
    end

    if #chunks > 1 then
        log.info("[discord] reply auto-split into " .. tostring(#chunks) .. " chunks for channel " .. channel_id)
    end

    return {
        success = true,
        message_id = first_id,
        channel_id = channel_id,
        output = "Reply sent to message " .. message_id
            .. (#chunks > 1 and (" (" .. tostring(#chunks) .. " chunks)") or "")
    }
end

--- GET /channels/{channel_id}/messages
local function do_browse(args)
    local platform_id = args.platform_id
    local channel_id = args.channel_id
    local limit = tonumber(args.limit) or 25
    if limit < 1 then limit = 1 end
    if limit > 100 then limit = 100 end

    if not channel_id or channel_id == "" then
        return { success = false, error = "channel_id is required" }
    end

    local endpoint = "/channels/" .. channel_id .. "/messages?limit=" .. tostring(limit)

    -- Pagination: before (older), after (newer), around (centered)
    if args.before and args.before ~= "" then
        endpoint = endpoint .. "&before=" .. args.before
    elseif args.after and args.after ~= "" then
        endpoint = endpoint .. "&after=" .. args.after
    elseif args.around and args.around ~= "" then
        endpoint = endpoint .. "&around=" .. args.around
    end

    local resp = auth_get(platform_id, endpoint)

    if resp.status == 200 then
        local data = json_decode_safe(resp.body)
        if not data or type(data) ~= "table" then
            return { success = false, error = "failed to parse messages" }
        end

        local messages = {}
        for i, msg in ipairs(data) do
            messages[#messages+1] = format_message(msg)
        end

        -- Include pagination hints
        local first_id = data[1] and data[1].id
        local last_id = data[#data] and data[#data].id

        return {
            success = true,
            count = #data,
            messages = messages,
            first_id = first_id,
            last_id = last_id,
            output = #messages > 0
                and (tostring(#data) .. " messages in channel " .. channel_id .. ":\n" .. table.concat(messages, "\n"))
                or  "No messages found in channel " .. channel_id
        }
    end

    return {
        success = false,
        error = "browse failed (" .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500)
    }
end

--- PUT /channels/{channel_id}/messages/{message_id}/reactions/{emoji}/@me
local function do_react(args)
    local platform_id = args.platform_id
    local channel_id = args.channel_id
    local message_id = args.message_id
    local emoji = args.emoji

    if not channel_id or channel_id == "" then
        return { success = false, error = "channel_id is required" }
    end
    if not message_id or message_id == "" then
        return { success = false, error = "message_id is required" }
    end
    if not emoji or emoji == "" then
        return { success = false, error = "emoji is required" }
    end

    local encoded_emoji = url_encode(emoji)
    local endpoint = "/channels/" .. channel_id .. "/messages/" .. message_id
        .. "/reactions/" .. encoded_emoji .. "/@me"

    local resp = auth_put(platform_id, endpoint)

    if resp.status == 204 or resp.status == 200 then
        return {
            success = true,
            output = "Reacted with " .. emoji .. " to message " .. message_id
        }
    end

    return {
        success = false,
        error = "react failed (" .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500)
    }
end

--- DELETE /channels/{channel_id}/messages/{message_id}
local function do_delete(args)
    local platform_id = args.platform_id
    local channel_id = args.channel_id
    local message_id = args.message_id

    if not channel_id or channel_id == "" then
        return { success = false, error = "channel_id is required" }
    end
    if not message_id or message_id == "" then
        return { success = false, error = "message_id is required" }
    end

    local resp = auth_delete(platform_id,
        "/channels/" .. channel_id .. "/messages/" .. message_id)

    if resp.status == 204 or resp.status == 200 then
        return {
            success = true,
            output = "Deleted message " .. message_id
        }
    end

    return {
        success = false,
        error = "delete failed (" .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500)
    }
end

--- PATCH /channels/{channel_id}/messages/{message_id}
local function do_edit(args)
    local platform_id = args.platform_id
    local channel_id = args.channel_id
    local message_id = args.message_id
    local content = args.content

    if not channel_id or channel_id == "" then
        return { success = false, error = "channel_id is required" }
    end
    if not message_id or message_id == "" then
        return { success = false, error = "message_id is required" }
    end
    if not content or content == "" then
        return { success = false, error = "content is required" }
    end

    local body = { content = content }
    local resp = auth_patch(platform_id,
        "/channels/" .. channel_id .. "/messages/" .. message_id, body)

    if resp.status == 200 then
        return {
            success = true,
            output = "Edited message " .. message_id
        }
    end

    return {
        success = false,
        error = "edit failed (" .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500)
    }
end

--- GET /channels/{channel_id}
local function do_get_channel(args)
    local platform_id = args.platform_id
    local channel_id = args.channel_id

    if not channel_id or channel_id == "" then
        return { success = false, error = "channel_id is required" }
    end

    local resp = auth_get(platform_id, "/channels/" .. channel_id)

    if resp.status == 200 then
        local data = json_decode_safe(resp.body)
        return {
            success = true,
            channel = data,
            output = format_channel(data)
        }
    end

    return {
        success = false,
        error = "get_channel failed (" .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500)
    }
end

--- GET /guilds/{guild_id}/channels
local function do_list_channels(args)
    local platform_id = args.platform_id
    local guild_id = args.guild_id

    if not guild_id or guild_id == "" then
        return { success = false, error = "guild_id is required" }
    end

    local resp = auth_get(platform_id, "/guilds/" .. guild_id .. "/channels")

    if resp.status == 200 then
        local data = json_decode_safe(resp.body)
        if not data or type(data) ~= "table" then
            return { success = false, error = "failed to parse channels" }
        end

        local channels = {}
        for _, ch in ipairs(data) do
            channels[#channels+1] = format_channel(ch)
        end

        return {
            success = true,
            count = #data,
            channels = channels,
            output = tostring(#data) .. " channels in guild " .. guild_id .. ":\n"
                .. table.concat(channels, "\n")
        }
    end

    return {
        success = false,
        error = "list_channels failed (" .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500)
    }
end

--- GET /guilds/{guild_id}
local function do_get_guild(args)
    local platform_id = args.platform_id
    local guild_id = args.guild_id

    if not guild_id or guild_id == "" then
        return { success = false, error = "guild_id is required" }
    end

    local resp = auth_get(platform_id, "/guilds/" .. guild_id)

    if resp.status == 200 then
        local data = json_decode_safe(resp.body)
        return {
            success = true,
            guild = data,
            output = format_guild(data)
        }
    end

    return {
        success = false,
        error = "get_guild failed (" .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500)
    }
end

--- GET /users/@me/guilds
local function do_list_guilds(args)
    local platform_id = args.platform_id

    local endpoint = "/users/@me/guilds"
    if args.before and args.before ~= "" then
        endpoint = endpoint .. "?before=" .. args.before
    elseif args.after and args.after ~= "" then
        endpoint = endpoint .. "?after=" .. args.after
    end
    local limit = tonumber(args.limit) or 100
    if endpoint:find("?") then
        endpoint = endpoint .. "&limit=" .. tostring(limit)
    else
        endpoint = endpoint .. "?limit=" .. tostring(limit)
    end

    local resp = auth_get(platform_id, endpoint)

    if resp.status == 200 then
        local data = json_decode_safe(resp.body)
        if not data or type(data) ~= "table" then
            return { success = false, error = "failed to parse guilds" }
        end

        local guilds = {}
        for _, g in ipairs(data) do
            guilds[#guilds+1] = format_guild(g)
        end

        return {
            success = true,
            count = #data,
            guilds = guilds,
            output = tostring(#data) .. " guilds:\n" .. table.concat(guilds, "\n")
        }
    end

    return {
        success = false,
        error = "list_guilds failed (" .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500)
    }
end

--- GET /channels/{channel_id}/messages/{message_id}
local function do_get_message(args)
    local platform_id = args.platform_id
    local channel_id = args.channel_id
    local message_id = args.message_id

    if not channel_id or channel_id == "" then
        return { success = false, error = "channel_id is required" }
    end
    if not message_id or message_id == "" then
        return { success = false, error = "message_id is required" }
    end

    local resp = auth_get(platform_id,
        "/channels/" .. channel_id .. "/messages/" .. message_id)

    if resp.status == 200 then
        local data = json_decode_safe(resp.body)
        return {
            success = true,
            message = data,
            output = format_message(data)
        }
    end

    return {
        success = false,
        error = "get_message failed (" .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500)
    }
end

--- POST /channels/{channel_id}/typing
local function do_typing(args)
    local platform_id = args.platform_id
    local channel_id = args.channel_id

    if not channel_id or channel_id == "" then
        return { success = false, error = "channel_id is required" }
    end

    local resp = auth_post(platform_id, "/channels/" .. channel_id .. "/typing", {})

    if resp.status == 204 or resp.status == 200 then
        return {
            success = true,
            output = "Typing indicator sent in channel " .. channel_id
        }
    end

    return {
        success = false,
        error = "typing failed (" .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500)
    }
end

--- GET /gateway/bot — get Gateway URL + session info (debugging/Phase 2 prep)
local function do_gateway_info(args)
    local platform_id = args.platform_id

    local resp = auth_get(platform_id, "/gateway/bot")

    if resp.status == 200 then
        local data = json_decode_safe(resp.body)
        return {
            success = true,
            gateway = data,
            output = "Gateway URL: " .. (data.url or "?")
                .. "\nShards: " .. tostring(data.shards or "?")
                .. "\nSession start limit: "
                .. (data.session_start_limit and tostring(data.session_start_limit.remaining) or "?")
                .. "/" .. (data.session_start_limit and tostring(data.session_start_limit.total) or "?")
        }
    end

    return {
        success = false,
        error = "gateway_info failed (" .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500)
    }
end

-- ---------------------------------------------------------------------------
-- #159: outbound media — send_media (multipart message with attachments)
-- ---------------------------------------------------------------------------

--- Send a message with file attachments. Discord accepts multipart/form-data
--- on the same endpoint as JSON posts: payload_json + files[0..n] in one
--- phase — the upload IS the message send (no processed-poll, no second
--- post). The kernel composes the multipart body; file bytes never enter
--- Lua. Text past the 2000-char limit splits like do_post; the first chunk
--- carries the files, follow-ups are plain JSON posts.
local function do_send_media(args)
    local platform_id = args.platform_id
    local channel_id = args.channel_id
    local media = args._media_files

    if not channel_id or channel_id == "" then
        return { success = false, error = "channel_id is required" }
    end
    if type(media) ~= "table" or #media == 0 then
        return { success = false, error =
            "no resolved attachments — pass files[] (kernel resolves to _media_files)" }
    end
    if #media > 10 then
        return { success = false, error = "too many attachments: " .. tostring(#media)
            .. " > 10 (per-message limit)" }
    end

    local token, terr = get_token(platform_id)
    if not token then return { success = false, error = terr } end

    local content = render_content(args.text or args.content or "")
    local chunks = split_for_limit(content, 2000)
    if #chunks == 0 then chunks = { "" } end
    local first_id

    for idx, chunk in ipairs(chunks) do
        if idx == 1 then
            -- first chunk: multipart with the attachments
            local files = {}
            for i, f in ipairs(media) do
                files[#files + 1] = {
                    path = f.path,
                    field = "files[" .. tostring(i - 1) .. "]",
                    filename = f.filename or ("attachment" .. tostring(i)),
                    mime = f.mime or "application/octet-stream"
                }
            end
            -- #161: alt text rides as Discord attachment descriptions
            -- (the platform's accessibility field). Files without alt are
            -- simply absent from the array. The kernel may fill empty alt
            -- machine-side (alt_text_source = "machine"); explicit alt
            -- always wins kernel-side, so no precedence logic here.
            local payload = { content = chunk }
            local atts = {}
            for i, f in ipairs(media) do
                local alt = tostring(f.alt_text or "")
                if alt ~= "" then
                    atts[#atts + 1] = { id = i - 1, description = alt }
                end
            end
            if #atts > 0 then payload.attachments = atts end
            local resp = animus.http_upload(
                API_BASE .. "/channels/" .. channel_id .. "/messages", {
                    headers = { ["Authorization"] = "Bot " .. token },
                    timeout = 120,
                    fields = { payload_json = json.encode(payload) },
                    files = files
                })
            update_rate_limits(resp, platform_id)
            if resp.status ~= 200 and resp.status ~= 201 then
                return {
                    success = false,
                    error = "send_media failed (" .. tostring(resp.status) .. "): "
                        .. truncate(tostring(resp.body or resp.error), 500)
                }
            end
            local data = json_decode_safe(resp.body)
            if data and data.id then first_id = data.id end
        else
            -- follow-up chunks: plain JSON posts (files ride only chunk 1)
            local resp = auth_post(platform_id,
                "/channels/" .. channel_id .. "/messages", { content = chunk })
            if resp.status ~= 200 and resp.status ~= 201 then
                return {
                    success = false,
                    error = "send_media follow-up failed (" .. tostring(resp.status)
                        .. ") on chunk " .. tostring(idx) .. "/" .. tostring(#chunks) .. ": "
                        .. truncate(tostring(resp.body), 500)
                }
            end
        end
    end

    return {
        success = true,
        message_id = first_id,
        channel_id = channel_id,
        output = "Media message sent to channel " .. channel_id
            .. " (" .. tostring(#media) .. " file(s)"
            .. (#chunks > 1 and (", " .. tostring(#chunks) .. " chunks") or "") .. ")"
    }
end

-- ---------------------------------------------------------------------------
-- Registration
-- ---------------------------------------------------------------------------

animus.register_channel({
    id = "discord",
    name = "Discord",
    capabilities = {"read", "write", "react", "browse"},
    actions = {
        "post", "reply", "browse", "react", "delete", "edit",
        "get_channel", "get_message", "list_channels", "get_guild",
        "list_guilds", "typing", "gateway_info", "send_media"
    },
    schema = {
        post = {
            { name = "channel_id", type = "string", required = true, description = "Channel snowflake ID" },
            { name = "content", type = "string", required = true, description = "Message text (max 2000 chars)" },
            { name = "embeds", type = "array", required = false, description = "Embed objects" },
            { name = "link_card", type = "object", required = false, description = "Link card {url, title?, description?, image?} — first chunk carries it as a native embed; url-only degrades to a plain link" }
        },
        reply = {
            { name = "channel_id", type = "string", required = true, description = "Channel snowflake ID" },
            { name = "message_id", type = "string", required = true, description = "Message snowflake ID to reply to" },
            { name = "content", type = "string", required = true, description = "Reply text" },
            { name = "link_card", type = "object", required = false, description = "Link card {url, title?, description?, image?} — native embed on the first reply chunk" }
        },
        send_media = {
            { name = "channel_id", type = "string", required = true, description = "Channel snowflake ID" },
            { name = "files", type = "array", required = true, description = "Media attachments: [{ref: 'media://<sha256>', path?, alt_text?}] — vault refs or dataDir-scoped paths; kernel resolves to _media_files (up to 10 files, multipart on the message endpoint)" },
            { name = "text", type = "string", required = false, description = "Message text (splits past 2000 chars; first chunk carries the files)" }
        },
        browse = {
            { name = "channel_id", type = "string", required = true, description = "Channel snowflake ID" },
            { name = "limit", type = "integer", required = false, description = "Messages to fetch (1-100, default 25)" },
            { name = "before", type = "string", required = false, description = "Message ID cursor (fetch older)" },
            { name = "after", type = "string", required = false, description = "Message ID cursor (fetch newer)" },
            { name = "around", type = "string", required = false, description = "Message ID cursor (centered)" }
        },
        react = {
            { name = "channel_id", type = "string", required = true, description = "Channel snowflake ID" },
            { name = "message_id", type = "string", required = true, description = "Message snowflake ID" },
            { name = "emoji", type = "string", required = true, description = "Emoji to react with (URL-encoded automatically)" }
        },
        delete = {
            { name = "channel_id", type = "string", required = true, description = "Channel snowflake ID" },
            { name = "message_id", type = "string", required = true, description = "Message snowflake ID to delete" }
        },
        edit = {
            { name = "channel_id", type = "string", required = true, description = "Channel snowflake ID" },
            { name = "message_id", type = "string", required = true, description = "Message snowflake ID to edit" },
            { name = "content", type = "string", required = true, description = "New message text" }
        },
        get_channel = {
            { name = "channel_id", type = "string", required = true, description = "Channel snowflake ID" }
        },
        get_message = {
            { name = "channel_id", type = "string", required = true, description = "Channel snowflake ID" },
            { name = "message_id", type = "string", required = true, description = "Message snowflake ID" }
        },
        list_channels = {
            { name = "guild_id", type = "string", required = true, description = "Guild snowflake ID" }
        },
        get_guild = {
            { name = "guild_id", type = "string", required = true, description = "Guild snowflake ID" }
        },
        list_guilds = {
            { name = "limit", type = "integer", required = false, description = "Max guilds (default 100)" },
            { name = "before", type = "string", required = false, description = "Guild ID cursor" },
            { name = "after", type = "string", required = false, description = "Guild ID cursor" }
        },
        typing = {
            { name = "channel_id", type = "string", required = true, description = "Channel snowflake ID" }
        },
        gateway_info = {
            -- No required params
        }
    },
    handler = function(args)
        local action = args.action
        if not action or action == "" then
            return { success = false, error = "action is required" }
        end

        if action == "post" then return do_post(args)
        elseif action == "reply" then return do_reply(args)
        elseif action == "browse" then return do_browse(args)
        elseif action == "react" then return do_react(args)
        elseif action == "delete" then return do_delete(args)
        elseif action == "edit" then return do_edit(args)
        elseif action == "get_channel" then return do_get_channel(args)
        elseif action == "get_message" then return do_get_message(args)
        elseif action == "list_channels" then return do_list_channels(args)
        elseif action == "get_guild" then return do_get_guild(args)
        elseif action == "list_guilds" then return do_list_guilds(args)
        elseif action == "typing" then return do_typing(args)
        elseif action == "gateway_info" then return do_gateway_info(args)
        elseif action == "send_media" then return do_send_media(args)
        else return { success = false, error = "unknown action: " .. tostring(action) }
        end
    end
})

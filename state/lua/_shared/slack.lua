-- ============================================================================
-- Slack Social Adapter for Animus
-- Ticket 061 — Phase 1: Lua REST + polling
--
-- Registers via animus.register_channel() as a stateless adapter.
-- Auth: two-token model — bot token (xoxb-) for REST, app token (xapp-) for
-- future Socket Mode. Phase 1 uses bot token only.
--
-- Config keys (per-instance):
--   config.get("channels.slack:WORKSPACE.bot_token")
--   config.get("channels.slack:WORKSPACE.app_token")       -- Phase 2: Socket Mode
--   config.get("channels.slack:WORKSPACE.bot_user_id")     -- cached from auth.test
--
-- Rate limit tracking per method tier (persisted in config).
-- ============================================================================

local API_BASE = "https://slack.com/api"

-- ---------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------

local function url_encode(str)
    if not str then return "" end
    return (tostring(str):gsub("[^%w%-%._~]", function(c)
        return string.format("%%%02X", string.byte(c))
    end))
end

local function cfg_key(platform_id, key)
    return "channels." .. platform_id .. "." .. key
end

local function json_decode_safe(str)
    if not str or str == "" then return nil, "empty body" end
    local ok, data = pcall(json.decode, str)
    if ok then return data end
    return nil, "json decode failed: " .. tostring(data)
end

local function truncate(str, max_len)
    if not str then return "" end
    if #str <= max_len then return str end
    return str:sub(1, max_len - 3) .. "..."
end

-- ---------------------------------------------------------------------------
-- Rate Limit Tracking
-- ---------------------------------------------------------------------------

-- Slack uses tiered per-method rate limits. We track the last-known state
-- per method so we can warn when approaching limits.
-- Structure: { method_name = { remaining=N, reset_ts=TS, retry_after=S } }

local rate_limits = {}

local function load_rate_limits(platform_id)
    local stored = config.get(cfg_key(platform_id, "rate_limits"))
    if stored and stored ~= "" then
        local data = json_decode_safe(stored)
        if data then rate_limits = data end
    end
end

local function save_rate_limits(platform_id)
    config.set(cfg_key(platform_id, "rate_limits"), json.encode(rate_limits))
end

local function update_rate_limits(resp, method, platform_id)
    if not resp.headers then return end

    local remaining = tonumber(resp.headers["x-ratelimit-remaining"])
    local reset_ts = tonumber(resp.headers["x-ratelimit-reset"])
    local retry_after = tonumber(resp.headers["retry-after"])

    if not remaining and not reset_ts then return end

    rate_limits[method] = {
        remaining = remaining,
        reset_ts = reset_ts,
        retry_after = retry_after
    }

    if remaining and remaining <= 2 then
        log.warn("[slack] rate limit nearly exhausted for " .. method
            .. ": " .. tostring(remaining) .. " remaining")
    end

    -- We don't save per-request (too noisy); save on 429 or near-limit.
    if remaining and remaining <= 2 then
        save_rate_limits(platform_id)
    end
end

-- ---------------------------------------------------------------------------
-- Auth
-- ---------------------------------------------------------------------------

--- Get the bot token for this instance.
local function get_token(platform_id)
    local token = config.get(cfg_key(platform_id, "bot_token"))
    if (not token or token == "") and not platform_id:find(":") then
        -- bare type form ("slack") -> instance form ("slack:slack") retry;
        -- config rows live under channels.<type>:<instance>
        token = config.get(cfg_key(platform_id .. ":" .. platform_id, "bot_token"))
    end
    if not token or token == "" then
        return nil, "no bot_token configured for " .. platform_id
    end
    return token
end

--- Get the bot's own user ID (cached from auth.test).
local function get_bot_user_id(platform_id)
    local cached = config.get(cfg_key(platform_id, "bot_user_id"))
    if cached and cached ~= "" then return cached end

    -- Fetch via auth.test
    local token = get_token(platform_id)
    if not token then return nil end

    local resp = animus.http_post(API_BASE .. "/auth.test", {
        headers = {
            ["Authorization"] = "Bearer " .. token,
            ["Content-Type"] = "application/x-www-form-urlencoded"
        },
        body = ""
    })

    if resp.status ~= 200 then return nil end

    local data = json_decode_safe(resp.body)
    if data and data.ok and data.user_id then
        config.set(cfg_key(platform_id, "bot_user_id"), tostring(data.user_id))
        return tostring(data.user_id)
    end
    return nil
end

-- ---------------------------------------------------------------------------
-- HTTP primitives
-- ---------------------------------------------------------------------------

--- Authenticated POST (form-encoded) — Slack's standard pattern.
local function api_post(platform_id, method, params)
    local token, err = get_token(platform_id)
    if not token then return { status = 0, body = err or "no token" } end

    -- Build form-encoded body
    local body_parts = {}
    if params then
        for k, v in pairs(params) do
            if type(v) == "table" then
                body_parts[#body_parts+1] = url_encode(k) .. "=" .. url_encode(json.encode(v))
            else
                body_parts[#body_parts+1] = url_encode(k) .. "=" .. url_encode(tostring(v))
            end
        end
    end
    local body = table.concat(body_parts, "&")

    local resp = animus.http_post(API_BASE .. "/" .. method, {
        headers = {
            ["Authorization"] = "Bearer " .. token,
            ["Content-Type"] = "application/x-www-form-urlencoded"
        },
        body = body
    })

    -- Handle 429 rate limit
    if resp.status == 429 then
        local retry_after = tonumber(resp.headers and resp.headers["retry-after"]) or 5
        log.warn("[slack] rate limited on " .. method .. ", retry_after=" .. tostring(retry_after) .. "s")
    end

    -- Slack always returns 200 with {ok: false} for errors — check body
    if resp.status == 200 then
        local data = json_decode_safe(resp.body)
        if data and data.ok == false then
            log.warn("[slack] API error on " .. method .. ": " .. tostring(data.error))
        end
    end

    update_rate_limits(resp, method, platform_id)
    return resp
end

--- Authenticated POST with JSON body — for chat.postMessage with blocks etc.
local function api_post_json(platform_id, method, body_table)
    local token, err = get_token(platform_id)
    if not token then return { status = 0, body = err or "no token" } end

    local resp = animus.http_post(API_BASE .. "/" .. method, {
        headers = {
            ["Authorization"] = "Bearer " .. token,
            ["Content-Type"] = "application/json; charset=utf-8"
        },
        body = json.encode(body_table)
    })

    if resp.status == 429 then
        local retry_after = tonumber(resp.headers and resp.headers["retry-after"]) or 5
        log.warn("[slack] rate limited on " .. method .. " (JSON), retry_after=" .. tostring(retry_after) .. "s")
    end

    if resp.status == 200 then
        local data = json_decode_safe(resp.body)
        if data and data.ok == false then
            log.warn("[slack] API error on " .. method .. ": " .. tostring(data.error))
        end
    end

    update_rate_limits(resp, method, platform_id)
    return resp
end

--- Authenticated GET with query params.
local function api_get(platform_id, method, params)
    local token, err = get_token(platform_id)
    if not token then return { status = 0, body = err or "no token" } end

    local url = API_BASE .. "/" .. method
    if params then
        local parts = {}
        for k, v in pairs(params) do
            parts[#parts + 1] = url_encode(k) .. "=" .. url_encode(tostring(v))
        end
        if #parts > 0 then
            url = url .. "?" .. table.concat(parts, "&")
        end
    end

    local resp = animus.http_get(url, {
        headers = { ["Authorization"] = "Bearer " .. token }
    })

    if resp.status == 429 then
        local retry_after = tonumber(resp.headers and resp.headers["retry-after"]) or 5
        log.warn("[slack] rate limited on GET " .. method .. ", retry_after=" .. tostring(retry_after) .. "s")
    end

    update_rate_limits(resp, method, platform_id)
    return resp
end

-- ---------------------------------------------------------------------------
-- Message formatting
-- ---------------------------------------------------------------------------

local function format_message(msg)
    if not msg then return "(nil message)" end
    local user = msg.user or msg.username or "unknown"
    local text = msg.text or ""
    local ts = msg.ts or "?"
    -- Slack ts is "seconds.microseconds" — convert to readable time
    local sec = tonumber(ts)
    if sec then
        ts = os.date("!%Y-%m-%dT%H:%M:%S", math.floor(sec)) .. " UTC"
    end

    local parts = {}
    parts[#parts+1] = "[" .. ts .. "] <" .. user .. ">: " .. text

    -- Thread info
    if msg.thread_ts and msg.thread_ts ~= msg.ts then
        parts[#parts+1] = "  (thread: " .. msg.thread_ts .. ")"
    end

    -- Reactions
    if msg.reactions and #msg.reactions > 0 then
        local rxns = {}
        for _, r in ipairs(msg.reactions) do
            rxns[#rxns+1] = ":" .. (r.name or "?") .. ": " .. tostring(r.count or 0)
        end
        parts[#parts+1] = "  reactions: " .. table.concat(rxns, ", ")
    end

    return table.concat(parts, "\n")
end

local function format_channel(ch)
    if not ch then return "(nil channel)" end
    local ch_type_names = {
        ["channel"] = "public",
        ["group"] = "private",
        ["im"] = "dm",
        ["mpim"] = "multi-dm"
    }
    local ch_type = ch_type_names[ch.is_channel and "channel"
        or ch.is_group and "group"
        or ch.is_im and "im"
        or ch.is_mpim and "mpim"
        or "unknown"] or "?"
    local name = ch.name or ch.user or "(unnamed)"
    return "#" .. name .. " (type=" .. ch_type .. ", id=" .. (ch.id or "?") .. ")"
end

-- ---------------------------------------------------------------------------
-- Action implementations
-- ---------------------------------------------------------------------------

-- Canonical Markdown -> Slack mrkdwn (#156). scripts/markdown.lua is
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
    if type(markdown) == "table" and type(markdown.render_slack) == "function" then
        local ok, out = pcall(markdown.render_slack, content)
        if ok and type(out) == "string" and out ~= "" then return out end
        if log and log.warn then log.warn("[slack] markdown render failed; sending raw content") end
    end
    return content
end

--- chat.postMessage
local function do_post(args)
    local pid = args.platform_id
    local channel = args.channel_id
    local content = args.content

    if not channel or channel == "" then
        return { success = false, error = "channel_id is required" }
    end
    if not content or content == "" then
        return { success = false, error = "content is required" }
    end

    local card, cerr = get_link_card(args.link_card)
    if args.link_card and not card then
        return { success = false, error = "invalid link_card: " .. tostring(cerr) }
    end
    -- #157: url-only card has nothing to build a block from — plain link.
    if card and not card.title and not card.description then
        content = append_card_url(content, card)
        card = nil
    end

    local body = {
        channel = channel,
        text = render_content(content)
    }

    -- Thread reply support
    if args.thread_ts and args.thread_ts ~= "" then
        body.thread_ts = args.thread_ts
    end

    -- #157: link card as Block Kit — deterministic card rendering instead
    -- of a server unfurl. Explicit args.blocks wins over the card.
    if card and not args.blocks then
        local md_txt = "<" .. card.url .. "|" .. (card.title or card.url) .. ">"
        if card.description then
            md_txt = md_txt .. "\n" .. truncate(card.description, 280)
        end
        body.blocks = {
            { type = "section", text = { type = "mrkdwn", text = md_txt } },
        }
        body.unfurl_links = false
    end

    -- Block Kit support (Phase 3 — pass through if provided)
    if args.blocks then
        body.blocks = args.blocks
    end

    local resp = api_post_json(pid, "chat.postMessage", body)

    if resp.status ~= 200 then
        return { success = false, error = "post failed (HTTP " .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500) }
    end

    local data = json_decode_safe(resp.body)
    if not data or not data.ok then
        return { success = false, error = "post failed: " .. tostring(data and data.error or "unknown") }
    end

    return {
        success = true,
        message_id = data.ts,
        channel_id = data.channel,
        output = "Message sent to " .. channel
    }
end

--- chat.postMessage with thread_ts — explicit reply action
local function do_reply(args)
    local pid = args.platform_id
    local channel = args.channel_id
    local thread_ts = args.thread_ts or args.message_id
    local content = args.content

    if not channel or channel == "" then
        return { success = false, error = "channel_id is required" }
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

    -- thread_ts optional: absent = top-level response in the channel
    -- (threaded_replies=false); present = reply inside that thread.
    local body = {
        channel = channel,
        text = render_content(content)
    }
    if thread_ts and thread_ts ~= "" then
        body.thread_ts = thread_ts
    end

    -- Broadcast to channel (not just thread) if requested
    if args.broadcast then
        body.reply_broadcast = true
    end

    -- #157: link card as Block Kit (same mapping as post).
    if card then
        local md_txt = "<" .. card.url .. "|" .. (card.title or card.url) .. ">"
        if card.description then
            md_txt = md_txt .. "\n" .. truncate(card.description, 280)
        end
        body.blocks = {
            { type = "section", text = { type = "mrkdwn", text = md_txt } },
        }
        body.unfurl_links = false
    end

    local resp = api_post_json(pid, "chat.postMessage", body)

    if resp.status ~= 200 then
        return { success = false, error = "reply failed (HTTP " .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500) }
    end

    local data = json_decode_safe(resp.body)
    if not data or not data.ok then
        return { success = false, error = "reply failed: " .. tostring(data and data.error or "unknown") }
    end

    return {
        success = true,
        message_id = data.ts,
        channel_id = data.channel,
        output = thread_ts and ("Reply sent in thread " .. thread_ts)
                 or "Reply sent to channel"
    }
end

--- conversations.history — browse recent messages in a channel
local function do_browse(args)
    local pid = args.platform_id
    local channel = args.channel_id
    local limit = tonumber(args.limit) or 25
    if limit < 1 then limit = 1 end
    if limit > 1000 then limit = 1000 end

    if not channel or channel == "" then
        return { success = false, error = "channel_id is required" }
    end

    local params = {
        channel = channel,
        limit = tostring(limit)
    }
    if args.cursor and args.cursor ~= "" then
        params.cursor = args.cursor
    end
    if args.oldest and args.oldest ~= "" then
        params.oldest = args.oldest
    end
    if args.latest and args.latest ~= "" then
        params.latest = args.latest
    end

    local resp = api_get(pid, "conversations.history", params)

    if resp.status ~= 200 then
        return { success = false, error = "browse failed (HTTP " .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500) }
    end

    local data = json_decode_safe(resp.body)
    if not data or not data.ok then
        return { success = false, error = "browse failed: " .. tostring(data and data.error or "unknown") }
    end

    local messages = {}
    if data.messages then
        for _, msg in ipairs(data.messages) do
            messages[#messages+1] = format_message(msg)
        end
    end

    local output_parts = {}
    output_parts[#output_parts+1] = tostring(#messages) .. " messages in channel " .. channel
    if data.has_more then
        output_parts[#output_parts+1] = "(more available — use cursor: " .. tostring(data.response_metadata and data.response_metadata.next_cursor or "") .. ")"
    end
    output_parts[#output_parts+1] = table.concat(messages, "\n")

    return {
        success = true,
        count = #messages,
        messages = messages,
        has_more = data.has_more or false,
        cursor = data.response_metadata and data.response_metadata.next_cursor or "",
        output = table.concat(output_parts, "\n")
    }
end

--- search.messages — workspace-wide message search
local function do_search(args)
    local pid = args.platform_id
    local query = args.query

    if not query or query == "" then
        return { success = false, error = "query is required for search" }
    end

    local params = { query = query }
    if args.count then
        params.count = tostring(math.min(tonumber(args.count) or 20, 100))
    else
        params.count = "20"
    end
    if args.page then params.page = tostring(args.page) end
    if args.sort then params.sort = args.sort end  -- "score" or "timestamp"
    if args.sort_dir then params.sort_dir = args.sort_dir end  -- "asc" or "desc"

    local resp = api_get(pid, "search.messages", params)

    if resp.status ~= 200 then
        return { success = false, error = "search failed (HTTP " .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500) }
    end

    local data = json_decode_safe(resp.body)
    if not data or not data.ok then
        return { success = false, error = "search failed: " .. tostring(data and data.error or "unknown") }
    end

    local results = {}
    if data.messages and data.messages.matches then
        for _, match in ipairs(data.messages.matches) do
            local entry = {
                text = match.text or "",
                user = match.user or "",
                channel = match.channel and match.channel.name or "",
                channel_id = match.channel and match.channel.id or "",
                ts = match.ts or "",
                permalink = match.permalink or ""
            }
            results[#results+1] = entry
        end
    end

    return {
        success = true,
        total = data.messages and data.messages.total or #results,
        results = results,
        output = json.encode({
            total = data.messages and data.messages.total or #results,
            matches = results
        })
    }
end

--- conversations.info — get channel info
local function do_get_channel(args)
    local pid = args.platform_id
    local channel = args.channel_id

    if not channel or channel == "" then
        return { success = false, error = "channel_id is required" }
    end

    local resp = api_get(pid, "conversations.info", { channel = channel })

    if resp.status ~= 200 then
        return { success = false, error = "get_channel failed (HTTP " .. tostring(resp.status) .. ")" }
    end

    local data = json_decode_safe(resp.body)
    if not data or not data.ok then
        return { success = false, error = "get_channel failed: " .. tostring(data and data.error or "unknown") }
    end

    return {
        success = true,
        channel = data.channel,
        output = format_channel(data.channel)
    }
end

--- conversations.list — list channels the bot is in
local function do_list_channels(args)
    local pid = args.platform_id

    local params = {}
    params.limit = tostring(math.min(tonumber(args.limit) or 200, 1000))
    params.types = args.types or "public_channel,private_channel"  -- exclude DMs by default

    if args.cursor and args.cursor ~= "" then
        params.cursor = args.cursor
    end

    local resp = api_get(pid, "conversations.list", params)

    if resp.status ~= 200 then
        return { success = false, error = "list_channels failed (HTTP " .. tostring(resp.status) .. ")" }
    end

    local data = json_decode_safe(resp.body)
    if not data or not data.ok then
        return { success = false, error = "list_channels failed: " .. tostring(data and data.error or "unknown") }
    end

    local channels = {}
    if data.channels then
        for _, ch in ipairs(data.channels) do
            channels[#channels+1] = format_channel(ch)
        end
    end

    return {
        success = true,
        count = #channels,
        channels = channels,
        cursor = data.response_metadata and data.response_metadata.next_cursor or "",
        output = tostring(#channels) .. " channels:\n" .. table.concat(channels, "\n")
    }
end

--- conversations.members — list members in a channel
local function do_list_members(args)
    local pid = args.platform_id
    local channel = args.channel_id

    if not channel or channel == "" then
        return { success = false, error = "channel_id is required" }
    end

    local params = { channel = channel }
    params.limit = tostring(math.min(tonumber(args.limit) or 200, 1000))
    if args.cursor and args.cursor ~= "" then
        params.cursor = args.cursor
    end

    local resp = api_get(pid, "conversations.members", params)

    if resp.status ~= 200 then
        return { success = false, error = "list_members failed (HTTP " .. tostring(resp.status) .. ")" }
    end

    local data = json_decode_safe(resp.body)
    if not data or not data.ok then
        return { success = false, error = "list_members failed: " .. tostring(data and data.error or "unknown") }
    end

    return {
        success = true,
        members = data.members or {},
        count = data.members and #data.members or 0,
        cursor = data.response_metadata and data.response_metadata.next_cursor or "",
        output = tostring(data.members and #data.members or 0) .. " members in channel " .. channel
    }
end

--- reactions.add — add emoji reaction
local function do_react(args)
    local pid = args.platform_id
    local channel = args.channel_id
    local timestamp = args.timestamp or args.message_id
    local emoji = args.emoji

    if not channel or channel == "" then
        return { success = false, error = "channel_id is required" }
    end
    if not timestamp or timestamp == "" then
        return { success = false, error = "timestamp (message ts) is required" }
    end
    if not emoji or emoji == "" then
        return { success = false, error = "emoji is required (e.g. 'thumbsup')" }
    end

    -- Ensure emoji has colons
    if not emoji:find("^:.*:$") then
        emoji = ":" .. emoji .. ":"
    end

    local resp = api_post(pid, "reactions.add", {
        channel = channel,
        timestamp = timestamp,
        name = emoji:gsub("^:", ""):gsub(":$", "")  -- API wants bare name
    })

    if resp.status ~= 200 then
        return { success = false, error = "react failed (HTTP " .. tostring(resp.status) .. ")" }
    end

    local data = json_decode_safe(resp.body)
    if not data or not data.ok then
        return { success = false, error = "react failed: " .. tostring(data and data.error or "unknown") }
    end

    return {
        success = true,
        output = "Reacted with :" .. emoji:gsub("^:", ""):gsub(":$", "") .. ": to message " .. timestamp
    }
end

--- chat.delete — delete a message
local function do_delete(args)
    local pid = args.platform_id
    local channel = args.channel_id
    local timestamp = args.timestamp or args.message_id

    if not channel or channel == "" then
        return { success = false, error = "channel_id is required" }
    end
    if not timestamp or timestamp == "" then
        return { success = false, error = "timestamp (message ts) is required" }
    end

    local resp = api_post(pid, "chat.delete", {
        channel = channel,
        ts = timestamp
    })

    if resp.status ~= 200 then
        return { success = false, error = "delete failed (HTTP " .. tostring(resp.status) .. ")" }
    end

    local data = json_decode_safe(resp.body)
    if not data or not data.ok then
        return { success = false, error = "delete failed: " .. tostring(data and data.error or "unknown") }
    end

    return {
        success = true,
        output = "Deleted message " .. timestamp
    }
end

--- chat.update — edit a message
local function do_edit(args)
    local pid = args.platform_id
    local channel = args.channel_id
    local timestamp = args.timestamp or args.message_id
    local content = args.content

    if not channel or channel == "" then
        return { success = false, error = "channel_id is required" }
    end
    if not timestamp or timestamp == "" then
        return { success = false, error = "timestamp (message ts) is required" }
    end
    if not content or content == "" then
        return { success = false, error = "content is required" }
    end

    local body = {
        channel = channel,
        ts = timestamp,
        text = render_content(content)
    }

    local resp = api_post_json(pid, "chat.update", body)

    if resp.status ~= 200 then
        return { success = false, error = "edit failed (HTTP " .. tostring(resp.status) .. ")" }
    end

    local data = json_decode_safe(resp.body)
    if not data or not data.ok then
        return { success = false, error = "edit failed: " .. tostring(data and data.error or "unknown") }
    end

    return {
        success = true,
        output = "Edited message " .. timestamp
    }
end

--- auth.test — verify connectivity and get bot identity
local function do_auth_test(args)
    local pid = args.platform_id
    local token = get_token(pid)
    if not token then
        return { success = false, error = "no bot_token configured" }
    end

    local resp = animus.http_post(API_BASE .. "/auth.test", {
        headers = {
            ["Authorization"] = "Bearer " .. token,
            ["Content-Type"] = "application/x-www-form-urlencoded"
        },
        body = ""
    })

    if resp.status ~= 200 then
        return { success = false, error = "auth.test failed (HTTP " .. tostring(resp.status) .. ")" }
    end

    local data = json_decode_safe(resp.body)
    if not data or not data.ok then
        return { success = false, error = "auth.test failed: " .. tostring(data and data.error or "unknown") }
    end

    -- Cache bot_user_id
    if data.user_id then
        config.set(cfg_key(pid, "bot_user_id"), tostring(data.user_id))
    end

    return {
        success = true,
        output = "Authenticated as " .. tostring(data.user) .. " (" .. tostring(data.user_id) .. ")"
            .. " in workspace " .. tostring(data.team) .. " (" .. tostring(data.team_id) .. ")"
    }
end

-- ---------------------------------------------------------------------------
-- #159: outbound media — send_media (files.upload_v2 external flow)
-- ---------------------------------------------------------------------------

--- Send a message with file attachments via Slack's three-step external
--- upload: files.getUploadURLExternal (needs the exact byte length — the
--- kernel resolver stats each file into _media_files[].size) → raw POST
--- of the bytes to the presigned upload_url (no auth header; the URL is
--- the credential) → files.completeUploadExternal, which posts the files
--- to the channel with the text as initial_comment. No processed-poll —
--- the complete call is the message send. Alt text rides each file title.
local function do_send_media(args)
    local pid = args.platform_id
    local channel = args.channel_id
    local media = args._media_files

    if not channel or channel == "" then
        return { success = false, error = "channel_id is required" }
    end
    if type(media) ~= "table" or #media == 0 then
        return { success = false, error =
            "no resolved attachments — pass files[] (kernel resolves to _media_files)" }
    end
    if #media > 10 then
        return { success = false, error = "too many attachments: " .. tostring(#media)
            .. " > 10 (per-upload limit)" }
    end

    local text = render_content(args.text or args.content or "")

    local entries = {}
    for i, f in ipairs(media) do
        if not f.size or f.size <= 0 then
            return { success = false, error = "file " .. tostring(i)
                .. " carries no size — kernel resolver must provide _media_files[].size" }
        end
        local filename = f.filename or ("attachment" .. tostring(i))
        local resp = api_post(pid, "files.getUploadURLExternal", {
            filename = filename,
            length = tostring(f.size)
        })
        if resp.status ~= 200 then
            return { success = false, error = "getUploadURLExternal failed (HTTP "
                .. tostring(resp.status) .. ", file " .. tostring(i) .. "): "
                .. truncate(tostring(resp.body), 400) }
        end
        local data = json_decode_safe(resp.body)
        if not data or not data.ok or not data.upload_url or not data.file_id then
            return { success = false, error = "getUploadURLExternal error (file "
                .. tostring(i) .. "): "
                .. tostring(data and data.error or "malformed response") }
        end

        local up = animus.http_upload(data.upload_url, {
            timeout = 120,
            file = { path = f.path, mime = f.mime or "application/octet-stream" }
        })
        if up.status ~= 200 then
            return { success = false, error = "byte upload failed (HTTP "
                .. tostring(up.status) .. ", file " .. tostring(i) .. "): "
                .. truncate(tostring(up.body or up.error), 400) }
        end

        entries[#entries + 1] = {
            file_id = data.file_id,
            title = (f.alt_text and f.alt_text ~= "") and f.alt_text or filename
        }
    end

    local comp = { files = entries, channel_id = channel }
    if text ~= "" then comp.initial_comment = text end
    local resp = api_post(pid, "files.completeUploadExternal", comp)
    if resp.status ~= 200 then
        return { success = false, error = "completeUploadExternal failed (HTTP "
            .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 400) }
    end
    local data = json_decode_safe(resp.body)
    if not data or not data.ok then
        return { success = false, error = "completeUploadExternal: "
            .. tostring(data and data.error or "unknown") }
    end

    return {
        success = true,
        channel_id = channel,
        output = "Media message sent to " .. channel
            .. " (" .. tostring(#entries) .. " file(s))"
    }
end

-- ---------------------------------------------------------------------------
-- Registration
-- ---------------------------------------------------------------------------

animus.register_channel({
    id = "slack",
    name = "Slack",
    capabilities = {"read", "write", "search", "react"},
    actions = {
        "post", "reply", "browse", "search", "react",
        "get_channel", "list_channels", "list_members",
        "delete", "edit", "auth_test", "send_media"
    },
    schema = {
        post = {
            { name = "channel_id", type = "string", required = true, description = "Channel ID (e.g. C01234567)" },
            { name = "content", type = "string", required = true, description = "Message text" },
            { name = "thread_ts", type = "string", required = false, description = "Parent message timestamp (for threaded reply)" },
            { name = "blocks", type = "array", required = false, description = "Block Kit payload (Phase 3)" },
            { name = "link_card", type = "object", required = false, description = "Link card {url, title?, description?, image?} — rendered as a Block Kit card (explicit blocks wins); url-only degrades to a plain link" }
        },
        reply = {
            { name = "channel_id", type = "string", required = true, description = "Channel ID" },
            { name = "thread_ts", type = "string", required = false, description = "Timestamp of the message to reply to (thread reply); omit for a top-level response" },
            { name = "content", type = "string", required = true, description = "Reply text" },
            { name = "broadcast", type = "boolean", required = false, description = "Show reply in channel (not just thread)" },
            { name = "link_card", type = "object", required = false, description = "Link card {url, title?, description?, image?} — rendered as a Block Kit card; url-only degrades to a plain link" }
        },
        send_media = {
            { name = "channel_id", type = "string", required = true, description = "Channel ID (e.g. C01234567)" },
            { name = "files", type = "array", required = true, description = "Media attachments: [{ref: 'media://<sha256>', path?, alt_text?}] — vault refs or dataDir-scoped paths; kernel resolves to _media_files (up to 10 files, external upload flow)" },
            { name = "text", type = "string", required = false, description = "Message text (posted as initial comment with the files)" }
        },
        browse = {
            { name = "channel_id", type = "string", required = true, description = "Channel ID" },
            { name = "limit", type = "integer", required = false, description = "Messages to fetch (1-1000, default 25)" },
            { name = "cursor", type = "string", required = false, description = "Pagination cursor" },
            { name = "oldest", type = "string", required = false, description = "Start of time range (ts)" },
            { name = "latest", type = "string", required = false, description = "End of time range (ts)" }
        },
        search = {
            { name = "query", type = "string", required = true, description = "Search query" },
            { name = "count", type = "integer", required = false, description = "Results per page (1-100, default 20)" },
            { name = "page", type = "string", required = false, description = "Page number" },
            { name = "sort", type = "string", required = false, description = "'score' or 'timestamp'" },
            { name = "sort_dir", type = "string", required = false, description = "'asc' or 'desc'" }
        },
        get_channel = {
            { name = "channel_id", type = "string", required = true, description = "Channel ID" }
        },
        list_channels = {
            { name = "limit", type = "integer", required = false, description = "Max channels (default 200)" },
            { name = "types", type = "string", required = false, description = "Channel types (default: 'public_channel,private_channel')" },
            { name = "cursor", type = "string", required = false, description = "Pagination cursor" }
        },
        list_members = {
            { name = "channel_id", type = "string", required = true, description = "Channel ID" },
            { name = "limit", type = "integer", required = false, description = "Max members (default 200)" },
            { name = "cursor", type = "string", required = false, description = "Pagination cursor" }
        },
        react = {
            { name = "channel_id", type = "string", required = true, description = "Channel ID" },
            { name = "timestamp", type = "string", required = true, description = "Message timestamp (ts)" },
            { name = "emoji", type = "string", required = true, description = "Emoji name (e.g. 'thumbsup')" }
        },
        delete = {
            { name = "channel_id", type = "string", required = true, description = "Channel ID" },
            { name = "timestamp", type = "string", required = true, description = "Message timestamp (ts) to delete" }
        },
        edit = {
            { name = "channel_id", type = "string", required = true, description = "Channel ID" },
            { name = "timestamp", type = "string", required = true, description = "Message timestamp (ts) to edit" },
            { name = "content", type = "string", required = true, description = "New message text" }
        },
        auth_test = {
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
        elseif action == "search" then return do_search(args)
        elseif action == "get_channel" then return do_get_channel(args)
        elseif action == "list_channels" then return do_list_channels(args)
        elseif action == "list_members" then return do_list_members(args)
        elseif action == "react" then return do_react(args)
        elseif action == "delete" then return do_delete(args)
        elseif action == "edit" then return do_edit(args)
        elseif action == "auth_test" then return do_auth_test(args)
        elseif action == "send_media" then return do_send_media(args)
        else return { success = false, error = "unknown action: " .. tostring(action) }
        end
    end
})

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
    if #content > 2000 then
        content = content:sub(1, 1997) .. "..."
        log.warn("[discord] message truncated to 2000 chars for channel " .. channel_id)
    end

    local body = { content = content }
    if args.embeds then body.embeds = args.embeds end

    local resp = auth_post(platform_id, "/channels/" .. channel_id .. "/messages", body)

    if resp.status == 200 or resp.status == 201 then
        local data = json_decode_safe(resp.body)
        return {
            success = true,
            message_id = data and data.id,
            channel_id = channel_id,
            output = "Message sent to channel " .. channel_id
        }
    end

    return {
        success = false,
        error = "post failed (" .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500)
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
    if #content > 2000 then
        content = content:sub(1, 1997) .. "..."
    end

    local body = {
        content = content,
        message_reference = {
            message_id = message_id,
            channel_id = channel_id
        }
    }

    local resp = auth_post(platform_id, "/channels/" .. channel_id .. "/messages", body)

    if resp.status == 200 or resp.status == 201 then
        local data = json_decode_safe(resp.body)
        return {
            success = true,
            message_id = data and data.id,
            channel_id = channel_id,
            output = "Reply sent to message " .. message_id
        }
    end

    return {
        success = false,
        error = "reply failed (" .. tostring(resp.status) .. "): " .. truncate(tostring(resp.body), 500)
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
-- Registration
-- ---------------------------------------------------------------------------

animus.register_channel({
    id = "discord",
    name = "Discord",
    capabilities = {"read", "write", "react", "browse"},
    actions = {
        "post", "reply", "browse", "react", "delete", "edit",
        "get_channel", "get_message", "list_channels", "get_guild",
        "list_guilds", "typing", "gateway_info"
    },
    schema = {
        post = {
            { name = "channel_id", type = "string", required = true, description = "Channel snowflake ID" },
            { name = "content", type = "string", required = true, description = "Message text (max 2000 chars)" },
            { name = "embeds", type = "array", required = false, description = "Embed objects" }
        },
        reply = {
            { name = "channel_id", type = "string", required = true, description = "Channel snowflake ID" },
            { name = "message_id", type = "string", required = true, description = "Message snowflake ID to reply to" },
            { name = "content", type = "string", required = true, description = "Reply text" }
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
        else return { success = false, error = "unknown action: " .. tostring(action) }
        end
    end
})

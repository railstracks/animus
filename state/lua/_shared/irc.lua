-- irc.lua — IRC adapter for Animus
-- Registers via animus.register_channel() as a stateless adapter.
--
-- IRC has no REST API. The C++ IrcLoop handles connection and message routing.
-- This adapter provides proactive send capability via the standard reply mechanism.
-- Join/part and channel listing require C++ bridge access (future).
--
-- Auth: Handled by C++ IrcLoop. Config has server, nick, channels.

-- json.encode / json.decode are provided by the Animus Lua sandbox

-- ===========================================================================
-- Config helpers
-- ===========================================================================

local function cfg_key(platform_id, field)
    return "channels." .. platform_id .. "." .. field
end

-- ===========================================================================
-- Action handlers
-- ===========================================================================

local actions = {}

function actions.post(args)
    local target = args.channel or args.target or ""
    local text = args.content or ""

    if target == "" then
        return { success = false, error = "Missing channel/target parameter" }
    end
    if text == "" then
        return { success = false, error = "Missing content parameter" }
    end

    -- Proactive IRC send requires C++ bridge (IrcLoop owns the socket).
    -- For now, advise using the standard reply mechanism.
    return {
        success = false,
        output = "Proactive IRC send not yet available via tool. " ..
                 "Use the standard reply mechanism — your text output will be " ..
                 "routed to the IRC channel or query automatically.",
        target = target,
    }
end

function actions.list_channels(args)
    local platform_id = args.platform_id or ""

    -- Read monitored channels from config
    local channels_str = config.get(cfg_key(platform_id, "monitored_channels"))
    local channels = {}
    if channels_str and channels_str ~= "" then
        for ch in channels_str:gmatch("[^,]+") do
            ch = ch:match("^%s*(.-)%s*$")  -- trim
            if ch ~= "" then channels[#channels + 1] = ch end
        end
    end

    local server = config.get(cfg_key(platform_id, "server")) or "unknown"
    local nick = config.get(cfg_key(platform_id, "nick")) or "unknown"

    return {
        success = true,
        output = "IRC channels on " .. server .. " (nick: " .. nick .. ")",
        channels = channels,
        server = server,
        nick = nick,
    }
end

function actions.join(args)
    return {
        success = false,
        output = "IRC join/part requires C++ bridge access (not yet implemented). " ..
                 "Configure channels in the admin UI.",
    }
end

function actions.part(args)
    return actions.join(args)  -- same limitation
end

function actions.get_channel(args)
    -- Return config info about a channel
    local platform_id = args.platform_id or ""
    local channels_str = config.get(cfg_key(platform_id, "monitored_channels")) or ""

    return {
        success = true,
        output = "Channel info for " .. (args.channel or "unknown"),
        channel = args.channel or "",
        monitored_channels = channels_str,
    }
end

-- ===========================================================================
-- Handler dispatch
-- ===========================================================================

local function handler(args)
    local action = args.action
    if not action then
        return { success = false, error = "Missing 'action' parameter" }
    end

    local fn = actions[action]
    if not fn then
        return { success = false, error = "Unknown IRC action: " .. action }
    end

    return fn(args)
end

-- ===========================================================================
-- Registration
-- ===========================================================================

animus.register_channel({
    id = "irc",
    name = "IRC",
    capabilities = {"read", "write"},
    actions = {"post", "list_channels", "join", "part", "get_channel"},
    schema = {
        post = {
            { name = "channel", type = "string", required = true, description = "Target channel (e.g. #steadyfort)" },
            { name = "content", type = "string", required = true, description = "Message text" },
        },
        list_channels = {},
        join = {
            { name = "channel", type = "string", required = true, description = "Channel to join" },
        },
        part = {
            { name = "channel", type = "string", required = true, description = "Channel to leave" },
        },
        get_channel = {
            { name = "channel", type = "string", required = false, description = "Channel name" },
        },
    },
    handler = handler,
})
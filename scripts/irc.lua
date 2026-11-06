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
    -- The C++ bridge in ChannelsTool intercepts irc post/reply before this
    -- handler runs, so reaching here means no channel instance matched —
    -- advise the standard reply path.
    local err = "IRC send not available for this instance (no C++ bridge match). " ..
                "Check the platform_id with channels list, or use the configured " ..
                "IRC instance."
    return {
        success = false,
        error = err,   -- #209: dispatcher reads error, not output, on failure
        output = err,  -- kept for readers of output
        target = target,
    }
end

-- reply shares post's send path in the C++ bridge (same SendReply route);
-- the Lua handler mirrors the same limitation and error text.
actions.reply = actions.post

-- send_action routes through the C++ bridge's SendAction path, which wraps
-- the text in a CTCP ACTION frame (\001ACTION text\001) — a /me on the wire.
-- The Lua handler mirrors post/reply's fallback error for unmatched instances.
actions.send_action = actions.post

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

-- #205: join/part are intercepted by the C++ bridge on live instances
-- (gated per-instance by the allow_join_part toggle in the /channels
-- admin form). These fallbacks run only when no bridge is present.
function actions.join(args)
    local err = "IRC join is available on live instances with channel roaming " ..
                "enabled (allow_join_part in the admin UI)."
    return {
        success = false,
        error = err,   -- #209: dispatcher reads error, not output, on failure
        output = err,
    }
end

function actions.part(args)
    return actions.join(args)  -- same limitation, same message shape
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
    actions = {"post", "reply", "send_action", "list_channels", "join", "part", "get_channel"},
    schema = {
        post = {
            { name = "channel", type = "string", required = true, description = "Target channel (e.g. #steadyfort)" },
            { name = "content", type = "string", required = true, description = "Message text" },
        },
        reply = {
            { name = "channel", type = "string", required = false, description = "Target channel (e.g. #steadyfort); filled automatically from the arrival when omitted" },
            { name = "content", type = "string", required = true, description = "Message text" },
        },
        send_action = {
            { name = "channel", type = "string", required = false, description = "Target channel (e.g. #steadyfort); filled automatically from the arrival when omitted" },
            { name = "content", type = "string", required = true, description = "Action text for a /me-style emote (e.g. 'leans over the console') — sent as a CTCP ACTION; do NOT prefix with /me" },
        },
        list_channels = {},
        join = {
            { name = "channel", type = "string", required = true, description = "Channel to join (e.g. #campaign-night)" },
            { name = "key", type = "string", required = false, description = "Channel key (password) for keyed channels" },
        },
        part = {
            { name = "channel", type = "string", required = true, description = "Channel to leave" },
            { name = "farewell", type = "string", required = false, description = "Optional part reason shown to the channel" },
        },
        get_channel = {
            { name = "channel", type = "string", required = false, description = "Channel name" },
        },
    },
    handler = handler,
})
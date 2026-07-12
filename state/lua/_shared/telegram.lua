-- telegram.lua — Telegram Bot API adapter for Animus
-- Registers via animus.register_channel() as a stateless adapter.
-- Auth: bot token from config (channels.telegram:INSTANCE.bot_token)
--
-- Telegram Bot API docs: https://core.telegram.org/bots/api
-- Rate limits: 30 msg/sec to same chat, 20 msg/sec across chats. No monthly caps.

-- json.encode / json.decode are provided by the Animus Lua sandbox

-- ===========================================================================
-- Config helpers
-- ===========================================================================

local function cfg_key(platform_id, field)
    return "channels." .. platform_id .. "." .. field
end

local function get_token(platform_id)
    return config.get(cfg_key(platform_id, "bot_token"))
end

-- ===========================================================================
-- HTTP helpers
-- ===========================================================================

local function api_call(token, method, params)
    local url = "https://api.telegram.org/bot" .. token .. "/" .. method
    local resp = animus.http_post(url, json.encode(params), {
        ["Content-Type"] = "application/json"
    })
    if not resp then
        return nil, "HTTP request failed"
    end
    local data = json.decode(resp.body or "{}")
    if not data.ok then
        return nil, (data.description or "unknown error")
    end
    return data.result, nil
end

local function api_get(token, method, params)
    -- Build query string for GET
    local qs = ""
    if params then
        local parts = {}
        for k, v in pairs(params) do
            parts[#parts + 1] = k .. "=" .. v
        end
        qs = "?" .. table.concat(parts, "&")
    end
    local url = "https://api.telegram.org/bot" .. token .. "/" .. method .. qs
    local resp = animus.http_get(url)
    if not resp then
        return nil, "HTTP request failed"
    end
    local data = json.decode(resp.body or "{}")
    if not data.ok then
        return nil, (data.description or "unknown error")
    end
    return data.result, nil
end

-- ===========================================================================
-- Action handlers
-- ===========================================================================

local actions = {}

function actions.send_message(args)
    local token = get_token(args.platform_id)
    if not token then return { success = false, error = "No bot token configured" } end

    local params = {
        chat_id = args.chat_id,
        text = args.content,
    }
    if args.parse_mode then params.parse_mode = args.parse_mode end
    if args.reply_to_message_id then params.reply_to_message_id = tonumber(args.reply_to_message_id) end

    local result, err = api_call(token, "sendMessage", params)
    if err then return { success = false, error = "Telegram sendMessage failed: " .. err } end

    return {
        success = true,
        output = "Message sent to " .. args.chat_id,
        message_id = tostring(result.message_id),
        chat_id = tostring(result.chat.id),
    }
end

function actions.get_messages(args)
    local token = get_token(args.platform_id)
    if not token then return { success = false, error = "No bot token configured" } end

    -- getUpdates returns recent updates (messages, etc.)
    local params = {
        limit = tostring(args.limit or "20"),
    }
    if args.offset then params.offset = tostring(args.offset) end

    local result, err = api_get(token, "getUpdates", params)
    if err then return { success = false, error = "Telegram getUpdates failed: " .. err } end

    local messages = {}
    for _, update in ipairs(result or {}) do
        if update.message then
            local msg = update.message
            messages[#messages + 1] = {
                update_id = tostring(update.update_id),
                message_id = tostring(msg.message_id),
                chat_id = tostring(msg.chat.id),
                chat_title = msg.chat.title or msg.chat.username or "DM",
                from = msg.from and (msg.from.first_name or msg.from.username or tostring(msg.from.id)) or "unknown",
                text = msg.text or "",
                date = tostring(msg.date),
            }
        end
    end

    return {
        success = true,
        output = "Retrieved " .. #messages .. " messages",
        messages = messages,
    }
end

function actions.get_conversations(args)
    -- Telegram doesn't have a direct "list chats" API.
    -- We return info based on recent getUpdates.
    return actions.get_messages(args)
end

function actions.get_me(args)
    local token = get_token(args.platform_id)
    if not token then return { success = false, error = "No bot token configured" } end

    local result, err = api_get(token, "getMe")
    if err then return { success = false, error = "Telegram getMe failed: " .. err } end

    return {
        success = true,
        output = "Bot: @" .. (result.username or "unknown"),
        id = tostring(result.id),
        username = result.username or "",
        first_name = result.first_name or "",
        is_bot = tostring(result.is_bot or false),
    }
end

function actions.get_chat(args)
    local token = get_token(args.platform_id)
    if not token then return { success = false, error = "No bot token configured" } end

    local result, err = api_get(token, "getChat", { chat_id = args.chat_id })
    if err then return { success = false, error = "Telegram getChat failed: " .. err } end

    return {
        success = true,
        output = "Chat: " .. (result.title or result.username or tostring(result.id)),
        id = tostring(result.id),
        type = result.type or "",
        title = result.title or "",
        username = result.username or "",
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
        return { success = false, error = "Unknown Telegram action: " .. action }
    end

    return fn(args)
end

-- ===========================================================================
-- Registration
-- ===========================================================================

animus.register_channel({
    id = "telegram",
    name = "Telegram",
    capabilities = {"read", "write"},
    actions = {"send_message", "get_messages", "get_conversations", "get_me", "get_chat"},
    schema = {
        send_message = {
            { name = "chat_id", type = "string", required = true, description = "Chat ID to send to" },
            { name = "content", type = "string", required = true, description = "Message text" },
            { name = "reply_to_message_id", type = "string", required = false, description = "Message ID to reply to" },
            { name = "parse_mode", type = "string", required = false, description = "Parse mode: MarkdownV2, HTML" },
        },
        get_messages = {
            { name = "limit", type = "number", required = false, description = "Max messages to retrieve (default 20)" },
            { name = "offset", type = "string", required = false, description = "Update ID offset" },
        },
        get_conversations = {
            { name = "limit", type = "number", required = false, description = "Max messages to retrieve" },
        },
        get_me = {},
        get_chat = {
            { name = "chat_id", type = "string", required = true, description = "Chat ID to look up" },
        },
    },
    handler = handler,
})
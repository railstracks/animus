-- whatsapp.lua — WhatsApp adapter for Animus
-- Registers via animus.register_channel() as a stateless adapter.
--
-- WhatsApp has no public REST API. Messages are sent via the C++ gateway
-- loop's outbound queue. This adapter provides proactive send_message
-- capability only — message receipt is handled by the C++ connector.
--
-- Auth: Handled by C++ gateway (Signal Protocol, QR pairing). No tokens here.

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

function actions.send_message(args)
    local jid = args.jid or args.chat_id or ""
    local text = args.content or ""

    if jid == "" then
        return { success = false, error = "Missing jid or chat_id parameter" }
    end
    if text == "" then
        return { success = false, error = "Missing content parameter" }
    end

    return {
        success = false,
        output = "Proactive WhatsApp send not yet available via tool. " ..
                 "Use the standard reply mechanism — your text output will be " ..
                 "routed to the WhatsApp conversation automatically.",
        jid = jid,
    }
end

function actions.get_conversations(args)
    local platform_id = args.platform_id or ""
    local phone = config.get(cfg_key(platform_id, "phone"))
    local name = config.get(cfg_key(platform_id, "name"))

    return {
        success = true,
        output = "WhatsApp conversations require gateway state access (not yet available via tool)",
        phone = phone or "",
        name = name or "",
    }
end

function actions.get_messages(args)
    return {
        success = false,
        output = "WhatsApp message history requires gateway state access. " ..
                 "Messages are received via the C++ connector in real-time.",
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
        return { success = false, error = "Unknown WhatsApp action: " .. action }
    end

    return fn(args)
end

-- ===========================================================================
-- Registration
-- ===========================================================================

animus.register_channel({
    id = "whatsapp",
    name = "WhatsApp",
    capabilities = {"write"},
    actions = {"send_message", "get_conversations", "get_messages"},
    schema = {
        send_message = {
            { name = "jid", type = "string", required = true, description = "WhatsApp JID (e.g. 31612345678@s.whatsapp.net)" },
            { name = "content", type = "string", required = true, description = "Message text" },
        },
        get_conversations = {},
        get_messages = {},
    },
    handler = handler,
})
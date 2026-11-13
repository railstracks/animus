-- nextcloud.lua — Nextcloud Talk outbound adapter for Animus
-- Registers via animus.register_channel() as a stateless adapter.
-- Hybrid: the C++ NextcloudAdapter owns inbound (long-poll dispatch,
-- session binding, auto-reply); this Lua seam owns explicit outbound
-- through the channels tool (proactive posts, e.g. the notetaker flow).
-- Auth: app password from config (channels.nextcloud:INSTANCE.*)
--   server_url / username / app_password — all in ChannelCredentialKeys()
--
-- Talk chat API: POST /ocs/v2.php/apps/spreed/api/v1/chat/<token>
-- OCS headers: Accept: application/json, OCS-APIREQUEST: true
-- Room types: 1=one-to-one (DM), 2=group, 3=public, 6=note-to-self
--
-- json.encode / json.decode are provided by the Animus Lua sandbox

-- ===========================================================================
-- Config helpers
-- ===========================================================================

local function cfg_key(platform_id, field)
    return "channels." .. platform_id .. "." .. field
end

-- config.get returns "" for unset keys (never nil) — empty-string checks
-- are the guard; `or` fallbacks are dead code against "".
local function get_creds(platform_id)
    if platform_id == nil or platform_id == "" then return nil end
    local base = config.get(cfg_key(platform_id, "server_url"))
    local user = config.get(cfg_key(platform_id, "username"))
    local pass = config.get(cfg_key(platform_id, "app_password"))
    if base == "" or user == "" or pass == "" then return nil end
    while base:sub(-1) == "/" do base = base:sub(1, -2) end
    return { base_url = base, user = user, pass = pass }
end

-- ===========================================================================
-- base64 (Basic auth) — pure arithmetic, no sandbox crypto primitives
-- ===========================================================================

local function b64(s)
    local chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    local parts = {}
    local i = 1
    local n = #s
    while i <= n do
        local b1, b2, b3 = s:byte(i), s:byte(i + 1), s:byte(i + 2)
        local v = b1 * 65536 + (b2 or 0) * 256 + (b3 or 0)
        local c4 = v % 64; v = math.floor(v / 64)
        local c3 = v % 64; v = math.floor(v / 64)
        local c2 = v % 64
        local c1 = math.floor(v / 64)
        parts[#parts + 1] = chars:sub(c1 + 1, c1 + 1)
            .. chars:sub(c2 + 1, c2 + 1)
            .. (b2 and chars:sub(c3 + 1, c3 + 1) or "=")
            .. (b3 and chars:sub(c4 + 1, c4 + 1) or "=")
        i = i + 3
    end
    return table.concat(parts)
end

local function ocs_headers(creds)
    return {
        ["Content-Type"] = "application/json",
        ["Accept"] = "application/json",
        ["OCS-APIREQUEST"] = "true",
        ["Authorization"] = "Basic " .. b64(creds.user .. ":" .. creds.pass),
    }
end

-- ===========================================================================
-- API helpers
-- ===========================================================================

local function fail_http(what, status, body)
    local detail = ""
    if body and body ~= "" then
        local ok, data = pcall(json.decode, body)
        if ok and data and data.ocs and data.ocs.meta
            and data.ocs.meta.message
            and data.ocs.meta.message ~= "" then
            detail = " - " .. tostring(data.ocs.meta.message)
        end
    end
    return { success = false,
             error = what .. " failed: HTTP " .. tostring(status) .. detail }
end

local function send_message(creds, token, text)
    local url = creds.base_url .. "/ocs/v2.php/apps/spreed/api/v1/chat/" .. token
    local resp = animus.http_post(url, {
        headers = ocs_headers(creds),
        body = json.encode({ message = text, silent = false }),
    })
    if not resp then
        return { success = false, error = "send failed: HTTP request failed" }
    end
    if resp.status ~= 200 and resp.status ~= 201 then
        return fail_http("send", resp.status, resp.body)
    end
    local id = ""
    local ok, data = pcall(json.decode, resp.body or "{}")
    if ok and data and data.ocs and data.ocs.data and data.ocs.data.id ~= nil then
        id = tostring(data.ocs.data.id)
    end
    local out = "sent to room " .. token
    if id ~= "" then out = out .. " (message id " .. id .. ")" end
    return { success = true, output = out }
end

local function list_rooms(creds)
    local url = creds.base_url .. "/ocs/v2.php/apps/spreed/api/v4/room?format=json"
    local resp = animus.http_get(url, { headers = ocs_headers(creds) })
    if not resp then
        return { success = false, error = "rooms failed: HTTP request failed" }
    end
    if resp.status ~= 200 then
        return fail_http("rooms", resp.status, resp.body)
    end
    local ok, data = pcall(json.decode, resp.body or "{}")
    if not (ok and data and data.ocs and data.ocs.data) then
        return { success = false, error = "rooms failed: unexpected payload" }
    end
    local rows = {}
    local type_names = { [1] = "dm", [2] = "group", [3] = "public", [6] = "note-to-self" }
    for _, room in ipairs(data.ocs.data) do
        if room.token then
            rows[#rows + 1] = tostring(room.token) .. " | "
                .. (type_names[room.type] or ("type" .. tostring(room.type)))
                .. " | " .. tostring(room.displayName or "")
        end
    end
    if #rows == 0 then
        return { success = true, output = "no rooms visible to this account" }
    end
    return { success = true, output = table.concat(rows, "\n") }
end

-- ===========================================================================
-- Actions
-- ===========================================================================

local function creds_or_error(args)
    local creds = get_creds(args.platform_id)
    if not creds then
        return nil, { success = false,
            error = "Nextcloud channel not available for this instance "
                .. "(no server_url/username/app_password in channel config). "
                .. "Pass platform_id \"nextcloud:<name>\" or run from a Talk "
                .. "session so the instance is resolved." }
    end
    return creds, nil
end

local actions = {}

actions["send"] = function(args)
    local token = args.token or ""
    local text = args.text or ""
    if token == "" then
        return { success = false,
                 error = "token is required (room token, e.g. from the arrival's room_token)" }
    end
    if text == "" then
        return { success = false, error = "text is required" }
    end
    local creds, err = creds_or_error(args)
    if err then return err end
    return send_message(creds, token, text)
end

actions["rooms"] = function(args)
    local creds, err = creds_or_error(args)
    if err then return err end
    return list_rooms(creds)
end

-- ===========================================================================
-- Handler
-- ===========================================================================

local function handler(args)
    local action = args.action
    if not action then
        return { success = false, error = "Missing 'action' parameter" }
    end
    local fn = actions[action]
    if not fn then
        return { success = false, error = "Unknown Nextcloud action: " .. action }
    end
    return fn(args)
end

-- ===========================================================================
-- Registration
-- ===========================================================================

animus.register_channel({
    id = "nextcloud",
    name = "Nextcloud Talk",
    capabilities = {"read", "write"},
    actions = {"send", "rooms"},
    schema = {
        send = {
            { name = "token", type = "string", required = true,
              description = "Room token to post to (from the arrival's room_token, or the rooms action)" },
            { name = "text", type = "string", required = true, description = "Message text" },
        },
        rooms = {},
    },
    handler = handler,
})

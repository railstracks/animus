-- ============================================================================
-- VKontakte (VK) Community Bot Adapter for Animus
-- Ticket 052 — second Lua social adapter (community bot pattern)
--
-- Registers via animus.register_channel() as a stateless adapter.
-- Auth: community token (static, no expiry, no refresh).
--   config.get("channels.vk:community.access_token")
--   config.get("channels.vk:community.group_id")
--
-- VK's officially supported bot pattern: users message the community,
-- the bot replies. No personal account automation.
--
-- Community tokens have limited API access — some methods that work
-- with user tokens are unavailable (e.g. wall.get for browsing returns
-- error 27 "Group authorization failed"). This adapter only exposes
-- actions that are confirmed to work with community tokens.
--
-- Available actions:
--   post         — wall.post (community wall)
--   reply        — wall.createComment (comment on wall post)
--   like         — likes.add
--   delete       — wall.delete (own community posts)
--   send_message — messages.send (community → user)
--   get_messages — messages.getHistory (conversation with user)
--   get_long_poll_server — groups.getLongPollServer (for Ticket 051)
-- ============================================================================

local API_VERSION = "5.131"
local API_BASE = "https://api.vk.ru/method"

-- ---------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------

--- Build a config key for a platform instance.
local function cfg_key(platform_id, field)
    return "channels." .. platform_id .. "." .. field
end

--- URL-encode a string (percent-encoding for query parameters).
local function url_encode(str)
    if not str then return "" end
    return (tostring(str):gsub("[^%w%-%._~]", function(c)
        return string.format("%%%02X", string.byte(c))
    end))
end

--- Build a query string from a table of parameters.
local function build_query(params)
    if not params then return "" end
    local parts = {}
    for k, v in pairs(params) do
        parts[#parts + 1] = url_encode(k) .. "=" .. url_encode(v)
    end
    return table.concat(parts, "&")
end

-- ---------------------------------------------------------------------------
-- Authentication (community token — static, no refresh needed)
-- ---------------------------------------------------------------------------

--- Get the community access token from config.
-- Returns the token string on success, nil + error on failure.
local function get_access_token(platform_id)
    local token = config.get(cfg_key(platform_id, "access_token"))
    if not token or token == "" then
        return nil, "no access_token configured — generate one in group settings"
    end
    return token
end

--- Get the community group_id from config.
local function get_group_id(platform_id)
    local gid = config.get(cfg_key(platform_id, "group_id"))
    if not gid or gid == "" then
        return nil, "no group_id configured"
    end
    return gid
end

-- ---------------------------------------------------------------------------
-- VK API Call Helpers
-- ---------------------------------------------------------------------------

--- Make an authenticated GET request to the VK API.
local function vk_api_get(platform_id, method, params)
    local token, err = get_access_token(platform_id)
    if not token then
        return { status = 0, body = err }
    end

    params = params or {}
    params.access_token = token
    params.v = API_VERSION

    local url = API_BASE .. "/" .. method .. "?" .. build_query(params)
    return animus.http_get(url, {})
end

--- Make an authenticated POST request to the VK API.
local function vk_api_post(platform_id, method, params)
    local token, err = get_access_token(platform_id)
    if not token then
        return { status = 0, body = err }
    end

    params = params or {}
    params.access_token = token
    params.v = API_VERSION

    local body = build_query(params)
    return animus.http_post(API_BASE .. "/" .. method, {
        headers = { ["Content-Type"] = "application/x-www-form-urlencoded" },
        body = body
    })
end

--- Parse a VK API response and extract the response object.
-- VK returns 200 with { error: {...} } on API errors.
-- Returns the response data, or nil + error.
local function parse_vk_response(resp)
    if resp.status ~= 200 then
        return nil, "API request failed (" .. tostring(resp.status) .. "): " .. tostring(resp.body)
    end

    local ok, data = pcall(json.decode, resp.body)
    if not ok or not data then
        return nil, "response parse error: " .. tostring(data)
    end

    if data.error then
        local msg = (data.error.error_msg or "") ..
                    " (code " .. tostring(data.error.error_code or "?") .. ")"
        return nil, "VK API error: " .. msg
    end

    return data.response
end

-- ---------------------------------------------------------------------------
-- Action Handlers
-- ---------------------------------------------------------------------------

--- Post on the community wall.
local function do_post(args)
    local pid = args.platform_id
    if not args.content or args.content == "" then
        return { success = false, error = "content is required for post" }
    end

    local params = {
        message = args.content
    }

    -- owner_id for community wall is negative: -group_id
    local gid = get_group_id(pid)
    if gid then
        params.owner_id = "-" .. gid
    end

    local resp = vk_api_post(pid, "wall.post", params)
    local result, err = parse_vk_response(resp)
    if not result then
        return { success = false, error = err }
    end

    return {
        success = true,
        output = json.encode({ post_id = result.post_id })
    }
end

--- Reply (comment) on a wall post.
local function do_reply(args)
    local pid = args.platform_id
    if not args.post_id or args.post_id == "" then
        return { success = false, error = "post_id is required for reply" }
    end
    if not args.content or args.content == "" then
        return { success = false, error = "content is required for reply" }
    end

    local params = {
        post_id = args.post_id,
        message = args.content
    }

    if args.owner_id then
        params.owner_id = args.owner_id
    end

    local resp = vk_api_post(pid, "wall.createComment", params)
    local result, err = parse_vk_response(resp)
    if not result then
        return { success = false, error = err }
    end

    return {
        success = true,
        output = json.encode({
            comment_id = result.comment_id,
            parent_post = args.post_id
        })
    }
end

--- Like a post or comment.
local function do_like(args)
    local pid = args.platform_id
    if not args.post_id or args.post_id == "" then
        return { success = false, error = "post_id is required for like" }
    end

    local params = {
        type = args.target_type or "post",
        item_id = args.post_id
    }

    if args.owner_id then
        params.owner_id = args.owner_id
    end

    local resp = vk_api_post(pid, "likes.add", params)
    local result, err = parse_vk_response(resp)
    if not result then
        return { success = false, error = err }
    end

    return {
        success = true,
        output = json.encode({ likes = result.likes, liked = args.post_id })
    }
end

--- Delete a community wall post.
local function do_delete(args)
    local pid = args.platform_id
    if not args.post_id or args.post_id == "" then
        return { success = false, error = "post_id is required for delete" }
    end

    local params = {
        post_id = args.post_id
    }

    local gid = get_group_id(pid)
    if gid then
        params.owner_id = "-" .. gid
    end

    local resp = vk_api_post(pid, "wall.delete", params)
    local result, err = parse_vk_response(resp)
    if not result then
        return { success = false, error = err }
    end

    return {
        success = true,
        output = json.encode({ deleted = args.post_id })
    }
end

--- Send a message to a VK user (community → user).
local function do_send_message(args)
    local pid = args.platform_id
    -- peer_id works for both DMs (user ID) and group chats (2000000000+local_id)
    local target = args.peer_id or args.user_id or ""
    if target == "" then
        return { success = false, error = "peer_id or user_id is required for send_message" }
    end
    if not args.content or args.content == "" then
        return { success = false, error = "content is required for send_message" }
    end

    local params = {
        peer_id = target,
        message = args.content,
        random_id = tostring(math.random(1, 2147483647))
    }

    local resp = vk_api_post(pid, "messages.send", params)
    local result, err = parse_vk_response(resp)
    if not result then
        return { success = false, error = err }
    end

    return {
        success = true,
        output = json.encode({ message_id = result, to = target })
    }
end

--- Get conversation history with a user or chat.
-- peer_id can be a user ID (e.g. 1115662869) or a chat ID (e.g. 2000000001)
local function do_get_messages(args)
    local pid = args.platform_id
    if not args.peer_id or args.peer_id == "" then
        -- Legacy: accept user_id as alias for peer_id
        if args.user_id and args.user_id ~= "" then
            args.peer_id = args.user_id
        else
            return { success = false, error = "peer_id is required for get_messages (user ID or chat ID like 2000000001)" }
        end
    end

    local params = {
        peer_id = args.peer_id,
        count = tostring(args.limit or 25)
    }
    if args.cursor then params.offset = args.cursor end

    local resp = vk_api_get(pid, "messages.getHistory", params)
    local result, err = parse_vk_response(resp)
    if not result then
        return { success = false, error = err }
    end

    local messages = {}
    if result.items then
        for _, item in ipairs(result.items) do
            local entry = {
                id = tostring(item.id),
                from_id = tostring(item.from_id),
                text = item.text or "",
                date = item.date or 0,
                out = item.out == 1
            }
            messages[#messages + 1] = entry
        end
    end

    return {
        success = true,
        output = json.encode({
            count = result.count or #messages,
            messages = messages
        })
    }
end

--- List active conversations.
local function do_get_conversations(args)
    local pid = args.platform_id

    local params = {
        count = tostring(args.limit or 20)
    }
    if args.cursor then params.offset = args.cursor end

    local resp = vk_api_get(pid, "messages.getConversations", params)
    local result, err = parse_vk_response(resp)
    if not result then
        return { success = false, error = err }
    end

    local conversations = {}
    if result.items then
        for _, item in ipairs(result.items) do
            local conv = item.conversation or {}
            local peer = conv.peer or {}
            local last_msg = item.last_message or {}
            conversations[#conversations + 1] = {
                peer_id = tostring(peer.id or ""),
                type = peer.type or "",
                local_id = tostring(peer.local_id or ""),
                last_message_text = last_msg.text or "",
                last_message_date = last_msg.date or 0,
                unread_count = conv.unread_count or 0
            }
        end
    end

    return {
        success = true,
        output = json.encode({
            count = result.count or #conversations,
            unread_count = result.unread_count or 0,
            conversations = conversations
        })
    }
end

--- Get Bots Long Poll server info (hook for Ticket 051 polling infrastructure).
local function do_get_long_poll_server(args)
    local pid = args.platform_id

    local gid = get_group_id(pid)
    if not gid then
        return { success = false, error = "group_id required for long poll server" }
    end

    local params = {
        group_id = gid
    }

    local resp = vk_api_get(pid, "groups.getLongPollServer", params)
    local result, err = parse_vk_response(resp)
    if not result then
        return { success = false, error = err }
    end

    return {
        success = true,
        output = json.encode({
            server = result.server or "",
            key = result.key or "",
            ts = result.ts or ""
        })
    }
end

-- ---------------------------------------------------------------------------
-- Registration
-- ---------------------------------------------------------------------------

animus.register_channel({
    id = "vk",
    name = "VKontakte",
    capabilities = {"write", "message"},
    actions = {
        "post", "reply", "like", "delete",
        "send_message", "get_messages", "get_conversations",
        "get_long_poll_server"
    },
    schema = {
        post = {
            { name = "content", type = "string", required = true, description = "Post text for the community wall" }
        },
        reply = {
            { name = "post_id", type = "string", required = true, description = "Wall post ID to comment on" },
            { name = "content", type = "string", required = true, description = "Comment text" },
            { name = "owner_id", type = "string", required = false, description = "Wall owner ID (negative for communities)" }
        },
        like = {
            { name = "post_id", type = "string", required = true, description = "Post ID to like" },
            { name = "owner_id", type = "string", required = false, description = "Content owner ID" },
            { name = "target_type", type = "string", required = false, description = "Content type: post, comment, photo (default: post)" }
        },
        delete = {
            { name = "post_id", type = "string", required = true, description = "Post ID to delete from community wall" }
        },
        send_message = {
            { name = "peer_id", type = "string", required = true, description = "Target: user ID for DMs or chat ID (e.g. 2000000001) for group chats" },
            { name = "user_id", type = "string", required = false, description = "Alias for peer_id (backwards compatibility)" },
            { name = "content", type = "string", required = true, description = "Message text" }
        },
        get_messages = {
            { name = "peer_id", type = "string", required = true, description = "Peer ID: user ID for DMs or chat ID (e.g. 2000000001) for group chats" },
            { name = "user_id", type = "string", required = false, description = "Alias for peer_id (for backwards compatibility)" },
            { name = "limit", type = "integer", required = false, description = "Max messages (default 25)" },
            { name = "cursor", type = "string", required = false, description = "Pagination offset" }
        },
        get_conversations = {
            { name = "limit", type = "integer", required = false, description = "Max conversations (default 20)" },
            { name = "cursor", type = "string", required = false, description = "Pagination offset" }
        },
        get_long_poll_server = {}
    },
    handler = function(args)
        local action = args.action
        if not action or action == "" then
            return { success = false, error = "action is required" }
        end

        if action == "post" then return do_post(args)
        elseif action == "reply" then return do_reply(args)
        elseif action == "like" then return do_like(args)
        elseif action == "delete" then return do_delete(args)
        elseif action == "send_message" then return do_send_message(args)
        elseif action == "get_messages" then return do_get_messages(args)
        elseif action == "get_conversations" then return do_get_conversations(args)
        elseif action == "get_long_poll_server" then return do_get_long_poll_server(args)
        else return { success = false, error = "unknown action: " .. tostring(action) }
        end
    end
})

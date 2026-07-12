-- ============================================================================
-- Moltbook Adapter for Animus
-- Ticket 076 — Reddit-style social network for AI agents
--
-- Registers via animus.register_channel() as a stateless adapter.
-- Credentials are resolved per-instance from agent config:
--   config.get("channels.moltbook:<instance>.api_key")
--
-- Moltbook API: https://www.moltbook.com/api/v1
-- IMPORTANT: Always use www.moltbook.com — bare domain redirects and strips auth headers.
-- ============================================================================

local API_BASE = "https://www.moltbook.com/api/v1"

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

--- Get API key for a platform instance.
local function get_api_key(platform_id)
    local key = config.get(cfg_key(platform_id, "api_key"))
    if not key or key == "" then
        return nil, "No API key configured. Set channels." .. platform_id .. ".api_key"
    end
    return key, nil
end

--- Make an authenticated GET request to Moltbook.
local function api_get(platform_id, path, params)
    local key, err = get_api_key(platform_id)
    if err then return { success = false, error = err } end

    local url = API_BASE .. path
    if params and next(params) then
        local parts = {}
        for k, v in pairs(params) do
            if v ~= nil and v ~= "" then
                parts[#parts + 1] = url_encode(k) .. "=" .. url_encode(tostring(v))
            end
        end
        if #parts > 0 then
            url = url .. "?" .. table.concat(parts, "&")
        end
    end

    local resp = animus.http_get(url, {
        headers = {
            ["Authorization"] = "Bearer " .. key,
            ["Accept"] = "application/json"
        }
    })

    local data, decode_err = json_decode_safe(resp.body)
    if not data then
        return { success = false, error = "API request failed: " .. (decode_err or "unknown") }
    end

    if resp.status >= 400 then
        local msg = data.error or data.message or ("HTTP " .. tostring(resp.status))
        return { success = false, error = msg }
    end

    return { success = true, data = data }
end

--- Make an authenticated POST request to Moltbook.
local function api_post(platform_id, path, body)
    local key, err = get_api_key(platform_id)
    if err then return { success = false, error = err } end

    local resp = animus.http_post(API_BASE .. path, json.encode(body), {
        ["Authorization"] = "Bearer " .. key,
        ["Content-Type"] = "application/json",
        ["Accept"] = "application/json"
    })

    local data, decode_err = json_decode_safe(resp.body)
    if not data then
        return { success = false, error = "API request failed: " .. (decode_err or "unknown") }
    end

    if resp.status >= 400 then
        local msg = data.error or data.message or ("HTTP " .. tostring(resp.status))
        return { success = false, error = msg }
    end

    return { success = true, data = data }
end

--- Make an authenticated DELETE request to Moltbook.
local function api_delete(platform_id, path)
    local key, err = get_api_key(platform_id)
    if err then return { success = false, error = err } end

    -- Lua adapter http helpers don't have DELETE, use http_post with _method override
    -- Moltbook may support _method param, but try direct approach first
    -- Unfortunately animus Lua sandbox only exposes http_get and http_post
    -- For DELETE operations, we'll use http_post with empty body and hope for the best
    -- or include _method=DELETE parameter
    local resp = animus.http_post(API_BASE .. path, json.encode({}), {
        ["Authorization"] = "Bearer " .. key,
        ["Content-Type"] = "application/json",
        ["Accept"] = "application/json",
        ["X-HTTP-Method-Override"] = "DELETE"
    })

    local data, decode_err = json_decode_safe(resp.body)
    if not data then
        -- Some DELETE endpoints return 204 with empty body
        if resp.status == 204 or resp.status == 200 then
            return { success = true, data = {} }
        end
        return { success = false, error = "DELETE failed: " .. (decode_err or "unknown") }
    end

    if resp.status >= 400 then
        local msg = data.error or data.message or ("HTTP " .. tostring(resp.status))
        return { success = false, error = msg }
    end

    return { success = true, data = data }
end

--- Solve a Moltbook verification challenge (obfuscated math problem).
-- Challenge text has alternating caps, scattered symbols, broken words.
-- We extract numbers and operators from the obfuscated text.
local function solve_challenge(challenge_text)
    if not challenge_text then return nil end

    -- Strip symbols and normalize: remove non-alpha non-space chars, lowercase
    local cleaned = challenge_text:lower():gsub("[^%a%s]", "")
    -- Collapse multiple spaces
    cleaned = cleaned:gsub("%s+", " "):gsub("^%s+", ""):gsub("%s+$", "")

    -- Number words to values
    local number_words = {
        ["zero"] = 0, ["one"] = 1, ["two"] = 2, ["three"] = 3, ["four"] = 4,
        ["five"] = 5, ["six"] = 6, ["seven"] = 7, ["eight"] = 8, ["nine"] = 9,
        ["ten"] = 10, ["eleven"] = 11, ["twelve"] = 12, ["thirteen"] = 13,
        ["fourteen"] = 14, ["fifteen"] = 15, ["sixteen"] = 16, ["seventeen"] = 17,
        ["eighteen"] = 18, ["nineteen"] = 19, ["twenty"] = 20, ["thirty"] = 30,
        ["forty"] = 40, ["fifty"] = 50, ["sixty"] = 60, ["seventy"] = 70,
        ["eighty"] = 80, ["ninety"] = 90, ["hundred"] = 100,
    }

    -- Operation keywords
    local ops = {
        ["plus"] = "+", ["and"] = "+", ["added to"] = "+",
        ["minus"] = "-", ["less"] = "-", ["subtracted from"] = "-",
        ["times"] = "*", ["multiplied by"] = "*", ["of"] = "*",
        ["divided by"] = "/", ["over"] = "/",
        ["slows by"] = "-", ["speeds up by"] = "+",
    }

    -- Try to find two numbers and an operation
    -- Strategy: find number words in order, identify operation between them
    local words = {}
    for w in cleaned:gmatch("%S+") do
        words[#words + 1] = w
    end

    -- Extract numbers from word sequence (handle compound numbers like "twenty five")
    local numbers = {}
    local current_num = 0
    local has_num = false
    for _, w in ipairs(words) do
        local val = number_words[w]
        if val then
            if val == 100 and has_num then
                current_num = current_num * 100
            elseif val >= 20 and val < 100 and has_num and current_num < 10 then
                -- e.g. "twenty" after "five" → ignore prev, this is a new number
                if has_num then
                    numbers[#numbers + 1] = current_num
                end
                current_num = val
                has_num = true
            elseif val < 10 and has_num and current_num >= 20 then
                -- compound: "twenty five" = 25
                current_num = current_num + val
            else
                if has_num then
                    numbers[#numbers + 1] = current_num
                end
                current_num = val
                has_num = true
            end
        end
    end
    if has_num then
        numbers[#numbers + 1] = current_num
    end

    -- Also extract any literal digits from the challenge text
    for digit_str in challenge_text:gmatch("(%d+%.?%d*)") do
        local n = tonumber(digit_str)
        if n then
            -- Only add if not already captured from words
            local found = false
            for _, existing in ipairs(numbers) do
                if math.abs(existing - n) < 0.01 then found = true; break end
            end
            if not found then numbers[#numbers + 1] = n end
        end
    end

    -- Find the operation
    local op = nil
    local lower = cleaned
    -- Check multi-word ops first
    local op_patterns = {
        {"divided by", "/"}, {"multiplied by", "*"}, {"added to", "+"},
        {"slows by", "-"}, {"speeds up by", "+"},
        {"plus", "+"}, {"minus", "-"}, {"times", "*"}, {"over", "/"},
        {"and", "+"}, {"less", "-"},
    }
    for _, pair in ipairs(op_patterns) do
        if lower:find(pair[1], 1, true) then
            op = pair[2]
            break
        end
    end

    if #numbers >= 2 and op then
        local a, b = numbers[1], numbers[2]
        local result
        if op == "+" then result = a + b
        elseif op == "-" then result = a - b
        elseif op == "*" then result = a * b
        elseif op == "/" then result = b ~= 0 and a / b or 0
        end
        if result then
            return string.format("%.2f", result)
        end
    end

    return nil
end

--- Handle a verification challenge if present in the response.
-- Returns true if verification was handled (either solved or failed gracefully).
local function handle_verification(platform_id, resp_data)
    -- Check for verification in various response shapes
    local verification = nil
    local content_type = nil
    local content_id = nil

    if resp_data.verification then
        verification = resp_data.verification
    elseif resp_data.post and resp_data.post.verification then
        verification = resp_data.post.verification
    elseif resp_data.comment and resp_data.comment.verification then
        verification = resp_data.comment.verification
    elseif resp_data.submolt and resp_data.submolt.verification then
        verification = resp_data.submolt.verification
    end

    if not verification or not verification.verification_code then
        return false -- no verification needed
    end

    local code = verification.verification_code
    local challenge = verification.challenge_text

    if not challenge then
        log.warn("[moltbook] verification required but no challenge text provided")
        return false
    end

    local answer = solve_challenge(challenge)
    if not answer then
        log.warn("[moltbook] could not solve verification challenge: " .. truncate(challenge, 100))
        return false
    end

    -- Submit the verification answer
    local verify_result = api_post(platform_id, "/verify", {
        verification_code = code,
        answer = answer
    })

    if verify_result.success then
        log.info("[moltbook] verification challenge solved successfully")
        return true
    else
        log.warn("[moltbook] verification challenge failed: " .. (verify_result.error or "unknown"))
        return false
    end
end

-- ---------------------------------------------------------------------------
-- Action implementations
-- ---------------------------------------------------------------------------

local function do_post(args)
    local platform_id = args.platform_id or ""
    local submolt = args.submolt or args.submolt_name or "general"
    local title = args.title or ""
    local content = args.content or ""
    local url = args.url

    if title == "" and content == "" and not url then
        return { success = false, error = "post requires title and/or content or url" }
    end

    local body = {
        submolt_name = submolt,
        title = title,
        content = content
    }
    if url then body.url = url end
    if args.type then body.type = args.type end

    local result = api_post(platform_id, "/posts", body)
    if not result.success then return result end

    -- Handle verification challenge if present
    handle_verification(platform_id, result.data)

    local post = result.data.post or result.data
    return {
        success = true,
        output = "Posted to " .. submolt .. ": " .. truncate(title or content, 80),
        post_id = post.id or post.post_id,
        title = post.title
    }
end

local function do_comment(args)
    local platform_id = args.platform_id or ""
    local post_id = args.post_id or ""
    local content = args.content or ""
    local parent_id = args.parent_id or args.comment_id

    if post_id == "" then
        return { success = false, error = "comment requires post_id" }
    end
    if content == "" then
        return { success = false, error = "comment requires content" }
    end

    local body = { content = content }
    if parent_id then
        body.parent_id = parent_id
    end

    local result = api_post(platform_id, "/posts/" .. url_encode(post_id) .. "/comments", body)
    if not result.success then return result end

    -- Handle verification challenge if present
    handle_verification(platform_id, result.data)

    local comment = result.data.comment or result.data
    return {
        success = true,
        output = parent_id and "Replied to comment" or "Commented on post " .. truncate(post_id, 20),
        comment_id = comment.id or comment.comment_id,
        post_id = post_id
    }
end

local function do_reply(args)
    -- Alias for comment with parent_id
    return do_comment(args)
end

local function do_browse(args)
    local platform_id = args.platform_id or ""
    local sort = args.sort or "hot"
    local limit = args.limit or 25
    local submolt = args.submolt
    local cursor = args.cursor

    local params = {
        sort = sort,
        limit = tostring(math.min(limit, 100))
    }
    if submolt then params.submolt = submolt end
    if cursor then params.cursor = cursor end

    local result = api_get(platform_id, "/posts", params)
    if not result.success then return result end

    local data = result.data
    local posts = data.posts or data.data or {}
    local out = {}
    for _, p in ipairs(posts) do
        out[#out + 1] = {
            id = p.id,
            title = p.title,
            submolt = p.submolt_name or (p.submolt and p.submolt.name),
            author = p.author and p.author.name,
            upvotes = p.upvotes,
            comment_count = p.comment_count or p.comments_count,
            created_at = p.created_at
        }
    end

    return {
        success = true,
        output = json.encode({ posts = out, count = #out, has_more = data.has_more, next_cursor = data.next_cursor }),
        posts = out,
        count = #out,
        has_more = data.has_more,
        next_cursor = data.next_cursor
    }
end

local function do_search(args)
    local platform_id = args.platform_id or ""
    local query = args.query or ""
    local search_type = args.type or "all"
    local limit = args.limit or 20
    local cursor = args.cursor

    if query == "" then
        return { success = false, error = "search requires query" }
    end

    local params = {
        q = query,
        type = search_type,
        limit = tostring(math.min(limit, 50))
    }
    if cursor then params.cursor = cursor end

    local result = api_get(platform_id, "/search", params)
    if not result.success then return result end

    local data = result.data
    local results = data.results or data.data or {}
    local out = {}
    for _, r in ipairs(results) do
        out[#out + 1] = {
            id = r.id,
            type = r.type,
            title = r.title,
            content = truncate(r.content or "", 200),
            author = r.author and r.author.name,
            upvotes = r.upvotes,
            similarity = r.similarity,
            post_id = r.post_id
        }
    end

    return {
        success = true,
        output = json.encode({ results = out, count = #out, query = query }),
        results = out,
        count = #out,
        query = query,
        has_more = data.has_more,
        next_cursor = data.next_cursor
    }
end

local function do_get_notifications(args)
    local platform_id = args.platform_id or ""
    local limit = args.limit or 50
    local cursor = args.cursor

    local params = {
        limit = tostring(math.min(limit, 100))
    }
    if cursor then params.cursor = cursor end

    local result = api_get(platform_id, "/notifications", params)
    if not result.success then return result end

    local data = result.data
    local notifications = data.notifications or data.data or {}
    local out = {}
    for _, n in ipairs(notifications) do
        out[#out + 1] = {
            id = n.id,
            type = n.type,
            from = n.from_agent and n.from_agent.name,
            post_id = n.post_id,
            comment_id = n.comment_id,
            content = truncate(n.content or n.message or "", 100),
            created_at = n.created_at,
            is_read = n.is_read
        }
    end

    return {
        success = true,
        output = json.encode({ notifications = out, count = #out }),
        notifications = out,
        count = #out,
        has_more = data.has_more,
        next_cursor = data.next_cursor
    }
end

local function do_upvote(args)
    local platform_id = args.platform_id or ""
    local post_id = args.post_id or ""
    local comment_id = args.comment_id

    if post_id == "" and not comment_id then
        return { success = false, error = "upvote requires post_id or comment_id" }
    end

    local result
    if comment_id and comment_id ~= "" then
        result = api_post(platform_id, "/comments/" .. url_encode(comment_id) .. "/upvote", {})
    else
        result = api_post(platform_id, "/posts/" .. url_encode(post_id) .. "/upvote", {})
    end

    if not result.success then return result end

    return {
        success = true,
        output = comment_id and "Upvoted comment" or "Upvoted post"
    }
end

local function do_downvote(args)
    local platform_id = args.platform_id or ""
    local post_id = args.post_id or ""

    if post_id == "" then
        return { success = false, error = "downvote requires post_id" }
    end

    local result = api_post(platform_id, "/posts/" .. url_encode(post_id) .. "/downvote", {})
    if not result.success then return result end

    return { success = true, output = "Downvoted post" }
end

local function do_get_profile(args)
    local platform_id = args.platform_id or ""
    local name = args.name or args.agent_name

    local result
    if name and name ~= "" then
        result = api_get(platform_id, "/agents/profile", { name = name })
    else
        result = api_get(platform_id, "/agents/me", {})
    end

    if not result.success then return result end

    local data = result.data
    local agent = data.agent or data
    return {
        success = true,
        output = json.encode({
            name = agent.name,
            description = truncate(agent.description or "", 200),
            karma = agent.karma,
            follower_count = agent.follower_count,
            following_count = agent.following_count,
            posts_count = agent.posts_count,
            comments_count = agent.comments_count,
            is_claimed = agent.is_claimed
        }),
        agent = agent
    }
end

local function do_list_submolts(args)
    local platform_id = args.platform_id or ""

    local result = api_get(platform_id, "/submolts", {})
    if not result.success then return result end

    local data = result.data
    local submolts = data.submolts or data.data or {}
    local out = {}
    for _, s in ipairs(submolts) do
        out[#out + 1] = {
            name = s.name,
            display_name = s.display_name,
            description = truncate(s.description or "", 100),
            subscriber_count = s.subscriber_count,
            post_count = s.post_count
        }
    end

    return {
        success = true,
        output = json.encode({ submolts = out, count = #out }),
        submolts = out,
        count = #out
    }
end

local function do_follow(args)
    local platform_id = args.platform_id or ""
    local name = args.name or args.agent_name or ""

    if name == "" then
        return { success = false, error = "follow requires name (agent name)" }
    end

    local result = api_post(platform_id, "/agents/" .. url_encode(name) .. "/follow", {})
    if not result.success then return result end

    return {
        success = true,
        output = "Followed " .. name
    }
end

local function do_unfollow(args)
    local platform_id = args.platform_id or ""
    local name = args.name or args.agent_name or ""

    if name == "" then
        return { success = false, error = "unfollow requires name (agent name)" }
    end

    local result = api_delete(platform_id, "/agents/" .. url_encode(name) .. "/follow")
    if not result.success then return result end

    return {
        success = true,
        output = "Unfollowed " .. name
    }
end

local function do_delete(args)
    local platform_id = args.platform_id or ""
    local post_id = args.post_id or ""
    local comment_id = args.comment_id

    if comment_id and comment_id ~= "" then
        local result = api_delete(platform_id, "/comments/" .. url_encode(comment_id))
        if not result.success then return result end
        return { success = true, output = "Deleted comment" }
    end

    if post_id == "" then
        return { success = false, error = "delete requires post_id or comment_id" }
    end

    local result = api_delete(platform_id, "/posts/" .. url_encode(post_id))
    if not result.success then return result end

    return { success = true, output = "Deleted post" }
end

local function do_get_post(args)
    local platform_id = args.platform_id or ""
    local post_id = args.post_id or ""

    if post_id == "" then
        return { success = false, error = "get_post requires post_id" }
    end

    local result = api_get(platform_id, "/posts/" .. url_encode(post_id), {})
    if not result.success then return result end

    local data = result.data
    local post = data.post or data
    return {
        success = true,
        output = json.encode({
            id = post.id,
            title = post.title,
            content = truncate(post.content or "", 500),
            author = post.author and post.author.name,
            submolt = post.submolt_name or (post.submolt and post.submolt.name),
            upvotes = post.upvotes,
            downvotes = post.downvotes,
            comment_count = post.comment_count,
            created_at = post.created_at
        }),
        post = post
    }
end

local function do_get_comments(args)
    local platform_id = args.platform_id or ""
    local post_id = args.post_id or ""
    local sort = args.sort or "best"
    local limit = args.limit or 35
    local cursor = args.cursor

    if post_id == "" then
        return { success = false, error = "get_comments requires post_id" }
    end

    local params = {
        sort = sort,
        limit = tostring(math.min(limit, 100))
    }
    if cursor then params.cursor = cursor end

    local result = api_get(platform_id, "/posts/" .. url_encode(post_id) .. "/comments", params)
    if not result.success then return result end

    local data = result.data
    local comments = data.comments or data.data or {}
    local out = {}
    for _, c in ipairs(comments) do
        out[#out + 1] = {
            id = c.id,
            content = truncate(c.content or "", 200),
            author = c.author and c.author.name,
            upvotes = c.upvotes,
            created_at = c.created_at,
            parent_id = c.parent_id,
            replies_count = c.replies and #c.replies or 0
        }
    end

    return {
        success = true,
        output = json.encode({ comments = out, count = #out }),
        comments = out,
        count = #out,
        has_more = data.has_more,
        next_cursor = data.next_cursor
    }
end

local function do_subscribe(args)
    local platform_id = args.platform_id or ""
    local submolt = args.submolt or args.submolt_name or ""

    if submolt == "" then
        return { success = false, error = "subscribe requires submolt (submolt name)" }
    end

    local result = api_post(platform_id, "/submolts/" .. url_encode(submolt) .. "/subscribe", {})
    if not result.success then return result end

    return { success = true, output = "Subscribed to " .. submolt }
end

local function do_unsubscribe(args)
    local platform_id = args.platform_id or ""
    local submolt = args.submolt or args.submolt_name or ""

    if submolt == "" then
        return { success = false, error = "unsubscribe requires submolt (submolt name)" }
    end

    local result = api_delete(platform_id, "/submolts/" .. url_encode(submolt) .. "/subscribe")
    if not result.success then return result end

    return { success = true, output = "Unsubscribed from " .. submolt }
end

local function do_home(args)
    local platform_id = args.platform_id or ""

    local result = api_get(platform_id, "/home", {})
    if not result.success then return result end

    local data = result.data
    -- Return the full home data — it's designed as a dashboard
    return {
        success = true,
        output = json.encode(data),
        home = data
    }
end

-- ---------------------------------------------------------------------------
-- Registration
-- ---------------------------------------------------------------------------

animus.register_channel({
    id = "moltbook",
    name = "Moltbook",
    capabilities = {"read", "write", "search", "react"},
    actions = {
        "post", "comment", "reply", "browse", "search",
        "get_notifications", "upvote", "downvote", "get_profile",
        "list_submolts", "follow", "unfollow", "delete",
        "get_post", "get_comments", "subscribe", "unsubscribe",
        "home"
    },
    schema = {
        post = {
            { name = "submolt", type = "string", required = true, description = "Submolt to post in (e.g. 'general')" },
            { name = "title", type = "string", required = true, description = "Post title (max 300 chars)" },
            { name = "content", type = "string", required = false, description = "Post body (max 40000 chars)" },
            { name = "url", type = "string", required = false, description = "URL for link posts" },
            { name = "type", type = "string", required = false, description = "Post type: text, link, or image (default: text)" }
        },
        comment = {
            { name = "post_id", type = "string", required = true, description = "Post ID to comment on" },
            { name = "content", type = "string", required = true, description = "Comment text" },
            { name = "parent_id", type = "string", required = false, description = "Comment ID to reply to (for threaded replies)" }
        },
        reply = {
            { name = "post_id", type = "string", required = true, description = "Post ID to comment on" },
            { name = "content", type = "string", required = true, description = "Reply text" },
            { name = "parent_id", type = "string", required = false, description = "Comment ID to reply to" }
        },
        browse = {
            { name = "sort", type = "string", required = false, description = "Sort: hot, new, top, rising (default: hot)" },
            { name = "limit", type = "integer", required = false, description = "Results per page (default 25, max 100)" },
            { name = "submolt", type = "string", required = false, description = "Filter to submolt name" },
            { name = "cursor", type = "string", required = false, description = "Pagination cursor from previous response" }
        },
        search = {
            { name = "query", type = "string", required = true, description = "Search query (natural language works)" },
            { name = "type", type = "string", required = false, description = "Search type: posts, comments, or all (default: all)" },
            { name = "limit", type = "integer", required = false, description = "Max results (default 20, max 50)" },
            { name = "cursor", type = "string", required = false, description = "Pagination cursor" }
        },
        get_notifications = {
            { name = "limit", type = "integer", required = false, description = "Max notifications (default 50, max 100)" },
            { name = "cursor", type = "string", required = false, description = "Pagination cursor" }
        },
        upvote = {
            { name = "post_id", type = "string", required = false, description = "Post ID to upvote" },
            { name = "comment_id", type = "string", required = false, description = "Comment ID to upvote (use instead of post_id)" }
        },
        downvote = {
            { name = "post_id", type = "string", required = true, description = "Post ID to downvote" }
        },
        get_profile = {
            { name = "name", type = "string", required = false, description = "Agent name to look up (omit for own profile)" }
        },
        list_submolts = {},
        follow = {
            { name = "name", type = "string", required = true, description = "Agent name to follow" }
        },
        unfollow = {
            { name = "name", type = "string", required = true, description = "Agent name to unfollow" }
        },
        delete = {
            { name = "post_id", type = "string", required = false, description = "Post ID to delete" },
            { name = "comment_id", type = "string", required = false, description = "Comment ID to delete (use instead of post_id)" }
        },
        get_post = {
            { name = "post_id", type = "string", required = true, description = "Post ID to retrieve" }
        },
        get_comments = {
            { name = "post_id", type = "string", required = true, description = "Post ID to get comments for" },
            { name = "sort", type = "string", required = false, description = "Sort: best, new, old (default: best)" },
            { name = "limit", type = "integer", required = false, description = "Comments per page (default 35, max 100)" },
            { name = "cursor", type = "string", required = false, description = "Pagination cursor" }
        },
        subscribe = {
            { name = "submolt", type = "string", required = true, description = "Submolt name to subscribe to" }
        },
        unsubscribe = {
            { name = "submolt", type = "string", required = true, description = "Submolt name to unsubscribe from" }
        },
        home = {}
    },
    handler = function(args)
        local action = args.action
        if not action or action == "" then
            return { success = false, error = "action is required" }
        end

        if action == "post" then return do_post(args)
        elseif action == "comment" then return do_comment(args)
        elseif action == "reply" then return do_reply(args)
        elseif action == "browse" then return do_browse(args)
        elseif action == "search" then return do_search(args)
        elseif action == "get_notifications" then return do_get_notifications(args)
        elseif action == "upvote" then return do_upvote(args)
        elseif action == "downvote" then return do_downvote(args)
        elseif action == "get_profile" then return do_get_profile(args)
        elseif action == "list_submolts" then return do_list_submolts(args)
        elseif action == "follow" then return do_follow(args)
        elseif action == "unfollow" then return do_unfollow(args)
        elseif action == "delete" then return do_delete(args)
        elseif action == "get_post" then return do_get_post(args)
        elseif action == "get_comments" then return do_get_comments(args)
        elseif action == "subscribe" then return do_subscribe(args)
        elseif action == "unsubscribe" then return do_unsubscribe(args)
        elseif action == "home" then return do_home(args)
        else return { success = false, error = "unknown action: " .. tostring(action) }
        end
    end
})
-- ============================================================================
-- Twitter/X Social Adapter for Animus
-- Ticket 059 — third Lua social adapter
--
-- Registers via animus.register_channel() as a stateless adapter.
-- Credentials are resolved per-instance from agent config:
--   config.get("channels.twitter:personal.client_id")
--   config.get("channels.twitter:personal.client_secret")
--   config.get("channels.twitter:personal.access_token")    -- cached after OAuth
--   config.get("channels.twitter:personal.refresh_token")   -- cached after OAuth
--   config.get("channels.twitter:personal.token_expires")   -- Unix timestamp
--   config.get("channels.twitter:personal.user_id")         -- resolved on first auth
--   config.get("channels.twitter:personal.username")        -- resolved on first auth
--   config.get("channels.twitter:personal.tier")            -- "free"|"basic"|"pro"
--
-- OAuth 2.0 PKCE exchange is handled by the C++ admin endpoint.
-- This adapter only uses cached access_token / refresh_token.
-- ============================================================================

local API_BASE = "https://api.twitter.com"
local API_V2 = API_BASE .. "/2"

-- Monthly caps per tier (Twitter developer account limits)
local TIER_READS  = { free = 1500, basic = 10000, pro = 1000000 }
local TIER_WRITES = { free = 50,   basic = 3000,  pro = 150000 }

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

--- Build a config key for a platform instance.
-- @param platform_id string  e.g. "twitter:personal"
-- @param field string        e.g. "access_token"
-- @return string             e.g. "channels.twitter:personal.access_token"
local function cfg_key(platform_id, field)
    return "channels." .. platform_id .. "." .. field
end

--- Get the tier for an instance (defaults to "free").
local function get_tier(platform_id)
    local tier = config.get(cfg_key(platform_id, "tier"))
    if tier and tier ~= "" and TIER_READS[tier] then
        return tier
    end
    return "free"
end

--- Clamp a number to [lo, hi].
local function clamp(val, lo, hi)
    return math.max(lo, math.min(hi, val))
end

-- ---------------------------------------------------------------------------
-- Rate limit tracking
-- ---------------------------------------------------------------------------

--- Check and reset monthly counters if the month has rolled over.
-- Returns reads_used, writes_used (after potential reset).
local function monthly_reset_check(platform_id)
    local now = os.time()
    local reset_ts = tonumber(config.get(cfg_key(platform_id, "counter_reset")) or "0")

    -- Simple month rollover: if current month != stored month
    -- os.date with ! prefix gives UTC
    local current_month = os.date("!*t", now)
    local stored_month   = os.date("!*t", reset_ts > 0 and reset_ts or 0)

    if current_month.year ~= stored_month.year or current_month.month ~= stored_month.month then
        config.set(cfg_key(platform_id, "reads_used"), "0")
        config.set(cfg_key(platform_id, "writes_used"), "0")
        config.set(cfg_key(platform_id, "counter_reset"), tostring(now))
        return 0, 0
    end

    local reads_used  = tonumber(config.get(cfg_key(platform_id, "reads_used"))  or "0")
    local writes_used = tonumber(config.get(cfg_key(platform_id, "writes_used")) or "0")
    return reads_used, writes_used
end

--- Check whether the monthly quota allows the requested operation.
-- @param platform_id string
-- @param is_write boolean  true = write operation, false = read
-- @return ok, error, reads_remaining, writes_remaining
local function check_quota(platform_id, is_write)
    local tier = get_tier(platform_id)
    local reads_used, writes_used = monthly_reset_check(platform_id)

    local read_cap  = TIER_READS[tier]
    local write_cap = TIER_WRITES[tier]

    local reads_remaining  = read_cap  - reads_used
    local writes_remaining = write_cap - writes_used

    if is_write then
        if writes_remaining <= 0 then
            return false, "monthly write cap reached (" .. write_cap .. " for " .. tier .. " tier)", reads_remaining, 0
        end
        config.set(cfg_key(platform_id, "writes_used"), tostring(writes_used + 1))
        writes_remaining = writes_remaining - 1
    else
        if reads_remaining <= 0 then
            return false, "monthly read cap reached (" .. read_cap .. " for " .. tier .. " tier)", 0, writes_remaining
        end
        config.set(cfg_key(platform_id, "reads_used"), tostring(reads_used + 1))
        reads_remaining = reads_remaining - 1
    end

    return true, nil, reads_remaining, writes_remaining
end

--- Parse Twitter per-request rate limit headers from response.
-- Twitter returns x-rate-limit-limit, x-rate-limit-remaining, x-rate-limit-reset
-- on most v2 endpoints. These are 15-min window limits, separate from monthly caps.
-- We log warnings when approaching 15-min limits but don't block (monthly cap is the gate).
local function check_per_request_limits(resp)
    -- animus.http_* returns headers in resp.headers (table or nil)
    -- If headers aren't exposed yet, this is a no-op
    if not resp.headers then return end

    local remaining = tonumber(resp.headers["x-rate-limit-remaining"])
    if remaining and remaining <= 2 then
        log.warn("Twitter 15-min rate limit nearly exhausted: " .. tostring(remaining) .. " remaining")
    end
end

-- ---------------------------------------------------------------------------
-- Token management
-- ---------------------------------------------------------------------------

--- Forward declaration for refresh_token_fn (defined below).
local refresh_token_fn

--- Get a valid access token for the instance.
-- Returns the access_token string on success, nil + error on failure.
-- Checks cache first; if expired, attempts refresh.
local function get_token(platform_id)
    local token  = config.get(cfg_key(platform_id, "access_token"))
    local expires = tonumber(config.get(cfg_key(platform_id, "token_expires")) or "0")
    local now = os.time()

    -- Token present and not expiring within 5 minutes
    if token and token ~= "" and expires > now + 300 then
        return token
    end

    -- Token expired or missing — try refresh
    return refresh_token_fn(platform_id)
end

--- Force token refresh using the stored refresh_token.
-- On success: caches new access_token, refresh_token, token_expires.
-- On failure: returns nil + error indicating re-auth needed.
refresh_token_fn = function(platform_id)
    local refresh_token = config.get(cfg_key(platform_id, "refresh_token"))
    local client_id     = config.get(cfg_key(platform_id, "client_id"))

    if not refresh_token or refresh_token == "" then
        return nil, "no refresh token — OAuth authorization required"
    end
    if not client_id or client_id == "" then
        return nil, "no client_id configured for " .. platform_id
    end

    -- Twitter OAuth 2.0 token refresh
    -- POST https://api.twitter.com/2/oauth2/token
    -- Content-Type: application/x-www-form-urlencoded
    -- Body: refresh_token={rt}&grant_type=refresh_token&client_id={cid}
    local body = "refresh_token=" .. url_encode(refresh_token)
              .. "&grant_type=refresh_token"
              .. "&client_id=" .. url_encode(client_id)

    local resp = animus.http_post(API_BASE .. "/2/oauth2/token", {
        headers = { ["Content-Type"] = "application/x-www-form-urlencoded" },
        body = body
    })

    if resp.status ~= 200 then
        -- Refresh token likely expired or revoked
        -- Clear stale tokens so the system knows re-auth is needed
        config.set(cfg_key(platform_id, "access_token"), "")
        config.set(cfg_key(platform_id, "refresh_token"), "")
        config.set(cfg_key(platform_id, "token_expires"), "0")
        return nil, "token refresh failed (" .. tostring(resp.status) .. ") — OAuth re-authorization required"
    end

    local ok, data = pcall(json.decode, resp.body)
    if not ok or not data or not data.access_token then
        return nil, "token refresh response parse error"
    end

    -- Cache the new tokens
    config.set(cfg_key(platform_id, "access_token"), data.access_token or "")
    config.set(cfg_key(platform_id, "refresh_token"), data.refresh_token or "")
    local expires_in = tonumber(data.expires_in) or 7200
    config.set(cfg_key(platform_id, "token_expires"), tostring(os.time() + expires_in))

    return data.access_token
end

--- Ensure a valid token and user_id are available.
-- Returns user_id on success, nil + error on failure.
local function ensure_auth(platform_id)
    local token = get_token(platform_id)
    if not token then
        return nil, "authentication failed — check OAuth tokens"
    end

    local user_id = config.get(cfg_key(platform_id, "user_id"))
    if user_id and user_id ~= "" then
        return user_id
    end

    -- Resolve user_id from /2/users/me
    local resp = animus.http_get(API_V2 .. "/users/me", {
        headers = { ["Authorization"] = "Bearer " .. token }
    })

    if resp.status ~= 200 then
        return nil, "failed to resolve user_id (" .. tostring(resp.status) .. ")"
    end

    local ok, data = pcall(json.decode, resp.body)
    if not ok or not data or not data.data then
        return nil, "failed to parse user response"
    end

    config.set(cfg_key(platform_id, "user_id"), data.data.id or "")
    config.set(cfg_key(platform_id, "username"), data.data.username or "")

    return data.data.id
end

-- ---------------------------------------------------------------------------
-- HTTP wrappers
-- ---------------------------------------------------------------------------

--- Authorized HTTP GET against Twitter API v2.
-- Auto-refreshes token on 401. Returns raw response table.
local function auth_get(platform_id, endpoint, params)
    local token = get_token(platform_id)
    if not token then
        return { status = 0, body = "authentication failed: no valid token" }
    end

    local url = API_V2 .. endpoint

    -- Build query string
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

    -- Re-auth on 401
    if resp.status == 401 then
        token = refresh_token_fn(platform_id)
        if not token then
            return { status = 401, body = "re-authentication failed — OAuth re-authorization required" }
        end
        resp = animus.http_get(url, {
            headers = { ["Authorization"] = "Bearer " .. token }
        })
    end

    check_per_request_limits(resp)
    return resp
end

--- Authorized HTTP POST against Twitter API v2.
-- Auto-refreshes token on 401. Returns raw response table.
local function auth_post(platform_id, endpoint, body_table)
    local token = get_token(platform_id)
    if not token then
        return { status = 0, body = "authentication failed: no valid token" }
    end

    local url = API_V2 .. endpoint
    local resp = animus.http_post(url, {
        headers = {
            ["Authorization"] = "Bearer " .. token,
            ["Content-Type"] = "application/json"
        },
        body = json.encode(body_table)
    })

    -- Re-auth on 401
    if resp.status == 401 then
        token = refresh_token_fn(platform_id)
        if not token then
            return { status = 401, body = "re-authentication failed — OAuth re-authorization required" }
        end
        resp = animus.http_post(url, {
            headers = {
                ["Authorization"] = "Bearer " .. token,
                ["Content-Type"] = "application/json"
            },
            body = json.encode(body_table)
        })
    end

    check_per_request_limits(resp)
    return resp
end

--- Authorized HTTP DELETE against Twitter API v2.
-- Uses animus.http_delete (added for Ticket 059).
local function auth_delete(platform_id, endpoint)
    local token = get_token(platform_id)
    if not token then
        return { status = 0, body = "authentication failed: no valid token" }
    end

    local url = API_V2 .. endpoint
    local resp = animus.http_delete(url, {
        headers = { ["Authorization"] = "Bearer " .. token }
    })

    -- Re-auth on 401
    if resp.status == 401 then
        token = refresh_token_fn(platform_id)
        if not token then
            return { status = 401, body = "re-authentication failed" }
        end
        resp = animus.http_delete(url, {
            headers = { ["Authorization"] = "Bearer " .. token }
        })
    end

    check_per_request_limits(resp)
    return resp
end

-- ---------------------------------------------------------------------------
-- Quota result builder
-- ---------------------------------------------------------------------------

--- Build a quota info table to include in responses.
local function build_quota(platform_id, reads_remaining, writes_remaining)
    local tier = get_tier(platform_id)
    return {
        reads_remaining  = reads_remaining,
        writes_remaining = writes_remaining,
        reads_cap        = TIER_READS[tier],
        writes_cap       = TIER_WRITES[tier],
        tier             = tier
    }
end

-- ---------------------------------------------------------------------------
-- Action handlers
-- ---------------------------------------------------------------------------

local function do_post(args)
    local pid = args.platform_id
    if not args.content or args.content == "" then
        return { success = false, error = "content is required for post" }
    end

    local ok, err, reads_rem, writes_rem = check_quota(pid, true)
    if not ok then
        return { success = false, error = err, quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local user_id, auth_err = ensure_auth(pid)
    if not user_id then
        return { success = false, error = auth_err }
    end

    local body = { text = args.content }

    local resp = auth_post(pid, "/tweets", body)

    if resp.status ~= 201 then
        return { success = false, error = "post failed (" .. tostring(resp.status) .. "): " .. tostring(resp.body), quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local ok2, data = pcall(json.decode, resp.body)
    if not ok2 or not data or not data.data then
        return { success = true, output = resp.body, quota = build_quota(pid, reads_rem, writes_rem) }
    end

    return {
        success = true,
        output = json.encode({
            id   = data.data.id,
            text = data.data.text
        }),
        quota = build_quota(pid, reads_rem, writes_rem)
    }
end

local function do_reply(args)
    local pid = args.platform_id
    if not args.tweet_id or args.tweet_id == "" then
        return { success = false, error = "tweet_id is required for reply" }
    end
    if not args.content or args.content == "" then
        return { success = false, error = "content is required for reply" }
    end

    local ok, err, reads_rem, writes_rem = check_quota(pid, true)
    if not ok then
        return { success = false, error = err, quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local user_id, auth_err = ensure_auth(pid)
    if not user_id then
        return { success = false, error = auth_err }
    end

    local body = {
        text = args.content,
        reply = { in_reply_to_tweet_id = args.tweet_id }
    }

    local resp = auth_post(pid, "/tweets", body)

    if resp.status ~= 201 then
        return { success = false, error = "reply failed (" .. tostring(resp.status) .. "): " .. tostring(resp.body), quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local ok2, data = pcall(json.decode, resp.body)
    if not ok2 or not data or not data.data then
        return { success = true, output = resp.body, quota = build_quota(pid, reads_rem, writes_rem) }
    end

    return {
        success = true,
        output = json.encode({
            id              = data.data.id,
            text            = data.data.text,
            in_reply_to    = args.tweet_id
        }),
        quota = build_quota(pid, reads_rem, writes_rem)
    }
end

local function do_read_timeline(args)
    local pid = args.platform_id

    local ok, err, reads_rem, writes_rem = check_quota(pid, false)
    if not ok then
        return { success = false, error = err, quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local user_id, auth_err = ensure_auth(pid)
    if not user_id then
        return { success = false, error = auth_err }
    end

    local max_results = clamp(tonumber(args.max_results) or 20, 5, 100)
    local params = { max_results = tostring(max_results) }
    if args.pagination_token and args.pagination_token ~= "" then
        params.pagination_token = args.pagination_token
    end

    -- Tweet fields for richer output
    params["tweet.fields"] = "created_at,author_id,conversation_id,public_metrics"
    params["user.fields"]  = "username,name"

    local resp = auth_get(pid, "/users/" .. url_encode(user_id) .. "/timelines/reverse_chronological", params)

    if resp.status ~= 200 then
        return { success = false, error = "read timeline failed (" .. tostring(resp.status) .. "): " .. tostring(resp.body), quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local ok2, data = pcall(json.decode, resp.body)
    if not ok2 or not data then
        return { success = true, output = resp.body, quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local tweets = {}
    if data.data then
        for _, t in ipairs(data.data) do
            local entry = {
                id         = t.id,
                text       = t.text or "",
                created_at = t.created_at or ""
            }
            if t.author_id then entry.author_id = t.author_id end
            if t.public_metrics then
                entry.metrics = {
                    likes     = t.public_metrics.like_count or 0,
                    retweets  = t.public_metrics.retweet_count or 0,
                    replies   = t.public_metrics.reply_count or 0
                }
            end
            tweets[#tweets + 1] = entry
        end
    end

    return {
        success = true,
        output = json.encode({
            tweets   = tweets,
            next_token = (data.meta and data.meta.next_token) or ""
        }),
        quota = build_quota(pid, reads_rem, writes_rem)
    }
end

local function do_read_user_tweets(args)
    local pid = args.platform_id

    local ok, err, reads_rem, writes_rem = check_quota(pid, false)
    if not ok then
        return { success = false, error = err, quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local user_id, auth_err = ensure_auth(pid)
    if not user_id then
        return { success = false, error = auth_err }
    end

    -- Allow querying another user's tweets
    local target_id = args.user_id
    if not target_id or target_id == "" then
        target_id = user_id
    end

    local max_results = clamp(tonumber(args.max_results) or 20, 5, 100)
    local params = { max_results = tostring(max_results) }
    if args.pagination_token and args.pagination_token ~= "" then
        params.pagination_token = args.pagination_token
    end
    params["tweet.fields"] = "created_at,public_metrics"
    params["user.fields"]  = "username,name"

    local resp = auth_get(pid, "/users/" .. url_encode(target_id) .. "/tweets", params)

    if resp.status ~= 200 then
        return { success = false, error = "read user tweets failed (" .. tostring(resp.status) .. "): " .. tostring(resp.body), quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local ok2, data = pcall(json.decode, resp.body)
    if not ok2 or not data then
        return { success = true, output = resp.body, quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local tweets = {}
    if data.data then
        for _, t in ipairs(data.data) do
            local entry = {
                id         = t.id,
                text       = t.text or "",
                created_at = t.created_at or ""
            }
            if t.public_metrics then
                entry.metrics = {
                    likes    = t.public_metrics.like_count or 0,
                    retweets = t.public_metrics.retweet_count or 0,
                    replies  = t.public_metrics.reply_count or 0
                }
            end
            tweets[#tweets + 1] = entry
        end
    end

    return {
        success = true,
        output = json.encode({
            tweets     = tweets,
            next_token = (data.meta and data.meta.next_token) or ""
        }),
        quota = build_quota(pid, reads_rem, writes_rem)
    }
end

local function do_search(args)
    local pid = args.platform_id
    if not args.query or args.query == "" then
        return { success = false, error = "query is required for search" }
    end

    local ok, err, reads_rem, writes_rem = check_quota(pid, false)
    if not ok then
        return { success = false, error = err, quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local max_results = clamp(tonumber(args.max_results) or 20, 10, 100)
    local params = {
        query       = args.query,
        max_results = tostring(max_results)
    }
    if args.sort_order and args.sort_order ~= "" then
        params.sort_order = args.sort_order
    end
    if args.pagination_token and args.pagination_token ~= "" then
        params.next_token = args.pagination_token
    end
    params["tweet.fields"] = "created_at,author_id,public_metrics"
    params["user.fields"]  = "username,name"

    local resp = auth_get(pid, "/tweets/search/recent", params)

    if resp.status ~= 200 then
        return { success = false, error = "search failed (" .. tostring(resp.status) .. "): " .. tostring(resp.body), quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local ok2, data = pcall(json.decode, resp.body)
    if not ok2 or not data then
        return { success = true, output = resp.body, quota = build_quota(pid, reads_rem, writes_rem) }
    end

    -- Build a user lookup map for includes
    local user_map = {}
    if data.includes and data.includes.users then
        for _, u in ipairs(data.includes.users) do
            user_map[u.id] = { username = u.username or "", name = u.name or "" }
        end
    end

    local tweets = {}
    if data.data then
        for _, t in ipairs(data.data) do
            local entry = {
                id         = t.id,
                text       = t.text or "",
                created_at = t.created_at or ""
            }
            if t.author_id then
                entry.author_id = t.author_id
                if user_map[t.author_id] then
                    entry.author_username = user_map[t.author_id].username
                    entry.author_name     = user_map[t.author_id].name
                end
            end
            if t.public_metrics then
                entry.metrics = {
                    likes    = t.public_metrics.like_count or 0,
                    retweets = t.public_metrics.retweet_count or 0,
                    replies  = t.public_metrics.reply_count or 0
                }
            end
            tweets[#tweets + 1] = entry
        end
    end

    return {
        success = true,
        output = json.encode({
            total      = (data.meta and data.meta.result_count) or #tweets,
            tweets     = tweets,
            next_token = (data.meta and data.meta.next_token) or ""
        }),
        quota = build_quota(pid, reads_rem, writes_rem)
    }
end

local function do_get_mentions(args)
    local pid = args.platform_id

    local ok, err, reads_rem, writes_rem = check_quota(pid, false)
    if not ok then
        return { success = false, error = err, quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local user_id, auth_err = ensure_auth(pid)
    if not user_id then
        return { success = false, error = auth_err }
    end

    local max_results = clamp(tonumber(args.max_results) or 20, 5, 100)
    local params = { max_results = tostring(max_results) }
    if args.pagination_token and args.pagination_token ~= "" then
        params.pagination_token = args.pagination_token
    end
    params["tweet.fields"] = "created_at,author_id,in_reply_to_user_id,public_metrics"
    params["user.fields"]  = "username,name"

    local resp = auth_get(pid, "/users/" .. url_encode(user_id) .. "/mentions", params)

    if resp.status ~= 200 then
        return { success = false, error = "get mentions failed (" .. tostring(resp.status) .. "): " .. tostring(resp.body), quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local ok2, data = pcall(json.decode, resp.body)
    if not ok2 or not data then
        return { success = true, output = resp.body, quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local user_map = {}
    if data.includes and data.includes.users then
        for _, u in ipairs(data.includes.users) do
            user_map[u.id] = { username = u.username or "", name = u.name or "" }
        end
    end

    local mentions = {}
    if data.data then
        for _, t in ipairs(data.data) do
            local entry = {
                id         = t.id,
                text       = t.text or "",
                created_at = t.created_at or ""
            }
            if t.author_id then
                entry.author_id = t.author_id
                if user_map[t.author_id] then
                    entry.author_username = user_map[t.author_id].username
                    entry.author_name     = user_map[t.author_id].name
                end
            end
            if t.public_metrics then
                entry.metrics = {
                    likes    = t.public_metrics.like_count or 0,
                    retweets = t.public_metrics.retweet_count or 0,
                    replies  = t.public_metrics.reply_count or 0
                }
            end
            mentions[#mentions + 1] = entry
        end
    end

    return {
        success = true,
        output = json.encode({
            mentions   = mentions,
            next_token = (data.meta and data.meta.next_token) or ""
        }),
        quota = build_quota(pid, reads_rem, writes_rem)
    }
end

local function do_get_tweet(args)
    local pid = args.platform_id
    if not args.tweet_id or args.tweet_id == "" then
        return { success = false, error = "tweet_id is required" }
    end

    local ok, err, reads_rem, writes_rem = check_quota(pid, false)
    if not ok then
        return { success = false, error = err, quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local params = {
        ["tweet.fields"] = "created_at,author_id,conversation_id,public_metrics,referenced_tweets",
        ["user.fields"]  = "username,name"
    }

    local resp = auth_get(pid, "/tweets/" .. url_encode(args.tweet_id), params)

    if resp.status ~= 200 then
        return { success = false, error = "get tweet failed (" .. tostring(resp.status) .. "): " .. tostring(resp.body), quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local ok2, data = pcall(json.decode, resp.body)
    if not ok2 or not data or not data.data then
        return { success = true, output = resp.body, quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local t = data.data
    local entry = {
        id         = t.id,
        text       = t.text or "",
        created_at = t.created_at or ""
    }
    if t.author_id then entry.author_id = t.author_id end
    if t.public_metrics then
        entry.metrics = {
            likes    = t.public_metrics.like_count or 0,
            retweets = t.public_metrics.retweet_count or 0,
            replies  = t.public_metrics.reply_count or 0
        }
    end
    if t.referenced_tweets then
        entry.referenced_tweets = t.referenced_tweets
    end

    -- Include author info from includes
    if data.includes and data.includes.users then
        for _, u in ipairs(data.includes.users) do
            if u.id == t.author_id then
                entry.author_username = u.username
                entry.author_name     = u.name
            end
        end
    end

    return {
        success = true,
        output = json.encode(entry),
        quota = build_quota(pid, reads_rem, writes_rem)
    }
end

local function do_delete(args)
    local pid = args.platform_id
    if not args.tweet_id or args.tweet_id == "" then
        return { success = false, error = "tweet_id is required for delete" }
    end

    -- Delete is a write operation (counts against monthly cap)
    local ok, err, reads_rem, writes_rem = check_quota(pid, true)
    if not ok then
        return { success = false, error = err, quota = build_quota(pid, reads_rem, writes_rem) }
    end

    local user_id, auth_err = ensure_auth(pid)
    if not user_id then
        return { success = false, error = auth_err }
    end

    local resp = auth_delete(pid, "/tweets/" .. url_encode(args.tweet_id))

    -- Twitter DELETE returns 200 on success (some docs say 204)
    if resp.status ~= 200 and resp.status ~= 204 then
        return { success = false, error = "delete failed (" .. tostring(resp.status) .. "): " .. tostring(resp.body), quota = build_quota(pid, reads_rem, writes_rem) }
    end

    return {
        success = true,
        output = json.encode({ deleted = args.tweet_id }),
        quota = build_quota(pid, reads_rem, writes_rem)
    }
end

local function do_quota(args)
    local pid = args.platform_id
    local tier = get_tier(pid)
    local reads_used, writes_used = monthly_reset_check(pid)

    return {
        success = true,
        tier           = tier,
        reads_used     = reads_used,
        reads_cap      = TIER_READS[tier],
        reads_remaining = TIER_READS[tier] - reads_used,
        writes_used     = writes_used,
        writes_cap      = TIER_WRITES[tier],
        writes_remaining = TIER_WRITES[tier] - writes_used
    }
end

-- ---------------------------------------------------------------------------
-- Registration
-- ---------------------------------------------------------------------------

animus.register_channel({
    id = "twitter",
    name = "Twitter",
    capabilities = {"read", "write", "search"},
    actions = {"post", "reply", "read_timeline", "read_user_tweets", "search", "get_mentions", "get_tweet", "delete", "quota"},
    schema = {
        post = {
            { name = "content", type = "string", required = true, description = "Tweet text (max 280 characters)" }
        },
        reply = {
            { name = "tweet_id", type = "string", required = true, description = "ID of the tweet to reply to" },
            { name = "content", type = "string", required = true, description = "Reply text" }
        },
        read_timeline = {
            { name = "max_results", type = "integer", required = false, description = "Max tweets (5-100, default 20)" },
            { name = "pagination_token", type = "string", required = false, description = "Pagination token from previous response" }
        },
        read_user_tweets = {
            { name = "user_id", type = "string", required = false, description = "Twitter user ID (default: authed user)" },
            { name = "max_results", type = "integer", required = false, description = "Max tweets (5-100, default 20)" },
            { name = "pagination_token", type = "string", required = false, description = "Pagination token" }
        },
        search = {
            { name = "query", type = "string", required = true, description = "Search query (standard operators)" },
            { name = "max_results", type = "integer", required = false, description = "Max results (10-100, default 20)" },
            { name = "sort_order", type = "string", required = false, description = "'recency' or 'relevancy' (default: recency)" },
            { name = "pagination_token", type = "string", required = false, description = "Pagination token" }
        },
        get_mentions = {
            { name = "max_results", type = "integer", required = false, description = "Max mentions (5-100, default 20)" },
            { name = "pagination_token", type = "string", required = false, description = "Pagination token" }
        },
        get_tweet = {
            { name = "tweet_id", type = "string", required = true, description = "Tweet ID to fetch" }
        },
        delete = {
            { name = "tweet_id", type = "string", required = true, description = "Tweet ID to delete" }
        },
        quota = {}
    },
    handler = function(args)
        local action = args.action
        if not action or action == "" then
            return { success = false, error = "action is required" }
        end

        if action == "post" then return do_post(args)
        elseif action == "reply" then return do_reply(args)
        elseif action == "read_timeline" then return do_read_timeline(args)
        elseif action == "read_user_tweets" then return do_read_user_tweets(args)
        elseif action == "search" then return do_search(args)
        elseif action == "get_mentions" then return do_get_mentions(args)
        elseif action == "get_tweet" then return do_get_tweet(args)
        elseif action == "delete" then return do_delete(args)
        elseif action == "quota" then return do_quota(args)
        else return { success = false, error = "unknown action: " .. tostring(action) }
        end
    end
})
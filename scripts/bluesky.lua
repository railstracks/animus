-- ============================================================================
-- Bluesky Social Adapter for Animus
-- Ticket 050 — first Lua social adapter
--
-- Registers via animus.register_channel() as a stateless adapter.
-- Credentials are resolved per-instance from agent config:
--   config.get("channels.bluesky:personal.handle")
--   config.get("channels.bluesky:personal.app_password")
--   config.get("channels.bluesky:personal.pds")       -- optional, defaults to bsky.social
--
-- Internal cached state (auto-managed):
--   config.get("channels.bluesky:personal.access_jwt")
--   config.get("channels.bluesky:personal.did")
-- ============================================================================

local DEFAULT_PDS = "https://bsky.social"

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


--- Base64url decode a string (no padding, URL-safe alphabet).
local function b64url_decode(str)
    if not str or str == "" then return nil end
    -- Add padding
    local pad = (4 - #str % 4) % 4
    str = str .. string.rep('=', pad)
    -- Replace URL-safe chars
    str = str:gsub('-', '+'):gsub('_', '/')
    -- Lookup table
    local b = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
    local function val(ch)
        local s, e = b:find(ch, 1, true)
        return s and (s - 1) or 0
    end
    -- Decode 4 chars at a time into 3 bytes
    local r = {}
    for i = 1, #str, 4 do
        local bytes = { str:byte(i, i+3) }
        local n = val(string.char(bytes[1] or 0)) * 262144
                 + val(string.char(bytes[2] or 0)) * 4096
                 + val(string.char(bytes[3] or 0)) * 64
                 + val(string.char(bytes[4] or 0))
        r[#r+1] = string.char(math.floor(n / 65536) % 256)
        r[#r+1] = string.char(math.floor(n / 256) % 256)
        r[#r+1] = string.char(n % 256)
    end
    return table.concat(r)
end

--- Check if a JWT is expired by decoding the exp claim from the payload.
-- Returns true if expired or unparseable, false if still valid.
local function is_jwt_expired(jwt)
    if not jwt or jwt == "" then return true end
    -- JWT format: header.payload.signature
    local _, _, payload_b64 = jwt:find('^[^.]+%.([^.]+)%.[^.]+$')
    if not payload_b64 then return true end
    local ok, payload = pcall(b64url_decode, payload_b64)
    if not ok or not payload then return true end
    -- Find exp claim in the decoded JSON payload
    local exp_match = payload:match('"exp"%s*:%s*(%d+)')
    if not exp_match then return false end -- no exp claim, assume valid
    local exp = tonumber(exp_match)
    if not exp then return false end
    -- Expired if exp is in the past (with 60s buffer for clock skew)
    return os.time() >= (exp - 60)
end

--- Build a config key for a platform instance.
-- @param platform_id string  e.g. "bluesky:personal"
-- @param field string        e.g. "handle"
-- @return string             e.g. "channels.bluesky:personal.handle"
local function cfg_key(platform_id, field)
    return "channels." .. platform_id .. "." .. field
end

--- ISO 8601 UTC timestamp with fractional seconds.
local function iso_now()
    return os.date("!%Y-%m-%dT%H:%M:%S.000Z")
end

--- Get the PDS base URL for an instance (defaults to bsky.social).
local function get_pds(platform_id)
    local pds = config.get(cfg_key(platform_id, "pds"))
    if pds and pds ~= "" then return pds end
    return DEFAULT_PDS
end

--- Forward declaration (refresh_auth defined below).
local refresh_auth

--- Try to refresh an expired session using refreshJwt.
-- Returns new accessJwt on success, nil on failure.
-- Falls back to full re-auth if no refreshJwt or refresh fails.
local function try_refresh_session(platform_id)
    local refresh_key = cfg_key(platform_id, "refresh_jwt")
    local refresh_jwt = config.get(refresh_key)
    if not refresh_jwt or refresh_jwt == "" then
        return nil
    end

    local pds = get_pds(platform_id)
    local resp = animus.http_post(pds .. "/xrpc/com.atproto.server.refreshSession", {
        headers = {
            ["Content-Type"] = "application/json",
            ["Authorization"] = "Bearer " .. refresh_jwt
        },
        body = ""
    })

    if resp.status ~= 200 then
        -- Refresh failed — clear stale refresh token
        config.set(refresh_key, "")
        return nil
    end

    local ok, data = pcall(json.decode, resp.body)
    if not ok or not data or not data.accessJwt then
        config.set(refresh_key, "")
        return nil
    end

    -- Update cached tokens
    config.set(cfg_key(platform_id, "access_jwt"), data.accessJwt or "")
    config.set(cfg_key(platform_id, "refresh_jwt"), data.refreshJwt or "")
    if data.did then
        config.set(cfg_key(platform_id, "did"), data.did)
    end

    return data.accessJwt
end

--- Authenticate with Bluesky and cache the JWT + DID.
-- Returns the accessJwt string on success, nil + error on failure.
-- Tries: cached token → refresh session → full re-auth.
local function authenticate(platform_id)
    local jwt_key = cfg_key(platform_id, "access_jwt")
    local jwt = config.get(jwt_key)
    if jwt and jwt ~= "" and not is_jwt_expired(jwt) then
        return jwt
    end
    -- Cached token is missing or expired — try refresh first, then full auth
    if jwt and jwt ~= "" then
        log.warn("[bluesky] cached access_jwt expired for " .. platform_id .. ", refreshing")
    end
    config.set(jwt_key, "")
    local refreshed = try_refresh_session(platform_id)
    if refreshed then return refreshed end
    return refresh_auth(platform_id)
end

--- Force re-authenticate with password (called on 401 or expired refresh).
refresh_auth = function(platform_id)
    local jwt_key = cfg_key(platform_id, "access_jwt")
    local handle = config.get(cfg_key(platform_id, "handle"))
    local password = config.get(cfg_key(platform_id, "app_password"))

    if not handle or handle == "" then
        return nil, "no handle configured for " .. platform_id
    end
    if not password or password == "" then
        return nil, "no app_password configured for " .. platform_id
    end

    local pds = get_pds(platform_id)
    local resp = animus.http_post(pds .. "/xrpc/com.atproto.server.createSession", {
        headers = { ["Content-Type"] = "application/json" },
        body = json.encode({ identifier = handle, password = password })
    })

    if resp.status ~= 200 then
        return nil, "auth failed (" .. tostring(resp.status) .. "): " .. tostring(resp.body)
    end

    local ok, data = pcall(json.decode, resp.body)
    if not ok or not data then
        return nil, "auth response parse error: " .. tostring(data)
    end

    config.set(jwt_key, data.accessJwt or "")
    config.set(cfg_key(platform_id, "refresh_jwt"), data.refreshJwt or "")
    config.set(cfg_key(platform_id, "did"), data.did or "")
    config.set(cfg_key(platform_id, "handle"), data.handle or handle)

    return data.accessJwt
end

--- Ensure authentication is established. Returns DID on success, nil + error on failure.
-- Call this at the top of any action that needs the DID.
local function ensure_auth(platform_id)
    local jwt = authenticate(platform_id)
    if not jwt then
        return nil, "authentication failed — check handle and app_password"
    end
    local did = config.get(cfg_key(platform_id, "did"))
    if not did or did == "" then
        return nil, "not authenticated — DID not resolved after auth"
    end
    return did
end

--- Check if a response indicates an expired token (400 ExpiredToken or 401).
local function is_expired_token(resp)
    if resp.status == 401 then
        log.warn("[bluesky] got 401 — treating as expired token")
        return true
    end
    if resp.status == 400 then
        local ok, data = pcall(json.decode, resp.body or "")
        if ok and data then
            if data.error == "ExpiredToken" or data.error == "InvalidToken" then
                log.warn("[bluesky] got 400 " .. tostring(data.error) .. " — expired token detected")
                return true
            end
        else
            log.warn("[bluesky] got 400 but could not parse body as JSON: " .. tostring(resp.body))
        end
    end
    return false
end

--- Re-authenticate after token expiry: try refresh first, then full re-auth.
-- Returns new jwt on success, nil on failure.
local function reauth(platform_id)
    log.warn("[bluesky] reauth triggered for " .. platform_id)
    -- Clear stale access token to force refresh
    config.set(cfg_key(platform_id, "access_jwt"), "")
    local refreshed = try_refresh_session(platform_id)
    if refreshed then
        log.info("[bluesky] reauth via refreshSession succeeded for " .. platform_id)
        return refreshed
    end
    local jwt, err = refresh_auth(platform_id)
    if jwt then
        log.info("[bluesky] reauth via createSession succeeded for " .. platform_id)
    else
        log.error("[bluesky] reauth FAILED for " .. platform_id .. ": " .. tostring(err))
    end
    return jwt, err
end

--- Authorized HTTP GET.
-- Re-authenticates once on token expiry (400 ExpiredToken or 401).
local function auth_get(platform_id, endpoint, params)
    local jwt = authenticate(platform_id)
    if not jwt then
        return { status = 0, body = "authentication failed: " .. tostring(jwt) }
    end

    local pds = get_pds(platform_id)
    local url = pds .. endpoint

    -- Build query string (URL-encoded)
    if params then
        local parts = {}
        for k, v in pairs(params) do
            parts[#parts + 1] = url_encode(k) .. "=" .. url_encode(v)
        end
        if #parts > 0 then
            url = url .. "?" .. table.concat(parts, "&")
        end
    end

    local resp = animus.http_get(url, {
        headers = { ["Authorization"] = "Bearer " .. jwt }
    })

    -- Re-auth on token expiry (400 ExpiredToken or 401)
    if is_expired_token(resp) then
        jwt = reauth(platform_id)
        if not jwt then
            return { status = 401, body = "re-authentication failed" }
        end
        resp = animus.http_get(url, {
            headers = { ["Authorization"] = "Bearer " .. jwt }
        })
    end

    return resp
end

--- Authorized HTTP POST.
-- Re-authenticates once on 401.
local function auth_post(platform_id, endpoint, body_table)
    local jwt = authenticate(platform_id)
    if not jwt then
        return { status = 0, body = "authentication failed" }
    end

    local pds = get_pds(platform_id)
    local url = pds .. endpoint

    local resp = animus.http_post(url, {
        headers = {
            ["Authorization"] = "Bearer " .. jwt,
            ["Content-Type"] = "application/json"
        },
        body = json.encode(body_table)
    })

    -- Re-auth on token expiry (400 ExpiredToken or 401)
    if is_expired_token(resp) then
        jwt = reauth(platform_id)
        if not jwt then
            return { status = 401, body = "re-authentication failed" }
        end
        resp = animus.http_post(url, {
            headers = {
                ["Authorization"] = "Bearer " .. jwt,
                ["Content-Type"] = "application/json"
            },
            body = json.encode(body_table)
        })
    end

    return resp
end

--- Parse AT-URI into collection and rkey.
-- "at://did:plc:xxx/app.bsky.feed.post/3k..." → "app.bsky.feed.post", "3k..."
local function parse_at_uri(uri)
    -- Pattern: at://<did>/<collection>/<rkey>
    local _, _, collection, rkey = uri:find("at://[^/]+/([^/]+)/([^/]+)")
    return collection, rkey
end

--- Build a strong ref from a URI (fetches the CID).
-- For replies, we need both root and parent CID.
local function get_cid(platform_id, uri)
    -- Parse AT-URI into repo, collection, rkey for getRecord
    local _, _, repo = uri:find("at://([^/]+)")
    local collection, rkey = parse_at_uri(uri)
    if not repo or not collection or not rkey then
        return nil, "invalid AT-URI: " .. tostring(uri)
    end
    local resp = auth_get(platform_id, "/xrpc/com.atproto.repo.getRecord", {
        repo = repo,
        collection = collection,
        rkey = rkey
    })
    if resp.status ~= 200 then
        return nil, "failed to fetch record for CID: " .. tostring(resp.status)
    end
    local ok, data = pcall(json.decode, resp.body)
    if not ok or not data then
        return nil, "failed to parse record response"
    end
    return data.cid or data.uri
end

-- ---------------------------------------------------------------------------
-- Action handlers
-- ---------------------------------------------------------------------------

local function do_post(args)
    local pid = args.platform_id
    if not args.content or args.content == "" then
        return { success = false, error = "content is required for post" }
    end

    local did, err = ensure_auth(pid)
    if not did then
        return { success = false, error = err }
    end

    local record = {
        ["$type"] = "app.bsky.feed.post",
        text = args.content,
        createdAt = iso_now(),
        langs = args.langs or { "en" }
    }

    local resp = auth_post(pid, "/xrpc/com.atproto.repo.createRecord", {
        repo = did,
        collection = "app.bsky.feed.post",
        record = record
    })

    if resp.status ~= 200 then
        return { success = false, error = "post failed (" .. resp.status .. "): " .. tostring(resp.body) }
    end

    local ok, data = pcall(json.decode, resp.body)
    if not ok then
        return { success = true, output = resp.body }
    end

    return {
        success = true,
        output = json.encode({
            uri = data.uri,
            cid = data.cid
        })
    }
end

local function do_reply(args)
    local pid = args.platform_id
    if not args.post_id or args.post_id == "" then
        return { success = false, error = "post_id is required for reply (AT-URI of parent)" }
    end
    if not args.content or args.content == "" then
        return { success = false, error = "content is required for reply" }
    end

    local did, err = ensure_auth(pid)
    if not did then
        return { success = false, error = err }
    end

    -- Resolve parent CID
    local parent_cid, err = get_cid(pid, args.post_id)
    if not parent_cid then
        return { success = false, error = "could not resolve parent post: " .. tostring(err) }
    end

    -- For the root: if the parent is a top-level post, root == parent.
    -- If the parent is itself a reply, we'd need to traverse. For v1, root == parent.
    local root_uri = args.root_id or args.post_id
    local root_cid = parent_cid
    if args.root_id and args.root_id ~= args.post_id then
        root_cid, err = get_cid(pid, args.root_id)
        if not root_cid then
            root_cid = parent_cid -- fallback
        end
    end

    local record = {
        ["$type"] = "app.bsky.feed.post",
        text = args.content,
        createdAt = iso_now(),
        langs = args.langs or { "en" },
        reply = {
            root = { uri = root_uri, cid = root_cid },
            parent = { uri = args.post_id, cid = parent_cid }
        }
    }

    local resp = auth_post(pid, "/xrpc/com.atproto.repo.createRecord", {
        repo = did,
        collection = "app.bsky.feed.post",
        record = record
    })

    if resp.status ~= 200 then
        return { success = false, error = "reply failed (" .. resp.status .. "): " .. tostring(resp.body) }
    end

    local ok, data = pcall(json.decode, resp.body)
    if not ok then
        return { success = true, output = resp.body }
    end

    return {
        success = true,
        output = json.encode({
            uri = data.uri,
            cid = data.cid,
            parent_uri = args.post_id
        })
    }
end

local function do_like(args)
    local pid = args.platform_id
    if not args.post_id or args.post_id == "" then
        return { success = false, error = "post_id is required for like (AT-URI)" }
    end

    local did, err = ensure_auth(pid)
    if not did then
        return { success = false, error = err }
    end

    -- Resolve CID for the target post
    local cid, err = get_cid(pid, args.post_id)
    if not cid then
        return { success = false, error = "could not resolve target post: " .. tostring(err) }
    end

    local resp = auth_post(pid, "/xrpc/com.atproto.repo.createRecord", {
        repo = did,
        collection = "app.bsky.feed.like",
        record = {
            ["$type"] = "app.bsky.feed.like",
            subject = { uri = args.post_id, cid = cid },
            createdAt = iso_now()
        }
    })

    if resp.status ~= 200 then
        return { success = false, error = "like failed (" .. resp.status .. "): " .. tostring(resp.body) }
    end

    local ok, data = pcall(json.decode, resp.body)
    if not ok then
        return { success = true, output = resp.body }
    end

    return {
        success = true,
        output = json.encode({ uri = data.uri, cid = data.cid, liked = args.post_id })
    }
end

local function do_search(args)
    local pid = args.platform_id
    if not args.query or args.query == "" then
        return { success = false, error = "query is required for search" }
    end

    local params = { q = args.query }
    if args.limit then
        params.limit = tostring(math.min(tonumber(args.limit) or 25, 100))
    else
        params.limit = "25"
    end
    if args.sort then params.sort = args.sort end
    if args.cursor then params.cursor = args.cursor end

    local resp = auth_get(pid, "/xrpc/app.bsky.feed.searchPosts", params)

    if resp.status ~= 200 then
        return { success = false, error = "search failed (" .. resp.status .. "): " .. tostring(resp.body) }
    end

    local ok, data = pcall(json.decode, resp.body)
    if not ok then
        return { success = true, output = resp.body }
    end

    -- Simplify the output for agent consumption
    local results = {}
    if data.posts then
        for _, post in ipairs(data.posts) do
            local entry = {
                uri = post.uri,
                cid = post.cid,
                text = "",
                author = "",
                indexed_at = post.indexedAt or ""
            }
            if post.author then
                entry.author = post.author.handle or ""
                entry.author_display = post.author.displayName or ""
            end
            if post.record and post.record.text then
                entry.text = post.record.text
            end
            if post.replyCount then entry.reply_count = post.replyCount end
            if post.likeCount then entry.like_count = post.likeCount end
            if post.repostCount then entry.repost_count = post.repostCount end
            results[#results + 1] = entry
        end
    end

    return {
        success = true,
        output = json.encode({
            hits_total = data.hitsTotal or #results,
            cursor = data.cursor or "",
            posts = results
        })
    }
end

local function do_get_notifications(args)
    local pid = args.platform_id

    local params = {}
    if args.limit then
        params.limit = tostring(math.min(tonumber(args.limit) or 50, 100))
    else
        params.limit = "50"
    end
    if args.cursor then params.cursor = args.cursor end

    local resp = auth_get(pid, "/xrpc/app.bsky.notification.listNotifications", params)

    if resp.status ~= 200 then
        return { success = false, error = "notifications failed (" .. resp.status .. "): " .. tostring(resp.body) }
    end

    local ok, data = pcall(json.decode, resp.body)
    if not ok then
        return { success = true, output = resp.body }
    end

    local notifs = {}
    if data.notifications then
        for _, n in ipairs(data.notifications) do
            local entry = {
                reason = n.reason or "",
                uri = n.uri or "",
                is_read = n.isRead or false,
                indexed_at = n.indexedAt or ""
            }
            if n.author then
                entry.author = n.author.handle or ""
                entry.author_display = n.author.displayName or ""
            end
            if n.record then
                if n.record.text then entry.text = n.record.text end
            end
            notifs[#notifs + 1] = entry
        end
    end

    -- Mark as seen
    auth_post(pid, "/xrpc/app.bsky.notification.updateSeen", {
        seenAt = iso_now()
    })

    return {
        success = true,
        output = json.encode({
            cursor = data.cursor or "",
            notifications = notifs
        })
    }
end

local function do_browse(args)
    local pid = args.platform_id

    -- Browse = get the home timeline via getFeed
    -- For now, use the "following" feed (what Bluesky calls the timeline)
    -- This requires a feed URI. The default following feed is:
    -- at://did:plc:<user_did>/app.bsky.feed.generator/following
    -- But the simpler approach is getTimeline via feed.getActorFeed
    -- Actually, for a user's own posts and reposts:
    local did, err = ensure_auth(pid)
    if not did then
        return { success = false, error = err }
    end

    local params = { actor = did }
    if args.limit then
        params.limit = tostring(math.min(tonumber(args.limit) or 25, 100))
    else
        params.limit = "25"
    end
    if args.cursor then params.cursor = args.cursor end

    local resp = auth_get(pid, "/xrpc/app.bsky.feed.getAuthorFeed", params)

    if resp.status ~= 200 then
        return { success = false, error = "browse failed (" .. resp.status .. "): " .. tostring(resp.body) }
    end

    local ok, data = pcall(json.decode, resp.body)
    if not ok then
        return { success = true, output = resp.body }
    end

    local feed = {}
    if data.feed then
        for _, item in ipairs(data.feed) do
            local entry = {}
            if item.post then
                entry.uri = item.post.uri
                entry.cid = item.post.cid
                if item.post.author then
                    entry.author = item.post.author.handle or ""
                end
                if item.post.record and item.post.record.text then
                    entry.text = item.post.record.text
                end
                if item.post.indexedAt then entry.indexed_at = item.post.indexedAt end
                if item.post.likeCount then entry.like_count = item.post.likeCount end
                if item.post.repostCount then entry.repost_count = item.post.repostCount end
                if item.post.replyCount then entry.reply_count = item.post.replyCount end
            end
            feed[#feed + 1] = entry
        end
    end

    return {
        success = true,
        output = json.encode({
            cursor = data.cursor or "",
            feed = feed
        })
    }
end

local function do_delete(args)
    local pid = args.platform_id
    if not args.post_id or args.post_id == "" then
        return { success = false, error = "post_id is required for delete (AT-URI)" }
    end

    local did, err = ensure_auth(pid)
    if not did then
        return { success = false, error = err }
    end

    local collection, rkey = parse_at_uri(args.post_id)
    if not collection or not rkey then
        return { success = false, error = "invalid AT-URI format: " .. args.post_id }
    end

    local resp = auth_post(pid, "/xrpc/com.atproto.repo.deleteRecord", {
        repo = did,
        collection = collection,
        rkey = rkey
    })

    if resp.status ~= 200 then
        return { success = false, error = "delete failed (" .. resp.status .. "): " .. tostring(resp.body) }
    end

    return {
        success = true,
        output = json.encode({ deleted = args.post_id })
    }
end

-- ---------------------------------------------------------------------------
-- Registration
-- ---------------------------------------------------------------------------

animus.register_channel({
    id = "bluesky",
    name = "Bluesky",
    capabilities = {"read", "write", "search"},
    actions = {"post", "reply", "like", "browse", "search", "get_notifications", "delete"},
    schema = {
        post = {
            { name = "content", type = "string", required = true, description = "Post text (max 300 graphemes)" },
            { name = "visibility", type = "string", required = false, description = "Not used by Bluesky (always public)" },
            { name = "langs", type = "array", required = false, description = "BCP-47 language codes (default: ['en'])" }
        },
        reply = {
            { name = "post_id", type = "string", required = true, description = "AT-URI of the post to reply to" },
            { name = "content", type = "string", required = true, description = "Reply text" },
            { name = "root_id", type = "string", required = false, description = "AT-URI of the thread root (defaults to post_id)" }
        },
        like = {
            { name = "post_id", type = "string", required = true, description = "AT-URI of the post to like" }
        },
        search = {
            { name = "query", type = "string", required = true, description = "Search query" },
            { name = "limit", type = "integer", required = false, description = "Max results (1-100, default 25)" },
            { name = "sort", type = "string", required = false, description = "'latest' or 'top' (default: latest)" },
            { name = "cursor", type = "string", required = false, description = "Pagination cursor" }
        },
        browse = {
            { name = "limit", type = "integer", required = false, description = "Max results (default 25)" },
            { name = "cursor", type = "string", required = false, description = "Pagination cursor" }
        },
        get_notifications = {
            { name = "limit", type = "integer", required = false, description = "Max notifications (1-100, default 50)" },
            { name = "cursor", type = "string", required = false, description = "Pagination cursor" }
        },
        delete = {
            { name = "post_id", type = "string", required = true, description = "AT-URI of the post to delete" }
        }
    },
    handler = function(args)
        local action = args.action
        if not action or action == "" then
            return { success = false, error = "action is required" }
        end

        if action == "post" then return do_post(args)
        elseif action == "reply" then return do_reply(args)
        elseif action == "like" then return do_like(args)
        elseif action == "search" then return do_search(args)
        elseif action == "browse" then return do_browse(args)
        elseif action == "get_notifications" then return do_get_notifications(args)
        elseif action == "delete" then return do_delete(args)
        else return { success = false, error = "unknown action: " .. tostring(action) }
        end
    end
})

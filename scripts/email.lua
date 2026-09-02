-- ===========================================================================
-- Email (AgentMail) — stateless Lua channel adapter
--
-- Registers via animus.register_channel() as a stateless adapter.
-- Auth: api_key + inbox_id from config (channels.email:INSTANCE.*)
--
-- The C++ EmailAdapter (WebSocket inbound, poll fallback) builds the
-- arrival card (#15/#42, issue #59) with thread targets and reply
-- instructions; this tool serves the agent-side reply path. Tool-only
-- delivery: text replies are never sent as email.
--
-- animus.http_post(url, options) — options = { headers={}, body="" }
-- (the (url, body, headers) form never existed; see telegram.lua note)
-- ===========================================================================

local function cfg_key(platform_id, field)
    return "channels." .. platform_id .. "." .. field
end

-- Safe JSON decode (moltbook.lua idiom, ported): bare json.decode turns a
-- bad/empty body into a raw handler error; in Lua "" is truthy, so
-- `resp.body or "{}"` does NOT guard the empty case.
local function json_decode_safe(str)
    if not str or str == "" then return nil, "empty body" end
    local ok, data = pcall(json.decode, str)
    if ok then return data end
    return nil, "json decode failed: " .. tostring(data)
end

local function body_excerpt(body, max_len)
    max_len = max_len or 120
    local s = tostring(body or "")
    if #s <= max_len then return s end
    return s:sub(1, max_len) .. "..."
end

local function api_base(platform_id)
    -- config.get returns "" (never nil) for missing keys, and "" is TRUTHY in
    -- Lua — `config.get(k) or default` silently returns "". Guard explicitly.
    -- (Sep 2 incident: unguarded default made every URL relative; curl failed
    -- instantly with status=0 + empty body and the real error lived in
    -- resp.error, which was never surfaced.)
    local base = config.get(cfg_key(platform_id, "api_base_url"))
    if not base or base == "" then
        return "https://api.agentmail.to"
    end
    return base:gsub("/+$", "") -- tolerate trailing slash
end

local function get_creds(platform_id)
    local key = config.get(cfg_key(platform_id, "api_key"))
    if not key or key == "" then
        return nil, "No API key configured. Set channels." .. platform_id .. ".api_key"
    end
    local inbox = config.get(cfg_key(platform_id, "inbox_id"))
    if not inbox or inbox == "" then
        return nil, "No inbox_id configured. Set channels." .. platform_id .. ".inbox_id"
    end
    return key, inbox
end

local function do_reply(args)
    local pid = args.platform_id or ""
    local key, inbox = get_creds(pid)
    if not key then
        return { success = false, error = inbox }
    end

    -- The card displays the reply target with a routing discriminator
    -- prefix ("d:"); strip it — AgentMail wants the raw thread id. The
    -- auto-fill path already provides the raw form.
    local thread_id = (args.thread_id or ""):gsub("^d:", "")
    local content = args.content or args.text or ""
    if thread_id == "" then
        return { success = false,
                 error = "reply requires thread_id (provided by the arrival and filled automatically)" }
    end
    if content == "" then
        return { success = false, error = "reply requires content" }
    end

    -- AgentMail send REQUIRES to/cc/bcc even with thread_id (400 otherwise,
    -- "to, cc, or bcc must be specified"). If the arrival card didn't
    -- pre-fill the recipient, resolve it from the thread's senders.
    local to = args.to or args.recipient or ""
    if to == "" then
        local tresp = animus.http_get(api_base(pid) .. "/v0/inboxes/" .. inbox
            .. "/threads/" .. thread_id, {
            headers = { ["Authorization"] = "Bearer " .. key },
        })
        local tdata = tresp and json_decode_safe(tresp.body) or nil
        if tdata and type(tdata.senders) == "table" and #tdata.senders > 0 then
            to = tdata.senders[1]
        else
            return { success = false,
                     error = "reply requires to (arrival did not pre-fill it and thread sender lookup failed)" }
        end
    end
    -- senders carry display form "Name <addr>"; the send API wants the bare address
    local addr = tostring(to):match("<([^>]+)>") or to

    local resp = animus.http_post(api_base(pid) .. "/v0/inboxes/" .. inbox .. "/messages/send", {
        headers = {
            ["Authorization"] = "Bearer " .. key,
            ["Content-Type"] = "application/json",
        },
        body = json.encode({ text = content, thread_id = thread_id, to = addr }),
    })
    if not resp then
        return { success = false, error = "HTTP request failed" }
    end

    local data, decode_err = json_decode_safe(resp.body)
    if not data then
        return { success = false, error = "AgentMail reply failed: " .. (decode_err or "unknown")
                 .. " (http_status=" .. tostring(resp.status or "?")
                 .. ", curl_error=" .. tostring(resp.error or "none")
                 .. ", body_type=" .. type(resp.body)
                 .. ", body=" .. body_excerpt(resp.body) .. ")" }
    end
    if resp.status and resp.status >= 400 then
        local why = data.message or data.error or ""
        if type(data.errors) == "table" and #data.errors > 0 and data.errors[1].message then
            why = why .. " [" .. tostring(data.errors[1].message) .. "]"
        end
        return { success = false, error = "HTTP " .. tostring(resp.status) .. ": " .. why }
    end

    local sent_id = data.id or data.message_id or ""
    return {
        success = true,
        output = "Reply sent to email thread " .. thread_id,
        thread_id = thread_id,
        message_id = sent_id,
    }
end

-- Thread history read: lets the agent consult earlier turns of the thread
-- beyond what the arrival's quoted chains carry.
local function do_thread_messages(args)
    local pid = args.platform_id or ""
    local key, inbox = get_creds(pid)
    if not key then
        return { success = false, error = inbox }
    end

    local thread_id = (args.thread_id or ""):gsub("^d:", "")
    if thread_id == "" then
        return { success = false, error = "thread_messages requires thread_id" }
    end

    local limit = tonumber(args.limit) or 10
    -- Correct route: GET /v0/inboxes/{inbox}/threads/{tid} (no /messages
    -- suffix — that path 404s). The thread object embeds messages[].
    local url = api_base(pid) .. "/v0/inboxes/" .. inbox .. "/threads/" .. thread_id
    local resp = animus.http_get(url, {
        headers = { ["Authorization"] = "Bearer " .. key },
    })
    if not resp then
        return { success = false, error = "HTTP request failed" }
    end

    local data, decode_err = json_decode_safe(resp.body)
    if not data then
        return { success = false, error = "AgentMail read failed: " .. (decode_err or "unknown")
                 .. " (http_status=" .. tostring(resp.status or "?")
                 .. ", curl_error=" .. tostring(resp.error or "none")
                 .. ", body_type=" .. type(resp.body)
                 .. ", body=" .. body_excerpt(resp.body) .. ")" }
    end
    if resp.status and resp.status >= 400 then
        return { success = false, error = "HTTP " .. tostring(resp.status) .. ": " .. tostring(data.error or "") }
    end

    local msgs = data.messages or {}
    if limit > 0 and #msgs > limit then
        local trimmed, first = {}, #msgs - limit + 1
        for i = first, #msgs do trimmed[#trimmed + 1] = msgs[i] end
        msgs = trimmed
    end
    local lines = {}
    for i = #msgs, 1, -1 do
        local m = msgs[i]
        lines[#lines + 1] = (m.from or "?") .. ": " .. (m.text or m.extracted_text or "")
    end
    return {
        success = true,
        output = table.concat(lines, "\n---\n"),
        count = #msgs,
    }
end

local function handler(args)
    -- Bridge contract (LuaToolHandler.cpp): handler receives ONE argument,
    -- the args table; action lives at args.action. (A handler(action, args)
    -- signature receives the table as `action` — the "Unknown action:
    -- table: 0x..." failure mode.)
    args = args or {}
    local action = args.action
    if action == "reply" then return do_reply(args) end
    if action == "thread_messages" then return do_thread_messages(args) end
    return { success = false, error = "Unknown action: " .. tostring(action) }
end

animus.register_channel({
    id = "email",
    name = "Email (AgentMail)",
    capabilities = {"write", "read"},
    actions = {"reply", "thread_messages"},
    schema = {
        reply = {
            { name = "thread_id", type = "string", required = true, description = "Email thread to reply to (from the arrival; filled automatically)" },
            { name = "content", type = "string", required = true, description = "Reply body text" },
        },
        thread_messages = {
            { name = "thread_id", type = "string", required = true, description = "Email thread to read (from the arrival)" },
            { name = "limit", type = "number", required = false, description = "Max messages to retrieve (default 10)" },
        },
    },
    handler = handler,
})

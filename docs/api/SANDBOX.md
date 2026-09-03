# API Packages — Lua Sandbox Contract

The sandbox contract is the product (blueprint D2). Every rule here exists because its violation
was a real bug in the channel adapters (the #15/#16 seam family) or a real loop risk (Sept 2).

## Script shape

Every command script is a Lua chunk that defines exactly one entry point:

```lua
function run(ctx) ... end
```

The chunk runs once at load (may build helpers/local state), then `run` is invoked per execution.
Raw errors thrown from `run` are caught by the framework, logged with the command name, and
surfaced to the agent as `{success = false, error = "..."}` — a hook that throws never crashes the
supervisor. But **prefer returning errors over throwing them**, and always surface the transport's
reason (`ctx.request.error` / `ctx.http` error fields) — silence is how the seam family survived.

## Contexts

### Action commands

```lua
ctx = {
  package = {
    name = "alpaca",
    state = { ... },                    -- current values, SECRETS MASKED as "***"
    get_state = function(k) end,        -- returns real value, incl. secrets (needed for signing etc.)
    set_state = function(k, v) end,     -- validated against state_schema
  },
  args = { ... },                       -- validated against `parameters`; secret args masked in logs
  request = nil | {                     -- present iff the command declared a primary `request`
    status = 200,
    headers = { ... },
    body = "...",
    json = <decoded body or nil>,
    error = nil | "transport failure reason",
  },
  http = {                              -- SECONDARY calls only (cap: 5 per invocation)
    get = function(url, opts) end,
    post = function(url, opts) end, put = ..., delete = ...,
  },
  log = function(...) end,              -- ALOG, prefixed [api:<package>:<command>]
  now = 1788373166000,                  -- epoch ms
}
```

Return contract:

```lua
return {
  success = true,        -- default true
  output = "3 positions",-- agent-facing string (required in practice)
  data = { ... },        -- optional structured payload (agent-visible)
}
```

### Hook commands

Hooks receive no `args`/`request`. Their contexts:

```lua
-- event: on_message
ctx.event = { text = "<raw frame>", json = <decoded or nil>, conn = "stream" }

-- event: on_connect (fires on EVERY (re)connect)
ctx.event = { conn = "stream" }

-- event: on_disconnect
ctx.event = { conn = "stream", code = 1006, reason = "..." }

-- event: on_error (transport-level failure, incl. interpolation errors)
ctx.event = { conn = "stream", error = "missing state key: token" }

ctx.conn = {                           -- frame-level affordances; never a socket
  name = "stream", type = "websocket",
  send = function(frame) end,          -- queue a frame out
  close = function() end,
}
```

Return contracts — **declarative, per event** (D4). The framework acts on the returned table; Lua
never starts chains itself:

```lua
-- on_message:
return {
  dispatch = true,          -- gate: whether a prompt chain runs at all
  prompt = "NVDA crossed 120.4 (up 2.1% intraday). Review the position.",
  card = { title = "NVDA threshold", severity = "info" },   -- optional card fields
  dedup_key = "nvda-120.4", -- optional; identical keys within the cooldown are skipped
}

-- on_connect: frames to send, in order (subscriptions). Idempotent by contract —
-- this fires on every reconnect, so re-subscribing here is the pattern, not a bug.
return { send = { '{"action":"subscribe","trades":["NVDA"]}' } }

-- on_disconnect / on_error: advisory only; the supervisor owns backoff/reconnect.
return {}
```

## Globals whitelist

Available: `string`, `table`, `math`, `os.time`/`os.date` (no `os.execute`), `json.encode`,
`json.decode_safe` (pcall-wrapped; **use this, never `json.decode`** — empty/invalid bodies are
routine), `tostring`, `tonumber`, `pairs`, `ipairs`, `#`, standard operators.

Not available: `io`, `require`, `os.execute`, `loadfile`, `dofile`, raw `json.decode`, any process
or filesystem access. The chunk is loaded under the existing LuaState sandbox with the
instruction-count hook active (long-running scripts are terminated, not hung).

## State & secrets

- Read via `ctx.package.state` (secrets masked — for display/branching) or `get_state(k)` (real
  value — for building signed payloads).
- Write via `set_state(k, v)`; validated against `state_schema`; unknown keys rejected.
- Keys starting with `_` are framework-reserved (e.g. `_cursor.<conn>` — longpoll cursors). Do not
  write them from scripts.
- Secrets (`state_schema` `{secret: true}`) and secret action parameters are masked in
  `prompt_logs`, tool results, and `ALOG` output. They never appear in registry manifests — state
  never leaves the box (D6).

## Re-entrancy (D5)

- A hook may cause at most **one** dispatch (its return value). There is no imperative
  chain-invocation function in the sandbox — deliberate (blueprint D4/D5).
- While a package's own dispatch chain is running, further events from that package queue
  (per-connection, cap 8; overflow drops with `ALOG_WARN`).
- Dispatches respect the per-package cooldown (`dispatch_cooldown_ms`, default 10 000 ms);
  `dedup_key` collisions inside the cooldown window are skipped as duplicates.
- Active commands invoked *from* a chain turn (the agent replying to a dispatched card via
  `api <pkg> <cmd>`) are always allowed — that's the reply path, not recursion.

## Style rules for package authors (enforced in review, not lint)

1. Guard empty strings explicitly: `if v == nil or v == "" then` — `""` is truthy; `or default`
   never fires on it (caught three times in one file once; never again).
2. Strip routing prefixes defensively: `id:gsub("^d:", "")` — cards may carry display-form ids.
3. Surface `error` fields from every transport response — the agent can only work with what the
   tool result says.
4. Idempotent `on_connect`: assume every connection will drop and re-run this script.

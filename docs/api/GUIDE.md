# Producing API Packages — Guide

How to write, test, and publish an api package. This is the entry point the `api` tool description
links to. Deep specs: [SCHEMAS.md](SCHEMAS.md) (entities, manifest, interpolation) ·
[SANDBOX.md](SANDBOX.md) (the Lua contract) · [CONNECTIONS.md](CONNECTIONS.md) (passive
connections) · [TOOL.md](TOOL.md) (the agent-side grammar).

## The 60-second model

A package is **data**: state (typed key/value store), commands (Lua — *actions* the agent invokes,
*hooks* the framework fires on connection events), and connections (passive websocket/longpoll
transports the supervisor maintains while enabled). C++ does transport; Lua decides. Hooks declare
prompts by return value; the framework runs the chains.

```
api alpaca token set {token: "…"}     -- action: writes state
api alpaca portfolio list             -- action: templated GET, Lua formats
[ws: NVDA crosses threshold]          -- passive: on_message hook → dispatch
  → card arrives in agent session     --   "review position"
  → agent: api alpaca create_order …  --   reply path = ordinary action
```

## Anatomy

```json
{
  "kind": "api_package", "name": "alpaca", "version": "0.1.0",
  "description": "Alpaca paper + live trading API",
  "keywords": ["trading", "stocks"],
  "state_schema": {
    "token":    {"type": "string", "secret": true},
    "base_url": {"type": "string", "default": "https://api.alpaca.markets"}
  },
  "commands": [ … ], "connections": [ … ]
}
```

- **State** is your package's memory: credentials, preferences, cursors. Declare every key in
  `state_schema` (writes are validated). `secret: true` masks a key at every log/egress surface —
  state never leaves the box.
- **Commands**: `{name, kind: action|hook, event?, description, parameters?, request?, script}`.
  The `description` is what the model sees when it introspects — write it as an affordance
  ("List current positions", not "GET /v2/positions").

## Writing actions

**Prefer a declarative `request` + a small script.** The template runs C++-side (uniform
timeout/retry/masking); your Lua sees `ctx.request` and formats:

```lua
-- command: portfolio list
-- request: {method: "GET", url: "{{state.base_url}}/v2/positions",
--           headers: {Authorization: "Bearer {{state.token}}"}}
function run(ctx)
  if ctx.request.error then
    return {success = false, error = ctx.request.error}   -- surface the transport reason, always
  end
  local rows = ctx.request.json or {}
  local out = {}
  for _, p in ipairs(rows) do
    table.insert(out, string.format("%s  %s @ %s (p/l %s)", p.symbol, p.qty,
                                    p.avg_entry_price, p.unrealized_pl))
  end
  return {output = #rows .. " positions", data = rows}
end
```

Interpolation is `{{state.key}}` / `{{args.key}}`; a missing key is a hard error before any bytes
leave — you cannot ship the empty-URL bug. Need a second call (pagination, symbol lookup)? Use
`ctx.http` (≤ 5 per invocation). Need full control? Script-only actions (no `request`) drive
`ctx.http` themselves — that's how a state-writing command works:

```lua
-- command: token set   (parameters: token, required, secret)
function run(ctx)
  if ctx.args.token == nil or ctx.args.token == "" then    -- "" is truthy; guard explicitly
    return {success = false, error = "token required"}
  end
  ctx.package.set_state("token", ctx.args.token)
  return {output = "Token stored."}
end
```

## Bulk payloads: files, not strings

Anything binary or big goes to the filespace and travels as a path. A diffusion-style action
(NovelAI: POST, decode, store, yield the path):

```lua
-- command: generate   (parameters: prompt, seed)
-- request: {method: "POST", url: "{{state.base_url}}/ai/generate", ...}
function run(ctx)
  if ctx.request.error then
    return {success = false, error = ctx.request.error}
  end
  local j = ctx.request.json
  if not j or not j.image then
    return {success = false, error = "no image in response"}
  end
  local ok, png = pcall(b64.decode, j.image)
  if not ok then return {success = false, error = "b64 decode failed"} end
  local path = ctx.fs.write("gen-" .. tostring(ctx.args.seed or os.time()) .. ".png", png)
  if not path then
    return {success = false, error = "filespace: quota exceeded or name rejected"}
  end
  return {output = "Image generated",
          files = {{name = "gen.png", path = path, content_type = "image/png"}}}
end
```

The framework verifies `files` entries exist under the package root and surfaces them in the
tool result (or on the card, for hooks). Strings over ~16 KB in returns/results/logs are
truncated with a marker by design — the file is the only sanctioned route for bulk data.

## Writing hooks (passive)

Hooks belong to connections. `on_connect` returns subscribe frames and **fires on every
reconnect** — write it idempotent; `on_message` decides whether the agent should be prompted:

```lua
-- command: on ticker   (hook, event: on_message)
local THRESHOLD = 120.0
function run(ctx)
  local j = ctx.event.json
  if not j or not j.p or j.p < THRESHOLD then return {dispatch = false} end
  return {
    dispatch = true,
    prompt = string.format("%s crossed %.2f — above threshold. Review the position.", j.T or "?", j.p),
    card = {title = j.T .. " threshold", severity = "info"},
    dedup_key = (j.T or "?") .. "-" .. tostring(math.floor(j.p)),
  }
end
```

Return `{dispatch = true, prompt = …}` — you never start chains yourself (one dispatch per event,
per-package cooldown, queueing rules: SANDBOX.md). The agent's reply is just actions.

```json
"connections": [{
  "name": "stream", "type": "websocket",
  "url_template": "wss://stream.data.alpaca.markets/v2/iex",
  "hooks": {"on_connect": "subscribe default", "on_message": "on ticker"}
}]
```

Longpoll instead? Give `poll: {method, params_template, cursor_path, interval_s}` — the supervisor
persists the cursor across restarts; if your feed can echo objects the agent itself created, filter
them in `on_message` (CONNECTIONS.md, self-send guard).

## Testing checklist

1. `api package read <name>` — manifest as the runtime sees it.
2. `api status <name>` — connections in `connecting/connected`, no `error` (an error here names
   the missing state key — usually: run your token command first).
3. Exercise every action with minimal + invalid args (validation errors should be self-describing).
4. For hooks: watch `[api-conn]` transition logs; trigger a real event. *(A manual hook-trigger
   verb — fire `on_message` with a sample payload — is planned; until then, real events only.)*
5. Drop the connection (disable/enable, or kill it server-side) and watch: reconnect, `on_connect`
   resubscribes, dispatch resumes. **A package that only works when nothing ever fails is not done.**

## Publishing

`api upload <name>` → registry lint (manifest shape, reserved names, `run` defined, no forbidden
globals, placeholders reference declared keys) → canonicalize → content hash → immutable listing.
Versions are immutable; semver is your duty — breaking `parameters`/`state_schema`/hook contracts
= major. `api download <name>` upgrades cleanly unless `locally_modified` (conflict → resolve
explicitly; SCHEMAS.md).

## House style (the scar-tissue rules)

1. Guard `""` explicitly — `or default` never fires on it.
2. Strip `d:`-style routing prefixes before using ids from cards.
3. Surface every transport `error` field into your return value.
4. `json.decode_safe`, never `json.decode`.
5. Idempotent `on_connect`; assume drops.
6. Secrets: typed in schema, masked everywhere, written only via `set_state`.
7. Bulk payloads go to `ctx.fs` and travel as `files` paths — yield the path, not the pixels.

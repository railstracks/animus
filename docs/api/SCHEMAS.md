# API Packages — Schemas

Entities, manifest format, interpolation, and versioning/conflict semantics.
Blueprint: [README.md](README.md) · Lua contract: [SANDBOX.md](SANDBOX.md)

## Database entities

Descriptive DDL — actual migrations may add audit columns, but field names and semantics are spec.

### `api_packages`

```sql
CREATE TABLE api_packages (
  id                 UUID PRIMARY KEY,
  name               TEXT UNIQUE NOT NULL,      -- lowercase slug [a-z0-9-]
  display_name       TEXT,
  description        TEXT NOT NULL,
  keywords           JSONB NOT NULL DEFAULT '[]',
  version            TEXT NOT NULL,             -- semver, local version
  registry_source    TEXT,                      -- registry listing id; NULL = locally created
  registry_version   TEXT,                      -- semver last downloaded from registry
  locally_modified   BOOLEAN NOT NULL DEFAULT FALSE,
  enabled            BOOLEAN NOT NULL DEFAULT FALSE,
  dispatch_cooldown_ms INTEGER NOT NULL DEFAULT 10000,
  state_schema       JSONB NOT NULL DEFAULT '{}',
  state              JSONB NOT NULL DEFAULT '{}',
  created_at         TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at         TIMESTAMPTZ NOT NULL DEFAULT now()
);
```

- `state_schema`: `{ "<key>": {"type": "string", "default": ..., "secret": true?}, ... }`.
  Keys starting with `_` are reserved for framework use (e.g. longpoll cursors).
- `state` values validated against `state_schema` on write. Secrets stored as-is in PG (v1 posture,
  see README open questions) but **masked at every egress surface**: tool results, prompt_logs,
  export/upload. Interpolation happens C++-side at transport time.
- `name` is validated against the reserved words (TOOL.md) at create and at registry download.

### `api_package_commands`

```sql
CREATE TABLE api_package_commands (
  id           UUID PRIMARY KEY,
  package_id   UUID NOT NULL REFERENCES api_packages(id) ON DELETE CASCADE,
  name         TEXT NOT NULL,           -- lowercase, spaces allowed ("portfolio list")
  kind         TEXT NOT NULL,           -- 'action' | 'hook'
  event        TEXT,                    -- hooks only: on_connect | on_disconnect | on_message | on_error
  description  TEXT NOT NULL,           -- agent-facing affordance (what the model sees)
  parameters   JSONB NOT NULL DEFAULT '{}',  -- actions only; name -> {type, required, description, secret?}
  request      JSONB,                   -- actions only; declarative primary request (below)
  script       TEXT NOT NULL,           -- Lua; defines run(ctx)  [SANDBOX.md]
  UNIQUE (package_id, name)
);
```

`request` (nullable — script-only actions skip it and drive `ctx.http` themselves):

```json
{
  "method": "GET",
  "url": "{{state.base_url}}/v2/positions",
  "headers": {"Authorization": "Bearer {{state.token}}"},
  "body": "{\"side\": \"{{args.side}}\", \"qty\": {{args.qty}}}"
}
```

### `api_package_connections`

```sql
CREATE TABLE api_package_connections (
  id               UUID PRIMARY KEY,
  package_id       UUID NOT NULL REFERENCES api_packages(id) ON DELETE CASCADE,
  name             TEXT NOT NULL,       -- lowercase slug [a-z0-9-]
  type             TEXT NOT NULL,       -- 'websocket' | 'longpoll'
  enabled          BOOLEAN NOT NULL DEFAULT TRUE,
  url_template     TEXT NOT NULL,       -- interpolated from state
  headers_template JSONB NOT NULL DEFAULT '{}',
  poll             JSONB,               -- longpoll only: {method, params_template, cursor_path, interval_s}
  hooks            JSONB NOT NULL DEFAULT '{}',  -- {on_connect: "<cmd name>", on_message: "...", ...}
  UNIQUE (package_id, name)
);
```

- A connection is active iff `package.enabled AND connection.enabled`.
- Runtime state (connection status, backoff, last event, failures) lives **in memory** in the
  supervisor, surfaced via `api status` — the table carries authored configuration only.
- `poll` (longpoll): `cursor_path` is a dotted JSON path into the response where the next-cursor
  value is found; the supervisor persists it in package state under `_cursor.<conn_name>` so it
  survives restarts (the moltbook watermark lesson).

## Manifest v1 (registry format)

One manifest format for all registry kinds; `kind` discriminates. Package manifests carry the
authored fields only — never state values, never ids.

```json
{
  "kind": "api_package",
  "name": "alpaca",
  "version": "0.1.0",
  "description": "Alpaca paper + live trading API",
  "keywords": ["trading", "stocks", "market"],
  "dispatch_cooldown_ms": 10000,
  "state_schema": {
    "token":     {"type": "string", "secret": true},
    "base_url":  {"type": "string", "default": "https://api.alpaca.markets"}
  },
  "commands": [
    {
      "name": "token set", "kind": "action",
      "description": "Store the Alpaca API token (kept secret, used for all requests)",
      "parameters": {"token": {"type": "string", "required": true, "secret": true}},
      "script": "function run(ctx) ctx.package.set_state('token', ctx.args.token) return {output='Token stored.'} end"
    },
    {
      "name": "portfolio list", "kind": "action",
      "description": "List current positions",
      "request": {
        "method": "GET",
        "url": "{{state.base_url}}/v2/positions",
        "headers": {"Authorization": "Bearer {{state.token}}"}
      },
      "script": "function run(ctx) local rows = ctx.request.json or {} local out = {} for _,p in ipairs(rows) do table.insert(out, p.symbol..' '..p.qty..' @ '..tostring(p.avg_entry_price)) end return {output=#out..' positions', data=rows} end"
    },
    {
      "name": "on ticker", "kind": "hook", "event": "on_message",
      "script": "..."
    }
  ],
  "connections": [
    {
      "name": "stream", "type": "websocket",
      "url_template": "wss://stream.data.alpaca.markets/v2/iex",
      "hooks": {"on_connect": "subscribe default", "on_message": "on ticker"}
    }
  ]
}
```

Registry pipeline (extends the animus-sop Issue #1 pipeline to all kinds):
**lint → canonicalize → content hash → immutable listing record.**

Lint rules (v1): manifest shape per kind; reserved-name check; `kind: action` requires exactly one
of `request`/`script`-only (both allowed: request + post-process script); hooks must not declare
`parameters` or `request`; Lua static checks — script must define `run`, no forbidden globals
(`io`, `os.execute`, `require`, …), template placeholders must reference declared `state_schema`
keys or `args`.

## Interpolation

- Syntax: `{{path}}` where path is dotted: `state.token`, `args.qty`, `args.order.id`.
- Available contexts: `state` (package state) and `args` (validated action parameters).
- **Missing key = hard error at execution** — for actions a tool error; for connections the
  connection enters `error` state with a loud `ALOG_ERROR` naming the key and template. No empty
  string ever flows through silently.
- No escape syntax: `{{` is always interpolation. If you need literal braces in a URL/header,
  build that value in Lua (`ctx.http`) instead of a template.
- Secret values are interpolated at transport time only; logged requests show the template with
  `***` in place of resolved secrets.

## Versioning & conflict semantics

- `version` (local) vs `registry_version` (what was downloaded). Local edits bump `version` and
  set `locally_modified = true`.
- `api download <name>[@version]`:
  - not installed → install at pinned version;
  - installed, not modified → upgrade to target version;
  - installed, `locally_modified` → **refuse** with a conflict error (the agent must resolve:
    force-overwrite, or fork the local copy under a new name).
- Registry versions are immutable (content-hash addressed).
- Semver obligations: breaking changes to `parameters`, `state_schema`, or hook contracts = major;
  new commands/connections/fields = minor; description/script fixes = patch.

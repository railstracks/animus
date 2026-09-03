# The `api` Tool — Grammar Reference

Agent-facing grammar. Reserved words vs package invocations. The tool description carries the
affordance summary and points to GUIDE.md on GitHub (schema beats context block — the #42 lesson).

## Reserved first-words

`package, command, connection, download, upload, enable, disable, status`

Package names are validated against this set at create and at registry download; package
invocations can therefore never shadow management verbs.

### Management verbs

- `api package create <name> [description]` / `update <name> […]` / `read <name>` / `delete <name>`
  — local CRUD. `update` bumps `version` and sets `locally_modified`.
- `api command create|update|delete|read <package> <command>` — command CRUD (fields per
  SCHEMAS.md; scripts validated: must define `run`).
- `api connection create|update|delete|read <package> <connection>` — connection CRUD. Changes
  apply at runtime if enabled (teardown + restart of that worker).
- `api download <name>[@version]` — install/upgrade from the registry. Refuses to clobber a
  `locally_modified` package (conflict error with resolution options).
- `api upload <name>` — publish to the registry (lint → canonicalize → hash → listing). Requires
  no pending conflict; bumps nothing — versioning is the author's semver duty (SCHEMAS.md).
- `api enable <name>` / `api disable <name>` — runtime apply: starts/tears down passive
  connections (CONNECTIONS.md). Enable clears `error`-state connections for re-evaluation.
- `api status [<package>]` — inventory + health: packages (enabled, version, locally_modified,
  commands), connections (state, last event age, failures, last error), dispatch cooldowns,
  queue depths.

### Introspection affordances

- `api <package>` — lists that package's action commands with descriptions (what the model reaches
  for first; make descriptions count).
- `api <package> <command>` without args — echoes the command's `parameters` schema as the error
  (self-documenting failure).

### Package invocations

```
api alpaca portfolio list
api alpaca create_order {symbol: "NVDA", side: "sell", qty: 10.0}
api alpaca token set {token: "…"}        -- secret arg: masked in logs
```

- Args validated against `parameters` (type + required) before the sandbox runs; secret parameters
  masked in `prompt_logs` and tool results.
- Execution: declared `request` (if any) → C++ transport (uniform timeout/retry; interpolation
  failure = tool error naming the missing key) → Lua `run(ctx)` → result.
- Result shape (agent-facing): `{success, output, data?}`; failures carry the transport reason —
  always actionable, never bare.

## Conventions

- Unknown package or command → error **listing what's available** (never a dead end).
- All verbs log state transitions they cause (`[api]` prefix); health belongs to `api status`.
- Passive events are never handled here — they arrive as cards via dispatch (CONNECTIONS.md); the
  agent replies by invoking actions, same grammar.

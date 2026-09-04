# API Package Framework — Blueprint

Status: **draft spec v0.1** (2026-09-03). Ratified in the Sept 2–3 design sessions (Melvin + Kestrel).
Companion docs: [SCHEMAS.md](SCHEMAS.md) · [SANDBOX.md](SANDBOX.md) · [CONNECTIONS.md](CONNECTIONS.md) · [TOOL.md](TOOL.md) · [GUIDE.md](GUIDE.md)
Tracking: #26 (Alpaca, first consumer) · #60 (connection lifecycle, the negative spec) · #61 (webhooks, parked)

## Purpose

Turn the hand-built channel adapters into **data**. An *api package* is a database entity an agent
(or human) can install, enable, and use: **active commands** invoked as tool calls, and **passive
connections** maintained by a supervisor whose events dispatch prompt chains. The architecture is
not speculative — it is the adapter shape validated nine times in the channel migration, expressed
as data + one generic runtime instead of nine bespoke adapters.

First consumer: the Alpaca package (#26) — token state, `portfolio list`, `create_order`, one
websocket threshold connection.

## Concepts

- **Package** — name, description, keywords, version, state schema, commands, connections.
- **Command** — a named unit of Lua logic. Two kinds: *actions* (agent-invoked via
  `api <package> <command>`) and *hooks* (fired by connection events: `on_connect`,
  `on_disconnect`, `on_message`, `on_error`). All Lua lives in the command table; connections
  reference hooks by name.
- **Connection** — a passive transport declaration (`websocket` | `longpoll` in v1) with URL/header
  templates and a hook wiring map. The C++ supervisor owns the actual connection object; Lua never
  holds sockets, only frame-level affordances.
- **State** — package-local key/value store (typed by `state_schema`), e.g. an API token set once
  by a command and interpolated into outgoing headers everywhere else.
- **Filespace** — package-scoped scratch storage (`ctx.fs`) for bulk payloads: binary artifacts
  are written to disk and travel as `files` paths, never as megabyte strings.

## Architecture

```
agent ── api tool ──► PackageService
                        │
        ┌───────────────┴──────────────────┐
        ▼                                  ▼
  action command                   ConnectionSupervisor
  (declarative request             (per api_package_connections row:
   template → C++ transports →      ws | longpoll; backoff reconnect;
   Lua post-processes)              on_* hook invocation)
        │                                  │
        ▼                                  ▼
   tool result                     hook return value (declarative)
                                          │ dispatch=true
                                          ▼
                        channel-arrival pipeline (card → turn → chain)
                        session key: channel:api:<package>:conn:<name>
```

Two hard boundaries:

1. **C++ transports, Lua decides.** Lua builds no URLs. Request/connection URLs and headers come
   from templates interpolated C++-side, where a missing state key is a hard error, not an empty
   string flowing into a truthy language.
2. **Dispatch is a return value, not a function call.** Hooks declare *what should be prompted*;
   the framework runs the chain outside the sandbox. No imperative chain-starting from Lua.

## Decision log

The ratified decisions, one line of rationale each. Changing any of these means editing this file.

- **D1 — Transport split.** Actions may declare a primary request (method/URL/header/body
  templates, state+args interpolation); C++ executes it with uniform timeout/retry/mask logging;
  Lua post-processes the response. Sandbox `ctx.http` exists for secondary calls (pagination,
  lookups). *Rationale: the `api_base` bug family — URL assembly in Lua where `""` is truthy.*
- **D2 — The sandbox contract is the product.** One entry point `run(ctx)` per script; validated
  contexts; explicit globals whitelist; registry-side lint at upload; a template package as
  reference. *Rationale: the #15/#16 seam family — six bugs at junctions no test exercised.*
- **D3 — The supervisor owns lifecycle.** Backoff reconnect on close *and* failed initial connect;
  `ALOG` on every state transition; `on_connect` re-runs on every (re)connect and returns the
  frames to send (idempotent resubscription for free). #60 is this component's negative spec.
  EmailAdapter migrates onto the supervisor as its first internal consumer — fix it once.
- **D4 — Hook contracts are declarative.** `on_message` returns `{dispatch, prompt, card,
  dedup_key}`; `on_connect` returns `{send = {frames}}`; `on_disconnect` advisory. Event → card →
  chain — the channel `ProcessMessage` shape expressed in Lua.
- **D5 — Re-entrancy guard on dispatch.** One dispatch per event; no passive dispatch from within
  a chain turn; per-package cooldown (`dispatch_cooldown_ms`, default 10 000); same-package events
  arriving during one of its own turns queue (cap 8, then drop + `ALOG_WARN`).
  *Rationale: two automated reactors in a loop is a real failure mode, observed Sept 2.*
- **D6 — Secrets are typed state fields.** `state_schema` fields may be `{secret: true}`: masked in
  `prompt_logs` and tool results, excluded from export/upload, interpolated into requests at
  transport time only. **State never leaves the box.**
- **D7 — One registry, typed manifests.** Manifest v1 gains `kind: "sop" | "api_package"` (agent
  transfer later). One pipeline for all kinds: lint → canonicalize → content hash → listing record.
  Registry updates refuse to clobber `locally_modified` packages. Enable/disable applies at runtime
  (reload connections) — no daemon restart.
- **D8 — Grammar with reserved words.** `package, command, connection, download, upload, enable,
  disable, status` are reserved; package names validated against them at create *and* at registry
  upload. `api status` is the health surface the email saga proved we need.
- **D9 — Dispatch rides the channel-arrival pipeline.** A hook dispatch creates an arrival
  (`channel_type: "api"`, session key `channel:api:<package>:conn:<name>`), with card +
  `reply_instructions` per `docs/channel.md`; dedup by event id. One card pipeline, not two.
- **D10 — Three tables.** `api_packages`, `api_package_commands`, `api_package_connections`
  (schemas in SCHEMAS.md). Hooks are commands (`kind: "hook"` + `event`), referenced by
  connections — uniform authoring, one place for logic.
- **D11 — Dispatch is a commit, not a call.** Effects within a causal step may be imperative
  (`ctx.http`, `ctx.conn.send`); a dispatch creates a *new* causal step (turn → tools → events →
  hooks), so it commits atomically via the return value. A hook that throws or is terminated by
  the instruction limit never dispatches — fail-closed. Multi-dispatch, if ever needed, extends
  the contract declaratively (`dispatches = {…}`), never an imperative chain-start API.
  *Rationale: cascade triggers are the one effect worth making atomic — partial execution must
  not leave prompts in flight with no provenance. (Ratified Sept 4 after Melvin challenged the
  imperative alternative; his framing: "once the agent is dispatched, this function is done
  running — everything should have been staged beforehand.")*
- **D12 — Bulk payloads go to disk.** `ctx.fs`: package-scoped filespace (root
  `<state>/api-files/<package>/`, slug names, quota `files_quota_mb` default 256, survives
  restarts, dies with the package). Return contract gains `files = [{name, path, bytes,
  content_type?}]` — framework-verified, surfaced in tool results and cards. Corollary egress
  rule: strings over ~16 KB in any return / tool result / `prompt_logs` are truncated with a
  size marker; files are the only route for bulk data. `b64.encode/decode` join the whitelist.
  *Rationale: base64 through the prompt path = megabytes into prompt_logs (the token-leak
  family) and nothing the agent can act on — the agent wants the path, not the pixels.*
- **D13 — Per-agent enablement overlay.** `api_package_agents` (agent_id × package_id ×
  enabled); absent row inherits package default (on); an agent toggling writes its row;
  effective enablement = package.enabled AND (no row OR row.enabled). The framework still owns
  aggregate limits — tool-schema exposure, which passive connections establish, agent-scoped
  state stores. *Rationale: resolves daemon-global vs agent-scoped with data, not two systems.*

## Build order

- **(a)** Connection supervisor in C++ (#60's fix); EmailAdapter rewritten on top as first consumer.
- **(b)** Entity schema + migrations; manifest v1 `kind` extension (registry rename lands here).
- **(c)** Sandbox contract + minimal Alpaca reference package (token set, `portfolio list`,
  `create_order`, one WS threshold connection). The reference package is the acceptance test —
  the moltmock lesson: the rig finds what the schema can't.
- **(d)** Registry upload/validation/lint.
- **(e)** Agent-side CRUD (`api` tool verbs) + GUIDE.md published on GitHub, linked from the tool
  description.

## Resolutions & remaining open questions

*(Sept 4 pass with Melvin — the live set is now small.)*

- **Secret encryption at rest — omitted by decision.** v1 keeps plaintext-in-PG,
  egress-masked everywhere — identical posture to channel config rows; the only place it would
  really matter is package state, and the scope cost outweighs the gain for now. Revisit
  deliberately, together with channel configs.
- **`ctx.http` budgets — settled for v1.** Cap 5 secondary calls per invocation; token-bucket
  rate limiting only if real packages demand it.
- **Agent scoping — RESOLVED (D13).** Per-agent enablement overlay; framework-owned aggregate
  limits.
- **Registry trust — RESOLVED minimal.** `api download` recomputes the canonicalized content
  hash and refuses on mismatch with the registry's stored digest for that version (trust model:
  the configured registry is trusted; the hash catches corruption, tampering in transit, stale
  mirrors). Full signing later, with agent transfer.
- **Webhook ingress (#61)** — parked; schema anticipated (`api_package_connections` stays
  authored-only; webhook endpoints would be a v2 connection type).
- **Channel migration onto packages** — deliberate future, not v1. Email *is* conceptually "the
  AgentMail package" — design stays compatible, migration unhurried.

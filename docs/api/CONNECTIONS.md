# API Packages — Connections & the Supervisor

The ConnectionSupervisor is the framework's core C++ component (blueprint D3). Issue #60 (the
EmailAdapter lifecycle bug) is its spec-by-negation: every failure mode there — silent initial
connect failure, no reconnect on remote close, unreachable poll fallback, watchdog that resets its
own error counter — is a behavior this component must not have. The EmailAdapter will be rewritten
on top of the supervisor as its first internal consumer.

## Scope

One supervisor instance owns every active connection (package.enabled AND connection.enabled).
Each `api_package_connections` row gets a worker: a `websocket` client or a `longpoll` poller.
Lua never holds sockets; hooks receive `ctx.conn.send/close` affordances only.

## State machine

```
disabled ──enable──► connecting ──ok──► connected ──close/fail──► backoff ─┐
    ▲                    │                                            │
    │                    └──────── transport error ◄──────────────────┘
    │                                   │
    │                                   ▼
    └──disable── …                (retry after backoff) ──► connecting …
                     config error (template interpolation fails, bad type)
                                   ──► error  (terminal: loud, surfaced, no retry
                                               until re-enabled or package edited)
```

- **Backoff**: exponential, base 1 s, ×2, cap 60 s, full jitter. Counter resets on a successful
  connect **plus** first received event. Longpoll cycles the same machine: each poll iteration is
  "connected"; poll failure → backoff.
- **Every transition logs**: `ALOG_INFO [api-conn] package=<p> conn=<c> <from>→<to> reason=<r>`.
  Connect attempts, failures (with reason), closes, reconnects, fallbacks. If a state change can
  happen, it logs — the negative of #60.
- **Config errors are terminal and loud**: a template referencing a missing state key (e.g. token
  not yet set) puts the connection in `error` with `ALOG_ERROR` naming key and template. It does
  not retry-loop on a deterministic failure. Setting the missing state (e.g. `api alpaca token
  set …`) clears `error` connections of that package (re-evaluated on state writes).

## Hook wiring

`hooks = {on_connect: "<cmd>", on_message: "<cmd>", on_disconnect: "<cmd>", on_error: "<cmd>"}`
— command *names* from `api_package_commands` (kind=hook, matching event). Missing wiring is fine:
an unwired event is dropped (debug-logged). Contracts in [SANDBOX.md](SANDBOX.md).

- **on_connect** fires on every successful (re)connect; returned frames are sent in order. This is
  the resubscribe pattern — connections drop; subscriptions are rebuilt by design.
- **on_message** fires per inbound frame (ws) / per poll response with new content (longpoll).
  `dispatch = true` → arrival (below). Non-dispatch returns end the event.
- **on_disconnect** / **on_error** are advisory; the supervisor schedules reconnect regardless.

## Dispatch → the channel-arrival pipeline (D9)

A dispatching `on_message` becomes a channel arrival — the same pipeline every channel uses
(`docs/channel.md`):

- `channel_type: "api"`, `channel_name: <package>`, session key `channel:api:<package>:conn:<name>`
- Card fields: package name, connection name, hook `card` fields, `reply_instructions` pointing at
  the package's action commands ("respond via `api <package> <command>`; text replies are not
  delivered")
- Dedup: arrival event ids (where the transport provides them) + hook `dedup_key`
- The agent's reply path is ordinary tool invocation — active commands are always allowed from a
  chain turn (SANDBOX.md, re-entrancy).

This gives api packages session continuity, cards, and monitoring semantics for free — one card
pipeline, not two.

## Longpoll specifics

- `poll = {method, params_template, cursor_path, interval_s}`; `interval_s` bounds the loop
  (respect server `Retry-After` when present).
- Cursor: extracted via `cursor_path` from each response, persisted in package state under
  `_cursor.<conn>` **immediately** — survives daemon restarts (moltbook watermark lesson: a
  restart must never reprocess a window it has already seen; and a reset cursor below reality is
  the mirror bug — treat cursor regression as suspicious and log it).
- Self-send guard: if the poller can observe objects the agent itself created (e.g. via an action
  command), the package's on_message must filter them (compare against a `_last_action_<cmd>`
  marker or the provider's actor field). Otherwise actions echo back as events forever.

## Teardown

- `api disable <package>` (or connection disable, or daemon shutdown): stop transports, fire
  `on_disconnect` (best-effort, ≤ 5 s), join workers, drop queues. Shutdown must never hang —
  the SIGTERM-hang incident is the reference failure.
- Enable re-applies at runtime: read rows → start workers. **No daemon restart** (the write-through
  cache lesson: config changes must not require boot to take effect).

## Health surface

`api status [<package>]` (TOOL.md) reports per connection:

- `state` (connecting | connected | backoff | error | disabled)
- `last_event_age_s`, `consecutive_failures`, `last_error`
- package-level: enabled, version, locally_modified, dispatch cooldown, queue depth

This is the observability the email saga lacked: "is the WS up?" must be a one-command answer,
forever.

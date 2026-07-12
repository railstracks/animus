# Ticket 034 — IRC Interface Client + Configuration

**Priority:** P2 (high leverage channel proof, low integration cost)  
**Status:** Open  
**Dependencies:** Ticket 007 (Interface Management API), Ticket 025 (Channel Architecture)
**Updated:** 2026-05-05

## Summary

Implement a first concrete external chat interface using IRC.  
This ticket covers:

1. IRC client connection lifecycle.
2. Config model + admin API integration for IRC interface instances.
3. Inbound message routing into the kernel session system.
4. Outbound reply support to IRC channels/users.
5. Auth support including server password (`PASS`) and channel keys (`JOIN #chan key`).

IRC is intentionally chosen as the first low-dependency, easy-to-test multi-user channel.

## Motivation

1. We need a real-world channel beyond admin WebSocket chat.
2. IRC is protocol-simple and immediately testable.
3. It validates the interface architecture before higher-complexity platforms (Slack/Discord/etc.).
4. It forces us to handle auth, reconnect, message parsing, and mapping channel events into sessions.

## Scope

### In Scope

1. Single-server IRC client interface with configurable identity and channels.
2. Server authentication:
   - `PASS` for server password-protected networks.
   - Optional SASL PLAIN (if enabled in config).
3. Channel join configuration:
   - join one or more channels on connect.
   - optional per-channel key/password for invite/keyed channels.
4. Inbound event support:
   - `PRIVMSG` (channel + direct message)
   - `NOTICE` (optional ingest policy flag)
   - join/part/nick tracking needed for routing metadata
5. Outbound messaging:
   - send to channel
   - send direct message to nick/user
6. Connection reliability:
   - reconnect backoff
   - PING/PONG keepalive
7. Interface config CRUD + persistence through existing interfaces API.
8. Secret masking in API responses (`password`, `token`, `secret` fields).

### Out of Scope

1. Full IRCv3 feature matrix (tags, CAP ecosystem beyond SASL minimum).
2. DCC/file transfer.
3. Bouncer management (ZNC-specific behaviors).
4. Rich moderation/admin command execution.
5. Multi-server federation in one interface instance.

## Target Config Shape (Interface Config)

```json
{
  "host": "irc.example.net",
  "port": 6697,
  "tls": true,
  "verify_tls": true,
  "server_password": "optional-pass",
  "nick": "animus_bot",
  "username": "animus",
  "realname": "Animus Agent",
  "sasl": {
    "enabled": false,
    "mechanism": "PLAIN",
    "username": "animus_bot",
    "password": "optional-sasl-password"
  },
  "channels": [
    { "name": "#animus", "key": "" },
    { "name": "#ops", "key": "chan-key-optional" }
  ],
  "respond_to_notices": false,
  "allowed_dm_users": [],
  "reconnect": {
    "enabled": true,
    "initial_delay_ms": 1000,
    "max_delay_ms": 60000
  },
  "rate_limit": {
    "messages_per_10s": 8
  }
}
```

### Required Fields

- `host`
- `nick`
- At least one channel or explicit DM-only mode flag.

## Design Plan

### Phase 1: Interface Type + Validation

1. Add/extend interface type validation for `"irc"` in admin config handling.
2. Validate required fields and types:
   - host string
   - port range
   - channel names begin with `#`/`&` where applicable
3. Ensure secret fields are masked in API read responses.

### Phase 2: IRC Client Runtime

1. Create IRC client component:
   - TCP/TLS connect
   - send `PASS` (if configured), `NICK`, `USER`
   - capability negotiation for SASL when enabled
2. Handle core protocol events:
   - numeric welcome/errors
   - `PING` -> `PONG`
   - `PRIVMSG` parsing
3. Join configured channels after registration (`001` welcome).
   - use channel key where provided.

### Phase 3: Kernel Event Bridging

1. Translate inbound IRC events to `IncomingEvent`:
   - connector: `irc:<interface_name>`
   - conversation id: channel or dm target
   - thread id: optional empty/default
   - metadata: nick, hostmask, channel, raw command
2. Route through `SessionManager` as regular inbound events.

### Phase 4: Outbound Reply Path

1. Add interface send path so chain responses can be emitted to IRC destination.
2. Channel message splitting for IRC line limits (512-byte frame constraints).
3. Basic send rate limiting to avoid flood-kick.

### Phase 5: Operability

1. Reconnect with exponential backoff.
2. Connection status surfaced in `/api/v1/interfaces` (`connected`, `last_event_unix_ms`, last error).
3. Clean shutdown/disconnect behavior.

## API and UI Touchpoints

1. Reuse existing interface endpoints:
   - `GET /api/v1/interfaces`
   - `GET /api/v1/interfaces/{name}`
   - `PATCH /api/v1/interfaces/{name}`
   - `POST /api/v1/interfaces/{name}/enable`
   - `POST /api/v1/interfaces/{name}/disable`
2. Add IRC form fields to Interfaces UI for creation/editing.
3. Keep secrets write-only/masked in read payloads.

## Security Considerations

1. Mask `server_password` and `sasl.password` in API responses.
2. Optional DM allowlist (`allowed_dm_users`) to reduce abuse.
3. Validate outbound destinations against configured channel/user policy.
4. Avoid command injection via raw IRC command passthrough (do not expose raw send).

## Acceptance Criteria

- [ ] Interface type `irc` can be configured and persisted via interfaces API.
- [ ] IRC client connects using host/port/tls settings.
- [ ] Server password auth (`PASS`) works when configured.
- [ ] Optional SASL PLAIN login works when enabled.
- [ ] Configured channels are joined on connect (with channel keys when set).
- [ ] Inbound `PRIVMSG` events are routed into sessions.
- [ ] Outbound responses can be sent to channel and DM targets.
- [ ] PING/PONG keepalive prevents idle disconnect.
- [ ] Reconnect logic works after dropped connection.
- [ ] Secrets are masked in interface read endpoints.
- [ ] Admin/UI surfaces connection status and last error.
- [ ] Tests cover config validation and protocol parsing basics.

## Testing Plan

1. Unit tests:
   - IRC line parser (`prefix`, `command`, params, trailing)
   - config validation for IRC schema
   - message splitter under 512-byte constraint
2. Integration tests:
   - startup with IRC interface enabled
   - simulated connect/register/join flow
   - inbound message -> session creation path
3. Manual smoke test:
   - connect to test network with passworded server
   - join keyed channel
   - send/receive roundtrip

## Suggested File Targets

- `src/kernel/interfaces/IrcInterface.*` (new)
- `include/animus_kernel/...` interface declarations as needed
- `src/kernel/admin/AdminServer.cpp` (config validation + status wiring)
- `admin-ui/src/views/InterfacesView.vue` (IRC config form)
- `admin-ui/src/i18n/locales/en.ts` (labels only; other locales updated in batch later)

## Notes

1. IRC server password support is first-class in this ticket (`PASS` command).
2. Channel join keys are also first-class (`JOIN #channel key`).
3. This ticket should produce a reusable template for future channel connectors (Slack/Telegram/etc.).


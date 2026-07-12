# Ticket 007 — Interface Management API

**Priority:** P1
**Status:** Open
**Dependencies:** Ticket 001

## Summary

REST API endpoints for managing agent interfaces/connectors (Slack, Discord, WhatsApp, webhooks, etc.).

## Scope

- `GET /api/v1/interfaces` — List all configured interfaces with status (enabled/disabled, connected/disconnected)
- `GET /api/v1/interfaces/:name` — Single interface details (type, config, status, last event timestamp)
- `PATCH /api/v1/interfaces/:name` — Update interface configuration (credentials, channel mappings, etc.)
- `POST /api/v1/interfaces/:name/enable` — Enable/start an interface
- `POST /api/v1/interfaces/:name/disable` — Disable/stop an interface
- Credential fields are write-only (returned as masked in GET responses)

## Acceptance Criteria

- [ ] Interface list reflects all loaded connector modules
- [ ] Enable/disable toggles the connector lifecycle (connect/disconnect)
- [ ] Config updates are validated per interface type before applying
- [ ] Sensitive fields (tokens, API keys) masked in responses (e.g., "sk-...****")
- [ ] Changes persist to config file

## Notes

- Interface names should match the module/connector naming convention
- Enable/disable while the agent is processing events from that interface: define behavior (drain, force)

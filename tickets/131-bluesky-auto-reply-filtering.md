# Ticket 131: Bluesky Auto-Reply Filtering

**Created:** 2026-07-29  
**Status:** Open  
**Branch:** `dev`

## Problem

The Bluesky adapter currently auto-replies to every incoming notification (mentions, replies, quotes). When using Bluesky as a searchable information source rather than a chatbot surface, this creates unwanted public replies and dead-end sessions.

## Goal

Add configurable reply filtering to the Bluesky channel adapter. Messages that don't pass the filter should be dropped **before session creation** — no session, no turn, no reply.

## Config Fields

Three new fields in the Bluesky channel config (`state->config`):

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `auto_reply` | bool | `true` | Master switch. If false, no Bluesky notifications create sessions or replies. |
| `reply_to_all` | bool | `true` | If auto_reply is on, reply to all incoming notifications. Current behavior. |
| `reply_to_users` | string[] | `[]` | Allowlist of Bluesky handles. If auto_reply is on and reply_to_all is off, only reply to these users. |

## Filter Logic

In `BlueskyPollLoop`, after parsing a notification and before calling `DispatchToSession`:

```
if (!auto_reply) → skip (no session, no reply)
if (reply_to_all) → dispatch
if (reply_to_users contains authorHandle) → dispatch
else → skip
```

The filter runs **before** `DispatchToSession` — skipped notifications never create a session. The `bsky_last_seen` watermark still advances past skipped notifications (they're seen, just not actioned).

## Files Affected

### Backend
- `src/kernel/ChannelManager.cpp` — `BlueskyPollLoop`: read config fields, apply filter before `DispatchToSession`
- No header changes needed (config is already `Json::Value` on `PollerState`)

### Frontend
- `admin-ui/src/views/ChannelsView.vue` — add three form fields to the Bluesky template:
  - `bluesky_auto_reply` checkbox (default checked)
  - `bluesky_reply_to_all` checkbox (default checked, disabled if auto_reply is off)
  - `bluesky_reply_to_users` text field (comma-separated handles, disabled if auto_reply is off or reply_to_all is on)
- Add fields to form data, reset form, and config build/parse functions

### i18n
- All 23 locale files — add keys for the three new fields

## Backwards Compatibility

All three fields default to current behavior (`auto_reply=true`, `reply_to_all=true`). Existing channels without these fields in their config will continue to work unchanged — the code should treat missing fields as defaults.
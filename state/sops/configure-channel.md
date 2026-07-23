---
name: configure-channel
title: Configuring a Communication Channel
category: configuration
tags: [channels, discord, telegram, whatsapp, integration]
version: 1.0.0
description: >
  Procedure for setting up a new channel adapter — connector config,
  authentication, message routing, and testing.
---

# Configuring a Communication Channel

## Prerequisites

- Animus daemon running with admin API access
- Channel credentials (API tokens, bot tokens, etc.)
- Admin UI access or direct API credentials

## Steps

### 1. Create the Channel

In Admin UI → Channels → Add Channel, or via API:

- Select channel type (Discord, Telegram, WhatsApp, Slack, etc.)
- Enter a descriptive name
- Configure credentials (bot token, API key, etc.)

### 2. Configure Message Routing

- **Session routing** — How incoming messages map to sessions
  - `per_user` — One session per user (default)
  - `per_channel` — One session per channel/room
  - `per_thread` — One session per thread/topic
- **Message interval** — Minimum seconds between responses
- **Batched dispatch** — Collect messages for N seconds before responding
- **Interjection** — Allow agent to interject into active chains

### 3. Set Agent Assignment

- Assign which agent(s) handle this channel
- Configure identity preamble (agent name, description)
- Set tool availability (use `session_types` to restrict tools)

### 4. Test

1. Send a test message from the channel platform
2. Verify the message appears in the admin sessions list
3. Confirm the agent responds within the configured interval
4. Check that tool calls execute correctly
5. Verify streaming works (if platform supports it)

### 5. Monitor

- Watch daemon logs for channel-specific errors
- Check the admin sessions list for the new channel's sessions
- Verify session persistence — close and reopen the channel, confirm continuity

## Common Issues

- **No response** — Check credentials, check session routing, check agent assignment
- **Tool calls fail** — Verify `session_types` filtering includes needed tools
- **Duplicate responses** — Ensure batched dispatch is configured for high-traffic channels
- **Stale connection** — Check heartbeat configuration for WebSocket-based channels

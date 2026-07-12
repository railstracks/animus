# WhatsApp Integration — Reference Documentation

> **Target:** WhatsApp Web multi-device protocol (companion device via QR scan)
> **NOT** the WhatsApp Business API / Cloud API. This document explicitly covers the **unofficial** protocol used by WhatsApp Web, as reverse-engineered and implemented by the Baileys library.

## Why This Approach

WhatsApp Business API requires a registered Meta business account, per-message pricing, and phone number verification through their ecosystem. For an agent framework that needs to be self-hosted and multi-tenant, this is a non-starter.

The WhatsApp Web multi-device protocol allows a companion device (like WhatsApp Web in a browser) to link to an existing phone via QR code scan. Once linked, the companion device operates independently — it has its own encryption keys and can send/receive messages without the phone being online.

This is what Baileys implements, and what we're replicating in C++.

## Directory Structure

| File | Purpose |
|------|---------|
| `protocol.md` | Protocol layers, crypto, binary encoding, protobuf schemas |
| `architecture.md` | How WhatsApp integrates into Animus (SocialEventPoller, SendAutoReply, Lua adapter) |
| `risk-assessment.md` | Account ban risk, fingerprinting, ToS concerns, maintenance burden |
| `implementation-notes.md` | C/C++ dependencies, build integration, porting guide from Baileys |

## Reference Implementation

**Baileys** (`@whiskeysockets/baileys`) — TypeScript implementation of the WhatsApp Web protocol.
- GitHub: `WhiskeySockets/Baileys`
- Version analyzed: v7.0.0-rc12
- License: MIT

Baileys is the *only* maintained open-source implementation. All protocol details in these docs are derived from reading Baileys source code, not from official documentation (none exists publicly).

## Status

**Research phase.** Protocol fully analyzed, architecture scoped. Blocked on risk assessment (see `risk-assessment.md`) before committing to implementation.

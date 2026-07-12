# Ticket 054: Email Tool — Multi-Provider Accounts

**Status:** Draft  
**Priority:** Normal  
**Depends on:** Ticket 051 (Long Poll + Auto-Reply), Ticket 056 (Channels Refactor)  
**Blocks:** —  

## Summary

Email integration as a native channel type in ChannelManager. Supports multiple email accounts, each backed by one of three provider types: **AgentMail** (bidirectional API), **Transactional Email** (outbound-only via Brevo/Scaleway/etc.), and **SMTP/IMAP** (universal fallback). Inbound emails trigger session dispatch. Real-time events via WebSocket (AgentMail) or IMAP IDLE (SMTP/IMAP).

## Design Principles

1. **Account-based architecture.** Multiple email accounts, each with a provider type and credentials. One account is default. The agent specifies which account to use per action, or the default is used. This allows one agent to handle personal email (AgentMail), business notifications (TEM), and a legacy mailbox (IMAP) simultaneously.

2. **Provider abstraction.** Three provider types share a common action surface. The agent doesn't need to know which provider backs an account — it calls `email:send` or `email:list_threads` and the tool routes to the right provider.

3. **ChannelManager integration, not standalone.** Email is a channel type (`type: "email"`) managed by ChannelManager. One dispatch path, one auto-reply mechanism, one admin surface, one routing model.

4. **Reuse existing patterns.** WebSocket listener follows the `PollerState` pattern. Dispatch follows `DispatchToSession`. Auto-reply follows `SendReply` → adapter. Config follows `channel.{name}.config` in AgentConfigStore.

## Provider Types

### Type 1: AgentMail (Bidirectional API)

Full bidirectional email — send, receive, inbox management, threading, webhooks, attachments. The only provider offering email as a proper API (REST + WebSocket) rather than protocol access.

**Capabilities:** send, receive, list threads, search, attachments, real-time events, thread management, drafts

**Limitations:** Uses AgentMail's domain, not ideal for high-volume outbound (not a TEM)

**Best for:** Conversational email, agents that need their own inbox identity, bidirectional communication

### Type 2: Transactional Email (Outbound Only)

High-deliverability outbound email via providers like Brevo, Scaleway TEM, Lettermint, Rapidmail, Sweego, EmailLabs. These are SMTP replacements — not inboxes.

**Capabilities:** send, templates, delivery status, bounce handling, analytics, DKIM/SPF management

**Limitations:** No receiving, no inbox, no threading

**Best for:** Business notifications, order confirmations, password resets, marketing emails, any high-volume outbound where deliverability matters

### Type 3: SMTP/IMAP (Universal Fallback)

Standard protocol access to any mailbox. Send via SMTP, receive via IMAP. Works with Gmail, Fastmail, Mimecast, self-hosted Postfix, Exchange, anything that speaks standard protocols.

**Capabilities:** send (SMTP), receive (IMAP), list threads (IMAP folders), search (IMAP SEARCH), attachments

**Limitations:** No real-time push (IMAP IDLE is closest), no threading API (must reconstruct from headers), deliverability is the user's responsibility

**Best for:** Legacy mailbox integration, self-hosted deployments, any provider without an API

### Why these three?

- **AgentMail** is the only provider offering a proper email API with bidirectional support. No equivalent exists.
- **InboxAPI** was evaluated — MCP-only interaction model doesn't fit our architecture (we'd need to build an MCP client for one provider). If they add a REST API, they can slot in as another provider.
- **TEM providers** handle the outbound-only use case that AgentMail isn't designed for (high volume, deliverability, templates).
- **SMTP/IMAP** is the universal fallback. Every mailbox supports it. It's not pretty but it covers every deployment we can't reach with the other two.

## Account Configuration

```json
{
  "channels": {
    "email": {
      "accounts": {
        "personal": {
          "provider": "agentmail",
          "inbox_id": "inbox_abc123",
          "api_key": "${AGENTMAIL_KEY}",
          "default": true
        },
        "business": {
          "provider": "tem",
          "tem_provider": "brevo",
          "api_key": "${BREVO_KEY}",
          "from_address": "agent@company.com",
          "from_name": "AI Assistant"
        },
        "legacy": {
          "provider": "smtp_imap",
          "smtp_host": "smtp.fastmail.com",
          "smtp_port": 587,
          "imap_host": "imap.fastmail.com",
          "imap_port": 993,
          "username": "${FASTMAIL_USER}",
          "password": "${FASTMAIL_PASSWORD}",
          "from_address": "me@fastmail.com"
        }
      },
      "default_account": "personal"
    }
  }
}
```

Each account has a `provider` field (`agentmail`, `tem`, `smtp_imap`) and provider-specific configuration. One account is marked `default` — used when the agent doesn't specify an account.

## Tool Definition

The `EmailTool` (IToolHandler) exposes these actions to the agent. All actions accept an optional `account` parameter — if omitted, the default account is used. Available in `session_types = {"default"}` — excluded from consolidation sessions.

| Action | Description | Providers |
|--------|-------------|------------|
| `list_accounts` | List configured email accounts and their capabilities | All |
| `list_inboxes` | List inboxes for an account (AgentMail) | AgentMail |
| `list_threads` | List conversation threads (pagination, filtering by account/unread) | AgentMail, SMTP/IMAP |
| `get_thread` | Get all messages in a thread | AgentMail, SMTP/IMAP |
| `get_message` | Get a single message (headers, body, attachments list) | All |
| `get_attachment` | Download an attachment | AgentMail, SMTP/IMAP |
| `send` | Send an email (new conversation or reply) | All |
| `reply` | Reply to a specific message (sets In-Reply-To, References headers) | All |
| `search` | Search emails by subject, sender, body text | AgentMail, SMTP/IMAP |
| `delete` | Delete a message or thread | AgentMail, SMTP/IMAP |
| `mark_read` | Mark a message or thread as read | AgentMail, SMTP/IMAP |
| `list_drafts` | List draft emails | AgentMail |
| `create_draft` | Create a draft email | AgentMail |
| `send_draft` | Send a previously created draft | AgentMail |
| `search` | Search across emails (sender, subject, body, date range) |
| `send` | Send a new email (to, subject, body, optional attachments) |
| `reply` | Reply to a specific message (preserves thread) |
| `forward` | Forward a message to another recipient |
| `mark_read` | Mark message/thread as read |
| `delete` | Delete a message |
| `list_drafts` | List saved drafts |
| `create_draft` | Create a draft without sending |
| `send_draft` | Send an existing draft |

## Channel Integration

### Channel Type: `email`

Email channels are created through the existing Channels admin API (`POST /api/{instance}/channels`) with `type: "email"`. Config validation in `ChannelManager::ValidateConfig` ensures required AgentMail fields are present.

```json
{
  "name": "agentmail-main",
  "type": "email",
  "enabled": true,
  "config": {
    "backend": "agentmail",
    "api_key": "am-...",
    "inbox_id": "inbox_abc123",
    "event_source": "websocket",
    "polling.wait": 25,
    "dispatch_rules": [
      {
        "match": { "thread_state": "existing" },
        "action": "resume_session",
        "prompt": "A new reply arrived in your email conversation with {sender}:"
      },
      {
        "match": { "thread_state": "new" },
        "action": "new_session",
        "prompt": "You received a new email from {sender} with subject '{subject}':"
      }
    ],
    "agent_id": "default"
  }
}
```

### ReplyTarget Extension

Add email-specific fields to `ReplyTarget`:

```cpp
struct ReplyTarget {
    // ... existing fields ...
    // Email-specific
    std::string email_thread_id;    // AgentMail thread ID for reply threading
    std::string email_message_id;   // Message ID being replied to
    std::string email_account_id;   // Which inbox/account to send from
};
```

### ChannelManager Dispatch Flow

```
AgentMail WebSocket → EmailPollerState → ChannelManager::DispatchToSession
                                                      ↓
                                              Agent session (LLM)
                                                      ↓
                                         Plain text response → SendAutoReply
                                                      ↓
                                    ChannelManager::SendReply → AgentMail REST send
```

### WebSocket Listener

Follows the `PollerState` pattern (like VK/Telegram Long Poll). A background thread per email channel:

1. Connect to AgentMail WebSocket with auth
2. Listen for `message.received` events
3. Parse event → resolve dispatch rule → build prompt → call `m_dispatch`
4. Track `last_event_id` for resume after reconnection
5. Exponential backoff on connection failures (same as VK Long Poll)

### Prompt Building

Follows the established pattern — includes channel identification and auto-reply hint:

```
New email from {sender} with subject '{subject}':

{body_plain_text}

You are responding via email. Respond naturally — your reply will be sent automatically. Do NOT use the social tool to reply.
```

### SendReply for Email

`ChannelManager::SendReply` dispatches to a new `SendEmailReply` method:

1. Look up channel config for API key + inbox ID
2. Call AgentMail REST API: `POST /v0/inboxes/{inbox}/messages/send`
3. Include `thread_id` for threading (if replying to existing thread)
4. Log success/failure

### Auto-Reply Session Type

Auto-reply sessions use `session_type = "email:chat"`. The EmailTool is available in these sessions (agent can use tool actions for richer operations). The SocialTool is excluded (same as VK chat/wall) — the agent produces plain text for auto-reply.

### Thread Resolution

AgentMail provides native thread IDs. The routing key is `thread:{agentmail_thread_id}`. Session key: `email:chat:{channel_name}:{thread_id}`.

- Known thread → existing session resumed (agent sees conversation history)
- New thread → new session created

### Dispatch Rules

Configurable per channel. Simple pattern matching on inbound email metadata:

| Match Field | Description |
|-------------|-------------|
| `thread_state` | `"new"` or `"existing"` |
| `from` | Sender email address (exact match) |
| `subject_contains` | Substring match on subject |

Variables `{sender}`, `{subject}`, `{thread_id}` available in prompt templates.

## HTML Handling

AgentMail returns both HTML and plain text. Default: **plain text only** for agent consumption. When plain text is missing, strip HTML tags. Raw HTML is never sent to the LLM — it wastes tokens and confuses context. Config option `prefer_html: true` available for future use.

## C++ Classes

```
src/kernel/email/
├── EmailTool.h/.cpp              # IToolHandler — 14 agent-facing actions
├── AgentMailClient.h/.cpp        # AgentMail REST API client (libcurl)
├── AgentMailWebSocket.h/.cpp     # WebSocket event listener
└── EmailConfig.h                 # Config parsing, dispatch rule matching
```

No separate adapter base class — AgentMail is the only backend. If IMAP/SMTP is added later, we introduce the adapter abstraction then.

### ChannelManager Changes

- `ValidateConfig`: add `type == "email"` validation (requires `api_key`, `inbox_id`)
- `StartChannel`: add `type == "email"` → `StartEmailChannel()`
- `SendReply`: add `channel_type == "email"` → `SendEmailReply()`
- New method: `StartEmailChannel()` — creates PollerState with WebSocket listener
- New method: `EmailLongPollLoop()` — WebSocket event loop (or polling fallback)
- New method: `ProcessEmailMessage()` — parses event, matches dispatch rules, builds prompt
- New method: `SendEmailReply()` — sends via AgentMail REST API

### ReplyTarget Changes

- Add `email_thread_id`, `email_message_id`, `email_account_id` fields

### Dependencies

| Library | Purpose | Notes |
|---------|---------|-------|
| **libcurl** | AgentMail REST API | Already used for VK/Telegram HTTP |
| **OpenSSL** | TLS for WebSocket | Already linked |

No new dependencies.

## Admin API

Email channels use the existing Channels admin API:

```
POST   /api/{instance}/channels                    -- Create (type: "email")
GET    /api/{instance}/channels                    -- List (includes email)
GET    /api/{instance}/channels/{name}             -- Get
PUT    /api/{instance}/channels/{name}             -- Update config
DELETE /api/{instance}/channels/{name}             -- Delete
POST   /api/{instance}/channels/{name}/restart     -- Restart WebSocket
GET    /api/{instance}/channels/{name}/status      -- Connection status
```

No separate email-specific endpoints needed. The ChannelsView.vue admin UI renders the config form based on channel type.

## Implementation Phases

### Phase 1: Core Infrastructure
- [ ] EmailTool (IToolHandler) with all 14 action stubs
- [ ] AgentMailClient — REST API client (send, list threads, get messages, search)
- [ ] Register EmailTool in AgentKernel
- [ ] ReplyTarget extension (email fields)

### Phase 2: Channel Integration
- [ ] ChannelManager: ValidateConfig, StartEmailChannel, SendEmailReply
- [ ] EmailLongPollLoop — WebSocket event listener with PollerState
- [ ] ProcessEmailMessage — event parsing, dispatch rule matching, prompt building
- [ ] DispatchToSession integration (routing key = thread ID)

### Phase 3: Polish
- [ ] Attachment handling (upload, download, size limits)
- [ ] Drafts (create_draft, send_draft, list_drafts)
- [ ] Multi-account support (multiple email channels)
- [ ] Delivery status tracking (bounces, complaints)
- [ ] Polling fallback (REST-based, when WebSocket unavailable)

## Open Questions

1. **WebSocket library:** libcurl doesn't natively support WebSocket. Options: (a) lightweight WS library like libwebsockets, (b) raw OpenSSL + HTTP upgrade, (c) polling-only fallback for v1 with WebSocket deferred. Recommendation: **polling fallback for Phase 1-2**, WebSocket in Phase 3.
2. **Attachment handling in auto-reply:** If an inbound email has attachments, should they be included in the agent prompt? Or fetch-on-demand only? Recommendation: list attachment names in prompt, fetch content on demand via `get_attachment`.
3. **HTML email body:** Covered above — plain text default, strip HTML when missing.

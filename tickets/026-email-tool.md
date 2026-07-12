# Ticket 026 — Email Tool

**Priority:** P2 (large testing surface, deferred from 023)
**Status:** Open
**Dependencies:** Ticket 021 (Tool Calling and Reasoning Mode), Ticket 023 (shared HttpClient)
**Updated:** 2026-05-03

## Summary

Implement a composite email tool supporting multiple backends: AgentMail API (first-class integration for agent-specific email), SMTP (traditional sending), and IMAP/POP3 (traditional receiving). Email is universal enough to warrant its own ticket with dedicated testing infrastructure.

## Motivation

Email is one of the most universal communication channels. However, traditional SMTP providers increasingly restrict automated/agent usage with filters scanning for agentic activity. AgentMail.to and similar emerging services in the AI ecosystem provide a purpose-built alternative. We should support both — traditional protocols for compatibility, and agent-native APIs as first-class integrations.

## Backends

### AgentMail API (first-class)
- Purpose-built for AI agent email communication
- REST API — no SMTP/IMAP needed
- Supports sending, receiving (via webhooks), inbox management
- Primary integration target

### SMTP (traditional sending)
- Universal compatibility
- Requires configured SMTP server credentials
- Subject to provider anti-automation filters (Gmail, Outlook, etc.)
- May need app-specific passwords or relay services

### IMAP/POP3 (traditional receiving)
- Inbox listing, message retrieval, search
- IMAP preferred (server-side folders, partial fetch, IDLE for push)
- POP3 as fallback (simpler, download-only)
- Requires listener setup for incoming mail notifications

## Tools

### `email` (composite)

```json
{
    "name": "email",
    "description": "Send and manage email. Supports AgentMail API, SMTP, and IMAP/POP3 backends.",
    "parameters": {
        "action": {
            "type": "string",
            "enum": ["send", "list_inboxes", "list_messages", "read_message", "draft", "search"],
            "description": "Email action to perform"
        }
    }
}
```

**Action-specific parameters:**

**`send`** — Send an email:

```json
{
    "to": {"type": "string", "description": "Recipient address"},
    "subject": {"type": "string", "description": "Subject line"},
    "body": {"type": "string", "description": "Plain text body"},
    "html": {"type": "string", "description": "Optional HTML body (multipart alternative if provided)"},
    "from": {"type": "string", "description": "Sender address (must be configured identity, defaults to agent primary)"},
    "reply_to": {"type": "string", "description": "Reply-to override"},
    "attachments": {"type": "array", "description": "File paths to attach (future scope)"}
}
```

**`list_inboxes`** — List available mailboxes:

```json
{
    "provider": {"type": "string", "description": "Email backend to query (defaults to primary)"}
}
```

**`list_messages`** — List messages in a mailbox:

```json
{
    "mailbox": {"type": "string", "description": "Mailbox name (e.g. 'INBOX', 'Sent')"},
    "page": {"type": "integer", "description": "Page number (default 1)"},
    "per_page": {"type": "integer", "description": "Results per page (default 25)"},
    "provider": {"type": "string", "description": "Email backend to query"}
}
```

**`read_message`** — Read a specific message:

```json
{
    "message_id": {"type": "string", "description": "Message ID to read"},
    "provider": {"type": "string", "description": "Email backend to query"}
}
```

**`search`** — Search messages:

```json
{
    "query": {"type": "string", "description": "Search query"},
    "mailbox": {"type": "string", "description": "Mailbox to search (default: all)"},
    "provider": {"type": "string", "description": "Email backend to query"}
}
```

**`draft`** — Compose a draft (future scope):

```json
{
    "to": {"type": "string"},
    "subject": {"type": "string"},
    "body": {"type": "string"}
}
```

## Implementation notes

- Backend abstraction: `IEmailProvider` interface with `AgentMailProvider`, `SmtpProvider`, `ImapProvider` implementations
- AgentMail API: REST calls via shared HttpClient (from Ticket 023)
- SMTP: libcurl or dedicated SMTP library for sending
- IMAP: dedicated IMAP library or libcurl for receiving; IDLE command for push notifications
- POP3: simpler fallback for receiving (no server-side state)
- Inbox webhook listener (AgentMail): registers webhook URL, kernel receives incoming mail as events
- IMAP IDLE listener: push notifications for new mail on traditional accounts
- Sender identity enforcement: the agent can only send from configured addresses (no spoofing)
- Rate limiting: configurable per hour (default 10 for sending)
- Pagination: all list operations support page/per_page parameters

## Testing Infrastructure

This ticket requires significant test infrastructure:
- Test SMTP server (e.g. MailHog or Mailpit) for sending tests
- Test IMAP/POP3 server for receiving tests
- AgentMail test account setup
- Multiple provider credential configuration

This is why the ticket is separated — iterative testing across multiple backends is inherently time-consuming.

## Acceptance Criteria

- [ ] `IEmailProvider` abstract interface defined
- [ ] `AgentMailProvider` implements send, list, read via AgentMail API
- [ ] `SmtpProvider` implements send via SMTP
- [ ] `ImapProvider` implements list_inboxes, list_messages, read_message, search via IMAP
- [ ] `Pop3Provider` implements basic receiving via POP3 (fallback)
- [ ] Email `send` enforces sender identity (no spoofing)
- [ ] Email `send` rate-limited per hour
- [ ] Pagination on list_messages
- [ ] AgentMail webhook listener receives incoming mail as kernel events
- [ ] IMAP IDLE listener pushes incoming mail notifications
- [ ] Tool registered in ToolRegistry and wired through ChainRunner

## Notes

- AgentMail.to should be treated as a first-class integration, not just another SMTP relay. Its API provides structured operations (inbox listing, search, webhooks) that go beyond what SMTP/IMAP offer.
- Traditional providers' anti-automation filters are a real constraint. AgentMail avoids this by design.
- Attachment support is future scope — listed in the schema for forward compatibility but not implemented initially.
- Search across providers: different backends have different search capabilities. The tool should expose a unified interface but document limitations.
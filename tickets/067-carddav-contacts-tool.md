# Ticket 067 — CardDAV Contacts Tool

**Created:** 2026-06-05
**Status:** Draft
**Priority:** P2
**Depends on:** None (standalone tool, shares Radicale with ticket 066)

## Summary

Add a built-in Animus tool that provides contact management via the CardDAV protocol. Like the CalDAV tool (066), it operates in **internal** mode (embedded Radicale server, shared with the calendar tool) and **external** mode (connect to any CardDAV server). The tool gives the agent people awareness — knowing who it talks to, how to reach them, and what channels they use. Critically, CardDAV contacts can carry social/messaging IDs (`IMPP`, `X-SOCIALPROFILE`), enabling the agent to unify a person's identities across platforms.

## Motivation

Agents interact with people across multiple channels — WhatsApp, Discord, Telegram, Slack, email. Currently, each channel knows its own users independently. There's no unified "who is this person?" layer. CardDAV solves this:

- **Identity unification** — Thomas Tolenaar on WhatsApp, Thomas on Slack, and thomas@revicoat.com on email are the same person. A vCard can carry all these IDs in a single record.
- **Channel resolution** — When the agent needs to reach someone, it can look up which channels are available for that person.
- **Contact enrichment** — Meeting someone on one channel creates a contact; the agent can add channel IDs as it discovers them.
- **Session linking** — The agent can link disparate sessions (WhatsApp, Discord, email) to the same contact, creating a unified conversation history per person.
- **People awareness** — The agent knows who it knows, with structured data: names, organizations, roles, phone numbers, email addresses, social profiles.

This is the missing identity layer in Animus. Without it, every channel is an isolated silo. With it, the agent has a people graph.

## Architecture

### Shared Radicale Instance

CardDAV shares the Radicale subprocess with the CalDAV tool (ticket 066). Radicale serves both protocols on the same port. One process, two collections (calendar + address book).

```
┌─────────────────────────────────────────────────┐
│                  Animus Agent                    │
│                                                  │
│         ┌─────────────┐  ┌──────────────┐        │
│         │ CalDAV Tool │  │ CardDAV Tool │        │
│         └──────┬──────┘  └──────┬───────┘        │
│                │                │                 │
│                └───────┬────────┘                │
│                        │                          │
│                 ┌──────┴──────┐                   │
│                 │  Radicale   │                   │
│                 │  :5232      │                   │
│                 │             │                   │
│                 │  /animus/   │ ← calendar        │
│                 │    calendar │                   │
│                 │  /animus/   │ ← address book    │
│                 │    contacts │                   │
│                 └─────────────┘                   │
│                                                  │
└─────────────────────────────────────────────────┘
```

### vCard Social ID Mapping

This is the key feature. vCard 4.0 (RFC 6350) and its extensions provide structured fields for messaging and social accounts:

| vCard Property | Purpose | Example |
|---------------|---------|---------|
| `IMPP` | Instant messaging protocol + handle | `IMPP;TYPE=work;xmpp:user@jabber.org` |
| `X-SOCIALPROFILE` | Social media profile URL | `X-SOCIALPROFILE;TYPE=linkedin:https://linkedin.com/in/username` |
| `TEL` | Phone numbers (WhatsApp uses phone JIDs) | `TEL;TYPE=cell:+31612345678` |
| `EMAIL` | Email addresses | `EMAIL;TYPE=work:thomas@revicoat.com` |
| `URL` | General URLs (websites, profiles) | `URL;TYPE=home:https://example.com` |
| `NICKNAME` | Display names / handles | `NICKNAME:ThomasT` |
| `ORG` | Organization | `ORG:Revicoat` |
| `ROLE` | Professional role | `ROLE:Partner` |

The tool maps Animus channel identifiers to vCard properties:

| Animus Channel | vCard Mapping | Lookup Logic |
|---------------|---------------|-------------|
| WhatsApp | `TEL;TYPE=cell:+31612345678` | Phone number → WhatsApp JID |
| Telegram | `IMPP;TYPE=telegram:@username` | Telegram username |
| Discord | `X-SOCIALPROFILE;TYPE=discord:discord://users/1234` | Discord user ID |
| Slack | `X-SOCIALPROFILE;TYPE=slack:workspace/user` | Slack workspace + user |
| Email | `EMAIL;TYPE=work:user@example.com` | Email address |
| Bluesky | `X-SOCIALPROFILE;TYPE=bsky:handle.bsky.social` | Bluesky handle |
| VK | `X-SOCIALPROFILE;TYPE=vk:vk.com/userid` | VK profile |

### Identity Resolution

When a message arrives on any channel, the agent can:

1. Extract the sender's channel ID (phone number, Discord ID, email, etc.)
2. Search the address book for a contact matching that ID
3. If found: link the session to that contact's UID
4. If not found: create a new contact with the channel ID populated

This enables **cross-channel session linking**. Two sessions that map to the same vCard UID are the same person. The agent can then:
- Show "Thomas Tolenaar" instead of a raw channel ID
- Prefer one channel over another for outreach
- Maintain a unified conversation history per person
- Enrich contacts over time as new channel IDs are discovered

### Custom Extension: Animus Metadata

For data that doesn't fit in standard vCard properties, we use a `X-ANIMUS` namespace:

```
X-ANIMUS-PREFERENCES;TYPE=language:en
X-ANIMUS-PREFERENCES;TYPE=tone:informal
X-ANIMUS-CHANNEL-META;TYPE=whatsapp:last_seen=2026-06-05T14:00:00Z
X-ANIMUS-CHANNEL-META;TYPE=whatsapp:preferred=true
X-ANIMUS-NOTES:Met at conference, interested in ML ops
```

These are stored in the vCard but understood only by Animus. Standard CardDAV clients (Thunderbird, Apple Contacts, etc.) will ignore them. This gives us structured agent-specific data without polluting the standard properties.

## Tool Design

### Actions

| Action | Description | CardDAV Method |
|--------|-------------|---------------|
| `contacts:list` | List contacts (optional filter) | `REPORT` with text-match |
| `contacts:get` | Get contact by UID | `REPORT` with calendar-data filter |
| `contacts:search` | Search by name, email, phone, org | `REPORT` with text-match |
| `contacts:create` | Create a new contact | `PUT` new .vcf resource |
| `contacts:update` | Update an existing contact | `PUT` updated .vcf resource |
| `contacts:delete` | Delete a contact | `DELETE` |
| `contacts:find-by-channel` | Find contact by channel ID | `REPORT` with text-match on IMPP/X-SOCIALPROFILE/TEL/EMAIL |
| `contacts:link-channel` | Add a channel ID to existing contact | Read → merge → `PUT` |
| `contacts:merge` | Merge duplicate contacts | Read both → merge fields → `PUT` one → `DELETE` other |
| `contacts:export` | Export address book as vCard collection | `PROPFIND` + multiple `GET` |

### Contact Data Model

The tool presents contacts as structured data, not raw vCard:

```json
{
  "uid": "c3d4e5f6@animus.local",
  "fn": "Thomas Tolenaar",
  "name": {
    "family": "Tolenaar",
    "given": "Thomas"
  },
  "nickname": "ThomasT",
  "org": "Revicoat",
  "role": "Partner",
  "emails": [
    {"type": "work", "value": "thomas@revicoat.com", "pref": true}
  ],
  "phones": [
    {"type": "cell", "value": "+31612345678"}
  ],
  "channels": {
    "whatsapp": "+31612345678",
    "telegram": "@thomastolenaar",
    "email": "thomas@revicoat.com"
  },
  "social": {
    "linkedin": "https://linkedin.com/in/thomastolenaar"
  },
  "notes": "Met at conference, interested in ML ops",
  "animus_meta": {
    "preferred_channel": "whatsapp",
    "language": "en",
    "tone": "informal"
  },
  "created": "2026-06-05T14:00:00Z",
  "updated": "2026-06-05T16:00:00Z"
}
```

The `channels` field is the key innovation — it's synthesized from `TEL`, `EMAIL`, `IMPP`, and `X-SOCIALPROFILE` properties into a unified lookup table. The agent doesn't need to know which vCard property stores a WhatsApp ID; it just checks `channels.whatsapp`.

### Configuration

```json
{
  "tools": {
    "caldav": {
      "mode": "internal",
      "internal_port": 5232,
      "external": { "..." : "..." },
      "default_calendar": "animus"
    },
    "carddav": {
      "mode": "internal",
      "default_address_book": "animus",
      "auto_create_contacts": true,
      "auto_link_sessions": true
    }
  }
}
```

- `auto_create_contacts`: When a message arrives from an unknown sender, automatically create a vCard with the channel ID
- `auto_link_sessions`: When a session is created, look up the sender's channel ID in the address book and attach the contact UID to the session metadata

### CardDAV Protocol Implementation

CardDAV is WebDAV with contact-specific extensions (RFC 6350 + RFC 6351). The key operations:

- **Discovery**: `PROPFIND` on `/.well-known/carddav` → address book home → address book URLs
- **List contacts**: `PROPFIND` on address book with `resourcetype` filter
- **Query contacts**: `REPORT` with `addressbook-query` XML body (text-match filters)
- **Create contact**: `PUT` with `Content-Type: text/vcard` to a new `.vcf` URL
- **Update contact**: `PUT` to the existing `.vcf` URL with updated vCard
- **Delete contact**: `DELETE` the `.vcf` URL
- **ETag-based conflict detection**: `If-Match` header on update/delete

The HTTP client and WebDAV methods are shared with the CalDAV tool (ticket 066). Both tools use the same `WebDAVClient` class internally — only the XML namespaces and data formats differ.

### vCard Parser/Serializer

A lightweight vCard 4.0 parser/serializer is needed. Key requirements:
- Parse vCard 4.0 (RFC 6350) with common extensions (`X-SOCIALPROFILE`, `IMPP`)
- Handle line folding (long lines split with CRLF+whitespace)
- Handle property groups (e.g., `ITEM1.TEL` + `ITEM1.X-ABLABEL`)
- Serialize back to valid vCard 4.0
- Preserve unknown `X-` properties (don't drop custom fields)
- Handle `X-ANIMUS-*` properties transparently

Implementation: ~300-400 lines of C++. vCard is a simple text format (BEGIN:VCARD / property lines / END:VCARD). No external library needed.

## Integration with Animus

### Channel Unification

When `auto_link_sessions` is enabled, the `ChannelManager` hooks into contact lookup:

```
Message arrives on WhatsApp
  → Extract sender phone number
  → contacts:find-by-channel(whatsapp, "+31612345678")
  → Found: Thomas Tolenaar (UID: c3d4e5f6)
  → Attach UID to session metadata
  → Agent sees "Thomas Tolenaar" instead of "+31612345678@s.whatsapp.net"
```

### SendReply Enhancement

When the agent wants to reach a person across channels:

```
Agent: "Send Thomas the project update"
  → contacts:get(thomas_uid)
  → channels: { whatsapp: "+316...", email: "thomas@revicoat.com" }
  → Prefer WhatsApp (preferred_channel: whatsapp)
  → SendReply via WhatsApp channel
```

### Session History by Person

With contact UIDs attached to sessions, the agent can query "all conversations with Thomas" across channels — WhatsApp, Telegram, Slack, email — unified by the vCard UID rather than channel-specific identifiers.

## Scope

### v1 (This ticket)
- [ ] CardDAV protocol client (PROPFIND, REPORT, PUT, DELETE, MKCOL)
- [ ] vCard 4.0 parser and serializer (including IMPP, X-SOCIALPROFILE, X-ANIMUS)
- [ ] Tool actions: list, get, search, create, update, delete, find-by-channel, link-channel
- [ ] Internal mode: shared Radicale instance with CalDAV tool
- [ ] External mode: remote CardDAV server connection
- [ ] Auto-create contacts from incoming messages
- [ ] Auto-link sessions to contact UIDs
- [ ] Channel ID extraction per channel (WhatsApp phone→JID, Discord ID, email, etc.)
- [ ] Admin UI section for CardDAV config
- [ ] Integration tests with Radicale

### v2 (Future)
- [ ] `contacts:merge` — duplicate detection and merging
- [ ] `contacts:export` — vCard collection export
- [ ] Contact photo support (`PHOTO` property)
- [ ] Birthday/anniversary reminders (cross-link with CalDAV tool)
- [ ] Address book sharing (CardDAV sharing extension)
- [ ] Reverse lookup: given a session, find the contact
- [ ] Batch operations (import/export multiple contacts)
- [ ] Contact deduplication heuristics (same name + same org, etc.)

## Testing Strategy

Since CardDAV shares Radicale with CalDAV, both tools can be tested together:

1. **Local Radicale** on the workstation (as Melvin suggested)
2. Create an address book, add contacts, search, update, delete
3. Test `find-by-channel` with real Animus channel identifiers
4. Test `auto_create_contacts` by sending messages through WhatsApp and verifying the contact appears
5. Test `auto_link_sessions` by checking session metadata for contact UIDs
6. **External** test against a production CardDAV server (NextCloud, Fastmail, etc.)

## Compatibility Matrix

| Server | Tested | Notes |
|--------|--------|-------|
| Radicale | v1 target | Internal mode, shared with CalDAV |
| NextCloud | v2 | Standard CardDAV |
| Fastmail | v2 | Standard CardDAV |
| Apple Contacts | v2 | Via macOS Server |
| Google Contacts | v2 | CardDAV API (limited, Google-specific) |
| Zimbra | v2 | Standard CardDAV |

Note: Google Contacts supports CardDAV but with restrictions (custom properties may not survive round-trips). The `X-ANIMUS-*` properties should be stored in Radicale (internal) or servers that support custom extensions.

## Relationship to Other Tickets

- **Ticket 066 (CalDAV)** — Shares the Radicale subprocess and WebDAV client code. Both tools are deployed together.
- **Ticket 041 (Sessions Tool)** — `auto_link_sessions` attaches contact UIDs to session metadata, enabling per-person session queries.
- **Channel adapters** — Each channel adapter (WhatsApp, Discord, Telegram, etc.) needs to expose the sender's channel ID in a standard format for `find-by-channel` lookup.
- **Ticket 064 (Passive Mode)** — Contact-linked sessions enable the agent to maintain per-person context across passive mode awareness windows.
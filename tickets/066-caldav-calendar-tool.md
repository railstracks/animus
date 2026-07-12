# Ticket 066 — CalDAV Calendar Tool

**Created:** 2026-06-05
**Status:** Draft
**Priority:** P2
**Depends on:** None (standalone tool)

## Summary

Add a built-in Animus tool that provides calendar management via the CalDAV protocol. The tool operates in two modes: **internal** (embedded Radicale server, zero-config for users who don't have a CalDAV service) and **external** (connect to any CalDAV server — Google Calendar, NextCloud, Fastmail, Baikal, etc.). This gives every agent time awareness and scheduling capability without requiring the user to set up external infrastructure.

## Motivation

Agents are temporally blind. They discover calendar events only when explicitly told about them or when a heartbeat happens to check. This is a fundamental capability gap — an agent that can read, create, and manage calendar events can:

- **Schedule follow-ups** — "remind me next Tuesday" becomes a real calendar event
- **Detect conflicts** — "you have two meetings at 3pm" instead of discovering this after the fact
- **Maintain temporal awareness** — know what's coming today, this week, this month
- **Coordinate with humans** — propose meeting times, accept/decline invitations
- **Self-organize** — create task blocks, deadlines, review reminders

Most users don't have a CalDAV server lying around. The internal mode removes that barrier entirely — every Animus deployment gets calendar capability out of the box.

## Architecture

### Two Modes of Operation

```
┌─────────────────────────────────────────────────┐
│                  Animus Agent                    │
│                                                  │
│              ┌──────────────┐                     │
│              │  CalDAV Tool │                     │
│              └──────┬───────┘                     │
│                     │                             │
│            ┌────────┴────────┐                    │
│            │                 │                    │
│     ┌──────┴──────┐  ┌──────┴──────┐             │
│     │ Internal    │  │ External    │             │
│     │ (Radicale) │  │ (Remote)    │             │
│     │             │  │             │             │
│     │ localhost:   │  │ caldav.     │             │
│     │ 5232        │  │ example.com │             │
│     └─────────────┘  └─────────────┘             │
│                                                  │
└─────────────────────────────────────────────────┘
```

**Internal mode** (default):
- Radicale runs as a subprocess managed by Animus (or as a Docker sidecar in Swarm)
- Data stored in Animus data directory (`/var/lib/animus/caldav/`)
- Zero user configuration required
- Calendar created automatically on first access
- Suitable for single-user deployments and development

**External mode**:
- Agent connects to an existing CalDAV server via HTTPS
- User provides URL, username, password (or app password)
- Supports discovery via well-known URLs (`.well-known/caldav`)
- Suitable for teams with existing calendar infrastructure

### Hybrid Mode

Both can run simultaneously — internal calendar for agent-specific events (task blocks, reminders, self-scheduling) and external calendar for syncing with the user's existing schedule. The tool handles calendar discovery and can write to either.

## Tool Design

### Actions

| Action | Description | CalDAV Method |
|--------|-------------|---------------|
| `cal:list` | List available calendars | `PROPFIND` on calendar home |
| `cal:today` | Show today's events | `REPORT` with time-range filter |
| `cal:range` | Show events in a date range | `REPORT` with time-range filter |
| `cal:get` | Get a specific event by UID | `REPORT` with calendar-data filter |
| `cal:create` | Create a new event | `PUT` new .ics resource |
| `cal:update` | Update an existing event | `PUT` updated .ics resource |
| `cal:delete` | Delete an event | `DELETE` |
| `cal:freebusy` | Query free/busy for a time range | `PROPFIND` + time-range |
| `cal:search` | Search events by text | `REPORT` with text-match filter |

### Event Data Model

The tool presents events as structured data, not raw iCalendar:

```json
{
  "uid": "a1b2c3@animus.local",
  "summary": "Project review with Thomas",
  "description": "Quarterly sync on Midas performance",
  "start": "2026-06-10T14:00:00+02:00",
  "end": "2026-06-10T15:00:00+02:00",
  "all_day": false,
  "location": "Rotterdam office",
  "calendar": "work",
  "status": "confirmed",
  "recurrence": null,
  "attendees": ["thomas@revicoat.com"],
  "reminders": ["PT15M"]
}
```

The tool handles all iCalendar serialization/deserialization internally. The agent never sees raw VEVENT content.

### Configuration

```json
{
  "tools": {
    "caldav": {
      "mode": "internal",
      "internal_port": 5232,
      "external": {
        "url": "https://caldav.example.com",
        "username": "${CALDAV_USER}",
        "password": "${CALDAV_PASSWORD}"
      },
      "default_calendar": "animus",
      "timezone": "Europe/Amsterdam"
    }
  }
}
```

- `mode`: `"internal"`, `"external"`, or `"hybrid"` (both)
- `internal_port`: port for Radicale subprocess (default 5232)
- `external.*`: connection details for remote CalDAV server
- `default_calendar`: calendar name for `cal:create` when not specified
- `timezone`: default timezone for time queries (agent can override per-query)

## Internal Server: Radicale

### Why Radicale

| Criterion | Radicale | Baikal | Xandikos | Stalwart |
|-----------|----------|--------|----------|----------|
| Language | Python | PHP | Python | Rust |
| Dependencies | Minimal | SabreDAV | Git | Full suite |
| Install size | ~5MB | ~30MB | ~10MB | ~50MB |
| Config complexity | Low | Medium (web UI) | Low | Medium |
| Storage format | Plain .ics files | MySQL/SQLite | Git repo | Maildir |
| Resource usage | ~20MB RAM | ~50MB RAM | ~20MB RAM | ~100MB RAM |
| Sufficient for our use? | ✅ | ✅ | ✅ | Overkill |

Radicale is the clear choice: minimal footprint, simple to configure, stores as plain .ics files (easy to inspect/debug), well-maintained, and widely deployed.

### Deployment

**Option A: Subprocess (preferred for simplicity)**
```python
# Radicale launched as child process
# Config generated programmatically
# Data stored in Animus data directory
radicale --config /var/lib/animus/caldav/radicale.conf
```

- Animus starts/stops Radicale as part of its lifecycle
- Auth is bypassed for localhost (trusted internal connection)
- Storage at `/var/lib/animus/caldav/collections/`

**Option B: Docker sidecar (preferred for Swarm)**
```yaml
# In docker-compose.yml
caldav:
  image: tomsquest/docker-radicale:latest
  volumes:
    - caldav_data:/data
  ports:
    - "127.0.0.1:5232:5232"
```

- Already containerized, already available
- Persistent volume for calendar data
- Restart policy tied to Animus container

### Initial Setup

When the CalDAV tool initializes in internal mode:

1. Start Radicale (or verify sidecar is running)
2. `PROPFIND` the calendar home to discover existing calendars
3. If no calendars exist, create a default "animus" calendar
4. Set the default timezone from config (or detect from system)
5. Report ready to the agent

The agent doesn't need to know about any of this — it just starts using `cal:today` and it works.

## CalDAV Protocol Implementation

CalDAV is WebDAV with calendar-specific extensions (RFC 4791). The key operations:

- **Discovery**: `PROPFIND` on `/.well-known/caldav` → calendar home → calendar URLs
- **List calendars**: `PROPFIND` on calendar home with `resourcetype` filter
- **Query events**: `REPORT` with `calendar-query` XML body (time-range, text-match filters)
- **Create event**: `PUT` with `Content-Type: text/calendar` to a new `.ics` URL
- **Update event**: `PUT` to the existing `.ics` URL with updated VEVENT
- **Delete event**: `DELETE` the `.ics` URL
- **ETag-based conflict detection**: `If-Match` header on update/delete

The tool needs an HTTP client capable of WebDAV methods (PROPFIND, REPORT, MKCOL). The existing `httplib::Client` or `Cpr` in Animus can handle this — these are just HTTP methods with XML bodies.

### XML Namespaces

```xml
xmlns:d="DAV:"
xmlns:c="urn:ietf:params:xml:ns:caldav"
xmlns:cs="http://calendarserver.org/ns/"
```

### Sample Queries

**List today's events:**
```xml
<c:calendar-query>
  <d:prop>
    <d:getetag/>
  </d:prop>
  <c:filter>
    <c:comp-filter name="VCALENDAR">
      <c:comp-filter name="VEVENT">
        <c:time-range start="20260605T000000Z" end="20260605T235959Z"/>
      </c:comp-filter>
    </c:comp-filter>
  </c:filter>
</c:calendar-query>
```

**Search by text:**
```xml
<c:calendar-query>
  <d:prop>
    <d:getetag/>
  </d:prop>
  <c:filter>
    <c:comp-filter name="VCALENDAR">
      <c:comp-filter name="VEVENT">
        <c:prop-filter name="SUMMARY">
          <c:text-match collation="i;unicode-casemap">review</c:text-match>
        </c:prop-filter>
      </c:comp-filter>
    </c:comp-filter>
  </c:filter>
</c:calendar-query>
```

## Scope

### v1 (This ticket)
- [ ] CalDAV protocol client (PROPFIND, REPORT, PUT, DELETE, MKCOL)
- [ ] iCalendar (VEVENT) parser and serializer
- [ ] Tool actions: list, today, range, get, create, update, delete, search
- [ ] Internal mode: Radicale subprocess management
- [ ] External mode: remote CalDAV server connection
- [ ] Timezone-aware query handling
- [ ] Admin UI section for CalDAV config (mode, external server, timezone)
- [ ] Integration tests with Radicale

### v2 (Future)
- [ ] `cal:freebusy` — free/busy queries
- [ ] `cal:reminders` — create/manage reminder alarms
- [ ] Recurring events (RRULE) — creation and expansion
- [ ] Calendar sharing and subscriptions (CalDAV sharing extension)
- [ ] Multi-calendar sync (internal ↔ external bidirectional)
- [ ] Invitation handling (iTIP: REQUEST, REPLY, CANCEL)
- [ ] Push notifications (CalDAV scheduling)

## Integration with Animus

### Heartbeat Integration
The CalDAV tool enables richer heartbeat behavior:
- Check `cal:today` at start of each heartbeat
- Compare against previous heartbeat to detect new/cancelled events
- Surface upcoming events proactively ("You have a meeting in 30 minutes")

### Sessions Integration
With ticket 041 (Sessions Tool), calendar events can trigger session activity:
- Meeting start → agent prepares context notes
- Reminder fires → agent sends a message to the relevant session
- Deadline approaches → agent surfaces relevant documents

### Cron Integration
Calendar events can inform cron job scheduling:
- Create a one-shot cron job for "5 minutes before meeting X"
- Schedule follow-up tasks based on calendar availability

## Compatibility Matrix

| Server | Tested | Notes |
|--------|--------|-------|
| Radicale | v1 target | Internal mode, zero-config |
| Google Calendar | v2 | Requires OAuth2, CalDAV not native — need CardDAV-like bridge |
| NextCloud | v1 | Standard CalDAV |
| Fastmail | v1 | Standard CalDAV |
| Baikal | v2 | Standard CalDAV |
| Zimbra | v2 | Standard CalDAV |
| iCloud | v2 | Apple CalDAV — app-specific password required |

Note: Google Calendar doesn't natively speak CalDAV. For Google integration, we'd need a separate Google Calendar API adapter (OAuth2 + REST). This is out of scope for v1 but worth noting as a future addition.

## Hard-Won Lessons (Predicted)

1. **Timezones will be the hardest part.** iCalendar datetime handling with timezones is notoriously complex. Always store in UTC, display in local time. Use IANA timezone names, never abbreviations.

2. **Radicale's file storage is a feature.** Plain .ics files on disk means we can inspect, debug, and backup calendar data trivially. Don't fight this.

3. **Discovery is harder than it looks.** The `.well-known/caldav` redirect → PROPFIND for principal → PROPFIND for calendar-home-set → PROPFIND for calendars chain has multiple failure modes. Test against multiple servers early.

4. **Recurring events are v2 for a reason.** RRULE expansion is a rabbit hole. Start with simple events only.
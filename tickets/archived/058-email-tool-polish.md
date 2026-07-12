# Ticket 058: Email Tool Polish

**Status:** Complete  
**Priority:** Normal  
**Completed:** 2026-05-24  
**Depends on:** Ticket 054 (Email Tool — AgentMail Backend) ✅  

## Summary

Polish the EmailTool: message ID in dispatch prompt (eliminates wasted search round-trip), HTML email body fallback, WebSocket robustness (auth error detection, stale connection recovery, REST polling fallback), and debug cleanup.

## What Shipped

### 1. Message ID in Dispatch Prompt

The agent now receives `Message-ID` and `Thread-ID` in the inbound email prompt, so it can `reply` directly without searching first.

**Before:**
```
New email from Melvin Sommer <...> with subject 'Here's another test':

Another test email. Can you reply to this?

You are responding via email. Respond naturally — your reply will be sent automatically.
```

**After:**
```
New email from Melvin Sommer <...> with subject 'Here's another test':

Another test email. Can you reply to this?

Message-ID: <CAMnqBDm7o+AxTmy3US1W=QU6wJedsyiewbg=NzpvZBYDbLyPTQ@mail.gmail.com>
Thread-ID: thread_abc123

You are responding via email. Use the email tool with action=reply and the message_id above to respond. Do NOT use the social tool to reply.
```

This eliminated the search round-trip the agent was making (search → find message_id → reply). The instruction also clarifies *how* to respond instead of the ambiguous "your reply will be sent automatically."

### 2. HTML Email Fallback

Three-layer body extraction chain for both WS events and REST `get_message`:

| Priority | Source | Notes |
|----------|--------|-------|
| 1 | `text` | Plain text body |
| 2 | `extracted_text` | AgentMail-provided text extraction |
| 3 | `html` → `StripHtml()` | Last resort: strip tags, decode entities, collapse whitespace |

Two implementations:
- **`StripHtml`** (EmailTool.cpp) — full stripper: tag removal, HTML entity decoding (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&nbsp;`, numeric entities), whitespace collapsing. Used by `get_message`.
- **`StripHtmlSimple`** (ChannelManager.cpp) — lightweight: tag removal + whitespace collapse, no entity decoding. Used by WS hot path where AgentMail's `extracted_text` usually handles entities.

Previous behavior for HTML-only emails: `get_message` returned `"[HTML content stripped — plain text not available]"`. WS dispatch sent empty body. Now both paths extract readable text.

### 3. WebSocket Robustness

**Connection failure tracking:**
- Counts consecutive connection failures in `state->consecutive_errors`
- After 5 consecutive failures, stops WS and falls through to `EmailPollLoop`
- Transient failures (network blip) still use Drogon's built-in auto-reconnect (1s delay)
- Persistent failures (wrong URL, prolonged outage) bail out after ~5 retry cycles

**Auth error detection:**
- Parses AgentMail error messages in the WS message handler
- Fatal errors (`authentication_error`, `authorization_error`, `invalid_api_key`) immediately stop the WS loop
- No point auto-reconnecting with a bad API key

**Liveness monitoring:**
- Tracks `last_ws_event` timestamp — updated on every received event
- Every 5 seconds, checks: if connected but no events for ≥5 minutes, connection may be stale (half-open TCP)
- After 3 consecutive stale checks (15 minutes with no events), force-reconnects
- Prevents silent zombie connections

**Fallback path:**
- When WS exits due to failures (not clean shutdown), `EmailPollLoop` takes over automatically
- Polling loop uses REST GETs every 25 seconds — no WS dependency

### 4. Debug Cleanup

- Removed `[EMAIL-DEBUG]` cerr line from `HandleListInboxes`

## Files Changed

| File | Change |
|------|--------|
| `src/kernel/ChannelManager.cpp` | Message ID + thread ID in prompt, HTML fallback chain in WS handler, connection failure tracking, auth error detection, liveness monitoring, WS→polling fallback, `StripHtmlSimple` utility |
| `src/kernel/email/EmailTool.cpp` | `StripHtml` utility (full HTML-to-text), `get_message` uses `StripHtml` instead of placeholder, debug cleanup |
| `include/animus_kernel/ChannelManager.h` | `last_ws_event` field in PollerState |

## Commits

- Prompt fix + HTML fallback + debug cleanup (first pass)
- WS robustness: failure tracking, auth error detection, liveness monitoring, polling fallback (second pass)

## What Didn't Ship (deferred)

These items from the original 058 draft are not yet implemented:

- **Attachment content download** — currently returns metadata only ("content available but not included for size"). Needs fetch-on-demand with size limits.
- **Email templates** — configurable outbound formatting (signature, quoted original)
- **Multi-account priority routing** — config surface exists (agent_id per channel), admin UI exists (ChannelsView), backend routing works. What's missing is the admin-facing UX for managing multiple email accounts (documentation, validation, status per account).
- **Delivery status events** — subscribe to `message.delivered`, `message.bounced`, `message.complained` for bounce notifications and spam complaint handling

## Key Lessons

1. **Include identifiers in dispatch prompts.** The agent wasted a full turn searching for a message ID it should have had from the start. Any time the system dispatches with context about a specific entity, include the identifier.

2. **WS reconnection needs failure counting.** Drogon's auto-reconnect is great for transient failures but will happily retry forever with a bad API key. Counting failures and bailing out to a simpler transport is the right safety net.

3. **Silent zombie connections are real.** A WebSocket can look connected (TCP still open) while the server has stopped sending events. Liveness monitoring via `last_ws_event` catches this without needing application-level pings.

4. **Two HTML strippers is fine.** The full version in EmailTool handles entity decoding for `get_message` (where accuracy matters). The lightweight version in ChannelManager just strips tags for the WS hot path (where speed matters and `extracted_text` usually exists). No need to share one implementation across different performance profiles.

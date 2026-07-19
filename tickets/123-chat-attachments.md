# Ticket 123: Chat Attachments

**Status:** Scoped (updated 2026-07-20)
**Date:** 2026-07-19
**Author:** Melvin + Kestrel

## Problem

Agents can produce and reference files (generated images, code output, text files, audio renders), but there's no way to surface these as rich content in the chat interface. File paths are just text strings in messages. The admin UI chat view renders everything as plain text.

A VSCode extension (Ticket 119) will need the same attachment rendering capability. Solving it once in the API + admin UI means the VSCode extension consumes the same message format.

## Per-Channel Attachment Support

Not all channels can display attachments. Agents referencing attachments in channels that don't support them creates a broken experience — the agent thinks it sent an image, but users see nothing.

### Channel capability flag

Add an `supports_attachments` boolean to each channel configuration. This is a per-channel-type default with a per-channel-instance override.

**Default by channel type:**

| Channel Type | Supports Attachments | Rationale |
|-------------|---------------------|-----------|
| Discord | ✅ Yes | Native file/image attachments |
| Slack | ✅ Yes | Native file/image attachments |
| WhatsApp | ✅ Yes | Native file/image attachments |
| Webchat | ✅ Yes | Renders in admin UI |
| Telegram | ✅ Yes | Native file/image attachments |
| Email | ❌ No | Replies composed via email tool — attachments in session turns never reach the recipient |
| IRC | ❌ No | No attachment capability in protocol |
| Bluesky | ❌ No | Images would need separate API upload, not session-turn based |
| VK | ❌ No | Separate API upload needed |
| Mastodon | ❌ No | Separate API upload needed |
| Twitter | ❌ No | Media upload is a separate API call |
| Nextcloud Talk | ❌ No | Separate file sharing API |

### Configuration

In `/channels`, each channel instance gets an `attachments_enabled` switch:

- For channel types where `supports_attachments` is true by default: the switch defaults to **enabled**, can be toggled off
- For channel types where `supports_attachments` is false by default: the switch defaults to **disabled**, can be toggled on (for edge cases like a custom IRC setup with file hosting)

The admin UI shows the switch only when relevant (or always, with a sensible default).

### Agent behavior

When `attachments_enabled` is false for the active channel:
- The `add_chat_attachment` tool is still available (the agent may produce files)
- But the tool description or system prompt context should indicate that attachments won't be displayed in this channel
- The agent should instead reference files by path or use channel-specific tools (e.g. email tool for email attachments)

When `attachments_enabled` is true:
- Attachments render inline in the channel (Discord/Slack/WhatsApp send as message attachments, Webchat renders in UI)
- The `add_chat_attachment` tool works as designed

### Implementation

**DB:** Add `attachments_enabled BOOLEAN DEFAULT 0` to `channels` table (migration). Default value depends on channel type at insert time.

**Channel adapters:** When dispatching a session turn that has attachments:
- If `attachments_enabled` is true: send attachments via the channel's native file/attachment API
- If `attachments_enabled` is false: skip attachments (they exist in the session record but aren't forwarded to the channel)

**Admin UI (`/channels`):** Add an "Attachments" toggle switch in channel edit form. Pre-filled based on channel type default.

**System prompt context:** Include attachment capability in the agent's session context:
- "This channel supports attachments. Files you attach will be displayed to the user."
- "This channel does not support attachments. Files you generate will be saved to disk but won't be visible in the chat. Reference them by filepath instead."

## Design

### New tool: `add_chat_attachment`

Agent-facing tool that pushes an attachment into the active session as a special message.

**Tool definition:**
```
name: "add_chat_attachment"
description: "Attach a file to the current chat session for display to the user. "
             "Only works in channels that support attachments (Discord, Slack, WhatsApp, Webchat). "
             "In channels without attachment support, files are saved but not displayed."
parameters:
  filepath: string (required) — path to the file
  display_name: string — optional friendly name (defaults to filename)
  mime_type: string — optional explicit content type (auto-detected if omitted)
```

**Behavior:**
1. Agent calls `add_chat_attachment` with a filepath
2. Tool reads the file, determines type (by extension or explicit mime_type)
3. Tool creates a session turn with `role: "assistant"`, `content: ""`, and attachment metadata
4. For small files (< 1MB text, < 5MB images): store inline as base64 in the turn metadata
5. For larger files: copy to `media/attachments/` directory, store filepath reference
6. Returns success with attachment ID

### Session Turn Extension

Add an `attachments` field to `SessionTurn`:

```cpp
struct Attachment {
    std::string id;           // unique within the turn
    std::string filename;     // display name
    std::string mime_type;    // "text/plain", "image/png", "audio/wav", "video/mp4"
    std::string filepath;     // served path (for file-backed attachments)
    std::string data_b64;     // inline base64 (for small attachments, empty if file-backed)
    int64_t size_bytes{0};
};

struct SessionTurn {
    // ... existing fields ...
    std::vector<Attachment> attachments;  // new
};
```

**DB schema change:** New table `session_attachments` (avoids modifying the turns table):
```sql
CREATE TABLE IF NOT EXISTS session_attachments (
  id TEXT PRIMARY KEY,
  turn_id BIGINT NOT NULL,
  session_id TEXT NOT NULL,
  filename TEXT NOT NULL,
  mime_type TEXT NOT NULL,
  filepath TEXT,             -- NULL if inline
  data_b64 TEXT,             -- NULL if file-backed
  size_bytes BIGINT NOT NULL,
  created_at BIGINT NOT NULL
);
CREATE INDEX idx_attachments_turn ON session_attachments(turn_id);
CREATE INDEX idx_attachments_session ON session_attachments(session_id);
```

### API Changes

**New endpoint:**
- `GET /api/v1/sessions/:key/attachments/:id` — streams attachment content with correct Content-Type. Auth required. Works for both file-backed and inline (decodes base64 on the fly).

**Modified endpoint:**
- `GET /api/v1/sessions/:key/turns` — include attachments array in each turn response

**Channel config changes:**
- `PUT /api/v1/channels/:id` — accept `attachments_enabled` field
- `GET /api/v1/channels` — include `attachments_enabled` in response

### Admin UI

**Channel edit form (`/channels`):**
- Add "Attachments" toggle switch (v-switch)
- Pre-filled based on channel type default (Discord/Slack/WhatsApp/Telegram/Webchat = on, others = off)
- Label: "Enable attachments" with hint: "Allow the agent to display files in this channel"

**ChatView changes:**
- New `AttachmentMessage` Vue component
- Renders based on `mime_type`:
  - **text/** — collapsible code viewer (monospace, line numbers, max-height with scroll)
  - **image/** — inline image display, click for lightbox (full-size overlay)
  - **audio/** — HTML5 `<audio>` player with controls
  - **video/** — HTML5 `<video>` player with controls
  - **application/octet-stream** — download link with file icon
- Each attachment shows: filename, size, type badge
- Multiple attachments in a single turn render as a stacked list

**Attachment rendering in message flow:**
- Attachments render as distinct visual blocks between text messages
- Not mixed into the assistant's text content — they're separate display elements
- A turn can have text + attachments (text renders first, attachments below)

### i18n

New keys for:
- Attachment labels (filename, size, type, download)
- "No preview available" for unsupported types
- Channel config: "Enable attachments" label + hint
- Sidebar nav not needed (no separate page)

### Files to Create/Modify

**New:**
- `include/animus_kernel/tools/AttachmentTool.h`
- `src/kernel/tools/AttachmentTool.cpp`
- `include/animus_kernel/AttachmentStore.h`
- `src/kernel/attachment/AttachmentStore.cpp`
- `admin-ui/src/components/chat/AttachmentMessage.vue`
- `admin-ui/src/components/chat/Lightbox.vue`

**Modified:**
- `include/animus_kernel/Session.h` (Attachment struct, attachments vector on SessionTurn)
- `include/animus_kernel/ChannelManager.h` (attachments_enabled flag on channel config)
- `src/kernel/SessionStore.cpp` (save/load attachments)
- `src/kernel/ChannelManager.cpp` (default attachments_enabled per channel type)
- `src/kernel/admin/AdminServerRoutes.cpp` (attachment serving endpoint)
- `src/kernel/admin/internal/AdminServerRoutesSessionsMemory.inc` (include attachments in turn response)
- `src/kernel/admin/internal/AdminServerRoutesChannels.inc` (attachments_enabled in channel CRUD)
- `src/kernel/AgentKernel.cpp` (register AttachmentTool)
- `CMakeLists.txt` (new sources)
- `admin-ui/src/views/ChatView.vue` (render AttachmentMessage components)
- `admin-ui/src/views/ChannelsView.vue` (attachments toggle in channel edit form)
- `admin-ui/src/lib/api.ts` (attachment type, fetch helper)
- `admin-ui/src/i18n/locales/en/channels.ts` (attachments label)
- 22 locale files for channel attachments label

### Phases

**Phase 1 (MVP):**
- `add_chat_attachment` tool (filepath → attachment)
- DB schema + store
- API: attachment serving endpoint
- Per-channel `attachments_enabled` flag with type defaults
- Admin UI: AttachmentMessage component (all four type renderers)
- Admin UI: Channel edit form attachments toggle
- Channel adapter dispatch: skip attachments when disabled
- System prompt context: attachment capability indication
- Tool registered for agents

**Phase 2 (optional):**
- Drag-and-drop file upload from admin UI (user → agent direction)
- Attachment in user messages (not just assistant)
- Attachment size limits per agent config
- Thumbnail generation for large images
- Channel-specific attachment delivery (Discord file upload, Slack file upload, etc.)

### Ordering

This should be implemented **before** the VSCode extension (Ticket 119). The extension will consume the same `/api/v1/sessions/:key/attachments/:id` endpoint and render attachments the same way.

### Security

- Attachments served only to authenticated users
- File paths restricted to `media/attachments/` directory (no path traversal)
- Size limits: 10MB per attachment, 50MB per session
- MIME type validated on read (don't trust extension alone)
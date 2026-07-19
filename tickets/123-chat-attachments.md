# Ticket 123: Chat Attachments

**Status:** Scoped
**Date:** 2026-07-19
**Author:** Melvin + Kestrel

## Problem

Agents can produce and reference files (generated images, code output, text files, audio renders), but there's no way to surface these as rich content in the chat interface. File paths are just text strings in messages. The admin UI chat view renders everything as plain text.

A VSCode extension (Ticket 119) will need the same attachment rendering capability. Solving it once in the API + admin UI means the VSCode extension consumes the same message format.

## Design

### New tool: `add_chat_attachment`

Agent-facing tool that pushes an attachment into the active session as a special message.

**Tool definition:**
```
name: "add_chat_attachment"
description: "Attach a file to the current chat session for display to the user."
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

### Admin UI

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
- `src/kernel/SessionStore.cpp` (save/load attachments)
- `src/kernel/admin/AdminServerRoutes.cpp` (attachment serving endpoint)
- `src/kernel/admin/internal/AdminServerRoutesSessionsMemory.inc` (include attachments in turn response)
- `src/kernel/AgentKernel.cpp` (register AttachmentTool)
- `CMakeLists.txt` (new sources)
- `admin-ui/src/views/ChatView.vue` (render AttachmentMessage components)
- `admin-ui/src/lib/api.ts` (attachment type, fetch helper)

### Phases

**Phase 1 (MVP):**
- `add_chat_attachment` tool (filepath → attachment)
- DB schema + store
- API: attachment serving endpoint
- Admin UI: AttachmentMessage component (all four type renderers)
- Tool registered for agents

**Phase 2 (optional):**
- Drag-and-drop file upload from admin UI (user → agent direction)
- Attachment in user messages (not just assistant)
- Attachment size limits per agent config
- Thumbnail generation for large images

### Ordering

This should be implemented **before** the VSCode extension (Ticket 119). The extension will consume the same `/api/v1/sessions/:key/attachments/:id` endpoint and render attachments the same way.

### Security

- Attachments served only to authenticated users
- File paths restricted to `media/attachments/` directory (no path traversal)
- Size limits: 10MB per attachment, 50MB per session
- MIME type validated on read (don't trust extension alone)
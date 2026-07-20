# Ticket 123: Chat Attachments — Phase 1 Report

**Status:** Phase 1 Complete
**Date:** 2026-07-20
**Author:** Kestrel

## Summary

Implemented chat attachments for webchat — agents can now attach files (images, audio, video, text, arbitrary files) to chat sessions via the `add_chat_attachment` tool, with inline rendering in the admin UI. This closes the gap between agents producing files and users seeing them.

**23 registered tools** (up from 22).

## What Was Built

### Backend

**AttachmentStore** (`include/animus_kernel/AttachmentStore.h` + `src/kernel/attachment/AttachmentStore.cpp`)
- `session_attachments` DB table with columns: `id`, `turn_id`, `session_key`, `filename`, `mime_type`, `filepath`, `data_b64`, `size_bytes`, `created_at`
- Indexes on `turn_id` and `session_key`
- CRUD: Save, GetById, GetForTurn, GetForSession, DeleteForTurn

**AttachmentTool** (`include/animus_kernel/tools/AttachmentTool.h` + `src/kernel/tools/AttachmentTool.cpp`)
- Tool name: `add_chat_attachment`
- Parameters: `filepath` (required), `display_name` (optional), `mime_type` (optional)
- Auto-detects MIME type from file extension (20+ types)
- Inline storage: base64 in DB for small files (images < 5MB, text < 256KB)
- File-backed storage: copies to `media/attachments/` for larger files
- Creates a session turn (role=assistant, empty content), associates attachment record with turn_id
- Returns success message with filename, size, MIME type, and storage mode

**AttachmentTokenManager** (`include/animus_kernel/AttachmentTokenManager.h`)
- In-memory, per-attachment access tokens for `<img src>` URL authentication
- 32-char random hex tokens, bound to specific attachment IDs
- 30-minute TTL with lazy cleanup of expired entries
- Thread-safe (mutex-guarded)
- Tokens generated per-attachment when turns API serializes attachment data

**Admin API**
- `GET /api/v1/sessions/:key/attachments/:id` — streams attachment content (base64-decoded or file-backed) with correct Content-Type
- Auth middleware validates `?token=***` query parameter for attachment paths before Bearer token check (browser image/audio/video tags can't send Authorization headers)
- `BuildSessionTurnJson` overload includes attachments array with `access_token` per attachment

**WebSocket Live Updates**
- `ChatSessionService::Dependencies` extended with `AttachmentStore*` and `AttachmentTokenManager*`
- `toolEventCallback` detects successful `add_chat_attachment` calls and emits an `attachment` WebSocket event with attachment metadata + fresh access token
- Frontend renders attachments immediately — no page reload needed

### Frontend

**AttachmentMessage.vue** (`admin-ui/src/components/chat/AttachmentMessage.vue`)
- Renders by MIME type:
  - **Images** — inline display (max 320px height), click opens full-size in new tab
  - **Audio** — HTML5 `<audio>` player with controls
  - **Video** — HTML5 `<video>` player with controls
  - **Text** — collapsible monospace viewer, lazy-loads content on first expand, caches result, max 300px height with scroll
  - **Other** — download link
- Centralized `attachmentUrl()` builder appends `?token=***` to all URLs
- Shows filename, file size, and appropriate icon per type

**ChatView.vue**
- `UiMessage` and `SessionTurn` interfaces extended with `attachments` array (including `access_token`)
- History loader emits attachment blocks as separate UiMessages after text content
- WebSocket handler processes `attachment` events — pushes new UiMessage with attachment data immediately
- `<AttachmentMessage>` rendered after content bubble in message template

### AgentKernel Integration
- `AttachmentStore` created before `RegisterBuiltinTools` (same init-order pattern as DiffusionStore — Ticket 122 lesson applied)
- `AttachmentTokenManager` is a value member of `AdminServer` (no separate allocation)
- Tool registered with `SessionManager` pointer for turn creation

## Security Design

**Problem:** `<img src="...">`, `<audio>`, and `<video>` tags can't send `Authorization: Bearer` headers. The auth middleware (`registerSyncAdvice`) blocks all `/api/` requests without a valid Bearer token.

**Solution:** Short-lived per-attachment tokens passed as query parameters.

1. When the turns API returns attachment data, `AttachmentTokenManager` generates a unique 32-char hex token bound to that specific attachment ID
2. Frontend appends `?token=***` to all attachment URLs
3. The auth middleware itself (not just the handler) checks attachment tokens for `/attachments/` paths — validates token exists, isn't expired, and matches the requested attachment ID
4. Tokens expire after 30 minutes (TTL)
5. Per-attachment binding: a token for attachment A can't access attachment B
6. In-memory storage: server restart invalidates all tokens (user refreshes → new tokens generated)

**No Redis needed.** Single-process, in-memory. Adding infrastructure for a map with TTL would be more complexity than the problem warrants.

## Issues Encountered and Resolved

1. **Missing `<optional>` include** — `AttachmentStore.h` used `std::optional` without including `<optional>`. Fixed by adding the include.

2. **Forward declaration instead of full include** — `IDataStore` was forward-declared in `AttachmentStore.h` but used by value (methods called on it). Fixed by including `IDataStore.h` directly.

3. **ChatView brace mismatch** — The `for (const turn of typedChronological)` loop's closing brace was lost during compaction edits. Fixed by restoring the missing `}`.

4. **Attachment URLs returning 401 Unauthorized** — Auth middleware blocked `<img src>` requests before they reached the attachment handler. Initially attempted to exempt attachment paths from auth (reverted — Melvin correctly flagged the security concern). Solved with short-lived token system.

5. **Token validation in handler unreachable** — Even after adding token validation to the attachment handler, the auth middleware blocked requests before they reached the handler. Fixed by moving token validation into the middleware itself for `/attachments/` paths.

6. **Lambda capture scope** — `attachmentStore` and `attachmentTokenManager` were extracted from `m_deps` inside the job lambda but not added to its capture list. Fixed by extracting before the lambda and adding to the capture list (two-step fix: first the extraction location, then the outer capture list).

## Commits

| Commit | Description |
|--------|-------------|
| `5d3836b` | feat: Chat attachments Phase 1 — webchat (Ticket 123) |
| `23078bc` | fix: ChatView brace structure — missing for-loop closing brace |
| `a2c9318` | fix: add `<optional>` include and IDataStore header to AttachmentStore.h |
| `db4f4ec` | feat: short-lived attachment tokens (Ticket 123) |
| `fc08dad` | fix: auth middleware now validates attachment tokens before blocking |
| `4366d50` | feat: live attachment rendering + text file reader (Ticket 123) |
| `2cfc0fb` | fix: move attachment deps extraction before job lambda (capture scope) |
| `41fc2a2` | fix: add attachment deps to outer job lambda capture list |

## What's Next

**Phase 2 — Channel adapters (one at a time):**
Each channel needs its own file upload API integration:
- Discord (file upload via Discord API)
- WhatsApp (media upload via WhatsApp Business API)
- Slack (files.upload API)
- Telegram (sendDocument/sendPhoto API)

Per-channel `attachments_enabled` toggle in `/channels` config UI.

**Phase 3 (optional):**
- Drag-and-drop file upload from admin UI (user → agent direction)
- Attachment size limits per agent config
- Thumbnail generation
- Social platform attachment delivery

**Ticket 119 (VSCode extension):** Will consume the same `/api/v1/sessions/:key/attachments/:id` endpoint and render attachments the same way.

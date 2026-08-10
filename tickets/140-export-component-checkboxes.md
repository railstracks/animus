# Ticket 140: Export Component Checkboxes

**Status:** Planned
**Created:** 2026-08-10
**Priority:** Medium

## Problem

The agent export (`GET /api/v1/agents/:id/export`) always includes the same
components. The backend already has granular `AgentArchiveComponentFlags`
(15 booleans), but there's no way for the user to choose which components to
include from the UI.

This matters because sessions + session_turns dominate archive size. A full
export with sessions can be hundreds of MB; without them, the base archive is
typically <5 MB. Users need to choose.

## Scope

### Backend
- `GET /api/v1/agents/:id/export` accepts query params for each flag:
  `?sessions=true&gallivanting=true&embeddings=false` etc.
- Default values match current `AgentArchiveComponentFlags` defaults
  (sessions=false, reports=false, most others=true).

### Frontend
- Export button on `/agents` page opens a dialog with checkboxes:
  - Core (always included, greyed out): Agent record, Memory layers, Memory files
  - Optional: Ontology, Schedules, Gallivanting (+ history sub-checkbox),
    Diary, Sessions, Session Reports, Attachments, Lua scripts, Prompt logs,
    Embeddings, Auth tokens, Projects
- On confirm, calls export endpoint with selected flags and triggers download.

### Future: Large Export Handling
- If archive exceeds ~500MB, write to disk and return a download URL instead
  of streaming the response.
- Optional splitting for session-heavy archives (separate files for sessions
  by time range).
- Not in scope for this ticket — file as follow-up if needed.

## Testing
- Export with all flags on, verify archive contents match DB.
- Export with sessions=false, verify sessions/turns absent from archive.
- Import both exports round-trip clean.

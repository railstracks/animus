# Ticket 140: Export Component Checkboxes

**Status:** ✅ Shipped
**Created:** 2026-08-10
**Completed:** 2026-08-12
**Priority:** Medium
**Commits:** `9d05410`, `6359b3e`, `a0dea91`

## Problem

The agent export always included the same components. The backend had granular
`AgentArchiveComponentFlags` (15 booleans) from Ticket 139's import work, but
the frontend sent `components: "all"` with no UI for selection.

## What Was Done

### Frontend
- Export button on `/agents` now opens a dialog with grouped checkboxes:
  - **Core (always included, disabled):** Agent record, Memory layers, Memory files
  - **Optional components:** Ontology, Schedules, Gallivanting (+ nested History sub-checkbox), Diary, Lua scripts, Attachments, Prompt logs, Embeddings toggle
  - **Session data:** Sessions+Turns and Session Reports, both off by default with size hints
- `confirmExport()` builds a component string array from checkbox state and sends
  it as `POST /api/v1/agents/:id/export` with `JSON.stringify({components: [...]})`.

### Backend
- Component array parsing already existed from Ticket 139 — frontend just wasn't using it.
- **Bug fixed:** The `"all"` shortcut check called `.asString()` on the JSON
  `components` value without an `.isString()` guard. When the frontend sent an
  array, this threw `Json::LogicError: Type is not convertible to string`,
  crashing the export handler with HTTP 500.
  - Fix: `(*body)["components"].isString() &&` guard before `.asString()` comparison.
- **Postgres type mapping fixes** (found during crash investigation):
  - `boolean` (Oid 16) was unmapped → now maps to `ColumnType::Integer`
  - `json` (Oid 114) and `jsonb` (Oid 3802) were unmapped → now map to `ColumnType::Text`
  - `ColumnInt64` now converts Postgres `"t"`/`"f"` boolean text to 1/0
    (previously `std::atoll` returned 0 for both)
- **Try-catch** added around export handler so future exceptions return JSON
  errors instead of crashing Drogon.

## Root Cause of Export Crash

```
20260812 07:17:32 UTC ERROR Unhandled exception in
/api/v1/agents/.../export, what(): Type is not convertible to string
```

**Control flow:** Frontend sends `{"components": ["ontology", ...]}` (array).
Backend processes the array correctly, then falls through to the `"all"`
shortcut check which calls `.asString()` on the array value → JsonCpp throws.

The try-catch didn't catch the initial crash because the old binary was running.
After rebuild, the `.isString()` guard prevents the call entirely.

## Testing
- [x] Export with default selection (no sessions/reports) — succeeds
- [x] Export with all components — succeeds
- [x] Build clean on workstation
- [ ] Import round-trip verification (pending Ticket 141)

## Follow-ups
- Large export handling (>500MB → disk + download URL) — not needed yet
- Session dedup on merge import — Ticket 141
- Tiered turn storage — Ticket 142

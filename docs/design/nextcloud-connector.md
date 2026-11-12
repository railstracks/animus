# Nextcloud Connector — office api package + Talk channel adapter

**Status:** v2 — owner rulings folded 2026-11-12 (Kestrel)
**Tickets:** #293 (office package) + #69 (Talk adapter) — one connector effort, **two artifacts**
**Evidence:** probe pass on #162 (comment 6772647442) and #293 (comment 6782390182); ✅ items live-verified against `https://nextcloud.steadyfort.com` (NC 35.0.2.2, Collabora 26.04.5.2, spreed 25.0.7); Talk API ground truth read from installed source Nov 12.

## 1. The split (owner ruling, Nov 12)

Nextcloud is **two artifacts**, not one:

1. **`nextcloud` api package** — the office/workspace surfaces: Files (WebDAV), shares, Deck, Notes, Calendar (CalDAV), Activity, plus **recording-control actions** (recordings land in Files; they're file operations). A bag of actions, exactly as before.
2. **Nextcloud Talk channel adapter** — Talk sessions become **independent Animus sessions on the standard channel infrastructure**, the same as Discord or IRC. Talk is a chat network that also happens to carry calls: direct chat is the primary surface, voice rides on top. It does not translate to package actions any more than Discord does.

"Office functions" was a development *direction*; materially 0.5 is a Nextcloud connector containing both — but session-shaped things get session architecture, and file-shaped things get package actions.

## 2. Auth & connection model (shared across both artifacts)

- Dedicated service account `animus-agent` + **app password** (**ruled Nov 12**). Its work context subsumes the user-creds case (workspace collaboration = share a folder to `animus-agent`) while keeping an independent AI presence. App-password sessions act *fully as the owning user* — activity, comments, ownership all attribute to the account; only the auth/audit log's token device name distinguishes the session.
- Package: `connect` action validates and stores the precomputed `Authorization: Basic ***` header as one secret (`state.auth_header`); `user` + `base_url` plain for display.
- Adapter: same app-password model — the existing C++ `NextcloudAdapter` already implements it (this is evidence it works, not a commitment to keep that code).
- NC app passwords are account-wide (unscoped); acceptable for single-tenant self-host.

## 3. Egress

Package: derive-or-declare per #105 — request templates resolve to `{{state.base_url}}`, scope = the instance, declared in the manifest anyway. STT/TTS are separate packages with their own egress entries. Adapter: channel-adapter egress model, as IRC/Discord.

## 4. Package action inventory (office)

Legend: ✅ probe-proven live · ⚠️ endpoint shape verified, action untested · ❓ mini-probe during build

**Files (WebDAV):** `files list` (PROPFIND) ✅ · `file get/put/delete` ✅ (6 MB byte-exact round-trip) · `share create/list/delete` ✅ (public URL 200 unauthenticated)

**Deck:** `deck boards/stacks` ✅ · `deck card create/update/move/delete` ✅

**Notes:** `note list/get/create/update/delete` ✅ (full round-trip)

**Calendar (CalDAV):** `cal list` (PROPFIND) ✅ · `event put/delete` ✅ (VEVENT 201/204) · `event query` (REPORT time-range) ⚠️

**Activity:** `activity feed` — OCS read ❓

**Recording control (notetaker's hands):** `recording start/stop/status` ⚠️ (route shape verified; live test needs a real call)

Talk messaging actions are **gone from the package** — they belong to the adapter.

## 5. Talk channel adapter (#69)

**Session model:** rooms → Animus sessions on the standard channel infrastructure. Ground truth from installed source (`custom_apps/spreed/lib/Room.php`): room types are `TYPE_ONE_TO_ONE=1`, `TYPE_GROUP=2`, `TYPE_PUBLIC=3`, `TYPE_CHANGELOG=4`, `TYPE_ONE_TO_ONE_FORMER=5`, `TYPE_NOTE_TO_SELF=6`.

- **DMs factor in naturally** (investigation item resolved): a DM is a room of type 1 in the same API as everything else — the adapter maps it like a Discord DM lane, no special protocol. Group rooms = channels; public rooms = joinable servers, effectively.
- **Direct chat first:** message send/readback is the same API either way (`lookIntoFuture` long-poll proven in #162). This ships before voice.
- **Inbound voice:** spreed 25.0.7 has the **TaskProcessing speech-to-text pipeline wired in** (integration points live in `Recording/Listener.php`, `Chat/Changelog/Manager.php`, `Capabilities.php`, `Notification/Notifier.php`) — **but no STT backend app is installed** (`app:list` shows spreed alone; no assistant/speech app). So: the mechanism is builtin, the *worker* isn't. Options: install a community STT backend app, or wire our own #69 STT package as the TaskProcessing backend. Ruling needed at Phase C.
- **Outbound voice:** no builtin TTS — synthesis is ours. TTS package → audio file → share into the room (Talk plays shared audio inline). Correct as stated by owner; the synthesis work is the #69 voice lane.

**Bots:** `occ talk:bot:install` exists (probe-proven); whether the adapter presents as a bot participant or a plain user session is a Phase B implementation question. Bot webhooks, when they come, are a kernel inbound lane — and land naturally as an **interjection source for the Active session** (#295), not as package machinery.

## 6. Notetaker flow (Phase B composition)

1. Trigger (scheduled, or the Active session) → package `recording start` on the target room
2. Call ends → `recording status` → recording lands in Files (talk-recording backend verified healthy, #162)
3. Package `file get` pulls it → STT (separate package, per #69 stance) → transcript
4. Package `note create` writes the meeting note → **adapter posts the summary to the room** → optional `deck card create` for action items
5. `share create` if humans need a link

Orchestration lives in the agent/scheduler/Active session — the package stays a bag of actions, the adapter stays a channel.

## 7. Phasing

- **Phase A (0.5.0):** office package skeleton + Files/Shares/Deck/Notes/Calendar actions + tests (fixture discipline: every test cleans up). Registry-published per SOP.
- **Phase B (0.5.0):** Talk channel adapter — sessions, DM lanes, group rooms, message send/readback — plus the notetaker flow against a real call (two participants).
- **Phase C (0.5.x):** voice lanes (STT backend ruling, TTS package), bot presence/webhooks → Active-session interjection source, per-agent identities, activity triggers, and — owner direction Nov 12 — an internalized **contacts module**: a person registry that tracks identities and (as voice lanes mature) voice-prints, matching them to people so the agent recognizes humans *across channels*, not per-adapter. Comes later in 0.5.*; the 0.5.0 starting point is Nextcloud-only.

## 8. Open items

- ❓ **ApiRuntime verb support** (Phase A blocker): does the declarative `request` accept PROPFIND/REPORT? Fallback: script-side HTTP if sandboxed, else adapter-assisted reads.
- Basic-auth header from state: expected per alpaca pattern; confirm in build.
- STT backend absent on instance (evidence above) — Phase C ruling.
- ✅ Keeper/backup — resolved Nov 12 (#294 live).
- ✅ DM investigation — resolved Nov 12: DMs are type-1 rooms, same API.

## Appendix: manifest sketch (v2, office-only)

```json
{
  "kind": "api_package",
  "name": "nextcloud",
  "version": "0.1.0",
  "description": "Nextcloud office connector (files, shares, deck, notes, calendar, activity, recording control)",
  "keywords": ["nextcloud", "office", "workspace", "collaboration"],
  "dispatch_cooldown_ms": 2000,
  "files_quota_mb": 512,
  "state_schema": {
    "base_url":    {"type": "string", "default": "https://nextcloud.steadyfort.com"},
    "user":        {"type": "string", "default": "animus-agent"},
    "auth_header": {"type": "string", "secret": true}
  },
  "commands": [
    {"name": "connect", "kind": "action",
     "parameters": {"user": {"type": "string"}, "app_password": {"type": "string", "secret": true, "required": true}},
     "script": "…validate against status.php, store precomputed auth header…"},
    {"name": "files list", "kind": "action",
     "request": {"method": "PROPFIND", "url": "{{state.base_url}}/remote.php/dav/files/{{state.user}}/",
                 "headers": {"Authorization": "{{state.auth_header}}", "Depth": "1"}},
     "script": "…parse multistatus → name/size/id rows…"},
    {"name": "note create", "kind": "action",
     "request": {"method": "POST", "url": "{{state.base_url}}/index.php/apps/notes/api/v1/notes",
                 "headers": {"Authorization": "{{state.auth_header}}", "OCS-APIREQUEST": "true"},
                 "body": {"title": "{{args.title}}", "content": "{{args.content}}"}},
     "script": "…return note id + url…"}
  ]
}
```

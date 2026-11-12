# nextcloud (api package, v0.3.0 — Phase A + write lane + recording control)

Nextcloud office connector for Animus: **Files (WebDAV)**, **Notes**, **Calendar (CalDAV)**, **Shares**, **Talk recording control** — read and write lanes. Part of #293; Talk *messaging* is deliberately **not** here — the Talk channel adapter is a separate artifact (#69, session architecture, same as Discord). Recording control is a room utility (moderator surface), specced to the package per the ruling; the adapter stays the message lane.

## Connect

1. As the instance owner, create the `animus-agent` user (once) and log in as it: **Settings → Security → Devices & sessions → Create new app password** (label it `animus`).
2. Run the package's `connect` action with `user` + `app_password`. The script validates the credentials against `ocs/v2.php/cloud/user` **before** anything is stored, then persists:
   - `user` — the account name (default `animus-agent`)
   - `auth_header` (secret) — the precomputed `Authorization: Basic ***` header

Every request template interpolates `{{state.auth_header}}`; the raw password is never stored.

## The write lanes (v0.2.0, `writes_gated: true`)

Writes split by payload trust, matching the kernel's two write mechanisms:

- **Declared-template writes — #106-bound (no per-write friction).** `file put`, `file delete`, `event put`: raw-body templates whose content is byte-safe under plain interpolation (file bytes, iCalendar text). These ride the package content-hash approval — one owner approval of the package covers the declared write shapes.
- **Script-composed writes — #126 per-invocation approval (one-shot, payload-bound).** `note create`, `share create`: bodies built at runtime via `json.encode` because arbitrary text cannot ride JSON template interpolation safely. Each exact payload holds once pending owner approval, then passes; a modified payload is a new digest.

Reads (`files list`, `note list`, `cal list`) are never gated.

## Recording control + file get (v0.3.0)

The notetaker's hands (design doc §6): the agent orchestrates, the package provides utility actions.

- **`recording status`** (read) — room payload's `callRecording`: 0=none, 1=video, 2=audio, 3=video_starting, 4=audio_starting, 5=failed; plus `hasCall`/`activeSince`.
- **`recording start`** (declared-template write, #106-bound) — `status`: 1=video, 2=audio. Requirements live-verified against spreed 25.0.7 (source + live lane): **moderator of the room required**, and **an active call must exist**, else 400 with the mapped reason (`classified|status|recording|call|config`).
- **`recording stop`** (declared-template write, #106-bound) — stop + finalize; the file lands in the *starter's* Files under `Talk/` moments later.
- **`file get`** (read) — WebDAV GET into the sandbox files area: returns `{path, size, file}` (the sandbox slug); content inlined only when ≤ 4 KiB — the kernel caps result strings, so large payloads travel via the files lane (`files_quota_mb` caps loudly). Binary-safe end to end.

Live lane evidence (Nov 12, room `mzpfyojc`): start → ACTIVE (~3s) → stop → finalized, with room system-message receipts (`audio_recording_started`/`audio_recording_stopped`). File materialization needs a call with actual media — the headless empty-call probe proves control, not content.

## Scope & egress

- `egress_hosts` is bound to `nextcloud.steadyfort.com` (single-tenant). For another instance, edit the manifest **before** install — registry installs are egress-manifest-bound, there is no post-install override.
- `files list` / `cal list` parse the WebDAV multistatus with minimal Lua patterns (NC emits the `d:` namespace prefix consistently); a body that parses to zero rows fails loudly rather than returning an empty success.
- Paths in `file put` / `file delete` / `file get` are percent-encoded by the caller (`%20` for spaces).
- Binary uploads are a follow-up (script-lane b64 decode); v0.2 `file put` is text-safe raw body.

## Provenance

Endpoint shapes live-verified against NC 35.0.2.2 (probe evidence: #162 findings, #293 office-probe pass 1; OCS requires `Accept: application/json` + `OCS-APIREQUEST`). Kernel properties this package leans on are pinned by tests in `ApiRuntimeTests`: declared PROPFIND/PUT execute ungated-in-read-terms and verb-preserved on the wire (HttpClient verbatim-verb fix), and the declared-vs-script write split is asserted explicitly. v0.3.0 also pins the NUL-safety of the Lua string bridge (embedded-NUL bodies survive the round-trip — found live: a 50 KB PNG truncated to its 8-byte magic header) and the files-lane shape of `file get` (no oversized inline strings in JSON results).

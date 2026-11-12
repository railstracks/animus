# nextcloud (api package, v0.2.0 — Phase A + write lane)

Nextcloud office connector for Animus: **Files (WebDAV)**, **Notes**, **Calendar (CalDAV)**, **Shares** — read and write lanes. Part of #293; Talk is deliberately **not** here — the Talk channel adapter is a separate artifact (#69, session architecture, same as Discord).

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

## Scope & egress

- `egress_hosts` is bound to `nextcloud.steadyfort.com` (single-tenant). For another instance, edit the manifest **before** install — registry installs are egress-manifest-bound, there is no post-install override.
- `files list` / `cal list` parse the WebDAV multistatus with minimal Lua patterns (NC emits the `d:` namespace prefix consistently); a body that parses to zero rows fails loudly rather than returning an empty success.
- Paths in `file put` / `file delete` are percent-encoded by the caller (`%20` for spaces).
- Binary uploads are a follow-up (script-lane b64 decode); v0.2 `file put` is text-safe raw body.

## Provenance

Endpoint shapes live-verified against NC 35.0.2.2 (probe evidence: #162 findings, #293 office-probe pass 1; OCS requires `Accept: application/json` + `OCS-APIREQUEST`). Kernel properties this package leans on are pinned by tests in `ApiRuntimeTests`: declared PROPFIND/PUT execute ungated-in-read-terms and verb-preserved on the wire (HttpClient verbatim-verb fix), and the declared-vs-script write split is asserted explicitly.

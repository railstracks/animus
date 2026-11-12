# nextcloud (api package, v0.1.0 — Phase A)

Nextcloud office connector for Animus. Read lane first: **Files (WebDAV)**, **Notes**, **Calendar (CalDAV)**. Part of #293; Talk is deliberately **not** here — the Talk channel adapter is a separate artifact (#69, session architecture, same as Discord).

## Connect

1. As the instance owner, create the `animus-agent` user (once) and log in as it: **Settings → Security → Devices & sessions → Create new app password** (label it `animus`).
2. Run the package's `connect` action with `user` + `app_password`. The script validates the credentials against `ocs/v2.php/cloud/user` **before** anything is stored, then persists:
   - `user` — the account name (default `animus-agent`)
   - `auth_header` (secret) — the precomputed `Authorization: Basic …` header

Every request template interpolates `{{state.auth_header}}`; the raw password is never stored.

## Scope & egress

- `egress_hosts` is bound to `nextcloud.steadyfort.com` (single-tenant). For another instance, edit the manifest **before** install — registry installs are egress-manifest-bound, there is no post-install override.
- All four v0.1 commands are reads (GET/PROPFIND). Writes (`file put`, `note create`, `event put`, `share create`, …) land in the next version behind `writes_gated: true` + the #126 per-invocation approval gate.
- `files list` / `cal list` parse the WebDAV multistatus with minimal Lua patterns (NC emits the `d:` namespace prefix consistently); a body that parses to zero rows fails loudly rather than returning an empty success.

## Provenance

Endpoint shapes live-verified against NC 35.0.2.2 (probe evidence: #162 findings, #293 office-probe pass 1; OCS requires `Accept: application/json` + `OCS-APIREQUEST`). The declared-PROPFIND-ungated property this package leans on is pinned by a kernel test (`ApiRuntimeTests`, #126 gate suite).

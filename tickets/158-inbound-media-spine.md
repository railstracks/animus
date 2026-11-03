# Ticket 158: Inbound Media Spine — Egress-Scoped Download, sha256 Vault, Refs to Context

**Created:** 2026-11-03 (recon/design-grade start, gallivanting block 380)
**Status:** Recon complete — NOT dispatched. GitHub issue #158 carries the settled
design (Melvin, Oct 12). This file grounds it in the codebase and names the one
open design decision (scope binding) that must be settled before implementation.
**Branch target:** `dev` (spec work only in this pass; no code changed)

## Problem (from issue #158)

Inbound media (Bluesky, Discord, Mastodon, Slack) arrives as URL + metadata.
Today the URL is at best flattened into message text; the agent never gets a
reliable, deduped, size-capped local artifact, and platform metadata
(dimensions, duration, alt text) is dropped. Bytes must never enter the prompt.

## Code recon (what exists, verified 2026-11-03, dev @ 87418f6)

- **`src/kernel/attachment/AttachmentStore.cpp`** (Ticket 123) — DB table
  `session_attachments` (id, turn_id, session_key, filename, mime_type,
  filepath, data_b64, size_bytes, created_at). Session-scoped row storage for
  the **admin chat** lane; consumers are `admin/ChatSessionService.cpp` +
  `AdminServerInternals.h` only. NOT content-addressed; no media metadata
  fields. Its `data_b64` inline path is for the admin UI — **must not** be
  reused for inbound channel media (bytes-near-prompt hazard).
- **`src/kernel/tools/AttachmentTool.cpp`** — base64 encode + extension-based
  MIME sniffing; same admin lane. No download, no vault, no per-type caps.
- **#25 egress gate is LIVE and reusable** —
  `src/kernel/api/ApiConnectionManager.cpp:381–399`:
  `ApiRuntime::ParseEgressHosts(pkg.egress_hosts)` +
  `ApiRuntime::EgressAllowed(scope, url, …)` with WARN
  *"outside package scope — declare egress_hosts in the manifest"*; redirects
  re-checked per hop via `ApiRuntime::ExecuteScoped`. **Scope is per-package**
  (manifest field) — that's the crux below.
- **Kernel channel adapters** — `src/kernel/channel/` (Telegram, IRC, VK,
  `RemainingAdapters.cpp`, `PollerAdapterBase`), `DiscordGatewayLoop.cpp`,
  `WhatsAppGatewayLoop.cpp`. `ChannelManager.cpp` has **no inbound-media
  path** (grep clean). Per-adapter attachment-array extraction points to be
  pinned at implementation (Discord `attachments[]`, Slack `files[]`).
- **Bluesky & Mastodon are Lua api-packages** (`state/lua/_shared/bluesky.lua`,
  tickets 050/129/131/132; Mastodon rides #258 merge-not-copy) — they already
  live inside the #25 package-scope world.
- **Context card seam exists** — `src/kernel/context/` (participants card
  #46/#260 is the fresh precedent for structured additions to agent context).

## Design

**Pipeline (per inbound media item):** adapter extracts {url, mime?, size?,
dimensions?, duration?, alt_text?, filename?} → `MediaIngest` downloads via a
scoped HTTP client → content-addressed store → ref appended to the inbound
message block in context. Never execute; never render bytes.

**Storage — two layers, keep both:**
1. `MediaVault` (NEW): global content-addressed file store under
   `<data_dir>/media/<aa>/<sha256>`. Dedup by hash — refeeding the same
   attachment hits the store, no re-download (acceptance criterion). Per-type
   size caps (image/video/audio/file) with loud rejection, no truncation.
2. `AttachmentStore` rows remain the session view (which turn/session saw
   which ref), with `filepath` pointing at the vault path. No schema break
   for the admin lane.

**Ref format surfaced to context:** `{ref, mime, size, dimensions?, duration?,
alt_text?}` where ref = vault-relative path. Text of the message keeps the
sender's own URL as-is; the ref block is additive.

## Open design decision — scope binding (blocks dispatch)

#25's gate is **per-package**; kernel channels (Discord, WhatsApp, Telegram,
Slack) are not packages. Options:

- **(a) channel-config `egress_hosts`** mirroring the manifest field, enforced
  by the SAME `ApiRuntime::EgressAllowed` helper — no second gate. Recommended
  for kernel channels; per-channel CDN allowlists (cdn.discordapp.com,
  files.slack.com, …) are small and stable.
- **(b)** one system-scope allowlist for channel CDNs in kernel config —
  simpler, coarser; acceptable fallback.
- **(c)** package-side channels (Bluesky/Mastodon) simply declare the blob/CDN
  hosts in their existing manifests — no new mechanism needed.

Recommendation: (a) + (c), sharing the enforcement helper and WARN text.

## Open questions (Melvin / implementer)

- Cap defaults per type (lean: image 25 MB, audio 50 MB, video 200 MB, file 50 MB — confirm).
- Vault retention: content is shared across sessions → deletion needs refcount
  or tombstone policy; v1 = no GC, document.
- Per-agent vs per-kernel vault scoping (agent export/import #139/#140 interplay).
- Metadata trust: platform-reported mime/size vs sniffed on download (report both when they disagree).

## Acceptance criteria (from issue #158)

- [ ] Inbound attachments from Bluesky, Discord, Mastodon, Slack vault content-addressed
- [ ] Egress scope enforced on CDN downloads (deny + WARN outside scope)
- [ ] Size caps per type, loud errors
- [ ] Agent context shows refs with metadata; alt text carried when present
- [ ] Refeeding same attachment hits the sha256 store, no re-download

## Test strategy

- Vault: dedup identity, cap rejection (loud, no partial file), filename
  path-traversal rejection.
- Egress: host outside scope → WARN + no file written; redirect hop crossing
  scope → denied (reuse #25 semantics).
- Adapter fixtures: Discord `attachments[]`, Slack `files[]`, Mastodon
  `media_attachments` (incl. alt text), Bluesky blob post shapes.
- Regression: prompt log contains no base64 media payloads (grep prefix).

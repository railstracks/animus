# API Packages — Manifest Specification (v1, draft)

**Status:** draft for review — implements issue #21. The schema files live in [`schemas/`](../schemas/); the worked example in [`examples/alpaca/`](../examples/alpaca/) (draft fixture for the #26 pilot).

An API package turns a third-party service into agent tools as **configuration, not code**: typed actions (HTTP request templates + Lua response processors), optional event listeners, a local state store, vault-indirected auth, and an explicit egress allowlist. The kernel's `api` tool interprets the package (#22); agents receive per-action tool schemas generated from this manifest.

> **Registry lists; maintainers host.** Packages live in the maintainer's own Git repository. The Animus Registry (successor to the SOP registry) clones at submission time, validates, hashes, and records a listing — it never becomes the canonical host. Instance-side verification then trusts the *hash*, not the hosting server.

---

## 1. Package repository layout

A package is a Git repository:

```
api-package.json          # the manifest (this spec)
processors/*.lua          # response/event processors referenced by the manifest
...                       # any supporting files (docs, tests)
```

Constraints enforced at submission: no submodules, no Git LFS objects (pointer files would hash as-is and fetch nothing on install). The registry's tree walk covers **every file except `.git/`** — supporting files are part of the content hash.

## 2. Manifest reference (`api-package.json`)

Authoritative shape: [`schemas/api-package-manifest-v1.json`](../schemas/api-package-manifest-v1.json). Summary:

| Section | Required | Purpose |
|---|---|---|
| `manifestVersion` | ✓ | `1` |
| `package` | ✓ | identity: `name` (globally unique slug), semver `version`, `description`, optional `license`/`homepage`/`repository`/`keywords`/`publisher` |
| `compatibility` | – | `animus`: semver range tested against (e.g. `>=0.4.0`) |
| `auth` | ✓ | scheme + vault references. `none` \| `bearer` \| `header` \| `headers` \| `query` \| `basic` |
| `egress` | ✓ | `domains`: the complete, exact-hostname allowlist (max 16) |
| `state` | – | JSON Schema of the package-local state struct |
| `actions` | ✓ | 1–64 actions: `name`, `summary`, `params` (JSON Schema), `request` (HTTP template), `response.processor` |
| `events` | – | reserved for v0.5 (#22 phasing); v0.4 interpreters ignore with a warning |

### 2.1 Auth — `secret_ref` indirection only

Every credential is a **vault key** (`secretRef`), resolved kernel-side against the secrets vault (#23). The schema is strict-shaped (`additionalProperties: false` on every auth variant) so a literal token is **structurally unrepresentable** — there is no field that could carry one. The negative example in `examples/invalid/` is rejected by validation and serves as the regression witness for this property.

The kernel injects auth at request time per scheme (e.g. `bearer` → `Authorization: Bearer <secret>`; `headers` → one header per entry — Alpaca's `APCA-API-KEY-ID` + `APCA-API-SECRET-KEY` pair). Auth headers never appear in `request.headers`.

### 2.2 Actions

```json
{
  "name": "get_bars",
  "summary": "OHLCV bars for a symbol",
  "params": { "type": "object", "properties": { "symbol": { "type": "string" }, "timeframe": { "type": "string" } }, "required": ["symbol"] },
  "request": {
    "method": "GET",
    "url": "https://data.alpaca.markets/v2/stocks/{symbol}/bars",
    "query": { "timeframe": "{timeframe}", "limit": "100" }
  },
  "response": { "processor": { "language": "lua", "file": "processors/bars.lua" } }
}
```

**Placeholder substitution.** `{name}` in `url`, `query` values, and `body.template` string values references a declared param. Rules:

- Only declared `params` properties may be substituted (the interpreter rejects unknown placeholders — a typo is a loud error, not a silent blank).
- URLs are `https://` only — secrets never cross plaintext transport.
- Substitution is literal string replacement after param validation; the interpreter URL-encodes query values.
- `body.template` is an object (serialized as JSON) or a string (sent verbatim).

**Tool mounting (#22).** Each action mounts as `api.<package>.<action>` with its tool schema generated from `params` — agents trust schemas over prompts.

**Response processors.** Lua files run in the existing sandbox under `animus.http_get/post/delete` availability, the package egress allowlist, and the sandbox audit rules. The processor's returned string becomes the tool result. Processors may read/write package state (#24).

### 2.3 State — intentions, never mirrors

`state.schema` describes the local struct. The design rule from the devnotes: state holds things that **exist nowhere else** — the Alpaca pilot's strategy book (positions with strategic reasoning, entry time/volume/price, target exit, exit limits) is local because it exists nowhere else; broker positions are always pulled live. Mirrors drift; we don't build them. `advisoryMaxBytes` is advisory — the kernel's own hard cap governs (#24).

### 2.4 Events (reserved, v0.5)

Shape: `name`, `transport: "websocket"`, `wss://` URL, `subscribe` frames, Lua processor. Secret injection inside subscribe frames uses the `{"$secretRef": "name"}` marker object. The intended runtime role: processor as **filter/aggregator/threshold** — event floods must not dispatch a chain per tick; Lua decides when something is chain-worthy. v0.4 interpreters must ignore the section (with a warning), keeping v0.4-authored packages forward-compatible.

## 3. Registry listing record

Authoritative shape: [`schemas/registry-listing-v1.json`](../schemas/registry-listing-v1.json). The registry computes everything except `package` identity from its own clone:

| Field | Meaning |
|---|---|
| `source.git` + `source.commit` | maintainer's repo + immutable 40-hex pin (the submitted `ref` is recorded as provenance history) |
| `integrity.manifestDigest` | SHA-256 over the `api-package.json` bytes |
| `integrity.contentHash` | MD5 over the combined source tree (definition below); algorithm carried in-band so a future switch to SHA-256 changes values, not format |
| `limits` | observed `treeBytes` / `fileCount` / `largestFileBytes` — drift across versions becomes visible |
| `moderation` | `official` flag + `status` (`pending`/`approved`/`rejected`) — registry-side approval gate |

### 3.1 Submission pipeline

1. **Clone with caps.** `git clone --depth 1` of the submitted ref (or `git fetch --depth 1 origin <sha>` when the server permits direct SHA fetch). Timeout 120 s. Submodules and LFS pointers are rejected.
2. **Enforce size caps before hashing.** Tree walk at the pinned commit; defaults: **tree ≤ 64 MiB, ≤ 4096 files, largest file ≤ 16 MiB.** Over-cap submissions are rejected loudly — oversized repositories are an attack on the registry, not a package.
3. **Validate the manifest** against `schemas/api-package-manifest-v1.json`; reject invalid.
4. **Compute integrity:** manifestDigest (SHA-256 of `api-package.json`) + contentHash (below).
5. **Record the listing** with size stats and moderation state.

Caps and timeouts are registry tunables; the defaults above are the v1 recommendation.

### 3.2 Content hash definition

Deterministic walk of the tree at the pinned commit, excluding `.git/`:

1. List all files, **sorted by path, UTF-8 bytewise ascending**.
2. For each file, feed into one MD5 stream: `path` bytes, `0x00`, git `mode` (`100644`/`100755`/`120000`), `0x00`, content bytes (for symlinks: the link target string), `0x00`.
3. `contentHash` = MD5 of the stream, hex.

No timestamps, no permissions beyond git mode, no packing order — two independent implementations of this definition produce identical hashes. The instance-side installer implements the same walk and **refuses to install on mismatch** with the listing record. A malicious maintainer can rewrite history or swap content on their own server; the mismatch is detected at install time because the instance compares bytes, not trust.

MD5 is the v1 algorithm (registry cost, ubiquity). The threat it covers is content substitution, where the instance-side byte comparison is the actual gate; if collision-resistance becomes a concern, `algorithm: "sha256"` is already representable — a registry-side switch with a re-hash pass, no format change.

## 4. Versioning policy

- `manifestVersion` governs the manifest format. Additive optional fields may appear within v1 (validators with `additionalProperties: false` must be regenerated alongside). Breaking changes → v2.
- Package `version` is semver; the registry keys listings by `name@version` with the commit pin making each immutable — a re-submission of the same version with different content is a new commit pin and is rejected as a mutation attempt.

## 5. Validation

```sh
npx --yes ajv-cli@5 validate -s schemas/api-package-manifest-v1.json \
  -d "examples/alpaca/api-package.json" --spec=draft2020

# security regression witness — must FAIL:
npx --yes ajv-cli@5 validate -s schemas/api-package-manifest-v1.json \
  -d "examples/invalid/secret-literal.api-package.json" --spec=draft2020 \
  && echo "REGRESSION: literal secret accepted" || echo "ok: rejected"
```

## 6. Open questions (for review)

1. Placeholder grammar — plain `{name}` vs a namespaced `{{param.name}}` if body templates ever need literal braces. Default: plain, reject unknown placeholders.
2. Processor files are path-referenced only (no inline Lua strings) — keeps manifests reviewable and diffs clean. Confirm.
3. `egress.domains` max 16 — enough? (Alpaca needs 2.)
4. Should `state.schema` top level be forced to `"type": "object"` (store is a single JSON struct)?
5. Placeholder lint (all `{placeholders}` ∈ declared params) is an interpreter-side check (#22) — should the registry also run it pre-listing? Recommended: yes.
6. Events marker shape `{"$secretRef": ...}` — reserve now, finalize with v0.5 implementation.

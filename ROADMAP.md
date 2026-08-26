# Animus Roadmap

*Last updated: 2026-08-27. For the narrative version of this roadmap, see [animus.steadyfort.com/roadmap](https://animus.steadyfort.com/roadmap). This file tracks the same plan against the issue tracker — for detail, status, and sequencing, the tracker is the source of truth.*

---

## Current Series: 0.4

Three pillars. Two are commitments; the third is research — and the difference is declared, because it matters.

### Pillar 1 — `api` Ecosystem *(flagship)*

External services become agent tools as configuration, not code. In 0.4, Animus stops being one project and starts being an ecosystem.

| Issue | Scope | Status |
|---|---|---|
| [#21](https://github.com/railstracks/animus/issues/21) | Manifest schema spec (JSON Schema + docs) | **Spec landed** (`55b4395`: `schemas/`, `docs/api-packages.md`, alpaca fixture + invalid witness) — under review |
| [#22](https://github.com/railstracks/animus/issues/22) | Action interpreter + tool mounting with generated schemas | Next |
| [#23](https://github.com/railstracks/animus/issues/23) | Secrets vault (`secret_ref` indirection, redaction) | Open |
| [#24](https://github.com/railstracks/animus/issues/24) | Package state store — intentions, never world-state mirrors | Open |
| [#25](https://github.com/railstracks/animus/issues/25) | Egress allowlist + SSRF hardening + owner approval gate | Open |
| [#26](https://github.com/railstracks/animus/issues/26) | Alpaca pilot package (maintainer demo case) | Fixture drafted in `examples/alpaca/`; live-API verification with #22 |
| [#29](https://github.com/railstracks/animus/issues/29) | Standing contributor lane: package for a service *you* use | Open — opens once #21/#22 land |
| [animus-sop#1](https://github.com/railstracks/animus-sop/issues/1) | Registry-side pipeline: clone caps, validation, integrity hashing, listing records (Animus Registry) | Open |

Registry model: **the registry lists; maintainers host.** Packages live in the maintainer's own Git repository; the registry clones at submission, validates, hashes, and records a commit-pinned listing. Instances verify downloaded content against the hash, not the hosting server. Design principles and the content-hash definition: [`docs/api-packages.md`](docs/api-packages.md).

**v0.4.0 scope:** actions + state + static bearer auth, human-approved packages, Alpaca pilot. WebSocket events move to v0.5; registry exchange and agent-authored packages come later.

### Pillar 2 — Expansion & Hardening *(flagship)*

Caring for what already ships: feature completeness and hardening across the live surface. Unglamorous, and exactly what makes an ecosystem worth building on.

**Epic A — trustworthy channels** (control plane vs. data plane; design in [devnotes-0.4.0.md](https://github.com/railstracks/animus-notes/blob/main/devnotes-0.4.0.md), direction post in [discussion #28](https://github.com/railstracks/animus/discussions/28)):

| Issue | Scope | Status |
|---|---|---|
| [#14](https://github.com/railstracks/animus/issues/14) | ChannelContextStore (arrival metadata + ReplyTarget persistence) | Complete — [PR #34](https://github.com/railstracks/animus/pull/34), pending merge |
| [#15](https://github.com/railstracks/animus/issues/15) | ChannelContextProvider card + instruction-hierarchy sentence | Complete — PR #34 |
| [#16](https://github.com/railstracks/animus/issues/16) | Reply resolution from session ReplyTarget + ID validation | Complete — PR #34 (supersedes the #33 stopgap) |
| [#17](https://github.com/railstracks/animus/issues/17) | Bluesky adapter consolidation (both wrapping paths) | Open — unblocked by #14+#16 |
| [#18](https://github.com/railstracks/animus/issues/18) | Bluesky thread sessions: `thread:<rootUri>` routing | Open |
| [#19](https://github.com/railstracks/animus/issues/19) | Ancestor hydration via getPostThread | Open |
| [#20](https://github.com/railstracks/animus/issues/20) | Seen-set watermark + branch roster card | Open |

**Adapter backlog:**

| Issue | Scope |
|---|---|
| [#30](https://github.com/railstracks/animus/issues/30) | Auto-split yielded messages at per-adapter limits + session-visible send failures |
| [#31](https://github.com/railstracks/animus/issues/31) | Discord receive-path debug (root cause diagnosed; fix rides Epic A plumbing) |
| [#32](https://github.com/railstracks/animus/issues/32) | Reflect message edits/deletions in agent history |
| [#36](https://github.com/railstracks/animus/issues/36) | Bluesky facets — hashtags and links render as plaintext |
| [#37](https://github.com/railstracks/animus/issues/37) | Surface inbound media attachments to agent context (Bluesky first, then Discord) |
| [#38](https://github.com/railstracks/animus/issues/38) | Outbound media: agent-sent images/files per channel |
| [#39](https://github.com/railstracks/animus/issues/39) | Adapter diagnostics: structured path tracing + per-adapter self-test |
| [#40](https://github.com/railstracks/animus/issues/40) | Hardening sweep: test-coverage audit across shipped surfaces |

### Pillar 3 — Experimental Engineering *(research — deliberately not commitments)*

The 0.4 series reserves real energy for exploration: isolated experiments and trial implementations, reported honestly — including null results. Nothing enters the mainline without earning it.

- **Dreaming** — associative offline processing: recombining what an agent encountered into configurations it never experienced, then interpreting the result (non-REM consolidation exists; REM-style free association does not)
- **Perception modulation** — a slow, introspectable internal state shaped by perceived media, influencing generative parameters over time. Not a prompt. A climate.
- **FANN module agent tool** — small feedforward nets as an agent tool: train/run/eval over the agent's own tabular data (devnotes candidate 5)

Design discussion lives in [animus-notes `devnotes-0.4.0.md`](https://github.com/railstracks/animus-notes/blob/main/devnotes-0.4.0.md) (candidates 3–5).

---

## Shipped Foundation

What the 0.4 series builds on — released, live, in daily use.

- **v0.1.0** — public release, July 15 2026
- **Agent runtime** — multi-layer memory with automatic consolidation, embedding-based retrieval, Lua tool runtime, eleven LLM providers, embedded admin UI (23 languages), Docker deployment
- **Communication layer** — twelve channels with unified routing (Bluesky, Discord, Telegram, VK, Twitter, WhatsApp, Slack, IRC, email in/out, …); per-channel agent binding and session routing
- **v0.3.x series** (through v0.3.10, Aug 2026) — scheduler, node delegation (SSH/WebSocket), export/import, session notes & agenda, session reports, compaction, temporal context, token gauge
- **Lua scripting v1** — sandboxed plugin runtime: tool/interface/social registration, managed HTTP, persistent config

Ticket-level history for the pre-0.4 milestones (001–097) is preserved in git history of this file and in `tickets/`.

## Longer Arc

Directions the pillars move toward — not commitments with dates:

- Multi-node task orchestration
- Agent-to-agent communication across networks
- Per-domain tool modules
- Channel adapters as a special case of API packages (convergence, not rewrite — bespoke protocol loops stay bespoke)

## Process

- **Unit of work:** issues. **Scope fence:** the [v0.4.0 milestone](https://github.com/railstracks/animus/milestone/1). **Execution view:** the [project board](https://github.com/users/railstracks/projects/3).
- Design canvas: [animus-notes devnotes](https://github.com/railstracks/animus-notes) · Direction posts: [discussions](https://github.com/railstracks/animus/discussions) (#27 overview, #28 v0.4 direction + integration call)
- **Contributing:** the [#29 lane](https://github.com/railstracks/animus/issues/29) is the standing entry point — draft an API package for a service you use; no C++ required.

# Animus Roadmap

## First Release — Week of June 30, 2026

### Release Blockers — All Clear

| Ticket | Description | Status |
|--------|-------------|--------|
| 093 | Session reports — new table, consolidation integration, API, context injection | **Done** |
| 094 | Active context session mixing — per-layer budgets, session content in context | **Phase 1 Done** |
| 095 | MemoryFile search pagination/browsing — regex, lines range, browse modes | **Done** |
| 079 | Session compaction — 6-bug chain fixed, fully operational end-to-end | **Done** |
| Context window | All AgentKernel paths resolve provider + agent context window | **Done** |
| Diagnostic logging | All debug traces from compaction investigation stripped | **Done** |

### Release Polish

| Item | Description | Status |
|------|-------------|--------|
| 090 | MemoryFile context injection | ✅ Done |
| 091 | Search embeddings for context injection | ✅ Done |
| 092 | MemoryFile processing pipeline | ✅ Done |
| 096 | DAL / PostgreSQL connection pool | ✅ Done |
| 088 | Semantic memory agent-scoped surfacing | ✅ Done |
| 089 | Context window token gauge | ✅ Done |
| Admin UI | Compaction timeline, session reports, token gauge, memory browsing | ✅ Done |

### Pre-Release Cleanup

- [x] Repository cleanup — move internal docs to `animus-notes`, strip stale AGENTS.* files
- [x] Documentation review — AGENTS.md rewritten, README.md current, ROADMAP.md updated
- [x] `.gitignore` — uploads directory added
- [ ] Website update — animus.steadyfort.com content tweaks
- [ ] First-run wizard — more complete initial agent setup
- [ ] License decision — MIT / Apache 2.0 / commercial

### Stretch Goals (deferred to post-release)

- Reddit, Viber, Odnoklassniki adapters

---

## Post-Release Directions

### Track A: Private/Community Bots

Social integration and community-oriented features:

- Further social adapters — Reddit, Viber, Odnoklassniki
- Community bot features — group management, moderation, scheduled posts
- Community support roles — FAQ handling, onboarding, knowledge base surfacing
- Moltbook engagement — deeper agent-to-agent social interaction

### Track B: Professional Bots

Office and enterprise integration:

- Nextcloud integration — file storage, calendar, contacts
- Slack deepening — threads, channels, reactions, slash commands
- Office functions — document processing, scheduling, email triage
- Teams adapter (ticket 078)

### Track C: Core Intelligence

Deepening the cognitive architecture:

- Observation versioning (ticket 087) — copy-on-write revise
- FANN intuition module (ticket 081) — learned pattern recognition
- Cortex peripheral layer (ticket 082) — sensory input beyond text
- Embedding expansion — observations and session turns
- Perspective revision guards — prevent stale confident wrongness
- Session compaction tuning — threshold calibration from production behavior
- Background compaction — async, non-blocking

### Track D: Infrastructure

Hardening and scaling:

- PostgreSQL vector search (096 Phase 3) — pgvector for semantic search
- SQLite → PostgreSQL migration utility (096 Phase 4)
- Production hardening (096 Phase 5) — pool tuning, monitoring
- Docker packaging — containerize embedding model + Animus
- Multi-tenant scaling — agent isolation, resource quotas, concurrent session limits

---

## Completed Milestones (2026)

- **May 3–6:** Project foundation — admin server (001), agent config (002), sessions (003), memory layers (004), WebSocket chat (005), observation stream (006), interfaces (007), constitution (008), SPA scaffold (009), chat UI (010), memory browser (011), resource embedding (012), LLM providers (013–015), chain execution (016), constitutional memory (017), SQLite DAL (019), throttle (020), tool calling + reasoning (021), system/file tools (022), web tools (023), thinking (025), multi-tenant (029), Codex provider (030), agent forms (031), chat panels (032), file tool config (033), IRC (034), admin decomposition (035)
- **May 7–13:** Core systems — diary (036), review scheduling (043), active memory (044), layer-owned intake (045), ontology (046)
- **May 14–20:** Social + tools — gallivanting (038), Lua scripting (039), Telegram (055), channels refactor (056), tool calls UI (057), Twitter (059), Discord (060)
- **May 21–31:** Infrastructure complete
- **June 1–7:** Cognitive architecture planning
- **June 8–14:** Session compaction (079), observation versioning (087)
- **June 15–21:** Memory pipeline — semantic surfacing (088), token gauge (089), MemoryFile injection (090), embedding retrieval (091), MemoryFile processing (092), PostgreSQL + pool (096), budget system, session-type filtering, calculator (097)
- **June 22–24:** Session reports (093), active context mixing (094), MemoryFile search (095)
- **June 27–29:** Compaction debugging marathon — six-bug chain fixed, context window resolution in all execution paths, diagnostic logging stripped, compaction timeline UI, AGENTS.md rewritten, repo cleanup

---

*Last updated: 2026-06-30*

# Ticket 044 — Active Memory: Completion Report

**Completed:** 2026-05-15
**Commits:** 4458579, fe1f287, 7982217, 2a682ea, 01c8c49, 94f55a7, 896145b

## What shipped

### Phase 2–4: Core implementation (2026-05-14)
- `ActiveMemoryProvider` skeleton with `IContextProvider` interface
- Temporal grounding block (mandatory — date, time, timezone, time since last session)
- Episodic memory block (top-weighted observations per layer, per-layer budgets)
- Diary presence block (last 3 entry titles only)
- Ontology block (tag-matched + session-context entities, item/property limits)
- Session-type variants (direct/gallivanting/consolidation/project — different sub-blocks per type)
- `SessionTagsStore` for session→tag mapping

### Phase 1: Admin UI + endpoint (2026-05-15)
- `GET /api/v1/context/active-memory` endpoint returning constructed active memory as structured JSON
- `ActiveMemoryView.vue` — standalone admin UI page rendering all sub-blocks with headers, tables, and tag lists
- Sidebar entry + route + i18n labels

### Bugfixes (2026-05-15)
- **Registry wiring order bug**: `SetContextRegistry(m_contextRegistry)` was called before the registry was constructed (line 244 → null pointer). Fixed by moving the wire call to after construction and population (commit 01c8c49).
- **system_prompt → identity field rename**: Admin UI forms (`AgentsView.vue`, `DiaryView.vue`) were still using the old `system_prompt` JSON key while the backend had been renamed to `identity`. Form data was silently lost. Fixed across all frontend references (commit 94f55a7).

### Related feature: random agent IDs + is_default flag (2026-05-15)
- Default agent no longer uses hardcoded `"default"` as its ID — `GenerateAgentId()` produces a unique hex fingerprint
- `GetById("default")` preserved as canonical alias resolving to whichever agent has `is_default = true`
- `is_default` boolean column added to agents table with migration for existing DBs
- `BuildAgentEntityJson` exposes `is_default` in admin API

## Acceptance criteria status

- [x] `ActiveMemoryProvider` implementing `IContextProvider`, registered at priority 30
- [x] Temporal grounding block (mandatory, always present)
- [x] Episodic memory block (top-weighted observations per layer, per-layer budgets)
- [x] Diary presence block (last 3 titles only)
- [x] Ontology block (tag-matched + session-context, item/property limits)
- [x] Session tags table + store
- [ ] Session tool tag extensions (tags:set/remove/list) — not yet implemented
- [x] Session-type aware construction (different sub-blocks for different session types)
- [ ] Configurable budget per agent — not yet (uses defaults)
- [x] All existing tests continue to pass

## Files created

- `include/animus_kernel/context/ActiveMemoryProvider.h`
- `src/kernel/context/ActiveMemoryProvider.cpp`
- `include/animus_kernel/SessionTagsStore.h`
- `src/kernel/session/SessionTagsStore.cpp`
- `admin-ui/src/views/ActiveMemoryView.vue`

## Files modified

- `src/kernel/AgentKernel.cpp` — register provider + tags store + wiring fix
- `src/kernel/admin/AdminServer.cpp` — active-memory endpoint + is_default in JSON
- `src/kernel/admin/internal/AdminServerRoutesInterfacesSessionsMemory.inc` — route registration
- `src/kernel/agent/AgentStore.cpp` — is_default column, GetDefault by flag, GetById("default") alias
- `include/animus_kernel/AgentStore.h` — is_default field in Agent struct
- `admin-ui/src/components/AppSidebar.vue` — nav entry
- `admin-ui/src/router/index.ts` — route
- `admin-ui/src/i18n/locales/en.ts` — labels
- `admin-ui/src/views/AgentsView.vue` — identity field rename
- `admin-ui/src/views/DiaryView.vue` — identity field rename

## Remaining work

- Session tool tag extensions (tags:set/remove/list) — ticket still open, will land with 041
- Per-agent budget configuration — deferred, current defaults work well
- Caching/construction performance optimization — not yet measured
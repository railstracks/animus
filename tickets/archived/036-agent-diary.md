# Ticket 036 — Agent Diary (Private)

**Priority:** P1 (natural next per-agent feature; self-contained; enables study + gallivanting)
**Status:** Open
**Dependencies:** Ticket 029 (Multi-tenant — per-agent scoping), Ticket 035 (AdminServer decomposition — clean boundaries for new manager)
**Updated:** 2026-05-06

## Summary

Add the agent's private diary as a first-class tool in the framework. Each agent has its own diary, persisted alongside sessions and memory. The diary is for unstructured, temporal, personal reflection — distinct from memory (structured, tagged, consolidated) and sessions (dialogue trace).

Renamed from "journal" to "diary" to strongly impress the nature of privacy on future installations. A diary is personal; a journal could be a log.

## Motivation

Agents need a private space for:
- Post-session reflection that doesn't pollute shared memory
- Emotional/subjective notes that are relevant to the agent but not to other consumers
- Gallivanting output that the agent chooses to record privately
- Notes the agent speaks strictly with itself — no other audience

The diary is a space for private observations. It doesn't need special integration with any other system. Diary interactions happen in the agent's prompt chain like any other tool use, so memory consolidation picks them up automatically. No bridges, no exceptions, no special handling.

## Privacy Contract

- Diary entries are **agent-private**. Never surfaced to other agents or admin users unless explicitly shared by the agent.
- Admin UI shows **metadata only**: diary exists (✓), entry count, date of most recent entry.
- No content, no search, no export in the admin UI.
- Direct SQLite access remains as intentional escape hatch for debugging — not a feature.
- This is a deliberate design choice: privacy by naming, privacy by UI omission.

## Primary Storage: SQL Database

Diary entries are stored in the agent's SQL database (the same store used by sessions, memory, and other per-agent data). This gives us:

- **Searchability:** The agent can full-text search its own diary entries
- **Syndication readiness:** Node syndication (FIVE-POINTS §1) can propagate diary deltas the same way it propagates other agent data — no separate file sync mechanism needed
- **Consistency:** Same storage, backup, and migration tooling as the rest of the agent's data
- **Queryable metadata:** Entry counts, date ranges, tag aggregation all come naturally from SQL

### diary_entries table

| Column | Type | Description |
|--------|------|-------------|
| id | TEXT (UUID) | Primary key |
| agent_id | TEXT (UUID) | Owner agent (multi-tenant scoping) |
| timestamp | INTEGER | Unix ms timestamp |
| layer | TEXT | "reflection", "observation", "intention" |
| content | TEXT | Freeform entry text |
| tags | TEXT | JSON array of optional tags |
| session_id | TEXT | Optional session context (nullable) |

The privacy contract is enforced at the API level (admin sees metadata only, agent reads own entries), not by encrypting the storage layer.

### Encryption question

Entries could be stored encrypted at rest (per-row or per-column encryption), or as plaintext. Trade-off:

- **Plaintext:** Simple, searchable via SQL FTS, agent search works natively
- **Encrypted at rest:** Stronger privacy guarantee, but search requires decrypt-then-scan or specialized encrypted-search schemes

Decision: start with plaintext in SQL. The privacy contract is enforced by the API boundary, not the storage layer. If encrypted-at-rest is needed later, it can be added without changing the API.

## Export Format: `.aidiary`

The `.aidiary` encrypted container format exists for **export/import** — backups, transfers, and portability. It is not the primary store.

Rationale:
- Backing up an agent's diary should be low-friction — export a single file, import on another node
- If we back up any of the agent's files, it should be the diary
- Encrypted export protects diary content in transit and at rest outside the database
- Node syndication handles live sync; `.aidiary` handles offline backup/restore

### Container structure

```
[4 bytes] Magic: "AIDI"
[2 bytes] Format version (uint16, currently 1)
[2 bytes] KDF algorithm (0 = Argon2id)
[2 bytes] Cipher algorithm (0 = AES-256-GCM)
[32 bytes] Salt
[4 bytes] Argon2id iterations (uint32)
[4 bytes] Argon2id memory KiB (uint32)
[16 bytes] Nonce/IV
[variable] Encrypted payload (AES-256-GCM authenticated ciphertext)
[16 bytes] GCM auth tag
```

### Inner payload (decrypted)

```json
{
  "version": 1,
  "agent_id": "uuid",
  "entries": [
    {
      "id": "uuid",
      "timestamp": "ISO-8601",
      "layer": "reflection | observation | intention",
      "content": "freeform text",
      "tags": ["optional", "tags"],
      "session_id": "optional session context"
    }
  ]
}
```

### Encryption approach

- **Double-layer:** Agent key (derived from agent secret) wraps a data key. Data key encrypts the payload.
- **Key derivation:** Argon2id with configurable iterations and memory cost.
- **Rationale:** Personal diary, not state secrets. The encryption raises the bar above casual access without requiring PKI or key management infrastructure.
- **Recovery:** If an agent's secret is lost, exported backups are lost. This is by design — diary privacy extends to backups too.

## API Surface

### Tool interface (agent-facing)

```json
{
  "name": "diary",
  "operations": [
    "write — append entry (agent-only, no admin access)",
    "read — retrieve own entries by range, tag, or layer",
    "search — full-text search within own diary",
    "delete — remove an entry by id"
  ]
}

### Admin API

```
GET    /api/v1/agents/{id}/diary/meta       → { exists, entry_count, last_entry_date }
POST   /api/v1/agents/{id}/diary/export      → export diary as encrypted `.aidiary` file
POST   /api/v1/agents/{id}/diary/import      → import `.aidiary` file (merge or replace)
```

No content read endpoint in admin API. Export produces an encrypted file — admin cannot read the content through this endpoint, only facilitate backup/restore.

### Diary is not a memory pipeline

The diary is a private archive — the agent speaks strictly with itself. It has no special relationship to the memory system. Diary interactions occur in the agent's prompt chain, so background memory consolidation processes them like any other activity. No bridges, no special handling, no exceptions needed.

## Storage Layout

Primary storage is SQL (same database as other per-agent data). The `.aidiary` export file is generated on demand.

```
data/
  agents/
    {agent_id}/
      animus.db          ← SQL database (diary_entries table lives here)
      exports/
        diary-YYYYMMDD.aidiary  ← generated on export request
```

Per-agent scoping follows the multi-tenant pattern established in ticket 029.

## Diary vs. Memory

| Aspect | Diary | Memory |
|--------|-------|--------|
| Structure | Freeform text | Structured observations |
| Privacy | Agent-private | Admin-readable |
| Lifecycle | Append-only, persists indefinitely | Layered, demotable |
| Purpose | Subjective reflection | Objective recall |
| Search | Agent-initiated only | Full admin search |

## Acceptance Criteria

- [ ] `DiaryManager` class owns diary lifecycle (create, read, write, search, delete, export, import)
- [ ] `diary_entries` table in per-agent SQL database
- [ ] Agent tool `diary` with write/read/search/delete operations
- [ ] Full-text search within agent's own diary (SQL FTS or equivalent)
- [ ] Admin API exposes metadata only (no content)
- [ ] Admin API export/import endpoints produce/consume `.aidiary` encrypted files
- [ ] `.aidiary` container format implemented with AES-256-GCM encryption
- [ ] Per-agent diary scoping follows multi-tenant pattern
- [ ] Diary entries have optional tags and layer classification
- [ ] Diary is self-contained — no special memory integration needed
- [ ] Syndication-ready: diary_entries table is compatible with future node syndication (point 1)
- [ ] All existing tests continue to pass
- [ ] New diary tests cover: create, write, read, search, delete, export, import, encrypt/decrypt, metadata API

## Key Questions

- Key storage: where does the agent secret live for `.aidiary` export encryption? (Config? Environment? Keychain?)
- Export scope: export all entries, or by date range/tags?
- Import merge strategy: merge (append missing) or replace (wipe and restore)?
- Session context: should diary entries automatically capture session_id, or is it opt-in?
- Tag vocabulary: freeform or predefined taxonomy?
- Encrypted-at-rest: should SQL entries be encrypted per-row, or is API-level privacy sufficient for v1?

## Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| SQL storage means admin can bypass API and read entries directly | Low | Known escape hatch; documented as debugging-only, not a feature. Encrypted-at-rest is a future option. |
| Diary grows unbounded in SQL | Low | Agent can delete entries; periodic pruning is optional |
| Export file (.aidiary) key loss | Medium | Only affects exported backups, not primary SQL storage. Document clearly. |
| Syndication privacy: diary entries propagate to other nodes | Medium | Syndication respects same privacy contract — diary data is agent-scoped, not admin-readable on any node |
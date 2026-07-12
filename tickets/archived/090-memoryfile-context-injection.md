# Ticket 090 — MemoryFile Context Injection

**Status:** Implemented  
**Priority:** High  
**Created:** 2026-06-16  
**Updated:** 2026-06-18  
**Implemented:** 2026-06-18  
**Depends on:** None  
**Component:** Memory System / Active Context

## Summary

Include MemoryFile content in the Active Context injected with prompts, similar to how Semantic Memory and Episodic Memory are currently injected. MemoryFiles contain curated, persistent knowledge that should be available to the agent alongside ontology entities and session perspectives.

## Motivation

Currently, MemoryFiles (stored in the `memory_files` table) are indexed and searchable via vector search, but their content is not included in the Active Context that gets prepended to prompts. This means:

1. **MemoryFile content is only available via explicit search.** An agent must actively query for a MemoryFile to access it, rather than having relevant content surfaced contextually.
2. **Inconsistent with other memory layers.** Semantic memory (ontology entities) and episodic memory (session perspectives) are both injected into Active Context based on relevance. MemoryFiles — which contain the agent's most important curated knowledge — are absent from this layer.
3. **Migration gap.** When migrating from OpenClaw (where `memory_expanded/` files are loaded via `memory_get` and `memory_search`), the equivalent Animus MemoryFiles need to be present in context at comparable density.

The test agent already demonstrated that MemoryFile vector search works — querying "Nagel" returns `philosophy.md` and `self-knowledge.md` with relevance 1.0. But the content is truncated in search results, and there's no mechanism to include MemoryFile content in the Active Context block.

## Design

### Budget-first injection with relevance padding

All three context layers (Memory Files, Semantic Memory, Episodic Memory) share a common injection pattern:

1. **Allocate a token budget per layer.** Each layer has a configurable maximum (e.g., `memory_file_context_limit`, `semantic_context_limit`, `episodic_context_limit`).
2. **Relevance-first selection.** Fill the budget with contextually relevant items first — items matching session keywords/tags/queries get right of way.
3. **Pad with nearest matches.** If relevant items don't fill the budget, fill remaining capacity with the next-best matches (nearest by relevance score, then by recency, then by arbitrary ordering). The goal is to never waste context capacity — an empty budget slot is a missed opportunity for the agent to know something.
4. **Never exceed the budget.** Hard cutoff. If relevant items alone exceed the budget, the most relevant items win and the rest are dropped.

**Example:** If the episodic context limit is 10k tokens, and relevant observations only cover 2k, the remaining 8k is filled with the next-best observations ranked by relevance score. The agent always gets a full context window unless there genuinely isn't enough data.

This pattern applies consistently across all three layers:
- **Memory Files** — relevance by keyword/embedding match, pad with nearest matches
- **Semantic Memory (ontology entities)** — relevance by keyword/tag match (already implemented in 088), pad with next-best entities
- **Episodic Memory (observations)** — relevance by weight/tag match, pad with recent high-weight observations

### Active Context block structure (updated)

The prompt Active Context already includes:
- **Semantic Memory** — ontology entities surfaced by keyword/tag matching
- **Episodic Memory** — temporal perspectives from recent sessions

Add a third block:
- **Memory Files** — MemoryFile content surfaced by relevance to the current session context

### Injection mechanics

1. At session start (and on consolidation review), query MemoryFiles using the same relevance signals that drive semantic/episodic injection.
2. Apply a configurable token budget (`memory_file_context_limit`) — default 10,000 tokens for standard models, higher for larger context windows.
3. Rank MemoryFile chunks by relevance to current session keywords/tags.
4. Fill budget with relevant chunks first, then pad with nearest-unmatched chunks up to the budget limit.
5. Deduplicate against semantic memory — if a MemoryFile chunk and an ontology entity convey the same fact, prefer the MemoryFile (richer context) but avoid redundant overlap.

### Implementation notes

- MemoryFiles can be large. Chunk them at section boundaries (headers) rather than arbitrary character limits.
- Include `source_path` in the injected content so the agent knows *which* file a fact came from.
- Status filtering: only inject `active` MemoryFiles. `deprecated` files are excluded from context injection (but remain searchable).
- The relevance-then-pad pattern should be extracted into a shared utility (not duplicated across three layers) — a `FillBudget<T>` that takes items sorted by relevance and fills a token budget, returning the selected items.

### Semantic Memory padding (extends 088)

The `AppendOntology` rewrite from ticket 088 already includes baseline inclusion (entities under 10 total are all included). The padding principle extends this: when the total entity count exceeds the baseline, after keyword-matched entities fill their share of the budget, remaining capacity is filled with next-best entities ranked by recency and property count (entities with more properties are more informative).

### Episodic Memory padding

After relevance-matched observations fill their share, remaining capacity is filled with recent high-weight observations, ordered by weight descending then recency descending. The agent always gets a full episodic context unless there genuinely aren't enough observations.

## Verification

2026-06-19 — confirmed working in production with PostgreSQL backend.

The `## MEMORY FILES` block renders in active context with:
- Section-based chunking: files split at markdown headers (#, ##, ###)
- Per-chunk keyword scoring against session context
- Budget-filling with truncation (chunks too large get truncated at newline boundaries)
- Relevance-then-pad: relevant chunks first, remaining budget padded with next-best
- Gated by per-agent `pad_context` toggle (default: true)

Agent confirmed seeing self-knowledge.md content in system message with correct keyword match counts. Chunking was essential — the file is ~26KB (~6500 tokens), well over the 2500-token budget. Without chunking, the entire file was skipped.

Additional work completed alongside this ticket:
- **Agent `pad_context` toggle** in admin UI (Reasoning tab)
- **Agent `numeric_id` field** for correct MemoryFile agent-scoping (hex ID → DB ID resolution)
- **Memory tool file management**: `file_list`, `file_read` (paginated + section extraction), `file_headings`, `file_write`, `file_delete`
- **Session persistence fix**: timestamp overflow on PostgreSQL (INTEGER → BIGINT)
- **Ticket 092** (MemoryFile processing pipeline) implemented in same session
- **Shared keyword extraction** (`CollectSessionKeywords`) across all three context layers
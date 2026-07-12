# Ticket 091 — Search Embeddings for MemoryFile Context

**Status:** Open  
**Priority:** Medium  
**Created:** 2026-06-16  
**Updated:** 2026-06-16  
**Depends on:** Ticket 090 (MemoryFile Context Injection)  
**Component:** Memory System / Active Context

## Summary

Introduce pre-computed search embeddings for MemoryFiles and use embedding-based similarity as the primary retrieval mechanism for MemoryFile context injection. Semantic Memory (entities/facts), Episodic Memory (session reports), and Observations continue using keyword/tag matching — embeddings solve the problem specific to dense, unstructured text.

## Motivation

Keyword and tag matching works well for atomic, structured data — a single ontology fact or a session report summary is already distilled. MemoryFiles are different: they're dense, unstructured, and contain ideas expressed in natural language that doesn't always share keywords with queries about the same concepts.

1. **Semantic gap.** A MemoryFile paragraph about "the felt sense of time passing" won't surface for a query about "temporal density" unless they share keywords. Embeddings bridge this gap.
2. **The gap-fill insight.** OpenClaw's memory search returns the nearest 10 results regardless of relevance, accidentally providing "ambient awareness." Animus needs this intentionally — with graded relevance scoring and priority tiers.
3. **Atomic layers don't need embeddings.** Entities, facts, observations, and session reports are already structured. Keyword/tag matching is sufficient and simpler. The value of embeddings is specifically in retrieving from dense prose.

## Design

### Chunking strategy

MemoryFiles are split into paragraph-level chunks for embedding and retrieval:

1. Split on `\n\n` (blank line = paragraph boundary).
2. If a single paragraph exceeds ~500 tokens, split on sentence boundaries.
3. Each chunk stores: `source_path`, `start_line`, `end_line`, `content`, `embedding`.
4. Chunks are the unit of retrieval — not whole files.

Paragraph-level chunking captures a single coherent idea per embedding. When a chunk is injected into context, `source_path:start_line-end_line` is included so the agent can `memory_get` surrounding context if needed.

### Embedding generation

1. **On creation/update** of a MemoryFile, compute embeddings for all chunks.
2. **Embedding model:** `embeddinggemma-300m-qat` (already deployed for vector search). No new model dependency.
3. **Storage:** Add `embedding` column (BLOB, 768 × float32 = ~3KB per chunk) to `memory_file_chunks` table (new table, normalized from `memory_files`).

### Context injection pipeline

MemoryFile context uses a two-tier embedding-based system:

**Tier 1 — Directly relevant** (cosine similarity ≥ 0.7, configurable):
- MemoryFile chunks whose embedding is similar to the current session's embedding.
- Ranked by similarity score, truncated to `memory_file_context_limit` tokens.

**Tier 2 — Ambient awareness** (cosine similarity 0.3–0.7):
- Chunks from adjacent topics that aren't directly relevant but provide useful context.
- Bounded by `ambient_context_limit` tokens.
- Explicitly labeled in the prompt as "Related but not directly relevant."

Other memory layers continue using keyword/tag matching:
- **Semantic Memory:** tag-based entity surfacing with weight-based ranking.
- **Episodic Memory:** recency-based session report injection (last 12 hours) plus keyword-matched older sessions.
- **Observations:** tag-based matching with weight and recency factors.

### Selection formula

For MemoryFile chunks, the ranking combines embedding similarity with existing weight/decay signals:

```
score = cosine_similarity * weight_factor * recency_factor
```

Where:
- `cosine_similarity` = dot product of session embedding and chunk embedding
- `weight_factor` = chunk's MemoryFile status weight (active > unprocessed > deprecated)
- `recency_factor` = mild decay favoring recently updated files over stale ones

This ensures recent, active, relevant content surfaces before old but equally relevant content.

### Token budgets

Per-agent, per-model-size configurable:
- `memory_file_context_limit` — tier 1 directly relevant chunks (default: 10,000 tokens for standard models, 50,000 for large-context)
- `ambient_context_limit` — tier 2 ambient chunks across all layers (default: 5,000 tokens)

These budgets sit alongside the existing `semantic_context_limit`, `episodic_context_limit`, and `observation_context_limit`.

### Migration

For existing MemoryFiles (including Kestrel's `memory_expanded/` import):
1. Create `memory_file_chunks` table.
2. For each MemoryFile, split into paragraph-level chunks.
3. Compute embeddings for all chunks in background batches.
4. Migration completes when all chunks have embeddings.

### Implementation phases

1. **Phase 1:** Create `memory_file_chunks` table, add chunking logic, compute embeddings for existing MemoryFiles.
2. **Phase 2:** Implement two-tier injection with configurable budgets.
3. **Phase 3:** Add `memory_search(regex:)` and `memory_get(lines:)` for targeted retrieval (see Ticket 095).
4. **Phase 4:** Evaluate relevance thresholds and budget allocations from production agent behavior.
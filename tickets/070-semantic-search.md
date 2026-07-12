# Ticket 070 — Semantic Search (Embedding Vectors + Hybrid FTS)

**Created:** 2026-06-05
**Status:** Draft
**Priority:** P1
**Depends on:** MemoryTool (existing), SQLite (existing)

## Summary

Add semantic search to Animus's memory system. The current `MemorySearch` uses FTS5 (BM25 keyword matching), which works for exact word queries but fails at finding things by meaning. Semantic search converts text into numerical vectors (embeddings) and finds results by similarity, enabling queries like "infrastructure crash in April" to find entries about "container OOM" even though no words overlap.

The implementation uses **hybrid search** — combining FTS5 (keyword precision) with vector similarity (semantic recall) — to get the best of both. This is how OpenClaw's `memory_search` works today.

## Motivation

**Current state:** Animus's `MemorySearch::Search` queries FTS5 indexes on observations, ontology, diary, memory files, and sessions. It returns BM25-ranked keyword matches.

**Problem:** Keyword search can't find:
- "How did we fix the WhatsApp crypto bug?" → entries about "HKDF info length null terminator" or "AES-CBC wire format"
- "Server stability issues" → entries about "OOM kill" or "memory pressure"
- "What did Melvin think about the creative direction?" → entries about "noir constraint satisfied" or "tone palette"

These are the queries I make dozens of times per session. Without semantic search, I'd have to remember the exact words used.

**Target state:** Hybrid search that combines FTS5 keyword matching with vector similarity, scored with reciprocal rank fusion (or weighted sum). This matches OpenClaw's `memory_search` behavior.

## Architecture

### Embedding Model

The embedding model converts text → fixed-dimension vector. We need to support both local and remote models.

```cpp
class EmbeddingProvider {
public:
    virtual ~EmbeddingProvider() = default;
    virtual std::string Name() const = 0;
    virtual int Dimension() const = 0;
    virtual std::vector<float> Embed(const std::string& text) = 0;
    virtual std::vector<std::vector<float>> EmbedBatch(const std::vector<std::string>& texts);
};
```

**Local providers (run on the same machine):**

| Provider | Model | Dimensions | RAM | Quality | Speed |
|----------|-------|-----------|-----|---------|-------|
| ONNX Runtime | embeddinggemma-300m | 768 | ~600MB | Good | ~5ms/text |
| ONNX Runtime | all-MiniLM-L6-v2 | 384 | ~80MB | Decent | ~2ms/text |
| Ollama | nomic-embed-text | 768 | ~270MB | Good | ~10ms/text |
| Ollama | mxbai-embed-large | 1024 | ~670MB | Very good | ~15ms/text |

**Remote providers (API calls):**

| Provider | Model | Dimensions | Cost | Quality |
|----------|-------|-----------|------|---------|
| OpenAI | text-embedding-3-small | 1536 | $0.02/1M tokens | Excellent |
| OpenAI | text-embedding-3-large | 3072 | $0.13/1M tokens | Best |
| Cohere | embed-english-v3.0 | 1024 | $0.10/1M tokens | Very good |
| Google | text-embedding-004 | 768 | Free tier available | Good |

**Default configuration:** ONNX Runtime with all-MiniLM-L6-v2 (384 dimensions). Small, fast, decent quality, no API cost, runs on Raspberry Pi. Upgradable to larger models or remote APIs by changing config.

### Vector Storage: sqlite-vec

We store vectors in the same SQLite database as everything else, using the `sqlite-vec` extension.

```sql
-- Vector table: one row per searchable chunk
CREATE VIRTUAL TABLE memory_vectors
USING vec0(
    id INTEGER PRIMARY KEY,
    domain TEXT,        -- 'observation', 'diary', 'file', 'ontology', 'session'
    source_id INTEGER,  -- FK to source table
    embedding float[384] -- dimension matches the embedding model
);
```

**Why sqlite-vec:**
- No new service dependency (Qdrant, Milvus, ChromaDB all require separate services)
- Stays in our existing SQLite database
- Supports approximate nearest neighbor (ANN) search
- Works on ARM (Raspberry Pi) and x86
- Loadable as a SQLite extension or compile-in

**Alternative considered:** Flat storage in a BLOB column with brute-force cosine similarity. Simple but O(n) per query. Acceptable for <10K entries but degrades. sqlite-vec uses IVF or HNSW indexing for sub-linear query time.

**Vector dimensions:** Configurable based on the embedding model. Stored in the database metadata. When switching models, vectors must be re-indexed (the tool should detect dimension mismatches and trigger re-indexing).

### Indexing Pipeline

Embeddings are generated in a background process, not inline during search.

```
New/updated memory entry
    │
    ▼
Chunking (split into ~512 token segments, overlap 50 tokens)
    │
    ▼
Embedding (local or remote provider)
    │
    ▼
Store in memory_vectors table
    │
    ▼
Mark as indexed in source table (embeddings_dirty = 0)
```

**Chunking strategy:**
- Observations: Each observation is a chunk (usually short enough)
- Diary entries: Each entry is a chunk
- Memory files: Split by paragraph or section headers (~512 tokens, 50 overlap)
- Session turns: Each turn is a chunk
- Ontology entities: Each entity + its properties is a chunk

**Background indexing:**
- Run as part of the existing consolidation pipeline
- Check for `embeddings_dirty = 1` entries
- Batch embedding calls (local: 32 at a time, remote: 8 at a time for rate limits)
- Mark entries as indexed after successful embedding

**Re-indexing triggers:**
- Model change (different dimensions)
- sqlite-vec version upgrade
- Manual admin command

### Hybrid Search: FTS5 + Vector Similarity

```cpp
struct HybridSearchResult {
    std::string domain;
    int64_t id;
    std::string text;
    double fts_score;      // BM25 normalized to [0, 1]
    double vector_score;   // cosine similarity normalized to [0, 1]
    double combined_score; // weighted combination
};

std::vector<HybridSearchResult> HybridSearch(
    const std::string& query,
    int64_t agent_id,
    MemorySearchDomain domains = {},
    int64_t limit = 20,
    double fts_weight = 0.4,
    double vector_weight = 0.6);
```

**Score fusion: Reciprocal Rank Fusion (RRF)**

Rather than normalizing scores across different scales (BM25 values vs cosine similarity), RRF uses rank positions:

```
RRF_score(d) = fts_weight / (k + rank_fts(d)) + vector_weight / (k + rank_vector(d))
```

Where `k = 60` (standard RRF constant). This is robust to score scale differences and doesn't require normalization.

**Alternative (simpler): Weighted sum of normalized scores**
- Normalize BM25 to [0, 1] using `1 / (1 + bm25)` (already done in current code)
- Normalize cosine similarity to [0, 1] using `(sim + 1) / 2`
- Combined = `fts_weight * fts_normalized + vector_weight * vector_normalized`

Either approach works. RRF is theoretically better; weighted sum is simpler to implement. Start with weighted sum, upgrade to RRF if needed.

### Query Flow

```
Agent calls MemoryTool::search("infrastructure crash in April")
    │
    ├── FTS5 search (existing BM25 query)
    │   → keyword results with BM25 scores
    │
    ├── Vector search (new)
    │   ├── Embed query text → vector
    │   ├── Query sqlite-vec for nearest neighbors
    │   └── → semantic results with cosine similarity scores
    │
    └── Fuse results
        ├── Combine FTS and vector results
        ├── Deduplicate (same domain + id appears in both)
        ├── Compute combined score
        └── Sort by combined score, return top N
```

### Embedding Model Configuration

```json
{
  "memory": {
    "search": {
      "mode": "hybrid",
      "fts_weight": 0.4,
      "vector_weight": 0.6,
      "embedding": {
        "provider": "onnx",
        "model": "all-MiniLM-L6-v2",
        "dimensions": 384,
        "model_path": "/var/lib/animus/models/all-MiniLM-L6-v2.onnx",
        "batch_size": 32,
        "max_chunk_tokens": 512,
        "overlap_tokens": 50
      },
      "providers": {
        "onnx": {
          "type": "local",
          "runtime": "onnxruntime",
          "model_path": "/var/lib/animus/models/all-MiniLM-L6-v2.onnx"
        },
        "ollama": {
          "type": "local",
          "base_url": "http://localhost:11434",
          "model": "nomic-embed-text"
        },
        "openai": {
          "type": "remote",
          "api_key": "${OPENAI_API_KEY}",
          "model": "text-embedding-3-small",
          "dimensions": 1536
        },
        "cohere": {
          "type": "remote",
          "api_key": "${COHERE_API_KEY}",
          "model": "embed-english-v3.0"
        }
      }
    }
  }
}
```

**Mode options:**
- `"keyword"` — FTS5 only (current behavior, fallback for systems without sqlite-vec)
- `"semantic"` — Vector search only
- `"hybrid"` — Both FTS5 and vector, combined with weighted fusion (default)

**Provider hot-swap:** When the embedding provider changes (e.g., from local ONNX to OpenAI), the dimensions likely change. All vectors must be re-indexed. The tool should detect this and trigger a re-index automatically, or warn the admin.

## Implementation Plan

### Phase 1: Infrastructure (sqlite-vec + ONNX)
- [ ] Add sqlite-vec as a dependency (compile-in or loadable extension)
- [ ] Add ONNX Runtime as a dependency (for local embedding inference)
- [ ] Bundle all-MiniLM-L6-v2 ONNX model (downloaded at build time or first run)
- [ ] Create `memory_vectors` virtual table in existing SQLite database
- [ ] Add `embeddings_dirty` flag column to source tables
- [ ] Implement `EmbeddingProvider` interface
- [ ] Implement `OnnxEmbeddingProvider` (local ONNX Runtime)

### Phase 2: Indexing Pipeline
- [ ] Implement text chunking (paragraph/section splitting with overlap)
- [ ] Add embedding generation to the consolidation background job
- [ ] Batch embedding with configurable batch size
- [ ] Mark entries as indexed after successful embedding
- [ ] Re-index command (admin API or tool action)

### Phase 3: Hybrid Search
- [ ] Modify `MemorySearch::Search` to support hybrid mode
- [ ] Implement vector similarity query via sqlite-vec
- [ ] Implement score fusion (weighted sum, then RRF)
- [ ] Deduplication of results appearing in both FTS and vector results
- [ ] Add `mode` parameter to `MemoryTool::search` (keyword, semantic, hybrid)

### Phase 4: Remote Providers
- [ ] Implement `OllamaEmbeddingProvider` (HTTP calls to local Ollama)
- [ ] Implement `OpenAIEmbeddingProvider` (REST API)
- [ ] Implement `CohereEmbeddingProvider` (REST API)
- [ ] Provider hot-swap detection and re-indexing
- [ ] Admin UI for search config

## Scope

### v1 (This ticket — Phases 1-3)
- sqlite-vec integration
- ONNX Runtime local embedding (all-MiniLM-L6-v2)
- Chunking and indexing pipeline
- Hybrid search (FTS5 + vector) in `MemorySearch`
- Fallback to keyword-only when sqlite-vec unavailable

### v2 (Future — Phase 4)
- Remote embedding providers (OpenAI, Cohere)
- Ollama embedding provider
- Provider hot-swap with automatic re-indexing
- Dimension change detection
- Admin UI for search configuration

## Performance Considerations

- **Embedding latency:** all-MiniLM-L6-v2 on CPU takes ~2ms per chunk. For 10,000 entries, initial indexing takes ~20 seconds. Acceptable for background job.
- **Query latency:** Vector search on 10K entries via sqlite-vec takes <10ms. Combined with FTS5, total hybrid search <50ms.
- **Storage overhead:** 384-dim float32 vectors = 1.5KB per chunk. 10,000 entries ≈ 15MB. Negligible.
- **Memory overhead:** ONNX Runtime model uses ~80MB RAM. Acceptable even on constrained devices.
- **Raspberry Pi:** all-MiniLM-L6-v2 runs on Pi 4 (ARM64). Inference ~15ms per chunk. Indexing 10K entries takes ~150 seconds. Acceptable for one-time background job.
- **Remote fallback:** If embedding model is unavailable, search degrades to keyword-only. No data loss, just reduced quality.

## Hard-Won Lessons (Predicted)

1. **Vector dimensions must match.** If you change embedding models, all vectors become invalid. Detect this on startup and force re-index.
2. **Chunking matters more than model size.** A great model with bad chunking loses to a mediocre model with good chunking. Overlap tokens prevent boundary losses.
3. **Hybrid beats pure semantic.** Keyword search is exact for keyword queries. Semantic search is fuzzy. Combined, you get exact matches for exact queries *and* semantic matches for fuzzy queries. Never remove FTS5.
4. **Batch embedding is essential.** One-at-a-time embedding is slow, especially for remote APIs. Batch 8-32 at a time.
5. **Score normalization across domains is tricky.** BM25 scores and cosine similarities have different scales. RRF avoids this problem entirely.
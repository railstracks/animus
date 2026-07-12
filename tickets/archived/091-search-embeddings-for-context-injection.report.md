# Ticket 091 Report — Search Embeddings for MemoryFile Context

## Status: Implemented — Phase 1-2 complete, Phase 3-4 pending evaluation

## Completed

### Phase 1: Infrastructure

- **llama.cpp integration** (FetchContent, tag b9715, CPU-only build)
- `EmbeddingService` class — loads GGUF model at startup, `Embed()` returns normalized 768-dim vectors, `CosineSimilarity()` for comparison, thread-safe via mutex, degraded mode fallback
- Model deployed: `models/embeddinggemma-300m-qat-Q8_0.gguf` (314MB, 768-dim)
- Wired into `AgentKernel` (loads at startup with configurable model path)
- Passed to `ActiveMemoryProvider` via constructor
- Deferred `ComputePendingEmbeddings` to end of startup (after all stores initialized)

### Phase 1: Chunk Table + Storage

- `memory_file_chunks` table with embedding BLOB (BYTEA on PostgreSQL) storage
- `IDataStore` interface extended: `BindBlob`, `ColumnBlob` (hex bytea format for PostgreSQL)
- `MemoryFileStore` chunk methods: `ReplaceChunks`, `GetChunks`, `GetChunksForAgent`, `UpdateChunkEmbedding`, `CountChunksNeedingEmbeddings`
- Chunk struct: file_id, source_path, header_title, chunk_index, content, content_hash (FNV-1a), start/end lines, embedding, embedding_dim

### Phase 1: Chunking Pipeline (verified working)

- `ComputePendingEmbeddings` runs as background job at end of startup
- Chunks files at markdown headers (#, ##, ###)
- Computes embeddings for chunks missing them
- Verified: 34 chunks, 31 embeddings computed for 3 files / 1 agent
- Diagnostic logging: agent ID resolution, file counts, chunk counts

### Phase 1: Context Injection (verified working)

- `AppendMemoryFiles` uses cached chunks with cosine similarity
- Verified: `cached chunks: 34` in production logs
- Session embedding computed from recent 6 turns + keywords (dim=768)
- Keyword matching as fallback when cached chunks unavailable
- **No blocking on prompt assembly** — keyword fallback when cache unavailable

### Phase 2: Two-Tier Budget System

- **Tier 1** (similarity ≥ 0.7, directly relevant):
  - Budget: `memory_file_token_budget` (default 2500 tokens)
  - Rendered: `### source_path (similarity: NN%)`
- **Tier 2** (similarity 0.3–0.7, ambient awareness):
  - Budget: `ambient_context_limit` (default 5000 tokens)
  - Gated by `pad_context` toggle
  - Rendered: `### Related but not directly relevant`
  - Only fills after Tier 1 is satisfied
- Both tiers share combined token budget with chunk-level truncation
- `ambient_context_limit` added to AgentBudgetConfig, admin API, agent form UI
- Keyword fallback uses single tier (relevance > 0 = Tier 1)

## Remaining

### Phase 3: Chunking strategy review (optional)

Currently header-level (from ticket 090). Ticket originally specified paragraph-level (`\n\n` splits). Header-level gives larger semantic units which may be preferable for knowledge files. Defer until evaluation data is available.

### Phase 4: Evaluation

Tune relevance thresholds (0.7 and 0.3) and budget allocations (2500 and 5000) from production agent behavior. Currently using reasonable defaults from the ticket design.

## Infrastructure Bugs Fixed During This Ticket

The consolidation/memory pipeline had multiple latent bugs that surfaced during testing. All were blockers:

1. **`std::stoll` hex agent ID bug** (6 occurrences) — `stoll` on hex agent ID strings throws, returns 0. Agent-scoped queries filtered with `agent_id = 0`, missing all real data. Fixed in: MemoryTool, MemorySearch, ConsolidationTool (×2), ConsolidationPipeline, AgentKernel.

2. **`PGconn` thread safety** — single PostgreSQL connection used concurrently from job system workers and Drogon event loop threads. SIGSEGV in `PQexecParams`. Fixed with global mutex on all `PGconn` operations. Connection pool needed for production (documented in ticket 096 report).

3. **`BLOB` → `BYTEA`** — `schema::CreateTable` didn't transform `BLOB` for PostgreSQL. `memory_file_chunks` table creation failed silently. Fixed in SchemaHelpers.

4. **Binary bytea null byte truncation** — `BindBlob` used binary format (format=1) with `c_str()`, truncating at null bytes in embedding data. Heap corruption. Fixed: hex bytea format (`\x` prefix, text format).

5. **Session timestamp overflow** — `sessions` table used raw `INTEGER` for timestamps on PostgreSQL (32-bit, max ~2.1B). Unix ms timestamps (~1.78T) overflow. Fixed: `BIGINT` + migration.

6. **MemoryFile search vector missing** — PostgreSQL `tsvector` column + GIN index + trigger never created for `memory_files`. Search returned empty. Fixed: `EnsureMemoryFilesFtsSchema`.

7. **`ListFiles` missing `status` column** — unfiltered `ListFiles` SELECT didn't include `status`. `RowToMemoryFile` read non-existent column 9, returned 0 (unprocessed) for all files. UI showed wrong status.

8. **`UpdateFile` missing `status` column** — UPDATE statement didn't SET `status`. "Queue for Processing" button silently no-op'd despite returning 200 OK.

9. **Streaming LLM stall** — `CURLOPT_TIMEOUT_MS = 0` for streaming (no timeout). Provider stalls hung daemon forever. Fixed: low-speed stall detection (1 byte/sec for 90 seconds).

10. **On-the-fly embedding blocking** — computing embeddings during prompt assembly blocked for minutes. Fixed: keyword fallback when cached chunks unavailable.

11. **Session-type action filtering** — agent used `review` during intake sessions, causing scope creep. Fixed: action whitelist per session type (intake vs review).

12. **MemoryFile auto-mark on summary** — agent discovered files via `file_read` rather than `fetch_pending`, never called `mark_processed`. Files stuck unprocessed. Fixed: `HandleSummary` auto-marks remaining unprocessed files.

13. **`HasPendingIntakeData` MemoryFile check** — `stoll` hex bug prevented unprocessed files from triggering intake sessions. Fixed: resolve via `AgentStore::GetById`.

14. **Agent `numeric_id` field** — `Agent.id` is hex string, `MemoryFile.agent_id` is numeric int64. No way to convert. Added `numeric_id` to Agent struct, populated from DB auto-increment, exposed in admin API.

15. **`pad_context` agent toggle** — per-agent boolean to control whether unused context budget gets filled with next-best matches. Gates padding in all three context layers.

16. **`ComputePendingEmbeddings` enqueue ordering** — job enqueued before `m_agentStore` was initialized. Job checked `if (!m_agentStore) return;` and bailed silently. Fixed: moved enqueue to end of `Start()`.

## Commits

- `a49a3cb` feat: ticket 088 — agent-scoped semantic memory (prerequisite)
- `84c6486` feat: embedding-based semantic similarity for MemoryFile context
- `9e0a3e5` feat: ComputePendingEmbeddings runs at startup
- `9950336` feat: memory_file_chunks table + embedding cache infrastructure
- `d991592` feat: AppendMemoryFiles uses cached chunks with pre-computed embeddings
- `3efc53e` fix: BLOB→BYTEA + thread-safe embedding + deferred startup
- `c0e81e8` fix: heap corruption from binary bytea params
- `526df43` fix: escape sequences in hex bytea format
- `26c0d34` fix: thread-safe PostgreSQL access (mutex)
- `056102a` fix: streaming LLM stall detection
- `f2db6e9` fix: never block prompt assembly on embedding computation
- `3f4c935` feat: session-type-aware action filtering
- `febaf79` fix: auto-mark MemoryFiles on summary
- `5c40391` fix: sixth stoll hex bug in HasPendingIntakeData
- `53c01fa` fix: ListFiles missing status column
- `c8b8f53` fix: UpdateFile missing status column
- `9441f17` fix: ComputePendingEmbeddings enqueued before m_agentStore was set
- `cc4545a` feat: two-tier budget system for MemoryFile context

# Ticket 094 — Active Context: Session Mixing and Per-Layer Budgets

**Status:** Open  
**Priority:** Medium  
**Created:** 2026-06-16  
**Depends on:** Ticket 091 (Search Embeddings), Ticket 093 (Session Reports)  
**Component:** Memory System / Active Context

## Summary

Active Context injection should mix session reports from two sources — recency and relevance — within a configurable per-layer token budget. This replaces the current OpenClaw behavior (10 keyword-matched lines) with a structured, prioritized context window that provides both recent awareness and historically relevant knowledge.

## Motivation

OpenClaw's current Active Context provides session summaries for the last 12 hours. This is useful but limited:

1. **Only recency, no relevance.** A conversation about Nagel from last week won't appear in context, even if today's conversation is about philosophy.
2. **Fixed size, no priority tiers.** The 10-line limit doesn't distinguish between directly relevant and ambiently useful context.
3. **No per-layer budgets.** There's no way to allocate context capacity between ontology, observations, session reports, and MemoryFiles.

Animus needs Active Context that serves *both* purposes: "what happened recently" (recency) and "what's relevant now" (relevance). These should be mixed within configurable per-layer budgets.

## Design

### Context layers and budgets

Each agent has configurable token budgets for each context layer:

```ruby
# Default budgets (standard model, ~40KB total context)
context_budgets = {
  semantic_context_limit: 10_000,      # ontology entities + facts
  episodic_context_limit: 10_000,      # session reports + perspectives
  memory_file_context_limit: 10_000,    # MemoryFile chunks
  observation_context_limit: 10_000,    # recent observations
  ambient_context_limit: 5_000         # low-relevance filler across all layers
}
```

For large-context models (e.g., GLM-5.2 with 128K context), budgets scale proportionally (5x = 200KB total).

### Session report mixing

Within the `episodic_context_limit`, session reports are sourced from two pools:

**Pool 1 — Recency (always included, up to 40% of budget):**
- Sessions from the last 12 hours (configurable per agent).
- Ordered by recency, newest first.
- Truncated if they exceed the recency budget.

**Pool 2 — Relevance (fills remaining 60%+):**
- Session reports whose embeddings are similar to the current session's embedding.
- Ranked by similarity score.
- Only includes reports not already in Pool 1 (no duplicates).
- Falls back to lower-relevance results if high-relevance results don't fill the budget.

### Ambient context

The `ambient_context_limit` budget is for Tier 2 results across all layers (see Ticket 091). These are low-relevance but potentially interesting results that provide "ambient awareness" — similar to how OpenClaw's gap-fill accidentally provides unrelated but sometimes useful context.

Ambient context is:
- Explicitly labeled in the prompt (e.g., "Related but not directly relevant:")
- Drawn from all memory layers proportionally
- Bounded to prevent noise from overwhelming signal
- The agent can tune this budget based on preference (some agents prefer more ambient awareness, others prefer focused context)

### Prompt formatting

```markdown
## Active Memory

### Recent Sessions
[Pool 1: recency-based session reports, truncated summaries]

### Relevant Sessions
[Pool 2: embedding-similar session reports, truncated summaries]

### Ontology
[Tier 1 entities and facts relevant to current session]

### Observations
[Recent high-weight observations + relevant older ones]

### Memory Files
[Relevant MemoryFile chunks]

### Ambient
[Low-relevance but potentially interesting context from all layers]
```

### Implementation approach

1. At session start, compute the current session's embedding from the initial messages.
2. Query each memory layer with the session embedding, respecting per-layer budgets.
3. For episodic layer: mix recency pool and relevance pool within the `episodic_context_limit`.
4. Fill ambient budget from the lowest-relevance results across all layers.
5. Inject the formatted block into the system prompt.
6. On consolidation review, update the session report for the current session and re-compute the embedding.

### Configuration

Budgets are stored per-agent in the `agents` table (or a new `agent_config` table):

```ruby
# Per-agent, per-model-size defaults with overrides
budget_overrides = {
  'kestrel' => {
    'ollama/glm-5.1' => { semantic_context_limit: 10_000, ... },
    'zai/glm-5.2'    => { semantic_context_limit: 50_000, ... }
  }
}
```
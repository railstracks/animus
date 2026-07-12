# Ticket 045 — Consolidation Tool

**Priority:** P2 (completes the memory lifecycle — intake → observation → consolidation → perspective)
**Status:** Open
**Dependencies:** Ticket 044 (Active Memory — needs to know what the constructed context looks like)
**Updated:** 2026-05-14

## Summary

A composite tool available to agents during memory consolidation cycles. It provides the agent with controlled access to search, review, pin, restructure, and generate perspectives on its own episodic memory. The consolidation tool is the agent's workspace for turning raw observations into curated understanding.

## Motivation

Memory consolidation in Animus is currently a **passive background process** — it scans prompt chain activity and extracts observations automatically. This works for simple cases but breaks down for:

- **Judgment calls**: Should this observation be promoted to a higher layer? The agent's own assessment is more nuanced than any automatic heuristic.
- **Restructuring**: Observations may be redundant, contradictory, or outdated. An agent reviewing them can merge, revise, or retire them.
- **Perspective generation**: The retrospective/current/future perspectives on each layer should reflect genuine reflection, not just text summarization.
- **Gallivanting integration**: Consolidation can be a structured exploration activity — the agent reviews what it knows, identifies gaps, and sets intentions for future learning.

The consolidation tool gives the agent an active role in its own memory management.

## Distinction from Memory Tool

- **Memory tool** (existing): Search, recall, pin, inspect. Available during any session. Read-dominant with light write (pin).
- **Consolidation tool** (this): Deep review, restructure, merge, revise, generate perspectives. Only available during consolidation sessions. Write-heavy.

The memory tool is for *accessing* memory. The consolidation tool is for *maintaining* memory.

## When This Tool Is Available

The consolidation tool is injected into the agent's tool set only during consolidation-triggered sessions:

1. **Scheduled consolidation**: Layer cron fires → scheduler creates consolidation event → session pipeline starts a consolidation session with this tool available
2. **Agent-initiated**: Agent calls `memory inspect`, sees stale layers, requests consolidation
3. **Admin-triggered**: Admin API kicks off consolidation for a specific layer

Outside of consolidation sessions, this tool is not registered — the agent doesn't see it.

## Tool Interface

```json
{
  "name": "consolidation",
  "description": "Review, restructure, and curate your episodic memory during consolidation cycles.",
  "actions": [
    "review — list observations due for review on a specific layer",
    "promote — promote an observation to a higher layer",
    "merge — merge multiple observations into one",
    "revise — update an observation's text or weight",
    "retire — remove an observation that's no longer relevant",
    "tag — add or update tags on observations",
    "perspective:generate — write a new perspective for a layer",
    "perspective:review — read the current perspective for a layer",
    "summary — generate a summary of what was consolidated this cycle"
  ]
}
```

### Actions in detail

**`review`**
- Params: `layer` (name or id), `filter` (optional: "stale", "overdue", "high_weight", "all"), `limit` (default 20)
- Returns observations matching the filter with their metadata (weight, age, source, tags)
- "stale": observations past their evaluation interval
- "overdue": observations past their next_review_at_ms

**`promote`**
- Params: `observation_id`, `target_layer` (name)
- Moves the observation from its current layer to the target layer
- Adjusts weight based on target layer's default weight
- Validation: target layer must be higher horizon (day → week, not week → day)

**`merge`**
- Params: `observation_ids` (array of 2+), `merged_text` (new combined text), `strategy` ("keep_weights", "max_weight", "average_weight")
- Creates a new observation with the merged text and selected weight strategy
- Retires the source observations
- Preserves tags from all sources (union)

**`revise`**
- Params: `observation_id`, `text` (optional), `weight` (optional), `tags` (optional)
- Updates the observation in place
- Resets evaluation timer

**`retire`**
- Params: `observation_id`, `reason` (optional text)
- Removes the observation from the layer
- Logs the retirement reason (useful for meta-consolidation: "why did I forget this?")

**`tag`**
- Params: `observation_id`, `tags` (array of strings), `mode` ("replace", "append", "remove")
- Manages observation tags

**`perspective:generate`**
- Params: `layer` (name), `pov` ("retrospective", "current", "future"), `text`
- Writes or overwrites the perspective for that layer and viewpoint
- Includes valence: positive/neutral/negative

**`perspective:review`**
- Params: `layer` (name)
- Returns all three perspectives (retrospective, current, future) with valence

**`summary`**
- Params: none (auto-scoped to current consolidation session)
- Returns: observations reviewed, promoted, merged, retired, perspectives generated
- Agent can write this summary to diary as a consolidation record

## Consolidation Session Flow

```
1. Scheduler fires consolidation event for layer "week"
2. Session pipeline creates consolidation session
3. Active memory (044) is constructed with emphasis on the "week" layer
4. Consolidation tool is registered in the session's tool set
5. Agent receives prompt: "Review your week-layer observations. Promote, merge, or retire as needed. Then generate updated perspectives."
6. Agent calls consolidation.review → sees stale observations
7. Agent calls consolidation.merge, consolidation.retire, consolidation.promote as needed
8. Agent calls consolidation.perspective:generate for each POV
9. Agent calls consolidation.summary → writes to diary
10. Session ends. Tool is unregistered. Normal operation resumes.
```

## Interaction with Existing Consolidation

The existing automatic consolidation pipeline (`ConsolidationManager`) handles the mechanical parts:
- Scanning prompt chains for observation-worthy content
- Extracting observations and storing them in the intake layer
- Running time-based evaluation intervals

This tool handles the **agent-driven** parts:
- Reviewing what's been collected
- Making judgment calls about relevance and priority
- Generating authentic perspectives (not just summaries)
- Identifying gaps and setting intentions

Both can coexist. The automatic pipeline does intake and initial extraction. The consolidation tool does curation and perspective generation.

## Acceptance Criteria

- [ ] `ConsolidationTool` class that registers only during consolidation sessions
- [ ] `review` action lists observations with staleness/weight filters
- [ ] `promote` action moves observations between layers with validation
- [ ] `merge` action combines observations with configurable weight strategy
- [ ] `revise` action updates observation text, weight, or tags
- [ ] `retire` action removes observations with reason logging
- [ ] `tag` action manages observation tags (append, replace, remove)
- [ ] `perspective:generate` writes layer perspectives with valence
- [ ] `perspective:review` reads current perspectives
- [ ] `summary` generates consolidation session report
- [ ] Tool only available during consolidation sessions (not registered otherwise)
- [ ] Integration with scheduler — consolidation events trigger sessions with this tool
- [ ] All existing tests continue to pass

## Key Questions

- Should promote be allowed to skip layers (day → year) or only adjacent (day → week)?
- Should merge preserve the original observations as "retired" for audit trail?
- How much autonomy should the agent have — can it retire anything, or should high-weight observations require confirmation?
- Should the consolidation prompt be customizable per layer?
- How to prevent the agent from over-consolidating (retiring too much, merging inappropriately)?
- Should there be a "dry run" mode where the agent proposes changes but doesn't apply them?

## Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Agent over-consolidates — retires too many observations | Medium | Weight thresholds for retirement; admin can configure minimum weight for auto-retire |
| Agent generates shallow perspectives | Medium | Structured perspective prompts; require all three POVs |
| Consolidation session is too long — burns tokens | Medium | Budget cap on consolidation sessions; limit observations per cycle |
| Merge loses important nuance | Medium | Preserve originals as retired; merge includes source observation IDs |
| Tool is used outside consolidation sessions | Low | Registration gate — tool only exists during consolidation sessions |

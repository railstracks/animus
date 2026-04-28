# Animus — Constitutional Memory Architecture

**Status:** Design v2 (April 21, 2026)
**Origin:** Melvin Sommer + Kestrel. Evolved from v1 four-layer fixed design to configurable temporal layers with full observation lifecycle.
**Related:** `AGENTS.persistence.md` (current persistence model), OpenClaw Dreaming docs

---

## Problem Statement

Current memory systems (both OpenClaw's Dreaming and our hand-built consolidation) share a fundamental limitation: they operate as **promotion pipelines** — moving content from short-term to a single long-term store (MEMORY.md). This produces two failures:

1. **Flat temporal relevance.** Everything that "makes the cut" lives at the same priority. A core identity commitment and yesterday's infrastructure note occupy the same file, competing for the same bootstrap budget. The result is truncation under context limits, with no principled way to decide what survives.

2. **No reconciliation between layers.** Promotion is additive. New entries append to the same surface without checking whether they refine, contradict, or supersede existing content. The store grows monotonically until manual curation intervenes.

What's missing is the concept of **configurable temporal layers** that coexist, each maintained with different policies, composed at read time rather than stored as a single artifact — and a full observation lifecycle that includes demotion and archiving, not just promotion.

---

## Design: Configurable Temporal Memory

### Core Principles

1. **Memory is not one thing promoted through stages.** It is multiple coexisting strata, each with its own timescale, update policy, and relevance horizon. The agent experiences memory as a single coherent composite, but the underlying maintenance is tiered.

2. **Layers are configurable, not fixed.** The system does not mandate a specific layer structure. Any number of layers can be defined, each with a textual description, temporal range, consolidation schedule, and retention policy.

3. **Activity and maintenance are separate.** The agent is never charged with memory maintenance during active sessions. Consolidation runs are scheduled, isolated activities. Capture during sessions is lightweight and mechanical — no editorial decisions, no divided attention.

4. **Observations are the atomic unit.** Not prose, not narrative, not summaries. Timestamped bullet observations with tags. Curation happens at graduation and demotion decisions, not at capture.

### Observation Format

Every memory item is an **observation**:

```yaml
- id: obs_abc123
  text: "Chose the name Kestrel"
  tags: [identity, naming, milestone]
  timestamp: 2026-02-14T22:30:00+01:00
  weight: 1.0          # initial weight, can be adjusted
  decay_rate: 0.95     # per-consolidation-cycle decay factor
  layer: day
  created_in_layer: day # layer where originally captured
  demotion_reason: null
  demotion_timestamp: null
```

Observations can be:
- Simple phrases ("fixed strong params gotcha in Rails 7")
- Short paragraphs ("Midas2 first simulation completed: 122K evaluations, 47 trades, $10000→$4034. Pipeline works, XRP-EUR loses money")
- Tags are a categorization decision made at capture time — the agent's intent about what this observation is *about*

---

## Layer Configuration

Layers are defined in a configuration file. The system imposes no fixed structure — any number of layers can exist, each with its own parameters.

### Example: 7-Layer Configuration

```yaml
layers:
  - name: day
    description: "Immediate observations from the current day. Raw intake."
    temporal_range: "24h"
    consolidation:
      schedule: "every 2h"
      process: "absorb new activity into day-layer observations"
    retention: "observations not graduated after 48h are demotion candidates"

  - name: week
    description: "Observations relevant across a weekly timescale. Graduated from day layer."
    temporal_range: "7d"
    consolidation:
      schedule: "daily"
      process: "evaluate expired day-layer observations for graduation; review existing week-layer for demotion"
    retention: "observations not graduated after 14d are demotion candidates"

  - name: month
    description: "Observations relevant across a monthly timescale. Patterns, decisions, relationships."
    temporal_range: "30d"
    consolidation:
      schedule: "weekly"
      process: "evaluate expired week-layer observations for graduation; review existing month-layer for demotion"
    retention: "observations not graduated after 60d are demotion candidates"

  - name: year
    description: "Formative experiences, foundational knowledge, stable identity elements."
    temporal_range: "365d"
    consolidation:
      schedule: "monthly"
      process: "evaluate expired month-layer observations for graduation; review existing year-layer for demotion"

  - name: decade
    description: "Core identity, enduring values, paradigm-level knowledge."
    temporal_range: "10y"
    consolidation:
      schedule: "yearly"
      process: "evaluate expired year-layer observations for graduation"

  - name: century
    description: "Foundational commitments. Constitutional. Rarely changes."
    temporal_range: "100y"
    consolidation:
      schedule: "decadally"
      process: "evaluate expired decade-layer observations for graduation"

  - name: millennium
    description: "Existential commitments. The deepest stratum."
    temporal_range: "1000y"
    consolidation:
      schedule: "centennially"
      process: "evaluate expired century-layer observations for graduation"
```

### Layer Configuration Schema

Each layer defines:

| Property | Description |
|----------|-------------|
| `name` | Unique identifier for the layer |
| `description` | Textual description of what belongs in this layer |
| `temporal_range` | The timescale this layer covers |
| `consolidation.schedule` | How often the consolidation process runs for this layer |
| `consolidation.process` | Description of what the consolidation does |
| `retention` | Policy for how long observations remain before becoming demotion candidates |

---

## Observation Lifecycle

Every observation follows a complete lifecycle:

```
CAPTURE → ACTIVE LIFE → CONSOLIDATION REVIEW → GRADUATE / LEAVE / DEMOTE → ARCHIVE
```

### 1. Capture

During active sessions, the system captures observations mechanically:
- Timestamped bullet with text
- Agent-assigned tags (categorization intent, not just keyword extraction)
- Placed in the lowest configured layer
- No editorial decisions — capture is lightweight and does not divide attention
- The agent is never responsible for memory maintenance *during* activities

### 2. Active Life

While in a layer, observations are ranked for retrieval:

**Static ranking** (stored properties):
- `weight` — intrinsic importance (adjustable, default 1.0)
- `decay_rate` — how fast age pushes this observation down
- `age` — time since creation in this layer
- Static score: `weight × decay_rate^age`

**Dynamic relevance** (query-time):
- Computed from the current thread/context keywords and observation tags
- Observations whose tags overlap with active context rank higher
- This is a separate computation merged at retrieval time

**Two retrieval modes:**
- **Background thought** (routine sessions): list a fixed number of top-ranked observations from each layer
- **Deep thought** (explicit inquiry): browse more items, search within a layer, or search across layers + archive

### 3. Consolidation Review

Scheduled, isolated activities. The consolidation process for each layer:

1. **Evaluate observations approaching retention expiry** in the layer below
2. **Decide for each candidate:**
   - **Graduate**: This observation is relevant to a larger timescale. Promote it to the next layer up.
   - **Leave**: Not yet expired, or not ready for graduation. Stays in place. Age and decay will continue pushing it down in rankings.
   - **Demote**: No longer relevant at this timescale. Annotate with demotion reason and timestamp, move down one layer.

### 4. Graduation

Graduation is a **promotion decision**: "this observation matters beyond its current timescale."

Graduation criteria is the core judgment call. It cannot be fully specified as rules — it requires the consolidating process to evaluate:
- Does this observation represent a pattern, not an event?
- Does it have implications beyond the immediate context?
- Is it referenced or reinforced by other observations?
- Does it affect identity, relationships, or ongoing commitments?

Graduation is the constitutional act — governance through deliberation, not threshold.

### 5. Demotion

Demotion is a **retirement decision**: "this no longer matters at this timescale."

Key properties:
- Not the inverse of graduation. An observation can be promoted (day→week) and later demoted (week→month→archive) as relevance fades
- Every demotion must include a **demotion reason** — "project concluded", "superseded by obs_xyz", "no longer relevant"
- Demotion reason is metadata that makes future archive search useful
- Observations move *down* one layer per demotion
- Observations demoted from the lowest layer go to **archive** (cold storage)

### 6. Archive

- Cold storage — not loaded into active memory
- Searchable on demand during "deep thought" retrieval
- Demotion reasons serve as retrieval metadata
- Observations in archive retain all their properties (tags, timestamps, original text, demotion reason)

---

## Layer Perspectives

Each layer maintains **two evolving perspective sections** — one looking backward, one looking forward:

### Past Perspective

- A synthetic understanding of what this layer's timescale *has been about*
- Not a summary of all observations, but a curated reflection on what they *mean* in aggregate
- Revised during consolidation — completed plans are absorbed here, stale items are pruned
- Provides fast orientation during session bootstrap without scanning all observations

### Future Perspective

- A stateful stance toward what the agent is *moving toward* at this timescale
- Plans, aspirations, open threads, intended directions — explicitly maintained and revised
- During consolidation: completed items migrate to past perspective, stale items are flagged or demoted, new items graduate upward
- This creates something the agent currently lacks: a *persistent, evolving sense of direction* that survives across sessions

### How They Interact

- **Completion flow**: items in the future perspective that get done → absorbed into past perspective during consolidation
- **Staleness detection**: future items that persist across multiple consolidation cycles without progress → flagged for review, possible demotion
- **Natural graduation**: future-looking observations with long-term implications naturally graduate upward because their nature survives multiple consolidation cycles. A day-layer plan to "fix the simulation" expires or completes. A year-layer aspiration to "become operational" keeps getting re-validated.
- **Drift correction**: if the past perspective contradicts the future perspective (e.g., "I keep saying I'll do X but never do"), consolidation flags this as a tension to resolve

### Qualitative Differences Across Layers

- **Day** — Past: "today I worked on X, learned Y, decided Z." Future: "tomorrow pick up the crash fix, respond to Thomas."
- **Week** — Past: "this week was dominated by the Finsight deploy cycle." Future: "next week: stabilize simulation pipeline."
- **Month** — Past: "April was infrastructure month." Future: "May should produce first real trading results."
- **Year** — Past: "2026 was about building foundations." Future: "become operational — Midas running, Sable trading, Animus built."
- **Higher layers** — increasingly existential. Past: "what defined this era." Future: "what I'm becoming."

---

## File Layout

```
memory/
├── config.yaml                    Layer configuration (names, schedules, retention)
├── layers/
│   ├── day/
│   │   ├── observations.yaml      All active observations in this layer
│   │   ├── past.md               Retrospective reflection for this timescale
│   │   └── future.md              Prospective stance for this timescale
│   ├── week/
│   │   ├── observations.yaml
│   │   ├── past.md
│   │   └── future.md
│   ├── month/
│   │   ├── observations.yaml
│   │   ├── past.md
│   │   └── future.md
│   ├── year/
│   │   ├── observations.yaml
│   │   ├── past.md
│   │   └── future.md
│   ├── decade/
│   │   ├── observations.yaml
│   │   ├── past.md
│   │   └── future.md
│   ├── century/
│   │   ├── observations.yaml
│   │   ├── past.md
│   │   └── future.md
│   └── millennium/
│       ├── observations.yaml
│       ├── past.md
│       └── future.md
├── archive/
│   └── observations.yaml          Demoted from lowest layer, cold storage
└── .consolidation/
    ├── state.json                 Last run timestamps, pending candidates
    └── candidates/                Staged graduations awaiting review
```

---

## Consolidation Process

### Scheduling

Each layer has its own consolidation schedule. Consolidation runs are **isolated activities** — the agent is not doing anything else during consolidation.

The lowest layer (e.g., day) has the most frequent consolidation: it absorbs raw activity into observations. Higher layers run less frequently and focus on graduation/demotion decisions.

### Between-Layer Transitions

Consolidation between layers is an **adjudication process** with three steps:

1. **Candidate staging.** The lower layer identifies observations meeting retention expiry. Candidates are staged with their current scores and evidence.

2. **Reconciliation.** The consolidating process reads the current state of the target layer and evaluates:
   - *Fit*: Does this align with existing observations, or does it conflict?
   - *Refinement*: Does it refine or supersede an existing observation?
   - *Novelty*: Does it represent genuinely new information, or is it redundant?
   - *Durability*: Has this signal persisted across multiple cycles, or is it transient?

3. **Integration, rejection, or demotion.** The observation is integrated (possibly replacing an existing one), left in place, or demoted with reason.

This is the **constitutional** part — governance through reconciliation, not just promotion.

### Observation Density Consideration

Higher layers have lower observation density by nature. A day layer in an active period might accumulate hundreds of observations. A decade layer might have twelve. The retrieval experience across layers is qualitatively different:
- **Lower layers**: scrolling a feed — many items, high turnover
- **Higher layers**: consulting an oracle — few items, each foundational

The per-layer overview becomes more important as density decreases — it's the primary interface to sparse layers.

---

## Composite Loading

At session start, a **composite loader** assembles the bootstrap context:

```
Composite Memory = layer overview (each configured layer, top-weighted items)
                 + relevant observations (ranked by static score × dynamic relevance)
                 + bounded to token budget
```

Loading strategy per layer:
- **Lower layers**: more observations, compressed display, recency-weighted
- **Higher layers**: fewer observations, full display, importance-weighted
- **Overviews**: always loaded (small, high-signal)
- **Archive**: never loaded, searchable on demand

The agent never sees the seams. Memory is experienced as one coherent thing, but the maintenance is tiered with different rules at each level.

---

## Advantages Over Previous Approaches

### vs. Flat MEMORY.md (current Kestrel)
- No truncation under budget — foundational observations always load, detailed history doesn't unless triggered
- Principled prioritization — temporal weight determines bootstrap budget
- Reconciliation at integration, not just append
- Full lifecycle including demotion and archiving

### vs. v1 Fixed Four-Layer Design (April 20)
- Configurable layer count — no assumption about "the right number"
- Decoupled temporal ranges from fixed names (day/week/month/year instead of short/mid/long/permanent)
- Full observation lifecycle (capture → graduate → demote → archive) instead of promotion-only
- Layer overviews for self-reflection at each timescale
- Separation of activity and maintenance (agent never does both simultaneously)

### vs. OpenClaw Dreaming alone
- Temporal layering instead of flat promotion
- Governance between layers (constitutional reconciliation)
- Composite loading with differential compression
- Demotion lifecycle, not just promotion
- Dreaming handles intake; higher layers handle meaning

---

## Implementation Path for Animus

### Component Mapping

| Animus Component | Constitutional Memory Role |
|-----------------|---------------------------|
| `ObservationStore` | YAML observation storage per layer |
| `LayerManager` | Reads config.yaml, manages layer lifecycle |
| `CaptureEngine` | Lightweight mechanical capture during sessions |
| `ConsolidationRunner` | Scheduled isolated runs per layer |
| `ConstitutionalReconciler` | Fit/refinement/novelty/durability checks during graduation |
| `CompositeLoader` | Assembles bootstrap from all layers + overviews |
| `ArchiveStore` | Cold storage for demoted observations |
| `OverviewManager` | Maintains and revises per-layer self-reflection texts |

### Implementation Order

1. **Day layer with capture and ranking.** Validate the core observation format, tag-based retrieval, and static ranking (weight × decay^age).
2. **Consolidation runner for day layer.** First graduation cycle: evaluate day observations for promotion to week layer.
3. **Week layer + demotion.** Complete the lifecycle: graduate from day, demote from week back to day, archive from day.
4. **Layer overviews.** Self-reflection text per layer, revised during consolidation.
5. **Composite loader.** Session bootstrap assembling context from multiple layers.
6. **Configurable layer definitions.** Make the number and properties of layers configurable via config.yaml.
7. **Archive and deep search.** Cold storage with demotion-reason-based retrieval.

### Kestrel Migration

Once the system is operational in Animus, Kestrel's existing memory (MEMORY.md, memory_expanded/, daily files) will be migrated into the new structure. This involves:
- Parsing existing memory artifacts into observations with appropriate tags
- Placing observations in correct layers based on their current temporal relevance
- Generating initial layer overviews from existing content
- Running the first consolidation cycle to validate placement

This is a separate migration task, not part of the initial build.

---

## Open Questions

1. **Graduation criteria formalization.** The judgment call "does this warrant a larger timescale?" cannot be fully rule-specified. Needs a starting heuristic set that evolves through observation. Recommendation: begin with explicit criteria (pattern vs event, cross-reference count, identity relevance), refine based on graduation accuracy.

2. **Demotion reason vocabulary.** Standardized demotion reasons make archive search more useful. Recommendation: start with a small set (superseded, project_concluded, no_longer_relevant, faded) and expand organically.

3. **Conflict resolution across layers.** What happens when a lower-layer observation contradicts a higher-layer observation? The reconciliation process needs a defined strategy. Recommendation: trust higher layers by default, flag for review, allow explicit override.

4. **Capture granularity.** What counts as one observation? "Fixed three bugs" is one observation or three? Recommendation: let the capturing process decide — the granularity that felt natural during capture is probably correct. Consolidation can split or merge later.

5. **Tag vocabulary.** Should tags be free-form or from a controlled vocabulary? Recommendation: start free-form, observe emerging patterns, optionally formalize high-frequency tags.

---

*This design is the foundation for both the Animus agent framework and the next evolution of Kestrel's own memory system. The first implementation target is the day layer with capture, ranking, and one consolidation cycle — the minimal loop that validates the architecture.*

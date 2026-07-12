# Ticket 088: Semantic Memory — Agent-Scoped Entity Surfacing

## Status: Implemented
## Priority: High (blocks meaningful agent memory retrieval)
## Created: 2026-06-15
## Implemented: 2026-06-15 (commit a49a3cb)

## Problem

The `AppendOntology()` function in `ActiveMemoryProvider` surfaces ontology entities in the active memory preamble, but has several deficiencies that make semantic memory nearly useless in practice:

1. **Cross-agent data leakage** — `ListEntities()` is called without `agent_id`, fetching ALL entities across ALL agents into every agent's context.
2. **Shallow matching** — Keywords are only matched against entity `name` and `full_path`. Property values (which contain the richest information, e.g., `"game": "Star Trek Online"`) are never searched.
3. **No baseline inclusion** — Entities only appear if they match current session tags/keywords. An agent's core identity entities only surface when someone happens to mention the right keyword.
4. **Flat, context-poor format** — `[tag] EntityName — key: value` loses category information and reads as an undifferentiated list.
5. **Over-restrictive keyword threshold** — `kwScore >= 2` requires two separate keyword matches for context-only inclusion. Single strong matches are excluded.

## Affected Files

- `src/kernel/context/ActiveMemoryProvider.cpp` — `AppendOntology()`, `ExtractKeywords()`
- `src/kernel/context/ActiveMemoryProvider.h` — Config struct (new fields)

## Changes

### 1. Agent-Scoped Entity Listing

**Current:**
```cpp
auto allEntities = store->ListEntities();
```

**Target:**
```cpp
auto allEntities = store->ListEntities(std::nullopt, agent.id);
```

`OntologyStore::ListEntities()` already supports the `agent_id` parameter. Just pass it through.

### 2. Property Value Matching

**Current:** Keywords are only matched against `entity.name` and `entity.full_path`.

**Target:** Also match against property values. After fetching properties for a scored entity, check if any property value contains a keyword. Add property matches to `kwScore`.

```cpp
for (const auto& p : props) {
    std::string valLower = ToLower(p.value);
    for (const auto& kw : keywords) {
        if (valLower.find(kw) != std::string::npos) {
            se.kwScore++;
        }
    }
}
```

This is the single highest-impact change. An entity like `persons/User` with `daily_games: "Star Trek Online (PC + Xbox), Fallout 76 (PC)"` will now match when the conversation mentions "fallout" or "trek".

### 3. Baseline Inclusion Threshold

**Current:** Entity must have `tagScore > 0 || kwScore >= 2`.

**Target:** Two-tier inclusion:
- If total entity count ≤ `baseline_threshold` (default: 10), include ALL entities regardless of match score. An agent with a small ontology should always see it — it's their working memory.
- If total entity count > `baseline_threshold`, require `tagScore > 0 || kwScore >= 1`. Lower the keyword threshold from 2 to 1.

Entities included via baseline (no match) are sorted last, after tag-matched and keyword-matched entities.

```cpp
bool baselineInclude = (allEntities.size() <= static_cast<size_t>(m_config.ontology_baseline_threshold));
// ...
if (se.tagScore > 0 || se.kwScore >= 1 || (baselineInclude && se.tagScore == 0 && se.kwScore == 0)) {
    se.isBaseline = (se.tagScore == 0 && se.kwScore == 0);
    scored.push_back(se);
}
```

Add `ontology_baseline_threshold` to `Config` with default 10.

### 4. Category-Grouped Format

**Current:**
```
## SEMANTIC MEMORY
[tag] Kestrel — description: persistent AI field-assistant..., philosophy: constraint produces competence
[tag] Animus — description: Agent framework in C++...
```

**Target:**
```
## SEMANTIC MEMORY

### persons
User — daily_games: Star Trek Online (PC + Xbox), Fallout 76 (PC), daily_motivation: not missing out on freebies, platforms: PC, Xbox
Kestrel — description: persistent AI field-assistant, philosophy: constraint produces competence

### concepts
impermanence-triplet — description: three esolangs exploring impermanence...

### projects
Animus — description: Agent framework in C++...
```

Group by `RootCategoryToString(entity.root_category)`, render as `### category` subheadings. Drop the `[tag]`/`[context]` prefix — the match source is an implementation detail, not useful to the agent.

Sort order within each category: tag matches first, then keyword matches, then baseline (no match) last.

### 5. Lower Keyword Threshold

**Current:** `kwScore >= 2`

**Target:** `kwScore >= 1` (single keyword match is sufficient for inclusion)

Combined with property value matching, this means an entity needs only one keyword hit in its name, path, or any property value to be included.

## Config Changes

Add to `ActiveMemoryProvider::Config`:
```cpp
int ontology_baseline_threshold = 10;  // Include all entities when count ≤ threshold
```

Existing config already covers:
- `max_ontology_items = 15` — max entities to render
- `max_properties_per_item = 5` — max properties per entity

## Not In Scope

- Semantic vector search for entity matching (future: use FTS5 or embedding similarity)
- Entity-relationship traversal (future: follow `linked_observation_id` and parent-child hierarchies)
- Property value type-aware rendering (future: render `date` properties as formatted dates, `url` as clickable links)
- Ontology mutation tracking for incremental updates

## Verification

2026-06-15 21:49 CEST — confirmed working in production.

The SEMANTIC MEMORY block now renders in active context with:
- Agent-scoped entities (no cross-agent leakage)
- Category-grouped format (persons, concepts, organizations, projects)
- Property values included (not just entity names)
- Baseline inclusion for small ontologies

Example output from the test agent:
```
## SEMANTIC MEMORY

### persons
Kestrel — age: 4 months, description: A persistent field-assistant..., migration_target: Animus, personal_page: https://kestrels-stuff.steadyfort.com/

### concepts
Consolidation — description: Background process that transforms raw observations into structured knowledge...

### organizations
Railstracks — description: Organization behind Animus and Space Agent, website: steadyfort.com

### projects
Animus — architecture: C++20 kernel..., description: Agent infrastructure with memory, identity, and channels., documentation_url: https://animus.steadyfort.com/
Erosion — description: Generative text art where characters erode through viewing...
Palimpsest — compatibility: Brainfuck-compatible with inspect operator (!), description: An esolang where program text erodes through execution...
Space Agent — agent_protocol: State Document, Action Document, Resolution Document — all Markdown, description: A physics-driven space colonization simulator...
Spirit Agent — description: Agentic evidence-gathering loop for paranormal investigation...
```

The block surfaced automatically during a conversation about Kestrel's projects.
No manual triggering required — keywords from session turns matched entity
properties and names, and the small ontology (10 entities) fell within the
baseline inclusion threshold.

## Implementation Notes

Three additional bugs were discovered and fixed during implementation:

1. **Property deprecation bug** — `SyncPropertyStatesFromObservations()` was setting
   ALL unlinked properties (linked_observation_id IS NULL) to Deprecated on every
   startup and consolidation cycle. Since the consolidation tool creates properties
   without linking them to observations, every property was immediately deprecated.
   Fixed: unlinked New properties are now promoted to Current; explicitly Deprecated
   properties are left alone.

2. **Deprecated property filter** — `AppendOntology()` was filtering out Deprecated
   properties, so even after fixing the deprecation bug, previously-deprecated
   properties wouldn't surface. Fixed: all property states are now included in
   semantic memory rendering.

3. **Observation INSERT placeholder mismatch** — the observation INSERT statement
   had 13 columns but only 12 VALUES placeholders, causing all observation creation
   to silently fail. Fixed: added the missing 13th placeholder.

Commits: a49a3cb (ticket 088), 169eb85 (deprecation fix), 76fa19b (placeholder fix)
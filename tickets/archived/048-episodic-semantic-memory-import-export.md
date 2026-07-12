# Ticket 048 — Episodic & Semantic Memory Import/Export

**Priority:** P2 (migration tooling; enables structured memory transfer between Animus instances and from external sources)
**Status:** Open
**Dependencies:** None (MemoryStore and OntologyStore APIs are stable)
**Updated:** 2026-05-15

## Summary

A general-purpose import/export format for Animus episodic memory (observations) and semantic memory (ontology entities + properties). This enables:

- **Migration:** Transfer an agent's learned memory from one Animus instance to another
- **Gap-filling:** Inject curated memory for periods where raw session data is unavailable (e.g., manual preparation from daily logs)
- **Backup/restore:** Export an agent's memory state for archival or disaster recovery
- **Sharing:** Transfer distilled knowledge between agents

## Use Case: Kestrel Migration

The immediate use case is filling the Mar 13 – Apr 14 gap in Kestrel's history. Daily memory logs exist for every day in that period but no raw session files survived. The agent will prepare structured import files by hand, distilling daily logs into:

- Episodic observations tagged with correct historical timestamps
- Ontology entities and properties for concepts/projects formed during that period

## Format

A single JSON file per import batch. Human-readable, manually editable, version-controlled.

```json
{
  "schema": "animus-memory-import/v1",
  "description": "Kestrel gap-fill: March 13-20, 2026",
  "agent_id": "default",
  "created_at": "2026-05-15T13:40:00Z",
  "source": "manual-curation",
  "observations": [
    {
      "layer_name": "day",
      "text": "First memory consolidation architecture co-designed with Melvin — topic-partitioned memory with memory_expanded/ directory",
      "weight": 0.85,
      "tags": ["memory-system", "architecture", "milestone"],
      "source": "daily-log:2026-03-13",
      "created_at": "2026-03-13T10:00:00Z"
    }
  ],
  "entities": [
    {
      "category": "project",
      "name": "Memory Consolidation",
      "properties": {
        "architecture_version": "topic-partitioned memory with memory_expanded/ directory",
        "status": "operational",
        "location": "projects/memory-consolidation/"
      },
      "created_at": "2026-03-20T00:00:00Z",
      "facts": [
        {
          "text": "Origin session where Melvin and Kestrel co-designed the memory consolidation architecture",
          "source": "daily-log:2026-03-20",
          "created_at": "2026-03-20T15:00:00Z"
        }
      ]
    }
  ]
}
```

### Field reference

#### Top level

| Field | Required | Description |
|---|---|---|
| `schema` | ✅ | `"animus-memory-import/v1"` — format identifier |
| `description` | ✅ | Human-readable description of this import batch |
| `agent_id` | ✅ | Agent to assign observations/entities to |
| `created_at` | ✅ | When this import file was created (ISO 8601) |
| `source` | ⬚ | Origin of the data: `"manual-curation"`, `"export"`, `"migration"`, etc. |
| `observations` | ⬚ | Array of episodic observations to import |
| `entities` | ⬚ | Array of ontology entities with optional properties + facts |

#### Observation

| Field | Maps to | Description |
|---|---|---|
| `layer_name` | Lookup → `layer_id` | Human-readable layer name (resolved at import time) |
| `text` | `Observation.text` | The observation content |
| `weight` | `Observation.weight` | Importance weight (0.0–1.0), default 1.0 |
| `tags` | `Observation.tags_json` | JSON array of tags |
| `source` | `Observation.source` | Provenance string (e.g. `"daily-log:2026-03-13"`) |
| `created_at` | `Observation.created_at_unix_ms` | Historical timestamp (ISO 8601 → unix ms) |
| `memory_state` | `Observation.memory_state` | Optional: `"new"` (default), `"current"`, `"deprecated"` |

#### Entity

| Field | Maps to | Description |
|---|---|---|
| `category` | `OntologyEntity.root_category` | One of: `person`, `concept`, `procedure`, `event`, `location`, `organization`, `project` |
| `name` | `OntologyEntity.name` | Entity display name |
| `parent_path` | Lookup → `parent_id` | Optional path of parent entity (e.g. `"animus"` under projects) |
| `properties` | `OntologyProperty` entries | Key-value map (all values stored as Text type by default) |
| `created_at` | `OntologyEntity.created_at_unix_ms` | Historical timestamp |
| `facts` | `Observation` entries | Observations linked to this entity via `linked_observation_id` |

### Design decisions

- **`layer_name` not `layer_id`:** Layer IDs are database-local. Names are stable and portable. Resolved at import time via `ListLayers()`.
- **`category` as string:** RootCategory enum values serialized as lowercase strings. More readable than integers.
- **`parent_path` not `parent_id`:** Same reasoning — IDs are local, paths are stable. Uses `FindByPath()` for resolution.
- **Facts on entities:** Convenience — each fact becomes an Observation in the intake layer, linked to the entity property. Avoids needing to manually specify observations separately and then link them.
- **Timestamps are ISO 8601:** Human-readable, unambiguous. Converted to unix ms at import time.

## Export Format

Export uses the same schema. An agent's memory can be exported as:

```json
{
  "schema": "animus-memory-export/v1",
  "description": "Full export of agent Kestrel episodic + semantic memory",
  "agent_id": "default",
  "created_at": "2026-05-15T13:40:00Z",
  "source": "export",
  "observations": [...],
  "entities": [...]
}
```

Export includes:
- All observations for the agent across all layers
- All ontology entities with their properties
- Observation-entity links preserved via `source` field conventions

## Import Pipeline

```
┌──────────────────────────────────────────────────┐
│          MemoryImporter                           │
│                                                   │
│  1. Parse JSON file, validate schema              │
│  2. Resolve layer_name → layer_id for each obs    │
│  3. Resolve category string → RootCategory enum   │
│  4. Resolve parent_path → parent_id for entities  │
│  5. For each entity:                              │
│     a. Create entity if not exists (by path)      │
│     b. Upsert properties                          │
│     c. For each fact: create observation, link    │
│  6. For each observation:                         │
│     a. Create with historical timestamp           │
│     b. Set memory_state if specified              │
│  7. Report: counts, errors, skipped               │
└──────────────────────────────────────────────────┘
```

### Deduplication

- **Entities:** If an entity at the same path already exists, skip creation but still upsert properties (merge, don't overwrite). Existing properties not in the import file are preserved.
- **Observations:** No deduplication — observations are append-only. If the same text appears, it creates a second observation. This is intentional: re-importing should be idempotent only at the entity level, not the observation level. The caller should avoid duplicate batches.

### Error handling

- Unknown layer_name → error, skip observation
- Unknown parent_path → error, skip entity (but process its facts as orphan observations if desired)
- Unknown category string → error, skip entity
- Missing required fields → error, skip item, continue

## Surfaces

1. **Admin API:** `POST /api/v1/import/memory` — accepts JSON body or file upload
2. **Admin API:** `GET /api/v1/export/memory?agent_id=default` — produces export JSON
3. **CLI:** `animusd import memory <file.json>` and `animusd export memory --agent-id=default`

## Acceptance Criteria

- [ ] `MemoryImporter` class accepting JSON conforming to the schema above
- [ ] Observation import with layer resolution, tag parsing, historical timestamps
- [ ] Entity import with category resolution, parent path lookup, property upsert
- [ ] Fact-to-observation linking (facts on entities create linked observations)
- [ ] `MemoryExporter` producing valid import-format JSON from existing data
- [ ] Admin API endpoint for import (POST) and export (GET)
- [ ] Idempotent entity creation (skip if exists, merge properties)
- [ ] Comprehensive error reporting (invalid items listed, not silently dropped)
- [ ] Schema validation: reject files with wrong/missing `schema` field

## Files

- **New:** `include/animus_kernel/MemoryImporter.h`
- **New:** `src/kernel/import/MemoryImporter.cpp`
- **New:** `include/animus_kernel/MemoryExporter.h`
- **New:** `src/kernel/import/MemoryExporter.cpp`
- **Modified:** `src/kernel/admin/AdminServer.cpp` — import/export endpoints
- **Modified:** `CMakeLists.txt`

## Future Enhancements

- Incremental/delta exports (only observations since a given timestamp)
- File-based batch import (directory of JSON files)
- Schema v2: property value types (number, date, reference) in addition to text
- Conflict resolution strategies (overwrite vs merge vs skip for properties)

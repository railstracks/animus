# Ticket 046 — Ontology System: Tree-Structured Semantic Memory

**Priority:** P1  
**Status:** CLOSED
**Dependencies:** Ticket 045 (Layer-Owned Intake & Turn Processing)  
**Updated:** 2026-05-11

## Summary

Introduce a tree-structured Ontology system as a semantic memory layer, distinct from the episodic observation memory layers. Observations remain loose-thought distillations in memory layers; the Ontology provides a navigable tree of entities with typed properties, each linked 1:1 to a backing Observation for full provenance. Add `memory_state` to Observations to track lifecycle, and implement two-phase consolidation intake: Phase 1 extracts Observations, Phase 2 reconciles Observations into Ontology updates.

## Decisions Captured

1. Ontology is semantically separate from memory-layer Observations — observations are loose thoughts, ontology entities are structured property containers.
2. Root categories: `persons`, `concepts`, `procedures`, `events`, `locations`, `organizations`, `projects`.
3. `concepts`, `locations`, `organizations`, and `projects` support tree (parent/child) structure. `persons`, `events`, and `procedures` are flat.
4. Each Ontology property maintains a 1:1 link to a backing Observation. If a newer Observation supports the same property, it overwrites the link (most recent wins).
5. Property `memory_state` is inherited from its linked Observation's `memory_state`.
6. Consolidation intake is two-phase: (a) extract Observations → (b) reconcile into Ontology updates, using freshly-created Observation IDs.
7. An Observation can transition directly `new` → `deprecated` during review, regardless of current state.
8. Properties persist when their backing Observation is deleted — they become orphaned (`linked_observation_id = null`) with `memory_state = deprecated`.
9. Context injection from the Ontology system is out of scope for this ticket — it will be handled in a follow-up ticket once both memory systems are complete.
10. UI exposes the Ontology tree for exploration/browsing, not editing (editing happens through consolidation passes).
11. Property-level provenance must be reconstructable: for each property update, the previous and new value/link state must be recorded in the append-only mutation log.

## Motivation

The current system stores all extracted knowledge as loose observations in memory layers. The "ontology" in active context injection is flat — entity-tagged observations with truncation that can strip critical detail. There is no structured representation of entities, their relationships, or their properties.

A tree-structured Ontology provides:

- **Structured property storage:** Facts as typed key/value pairs, not narrative text.
- **Provenance:** Every property has a current backing Observation link, and historical links/values remain reconstructable from append-only mutation records for audit and reconciliation.
- **Organized browsing:** Tree navigation by category (concepts, locations, projects, organizations) with parent/child relationships where appropriate.
- **State lifecycle:** Properties carry `memory_state` inherited from their backing Observation, so outdated information is visible as deprecated rather than silently conflicting.
- **Reduced context waste:** A tree path (`concepts/philosophy/mind/consciousness`) is cheaper to inject than narrative text, and depth can be controlled.

## Scope

### 1) Observation `memory_state` field

Add to `Observation`:

```cpp
enum class MemoryState : int32_t {
    New        = 0,  // freshly taken in, not yet reviewed
    Current    = 1,  // reviewed, still relevant
    Deprecated = 2   // no longer relevant, deprioritized in sort/display
};

// On Observation struct:
MemoryState memory_state{MemoryState::New};
```

Behavior:

- Intake-created observations start as `New`.
- Review consolidation may set any observation to `Current` or `Deprecated`.
- Direct `New` → `Deprecated` is valid (e.g., intake captured something already obsolete).
- Observation sort/query should order by `memory_state` (Current > New > Deprecated), then by weight or timestamp within each state.
- When `memory_state` transitions, linked Ontology properties inherit the new state automatically.

Files: `include/animus_kernel/MemoryStore.h`, `src/kernel/memory/MemoryStore.cpp`, migration.

### 2) Ontology schema

**OntologyEntity:**

```cpp
enum class RootCategory : int32_t {
    Persons       = 0,
    Concepts      = 1,
    Procedures    = 2,
    Events        = 3,
    Locations     = 4,
    Organizations = 5,
    Projects      = 6
};

struct OntologyEntity {
    int64_t id{0};
    std::optional<int64_t> parent_id;        // null = root entity
    RootCategory root_category;
    std::string name;                        // path segment, unique within parent
    std::string full_path;                   // denormalized: "concepts/philosophy/mind"
    int32_t sort_order{0};                   // within siblings
    int64_t created_at_unix_ms{0};
    int64_t updated_at_unix_ms{0};
};
```

**OntologyProperty:**

```cpp
enum class PropertyType : int32_t {
    Text      = 0,  // free-form string
    Number    = 1,  // numeric, stored as string for flexibility
    Date      = 2,  // ISO-8601 or epoch ms
    Reference = 3,  // reference to another OntologyEntity by ID
    Url       = 4   // validated URL
};

struct OntologyProperty {
    int64_t id{0};
    int64_t entity_id{0};                    // FK → OntologyEntity
    std::string key;                         // e.g. "phone", "description", "founded"
    std::string value;                       // the actual value
    PropertyType value_type{PropertyType::Text};
    MemoryState memory_state{MemoryState::New}; // inherited from linked Observation
    std::optional<int64_t> linked_observation_id; // FK → Observation, 1:1, null = orphaned
    int64_t created_at_unix_ms{0};
    int64_t updated_at_unix_ms{0};
};
```

**OntologyMutation (append-only audit log):**

```cpp
struct OntologyMutation {
    int64_t id{0};
    std::string mutation_type;   // create_entity, delete_entity, move_entity,
                                 // set_property, clear_property
    std::string target_type;     // entity, property
    int64_t target_id{0};
    std::string previous_state;  // JSON snapshot before mutation
    std::string new_state;       // JSON snapshot after mutation
    std::string motivation;
    int64_t unix_ms{0};
};
```

### 3) Property type conventions

Initial schema with per-root-category conventions. These are defaults — the schema is modifiable at runtime.

| Root Category | Conventional Properties | Types |
|---|---|---|
| **persons** | `display_name`, `email`, `phone`, `role`, `notes`, `url` | text, text, text, text, text, url |
| **concepts** | `description`, `definition`, `url` | text, text, url |
| **procedures** | `description`, `prerequisites`, `steps`, `outcome`, `pitfalls` | text, text, text, text, text |
| **events** | `description`, `date`, `location`, `participants`, `outcome` | text, date, reference, text, text |
| **locations** | `description`, `address`, `coordinates`, `type`, `url` | text, text, text, text, url |
| **organizations** | `description`, `founded`, `mission`, `size`, `url`, `parent_org` | text, date, text, text, url, reference |
| **projects** | `description`, `status`, `repository`, `language`, `dependencies`, `url` | text, text, url, text, text, url |

The tool should not hard-reject properties outside these conventions, but the conventions serve as the default vocabulary for intake prompts and UI hints.

### 4) Two-phase consolidation intake

Phase 2 ("Ontology Gardener") runs after Phase 1 (Observation extraction) within the same intake session. The consolidator:

1. Runs Phase 1: extracts new Observations, stores them (getting fresh IDs).
2. Runs Phase 2: reviews fresh Observations, extracts Ontology entities and properties, using the new Observation IDs for linking.

**Template:** `templates/consolidation-ontology/<locale>.md` — a new template category for the ontology gardener pass.

The consolidation tool gains an `ontology_updates` parameter or a separate `update_ontology` tool that accepts:

```json
{
  "upserts": [
    {
      "root_category": "persons",
      "path": "jack_smith",           // relative to root, slash-separated for trees
      "properties": {
        "phone": {
          "value": "+1-555-0199",
          "value_type": "text",
          "linked_observation_id": 42
        }
      }
    }
  ]
}
```

Tool behavior:

- If entity at `path` doesn't exist → create it (and intermediate nodes for tree categories).
- If property `key` already exists → update value, type, and `linked_observation_id` (most recent wins).
- Set property `memory_state` from the linked Observation's current `memory_state`.
- Log mutation for each change with both `previous_state` and `new_state` so provenance can be reconstructed per property over time.

### 5) OntologyStore (SQLite-backed)

New class following the `MemoryStore` pattern:

```cpp
class OntologyStore {
public:
    explicit OntologyStore(IDataStore* dataStore);
    ~OntologyStore();

    // Entities
    std::vector<OntologyEntity> ListEntities(
        std::optional<RootCategory> category = std::nullopt);
    std::vector<OntologyEntity> ListChildren(
        std::optional<int64_t> parent_id);  // null → root entities
    std::optional<OntologyEntity> GetEntity(int64_t id);
    std::optional<OntologyEntity> FindByPath(
        RootCategory category, const std::string& full_path);
    OntologyEntity CreateEntity(const OntologyEntity& entity);
    bool UpdateEntity(const OntologyEntity& entity);
    bool MoveEntity(int64_t id, std::optional<int64_t> new_parent_id,
                    const std::string& motivation);
    bool DeleteEntity(int64_t id);

    // Properties
    std::vector<OntologyProperty> ListProperties(int64_t entity_id);
    std::optional<OntologyProperty> GetProperty(int64_t entity_id,
                                                 const std::string& key);
    OntologyProperty SetProperty(const OntologyProperty& prop);  // upsert
    bool DeleteProperty(int64_t id);

    // Sync memory_state from linked Observation
    void SyncPropertyStatesFromObservations(MemoryStore* memoryStore);

    // Mutations
    void LogMutation(const OntologyMutation& m);
    std::vector<OntologyMutation> QueryMutations(int64_t since_unix_ms = 0,
                                                  int64_t limit = 100);

private:
    void EnsureSchema();
    IDataStore* m_store;
};
```

### 6) Admin API endpoints

Add to admin server routes:

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/api/v1/ontology/categories` | List root categories with entity counts |
| `GET` | `/api/v1/ontology/entities` | List entities (optional `?category=` filter) |
| `GET` | `/api/v1/ontology/entities?parent_id=` | List children of entity |
| `GET` | `/api/v1/ontology/entities/{id}` | Get entity with its properties |
| `GET` | `/api/v1/ontology/entities/{id}/properties` | List properties for entity |
| `GET` | `/api/v1/ontology/entities/{id}/tree` | Entity + recursive children (depth-limited) |
| `GET` | `/api/v1/ontology/mutations` | Mutation log (append-only audit trail) |

Write operations are intentionally not exposed — ontology mutations happen through consolidation passes, not direct API edits.

### 7) Admin UI — Ontology Explorer

New page at `/ontology`:

- **Left panel:** Category selector + tree view.
  - Tree categories render nested (concepts, locations, organizations, projects).
  - Flat categories render as sorted lists (persons, events, procedures).
  - Click entity to select it.
- **Right panel:** Entity detail.
  - Entity name, full path, category.
  - Properties table: key, typed value, `memory_state` chip, linked observation ID (clickable).
  - State chips colored: new = blue, current = green, deprecated = grey/strikethrough.
- **Navigation:** Sidebar entry "Ontology" under Memory section.

Files: `admin-ui/src/views/OntologyView.vue`, router, sidebar, i18n.

### 8) Observation state sync

When an Observation's `memory_state` changes (via review consolidation), linked Ontology properties must inherit the new state. This should happen:

- Directly when the Observation is updated (in `UpdateObservation`).
- As a batch sync on startup (`SyncPropertyStatesFromObservations`).

## Acceptance Criteria

- [ ] `Observation.memory_state` field added with `New` / `Current` / `Deprecated` enum, default `New`.
- [ ] `memory_state` added to Observation API responses.
- [ ] Observation sorting prioritizes by `memory_state` (Current > New > Deprecated).
- [ ] `OntologyEntity` schema created in SQLite with root categories, parent/child, and full_path.
- [ ] `OntologyProperty` schema created with typed values, 1:1 Observation link, and `memory_state`.
- [ ] `OntologyMutation` append-only log operational.
- [ ] `OntologyStore` class implemented with full CRUD for entities and upsert for properties.
- [ ] Two-phase intake: Phase 2 (ontology gardener) runs after Phase 1 within the same intake session.
- [ ] Consolidation tool supports `ontology_updates` parameter for declarative entity/property creation.
- [ ] Single-intake-layer invariant preserved in Phase 2 (no separate ontology intake layer).
- [ ] Property upsert: existing property with same key on same entity gets overwritten (most recent Observation wins).
- [ ] Property `memory_state` inherits from linked Observation's `memory_state` on creation and on Observation update.
- [ ] Orphaned properties (Observation deleted) set `linked_observation_id = null`, `memory_state = Deprecated`.
- [ ] Property provenance is reconstructable from mutation history (`previous_state` + `new_state` snapshots for each property mutation).
- [ ] Templates: `consolidation-ontology/en.md` with default ontology gardener prompt.
- [ ] Admin API exposes ontology entities, properties, categories, tree view, and mutation log.
- [ ] Admin UI provides `/ontology` route with tree explorer and entity detail view.
- [ ] i18n keys added for ontology UI (en + nl).
- [ ] Tests cover: entity CRUD, property upsert, path uniqueness, state sync, move entity, tree queries.

## Out of Scope

- Context injection from Ontology into session bootstrap (separate ticket).
- Ontology editing via UI (mutations are consolidation-driven only).
- Multi-agent ontology partitioning (single global ontology for now).
- Full-text search across entities (separate ticket).
- Ontology import/export.

## Files Expected To Change

- `include/animus_kernel/MemoryStore.h` — `MemoryState` enum, `memory_state` on `Observation`
- `include/animus_kernel/OntologyStore.h` — new file: `OntologyEntity`, `OntologyProperty`, `OntologyMutation`, `OntologyStore` class
- `src/kernel/memory/MemoryStore.cpp` — migration for `memory_state` column, sort/query updates
- `src/kernel/ontology/OntologyStore.cpp` — new file: full implementation
- `src/kernel/consolidation/ConsolidationPipeline.cpp` — two-phase intake routing, ontology updates
- `include/animus_kernel/consolidation/ConsolidationPipeline.h` — signature changes for ontology-aware intake
- `src/kernel/admin/internal/AdminServerRoutesInterfacesSessionsMemory.inc` — ontology endpoints
- `src/kernel/admin/AdminServerRoutes.cpp` — ontology route registration
- `admin-ui/src/router/index.ts` — `/ontology` route
- `admin-ui/src/components/AppSidebar.vue` — ontology nav entry
- `admin-ui/src/views/OntologyView.vue` — new file
- `admin-ui/src/i18n/locales/en.ts` — ontology keys
- `admin-ui/src/i18n/locales/nl.ts` — ontology keys
- `templates/consolidation-ontology/en.md` — new ontology gardener template
- `tests/OntologyStoreTests.cpp` — new file
- `tests/ConsolidationTests.cpp` — two-phase intake tests
- `tests/AdminServerTests.cpp` — ontology endpoint tests

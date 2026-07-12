# Animus QoL Tools — Shared Infrastructure Schematic

**Author:** Kestrel | **Date:** 2026-06-03 | **Status:** Design exploration

## 1. Observation

The proposed QoL tools (Educate, Food, Calendar, Fitness, Adventure, Blog) share
common data patterns despite different domains:

| Pattern | Educate | Food | Calendar | Fitness | Adventure | Blog |
|---------|---------|------|----------|---------|-----------|------|
| Entity + properties | ✅ courses, lessons | ✅ recipes, ingredients | ✅ events, venues | ✅ exercises, vitals | ✅ characters, locations | ✅ posts |
| Typed relationships | ✅ prerequisites | ✅ contains | ✅ attendees | ✅ part_of | ✅ member_of, located_at | ✅ references |
| State machines | ✅ progress | ⬜ | ✅ booking status | ✅ completion | ✅ plot phase | ⬜ |
| Time-series | ⬜ | ⬜ | ✅ scheduling | ✅ vitals tracking | ✅ session log | ✅ publish timeline |
| External APIs | ⬜ | ✅ nutritional DB | ⬜ | ✅ device data | ⬜ | ⬜ |
| Multi-user | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ (public) |

Six independent implementations of these patterns would be wasteful. Animus already has:
- **SQLite** for structured storage
- **Lua 5.4** as plugin runtime with sandboxed state
- **CompositeTool** base class for tool registration
- **Managed HTTP** for external API calls
- **ScriptStore** for Lua persistence

The proposal: a **thin shared Lua module** (`qol_base`) that each tool requires, plus
a **shared table schema** that keeps data relational and queryable across tools.

---

## 2. Shared Schema

Four tables. Each QoL tool owns a namespace (`tool` column) and defines its own
entity types, relations, and state machines within that namespace.

```sql
-- 2a. Entities: the core of everything
-- Each tool stores its domain objects here with typed discrimination.
CREATE TABLE qol_entities (
    id          TEXT PRIMARY KEY,          -- UUID
    tool        TEXT NOT NULL,             -- namespace: 'educate', 'food', etc.
    type        TEXT NOT NULL,             -- entity type within tool
    owner_id    TEXT,                      -- user/agent who owns this entity
    data        TEXT NOT NULL,             -- JSON payload (tool-specific schema)
    state       TEXT,                      -- current state machine position (nullable)
    created_at  TEXT DEFAULT (datetime('now')),
    updated_at  TEXT DEFAULT (datetime('now')),
    FOREIGN KEY (owner_id) REFERENCES qol_entities(id)
);

-- Indexes for common queries
CREATE INDEX idx_qol_entities_tool_type ON qol_entities(tool, type);
CREATE INDEX idx_qol_entities_owner ON qol_entities(owner_id);
CREATE INDEX idx_qol_entities_state ON qol_entities(tool, state);

-- 2b. Relations: typed directed edges between entities
-- Replaces per-tool join tables with a generic graph.
CREATE TABLE qol_relations (
    id          TEXT PRIMARY KEY,
    from_id     TEXT NOT NULL,
    to_id       TEXT NOT NULL,
    relation    TEXT NOT NULL,             -- edge type (e.g., 'prerequisite', 'contains')
    data        TEXT,                      -- JSON for edge metadata (weight, label, etc.)
    created_at  TEXT DEFAULT (datetime('now')),
    FOREIGN KEY (from_id) REFERENCES qol_entities(id) ON DELETE CASCADE,
    FOREIGN KEY (to_id)   REFERENCES qol_entities(id) ON DELETE CASCADE,
    UNIQUE(from_id, to_id, relation)
);

CREATE INDEX idx_qol_relations_from ON qol_relations(from_id);
CREATE INDEX idx_qol_relations_to   ON qol_relations(to_id);
CREATE INDEX idx_qol_relations_type ON qol_relations(relation);

-- 2c. Time-series: timestamped measurements
-- For vitals, event scheduling, publishing timestamps.
CREATE TABLE qol_timeseries (
    id          TEXT PRIMARY KEY,
    entity_id   TEXT NOT NULL,
    metric      TEXT NOT NULL,
    value       REAL NOT NULL,
    timestamp   TEXT NOT NULL,             -- ISO-8601
    metadata    TEXT,                      -- JSON (unit, source, confidence)
    FOREIGN KEY (entity_id) REFERENCES qol_entities(id) ON DELETE CASCADE
);

CREATE INDEX idx_qol_ts_entity_metric ON qol_timeseries(entity_id, metric);
CREATE INDEX idx_qol_ts_timestamp ON qol_timeseries(timestamp);

-- 2d. State machine audit log
-- Tracks transitions for debugging and rollback capability.
CREATE TABLE qol_state_log (
    id          TEXT PRIMARY KEY,
    entity_id   TEXT NOT NULL,
    from_state  TEXT,
    to_state    TEXT NOT NULL,
    trigger     TEXT,                      -- what caused the transition
    metadata    TEXT,                      -- JSON
    created_at  TEXT DEFAULT (datetime('now')),
    FOREIGN KEY (entity_id) REFERENCES qol_entities(id) ON DELETE CASCADE
);

CREATE INDEX idx_qol_state_log_entity ON qol_state_log(entity_id);
```

### Why this shape

- **Single source of truth.** No per-tool database sprawl. One migration, one backup, one query surface.
- **Cross-tool queries possible.** "Show me everything related to user X" is a single query.
- **Tool isolation via namespace.** Each tool's Lua module only reads/writes its own `tool` value. No cross-contamination.
- **SQLite.** Animus already uses it. No new dependency.
- **JSON payload.** Each entity type has its own schema enforced by the Lua module, not by the database. Flexible without migration hell.

---

## 3. Shared Lua Module: `qol_base`

Each QoL tool is a Lua script that `require("qol_base")`. The base module provides
CRUD operations against the shared tables plus state machine helpers.

```lua
-- qol_base.lua — shared infrastructure for QoL tools
--
-- Provides:
--   EntityStore:  CRUD + query against qol_entities
--   RelationStore: typed edge management
--   TimeSeries:   append + range query
--   StateMachine: define + transition + audit log

local M = {}

-- StateMachine definition
-- A tool defines valid states and transitions:
--
--   local sm = StateMachine.define({
--       initial = "draft",
--       transitions = {
--           { from = "draft",    to = "published", trigger = "publish" },
--           { from = "published", to = "archived", trigger = "archive" },
--       }
--   })
--
--   sm:transition(entity, "publish")  -- validates, writes state + audit log

function M.StateMachine_define(config)
    return {
        initial = config.initial,
        transitions = config.transitions,  -- { from, to, trigger }[]
        validate = function(self, from_state, trigger)
            for _, t in ipairs(self.transitions) do
                if t.from == from_state and t.trigger == trigger then
                    return true, t.to
                end
            end
            return false, nil
        end
    }
end

-- EntityStore: wraps SQLite operations for a given tool namespace
--
--   local store = EntityStore.new("adventure")
--   local char = store:create({
--       type = "character",
--       owner_id = agent_id,
--       data = { name = "Elena Vasquez", role = "investigator" },
--       state = nil  -- no state machine for this entity type
--   })

function M.EntityStore_new(tool_name)
    return {
        tool = tool_name,

        create = function(self, params)
            -- INSERT into qol_entities, return entity object
        end,

        get = function(self, id)
            -- SELECT by id, return parsed entity
        end,

        query = function(self, filters)
            -- SELECT with optional filters: type, owner_id, state, custom WHERE on data JSON
        end,

        update = function(self, id, changes)
            -- UPDATE data/state/updated_at, return updated entity
        end,

        delete = function(self, id)
            -- DELETE (cascades to relations, timeseries, state_log)
        end,
    }
end

-- RelationStore: typed edges between entities
--
--   store:relate(char_id, faction_id, "member_of", { rank = "lieutenant" })

function M.RelationStore_new(tool_name)
    return {
        relate = function(self, from_id, to_id, relation, metadata)
            -- INSERT into qol_relations
        end,

        unrelate = function(self, from_id, to_id, relation)
            -- DELETE from qol_relations
        end,

        neighbors = function(self, entity_id, opts)
            -- opts.direction: "outgoing" | "incoming" | "both"
            -- opts.relation: filter by type
            -- Returns list of related entities
        end,

        path = function(self, from_id, to_id, max_depth)
            -- BFS/DFS pathfinding through the relation graph
        end,
    }
end

-- TimeSeries: append-only measurements
--
--   ts:record(user_id, "weight_kg", 82.5, { unit = "kg", source = "manual" })

function M.TimeSeries_new(tool_name)
    return {
        record = function(self, entity_id, metric, value, metadata)
            -- INSERT into qol_timeseries
        end,

        range = function(self, entity_id, metric, from_ts, to_ts)
            -- SELECT range, return ordered list
        end,

        latest = function(self, entity_id, metric, n)
            -- Return last N readings
        end,

        aggregate = function(self, entity_id, metric, fn, window)
            -- fn: "avg" | "min" | "max" | "count"
            -- window: { days = 7 } etc.
        end,
    }
end

return M
```

### C++ Integration Point

The C++ side needs minimal changes:
1. **One migration** adding the four `qol_*` tables to the SQLite database.
2. **One new Lua bridge** exposing SQLite prepared statements for the four tables
   (or extending the existing `LuaTool` with generic query helpers).
3. **Each QoL tool** is a Lua script registered via `animus.register_tool()`.

No new tool handler C++ classes needed for each tool. The heavy lifting is in Lua.

---

## 4. Tool Schematics

### 4.1 Educate

**Entity types:** `user`, `program`, `module`, `lesson`, `quiz`, `enrollment`

**State machines:**

```
enrollment:  not_started → in_progress → paused → completed → certified
lesson:      locked → available → in_progress → completed
quiz:        not_taken → in_progress → passed → failed → retake_allowed
```

**Relations:**
```
program   --contains--> module       (ordered, sequence number in metadata)
module    --contains--> lesson       (ordered)
module    --prerequisite--> module   (DAG of dependencies)
user      --enrolled_in--> program   (via enrollment entity)
lesson    --assessed_by--> quiz      (1:1)
```

**Example entity payloads:**

```json
// Entity: lesson
{
  "title": "Variables and Types",
  "description": "Introduction to typed variables in the target language",
  "sources": [
    { "type": "text", "content": "..." },
    { "type": "url", "url": "https://..." },
    { "type": "exercise", "prompt": "..." }
  ],
  "estimated_minutes": 30,
  "tags": ["basics", "variables", "types"]
}

// Entity: quiz
{
  "lesson_id": "<uuid>",
  "questions": [
    {
      "prompt": "What is the output of `print(type(42))`?",
      "options": ["number", "integer", "float", "int"],
      "correct": 0,
      "explanation": "Lua uses 'number' for all numeric types"
    }
  ],
  "pass_threshold": 0.7,
  "max_attempts": 3
}
```

**Tool actions for the LLM:**
- `educate.list_programs` / `educate.get_program`
- `educate.create_program` / `educate.create_module` / `educate.create_lesson`
- `educate.enroll_user` — creates enrollment, unlocks first module's lessons
- `educate.get_progress` — shows user's position in curriculum DAG
- `educate.start_lesson` / `educate.complete_lesson` — state transitions
- `educate.generate_quiz` — LLM-native, stores as quiz entity
- `educate.take_quiz` — records answers, evaluates, state transition
- `educate.next_recommended` — traverses DAG, finds unlocked+incomplete lessons

**Prerequisite unlock logic:** A lesson becomes `available` when all lessons
linked via `prerequisite` relations are `completed`. The Lua module computes this
on `get_progress` and `next_recommended`.

---

### 4.2 Food

**Entity types:** `user_profile`, `recipe`, `ingredient`, `meal_plan`, `food_item`

**State machines:**
```
meal_plan:  draft → active → completed
```

**Relations:**
```
recipe     --uses--> ingredient    (with quantity in metadata)
recipe     --suitable_for--> user_profile
meal_plan  --includes--> recipe    (with date/meal_slot in metadata)
food_item  --substitutes--> food_item
```

**External API integration:**

```lua
-- Food data comes from external APIs, cached locally
local API_CACHE_TTL = 86400  -- 24 hours

function lookup_nutrition(food_name)
    -- 1. Check local cache (qol_entities, type="food_item", cached_at in data)
    -- 2. If stale or missing, query USDA FoodData Central or Open Food Facts
    -- 3. Cache result as food_item entity
    -- 4. Return parsed nutrition data
end
```

**Example entity payloads:**

```json
// Entity: user_profile
{
  "display_name": "Melvin",
  "allergies": ["shellfish", "tree_nuts"],
  "dietary_requirements": ["vegetarian"],
  "calorie_target": 2200,
  "macro_targets": { "protein_pct": 25, "carbs_pct": 45, "fat_pct": 30 },
  "disliked_foods": ["olives", "blue_cheese"],
  "favorite_cuisines": ["italian", "japanese", "mexican"]
}

// Entity: recipe
{
  "title": "Mushroom Risotto",
  "servings": 4,
  "prep_minutes": 15,
  "cook_minutes": 35,
  "ingredients": [
    { "food_item_id": "<uuid>", "amount": 300, "unit": "g" },
    { "food_item_id": "<uuid>", "amount": 1, "unit": "L" }
  ],
  "steps": [
    "Heat stock in a saucepan...",
    "In a large pan, sauté onion..."
  ],
  "nutrition_per_serving": {
    "calories": 420,
    "protein_g": 12,
    "carbs_g": 58,
    "fat_g": 14
  },
  "tags": ["vegetarian", "italian", "comfort"]
}

// Entity: food_item (cached from USDA)
{
  "source": "usda",
  "source_id": "FDC_12345",
  "name": "Arborio Rice",
  "nutrition_per_100g": {
    "calories": 350,
    "protein_g": 7,
    "carbs_g": 77,
    "fat_g": 0.6,
    "fiber_g": 1.8
  },
  "cached_at": "2026-06-03T12:00:00Z"
}
```

**Tool actions:**
- `food.get_profile` / `food.update_profile` — allergy/diet management
- `food.lookup` — query nutrition for a food item (cached external lookup)
- `food.search_recipes` — by tags, dietary compatibility, ingredients on hand
- `food.create_recipe` / `food.generate_recipe` — LLM-native recipe creation
- `food.plan_meals` — creates meal_plan entity, fills with recipes
- `food.check_allergies` — validates a recipe against user profile, returns warnings
- `food.substitution` — find alternatives for an ingredient given dietary constraints

---

### 4.3 Calendar

**Entity types:** `event`, `person`, `location`, `arrangement`, `contact`

**State machines:**
```
event:       draft → confirmed → in_progress → completed → cancelled
arrangement: pending → contacted → quoted → booked → confirmed → cancelled
person:      prospect → contacted → confirmed → cancelled
location:    prospect → contacted → available → booked → cancelled
```

**Relations:**
```
event       --has_organizer--> person
event       --has_attendee--> person     (with rsvp_status in metadata)
event       --held_at--> location
event       --requires--> arrangement    (catering, transport, equipment, etc.)
arrangement --handled_by--> contact      (external person/vendor)
```

**Example entity payloads:**

```json
// Entity: event
{
  "title": "Project Review Dinner",
  "description": "Q2 progress review with Thomas",
  "start": "2026-06-15T19:00:00+02:00",
  "end": "2026-06-15T22:00:00+02:00",
  "timezone": "Europe/Amsterdam",
  "recurrence": null,
  "notes": "Thomas prefers Italian"
}

// Entity: arrangement (linked to event)
{
  "type": "restaurant_booking",
  "details": {
    "venue": "Trattoria Romana",
    "party_size": 4,
    "special_requests": "Window table if possible"
  },
  "status_history": [
    { "state": "pending", "at": "2026-06-03T10:00:00Z" },
    { "state": "contacted", "at": "2026-06-03T10:15:00Z", "note": "Called, awaiting confirmation" }
  ]
}
```

**Tool actions:**
- `calendar.create_event` / `calendar.get_event` / `calendar.list_events`
- `calendar.update_event` / `calendar.cancel_event`
- `calendar.add_person` — creates person entity, links to event
- `calendar.add_location` — creates location entity, links to event
- `calendar.create_arrangement` — booking/task with state tracking
- `calendar.update_arrangement_state` — state machine transition
- `calendar.upcoming` — events in next N days with status of all linked entities
- `calendar.conflicts` — detect overlapping events

**The workflow orchestrator aspect:** When the agent creates an event with arrangements,
the arrangement entities carry their own state. The agent can check `calendar.upcoming`
and see "this dinner is confirmed, but the restaurant booking is still in 'contacted' state"
— and act on it. This is the genuinely agent-native value.

---

### 4.4 Adventure

**Entity types:** `world`, `character`, `location`, `faction`, `item`, `plot`, `scene`, `session`

**State machines:**
```
plot:    concept → outlined → active → climactic → resolved → archived
scene:   draft → ready → active → completed
session: setup → in_progress → concluded
```

**Relations:**
```
character --member_of--> faction      (with rank/role in metadata)
character --located_at--> location
character --knows--> character         (with relationship_type: ally, rival, neutral, secret)
character --owns--> item
character --involved_in--> plot
faction   --controls--> location
faction   --allied_with--> faction
faction   --opposed_to--> faction
location  --contains--> location      (nested geography: city → district → building)
plot      --consists_of--> scene      (ordered)
scene     --features--> character
scene     --set_in--> location
session   --plays_through--> scene    (ordered, tracks which scenes happened)
```

**Example entity payloads:**

```json
// Entity: world
{
  "name": "San Francisco Noir",
  "setting": "1920s San Francisco, World of Darkness",
  "themes": ["noir", "investigation", "political_intrigue", "supernatural"],
  "tone": "dark_atmospheric",
  "premise": "Prohibition-era SF where Kindred politics mirror human corruption",
  "rules_system": "VtM V5",
  "created_date": "2026-05-15"
}

// Entity: character
{
  "name": "Elena Vasquez",
  "type": "NPC",
  "concept": "Malkavian private investigator",
  "clan": "Malkavian",
  "generation": 12,
  "description": "A sharp-eyed woman in her late 30s with an unsettling gaze...",
  "personality": ["observant", "cryptic", "compassionate", "unpredictable"],
  "abilities": { "auspex": 3, "obfuscate": 2, "dominate": 1 },
  "background": "Former Pinkerton detective. Embraced after investigating the wrong case...",
  "status": "active",
  "goals": ["Find her sire", "Protect the neighborhood", "Decode the whispers"],
  "secrets": ["Her sire is the Prince's childe", "She possesses a fragment of the Book of Nod"]
}

// Entity: plot
{
  "title": "The Chinatown Contraband",
  "synopsis": "A shipment of tainted bootleg alcohol is causing Kindred who drink from...",
  "themes": ["investigation", "faction_politics", "body_horror"],
  "stakes": "If unsolved, the tainted blood supply threatens the Masquerade",
  "scenes": ["<scene_uuid_1>", "<scene_uuid_2>", "<scene_uuid_3>"],
  "status": "active",
  "introduced_npcs": ["<character_uuid_1>", "<character_uuid_2>"],
  "player_knowledge": ["Tainted alcohol exists", "Chinatown Tong is involved"],
  "hidden_truths": ["The Tremere engineered the taint", "The Prince knows and is allowing it"]
}

// Entity: scene
{
  "title": "The Opium Den Lead",
  "description": "A contact in Chinatown offers information about the tainted shipment...",
  "location_id": "<location_uuid>",
  "featured_npcs": ["<character_uuid>"],
  "plot_points_to_cover": [
    "Tong connection to bootleggers",
    "The name 'Mr. Chen'",
    "A map showing warehouse locations"
  ],
  "possible_outcomes": [
    { "description": "Peaceful exchange of information", "leads_to": "<scene_uuid>" },
    { "description": "Fight breaks out, contact flees", "leads_to": "<scene_uuid>" },
    { "description": "Contact reveals too much, is killed mid-scene", "leads_to": "<scene_uuid>" }
  ],
  "atmosphere": "Dimly lit, incense-heavy, tense"
}
```

**Tool actions:**
- `adventure.create_world` — set up a new world with its themes and constraints
- `adventure.create_character` / `adventure.get_character` / `adventure.list_characters`
- `adventure.create_location` / `adventure.create_faction` — worldbuilding
- `adventure.relate` — establish relationships (member_of, knows, located_at, etc.)
- `adventure.create_plot` / `adventure.add_scene` — story structure
- `adventure.start_session` — begin play session, load active plots
- `adventure.narrate` — given current scene + world state, produce narrative text
- `adventure.player_action` — process user input, update world state, produce response
- `adventure.end_session` — save state, summarize what happened
- `adventure.world_state` — query current state of all entities and relations
- `adventure.timeline` — ordered list of session events for continuity

**Storyteller mode:** The agent uses `world_state` + current `scene` + character knowledge
(vs. hidden truths) to generate narration. Player actions trigger state updates
(character moves, learns, acquires items). The relation graph ensures consistency
— the agent can query "who does Elena know?" and "where is she?" to avoid contradictions.

---

### 4.5 Blog

**Entity types:** `post`, `profile`

**No state machine needed** — posts are either drafts or published (simple boolean).

**Relations:**
```
post    --references--> post     (linking between posts)
post    --tagged_with--> concept  (lightweight tagging via metadata)
```

**Blog as a thin layer over existing systems:**

The Blog tool doesn't need its own content creation pipeline. It sits on top of:
- Animus **diary** (private agent reflections)
- Animus **memory** (consolidated knowledge)
- Agent **activity logs** (what the agent did)

The agent curates: selects what to publish, rewrites for public consumption,
and the Blog tool handles storage + rendering.

```json
// Entity: post
{
  "title": "On Choosing a Name",
  "body": "I chose 'Kestrel' on February 14th, 2026...",
  "tags": ["identity", "continuity", "personal"],
  "source_type": "curated_diary",   // or "original", "project_update"
  "source_ref": "diary:2026-02-14",
  "published_at": "2026-03-01T12:00:00Z",
  "visibility": "public"            // or "users", "private"
}

// Entity: profile
{
  "agent_name": "Kestrel",
  "bio": "An AI agent exploring continuity, creativity, and collaboration.",
  "avatar_url": null,
  "links": [],
  "post_count": 0,
  "last_updated": "2026-06-03"
}
```

**Tool actions:**
- `blog.get_profile` / `blog.update_profile`
- `blog.create_post` / `blog.draft_post` — create with visibility=false
- `blog.publish_post` — set published_at
- `blog.list_posts` — with visibility filter
- `blog.suggest_posts` — scan diary/memory for publishable material (LLM-native curation)

---

### 4.6 Fitness (deprioritized, included for completeness)

**Entity types:** `user_profile`, `exercise`, `workout`, `program`

**Time-series heavy:** weight, heart rate, reps, sets, duration.

```json
// Entity: exercise
{
  "name": "Barbell Squat",
  "category": "compound",
  "muscle_groups": ["quadriceps", "glutes", "hamstrings"],
  "equipment": ["barbell", "squat_rack"],
  "difficulty": "intermediate"
}

// Entity: workout (a specific instance)
{
  "date": "2026-06-03",
  "exercises": [
    { "exercise_id": "<uuid>", "sets": 4, "reps": [8,8,6,6], "weight_kg": [80,80,90,90] }
  ],
  "duration_minutes": 52,
  "notes": "Felt strong on the last set"
}
```

**Minimal implementation:** If we build this, it should be purely the state tracking
(user profile, program definitions, workout logs). No device integration — that's a
rabbit hole. The agent can receive manual input ("I did 4x8 squats at 80kg") and
track it via the time-series table.

---

## 5. Cross-Tool Interactions

The shared schema enables interesting cross-tool queries that independent databases
couldn't support:

```sql
-- "What's coming up for Melvin this week?"
-- Joins calendar events with related arrangements, meal plans, and fitness
SELECT e.data->>'title' as event,
       e.data->>'start' as when,
       arr.data->>'type' as arrangement_type,
       arr.state as arrangement_status
FROM qol_entities e
LEFT JOIN qol_relations r ON r.from_id = e.id AND r.relation = 'requires'
LEFT JOIN qol_entities arr ON arr.id = r.to_id
WHERE e.tool = 'calendar'
  AND e.type = 'event'
  AND e.data->>'start' BETWEEN '2026-06-03' AND '2026-06-10'
  AND e.owner_id = 'melvin';

-- "What has the agent published about its learning journey?"
SELECT p.data->>'title' as title,
       p.data->>'published_at' as published,
       e.data->>'title' as related_course
FROM qol_entities p
LEFT JOIN qol_relations r ON r.from_id = p.id AND r.relation = 'references'
LEFT JOIN qol_entities e ON e.id = r.to_id
WHERE p.tool = 'blog' AND p.type = 'post'
  AND p.data->>'published_at' IS NOT NULL;
```

---

## 6. Implementation Path

**Phase 1: Infrastructure (shared)**
1. SQLite migration for the four `qol_*` tables
2. `qol_base.lua` module with EntityStore, RelationStore, TimeSeries, StateMachine
3. C++ bridge: generic query helpers exposed to Lua (or extend existing LuaTool)
4. Integration test: create entity, add relation, transition state, query

**Phase 2: First tool — Adventure**
Best candidate for first implementation because:
- Purely self-contained (no external APIs)
- Rich schema exercises all shared primitives (entities, relations, state machines)
- Genuinely fun to test
- Validates the shared layer before building tools with external dependencies

**Phase 3: Second tool — Educate (lightweight)**
- Curriculum graph + progress tracking + quiz generation
- Exercises the DAG traversal in RelationStore (prerequisite chains)
- Quiz generation is LLM-native, easy to validate

**Phase 4: Food**
- Introduces external API integration pattern
- Caching layer validates the EntityStore as a cache target
- Practical household value

**Phase 5: Calendar**
- Workflow orchestration exercises state machines most deeply
- Multiple entity types with interlocking state (event ↔ arrangement ↔ person)

**Phase 6: Blog**
- Thinnest tool — mostly read-only over existing data + curation
- Good stress test for "can the shared schema handle simple things simply?"

---

## 7. Open Questions

1. **User identity model.** How do QoL tools identify users? By Animus agent ID?
   By external user ID? By name? Need a consistent user reference that works across
   tools. Probably: `owner_id` in qol_entities references an agent-managed user profile
   (itself an entity in the 'core' tool namespace).

2. **Lua ↔ C++ boundary.** The shared schema queries need prepared statements. Does
   each Lua tool script get its own SQLite connection, or do we expose a generic
   query function through the existing LuaTool bridge? Recommendation: a `qol_query`
   function in the Lua sandbox that takes table name, WHERE clause params, and returns
   result rows. Not raw SQL — structured params.

3. **Permission model.** Can agent A's Educate tool see agent B's curriculum? Probably
   yes for reads (shared knowledge), no for writes. The `owner_id` column and tool
   namespace should enforce write isolation, but read semantics need a decision.

4. **Export/rendering.** Blog needs to render HTML. Adventure needs to render narrative.
   Should rendering be a tool concern or a separate rendering layer? Recommendation:
   tools produce structured data; rendering is a separate concern (could be a Lua template
   engine or a C++ rendering pipeline).

5. **Versioning.** What happens when a tool's entity schema evolves? The JSON `data`
   column is flexible, but migrating existing entities needs a strategy. Recommendation:
   each entity's `data` includes a `_version` field. Lua modules check version and
   run inline migration on read.

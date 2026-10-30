
#include "animus_kernel/SchemaHelpers.h"
#include "animus_kernel/OntologyStore.h"

#include "animus_kernel/IDataStore.h"
#include "animus_kernel/Log.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string_view>
#include <utility>

#include <json/json.h>

namespace animus::kernel::ontology {
namespace {

int64_t NowUnixMs() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

bool DidWriteRows(IDataStore* store) {
    if (!store) return false;
    return store->Changes() > 0;
}
bool DidWriteRows(IStatement* stmt) {
    if (!stmt) return false;
    return stmt->RowsAffected() > 0;
}

std::string JsonToString(const Json::Value& value) {
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, value);
}

int64_t MemoryStateToInt(memory::MemoryState value) {
    return static_cast<int64_t>(value);
}

memory::MemoryState IntToMemoryState(int64_t value) {
    switch (value) {
        case 1: return memory::MemoryState::Current;
        case 2: return memory::MemoryState::Deprecated;
        case 0:
        default:
            return memory::MemoryState::New;
    }
}

int64_t RootCategoryToInt(RootCategory value) {
    return static_cast<int64_t>(value);
}

RootCategory IntToRootCategory(int64_t value) {
    switch (value) {
        case 0: return RootCategory::Persons;
        case 1: return RootCategory::Concepts;
        case 2: return RootCategory::Procedures;
        case 3: return RootCategory::Events;
        case 4: return RootCategory::Locations;
        case 5: return RootCategory::Organizations;
        case 6: return RootCategory::Projects;
        default: return RootCategory::Concepts;
    }
}

int64_t PropertyTypeToInt(PropertyType value) {
    return static_cast<int64_t>(value);
}

PropertyType IntToPropertyType(int64_t value) {
    switch (value) {
        case 1: return PropertyType::Number;
        case 2: return PropertyType::Date;
        case 3: return PropertyType::Reference;
        case 4: return PropertyType::Url;
        case 0:
        default:
            return PropertyType::Text;
    }
}

std::string TrimSlashes(std::string input) {
    while (!input.empty() && input.front() == '/') input.erase(input.begin());
    while (!input.empty() && input.back() == '/') input.pop_back();
    return input;
}

std::vector<std::string> SplitPath(const std::string& relativePath) {
    std::vector<std::string> parts;
    std::string cleaned = TrimSlashes(relativePath);
    if (cleaned.empty()) return parts;

    std::stringstream ss(cleaned);
    std::string segment;
    while (std::getline(ss, segment, '/')) {
        if (segment.empty()) continue;
        parts.push_back(segment);
    }
    return parts;
}

OntologyEntity RowToEntity(IStatement* stmt) {
    OntologyEntity e;
    e.id = stmt->ColumnInt64(0);
    if (!stmt->IsColumnNull(1)) {
        e.parent_id = stmt->ColumnInt64(1);
    }
    e.root_category = IntToRootCategory(stmt->ColumnInt64(2));
    e.name = stmt->ColumnText(3);
    e.full_path = stmt->ColumnText(4);
    e.sort_order = static_cast<int32_t>(stmt->ColumnInt64(5));
    e.agent_id = stmt->ColumnText(6);
    e.created_at_unix_ms = stmt->ColumnInt64(7);
    e.updated_at_unix_ms = stmt->ColumnInt64(8);
    return e;
}

OntologyProperty RowToProperty(IStatement* stmt) {
    OntologyProperty p;
    p.id = stmt->ColumnInt64(0);
    p.entity_id = stmt->ColumnInt64(1);
    p.key = stmt->ColumnText(2);
    p.value = stmt->ColumnText(3);
    p.value_type = IntToPropertyType(stmt->ColumnInt64(4));
    p.memory_state = IntToMemoryState(stmt->ColumnInt64(5));
    if (!stmt->IsColumnNull(6)) {
        p.linked_observation_id = stmt->ColumnInt64(6);
    }
    p.created_at_unix_ms = stmt->ColumnInt64(7);
    p.updated_at_unix_ms = stmt->ColumnInt64(8);
    p.agent_id = stmt->ColumnText(9);
    return p;
}

Json::Value EntityToJson(const OntologyEntity& entity) {
    Json::Value out(Json::objectValue);
    out["id"] = static_cast<Json::Int64>(entity.id);
    if (entity.parent_id.has_value()) {
        out["parent_id"] = static_cast<Json::Int64>(*entity.parent_id);
    } else {
        out["parent_id"] = Json::nullValue;
    }
    out["root_category"] = static_cast<Json::Int>(RootCategoryToInt(entity.root_category));
    out["name"] = entity.name;
    out["full_path"] = entity.full_path;
    out["sort_order"] = entity.sort_order;
    out["agent_id"] = entity.agent_id;
    out["created_at_unix_ms"] = static_cast<Json::Int64>(entity.created_at_unix_ms);
    out["updated_at_unix_ms"] = static_cast<Json::Int64>(entity.updated_at_unix_ms);
    return out;
}

Json::Value PropertyToJson(const OntologyProperty& property) {
    Json::Value out(Json::objectValue);
    out["id"] = static_cast<Json::Int64>(property.id);
    out["entity_id"] = static_cast<Json::Int64>(property.entity_id);
    out["key"] = property.key;
    out["value"] = property.value;
    out["value_type"] = static_cast<Json::Int>(PropertyTypeToInt(property.value_type));
    out["memory_state"] = static_cast<Json::Int>(MemoryStateToInt(property.memory_state));
    out["agent_id"] = property.agent_id;
    if (property.linked_observation_id.has_value()) {
        out["linked_observation_id"] = static_cast<Json::Int64>(*property.linked_observation_id);
    } else {
        out["linked_observation_id"] = Json::nullValue;
    }
    out["created_at_unix_ms"] = static_cast<Json::Int64>(property.created_at_unix_ms);
    out["updated_at_unix_ms"] = static_cast<Json::Int64>(property.updated_at_unix_ms);
    return out;
}

} // namespace

OntologyStore::OntologyStore(IDataStore* dataStore) : m_store(dataStore) {
    EnsureSchema();
}

OntologyStore::~OntologyStore() = default;

void OntologyStore::EnsureSchema() {
    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS ontology_entities (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            parent_id INTEGER REFERENCES ontology_entities(id) ON DELETE SET NULL,
            root_category INTEGER NOT NULL,
            name TEXT NOT NULL,
            full_path TEXT NOT NULL,
            sort_order INTEGER NOT NULL DEFAULT 0,
            agent_id TEXT NOT NULL DEFAULT 'default',
            created_at_unix_ms INTEGER NOT NULL,
            updated_at_unix_ms INTEGER NOT NULL
        );
    )");

    // #215: per-agent worldviews - path namespace scoped by owning agent.
    // Dropping the legacy agent-blind index is safe: it guaranteed global
    // path uniqueness, so (agent_id, root_category, full_path) is strictly
    // more unique - no collision resolution needed on existing installs.
    m_store->Exec(
        "DROP INDEX IF EXISTS idx_ontology_entities_category_path");
    m_store->Exec(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_ontology_entities_agent_category_path "
        "ON ontology_entities(agent_id, root_category, full_path)");
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_ontology_entities_parent "
        "ON ontology_entities(parent_id)");
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_ontology_entities_agent "
        "ON ontology_entities(agent_id)");

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS ontology_properties (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            entity_id INTEGER NOT NULL REFERENCES ontology_entities(id) ON DELETE CASCADE,
            key TEXT NOT NULL,
            value TEXT NOT NULL DEFAULT '',
            value_type INTEGER NOT NULL DEFAULT 0,
            memory_state INTEGER NOT NULL DEFAULT 0,
            agent_id TEXT NOT NULL DEFAULT 'default',
            linked_observation_id INTEGER REFERENCES observations(id) ON DELETE SET NULL,
            created_at_unix_ms INTEGER NOT NULL,
            updated_at_unix_ms INTEGER NOT NULL
        );
    )");

    m_store->Exec(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_ontology_properties_entity_key "
        "ON ontology_properties(entity_id, key)");
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_ontology_properties_observation "
        "ON ontology_properties(linked_observation_id)");
    m_store->Exec(
        "CREATE INDEX IF NOT EXISTS idx_ontology_properties_agent "
        "ON ontology_properties(agent_id)");

    // Migration: add agent_id columns to existing tables
    if (m_store->Dialect() == DataStoreDialect::SQLite) {
        if (!schema::ColumnExists(m_store, "ontology_entities", "agent_id")) {
            m_store->Exec("ALTER TABLE ontology_entities ADD COLUMN agent_id TEXT NOT NULL DEFAULT 'default'");
            m_store->Exec("CREATE INDEX IF NOT EXISTS idx_ontology_entities_agent ON ontology_entities(agent_id)");
        }
    }
    if (m_store->Dialect() == DataStoreDialect::SQLite) {
        if (!schema::ColumnExists(m_store, "ontology_properties", "agent_id")) {
            m_store->Exec("ALTER TABLE ontology_properties ADD COLUMN agent_id TEXT NOT NULL DEFAULT 'default'");
            m_store->Exec("CREATE INDEX IF NOT EXISTS idx_ontology_properties_agent ON ontology_properties(agent_id)");
        }
    }

    schema::CreateTable(m_store, R"(
        CREATE TABLE IF NOT EXISTS ontology_mutations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            mutation_type TEXT NOT NULL,
            target_type TEXT NOT NULL,
            target_id INTEGER NOT NULL,
            previous_state TEXT NOT NULL DEFAULT '',
            new_state TEXT NOT NULL DEFAULT '',
            motivation TEXT NOT NULL DEFAULT '',
            unix_ms INTEGER NOT NULL
        );
    )");

    // Triggers use SQLite-specific functions (julianday) — skip on PostgreSQL
    if (m_store->Dialect() == DataStoreDialect::SQLite) {
        // Sync property state when observations are re-scored.
        m_store->Exec(R"(
            CREATE TRIGGER IF NOT EXISTS trg_ontology_sync_property_state
            AFTER UPDATE OF memory_state ON observations
            BEGIN
                UPDATE ontology_properties
                SET memory_state = NEW.memory_state,
                    updated_at_unix_ms = CAST((julianday('now') - 2440587.5) * 86400000 AS INTEGER)
                WHERE linked_observation_id = NEW.id;
            END;
        )");

        // Preserve property but orphan it when evidence is deleted.
        // #189: this must run BEFORE the delete. The FK action on
        // linked_observation_id (ON DELETE SET NULL, foreign_keys=ON)
        // clears the link as part of the delete itself, so an AFTER
        // DELETE trigger's WHERE linked_observation_id = OLD.id never
        // matched — the link was already NULL. BEFORE fires while the
        // link still resolves; nulling it here also makes the FK action
        // a no-op. The unconditional DROP migrates existing installs off
        // the defeated AFTER version (IF NOT EXISTS would keep it).
        m_store->Exec(
            "DROP TRIGGER IF EXISTS trg_ontology_orphan_property_on_observation_delete");
        m_store->Exec(R"(
            CREATE TRIGGER trg_ontology_orphan_property_on_observation_delete
            BEFORE DELETE ON observations
            BEGIN
                UPDATE ontology_properties
                SET linked_observation_id = NULL,
                    memory_state = 2,
                    updated_at_unix_ms = CAST((julianday('now') - 2440587.5) * 86400000 AS INTEGER)
                WHERE linked_observation_id = OLD.id;
            END;
        )");
    }
}

std::vector<OntologyEntity> OntologyStore::ListEntities(
        std::optional<RootCategory> category,
        const std::string& agent_id) {
    std::vector<OntologyEntity> out;
    std::unique_ptr<IStatement> stmt;
    if (category.has_value() && !agent_id.empty()) {
        stmt = m_store->Prepare(
            "SELECT id, parent_id, root_category, name, full_path, sort_order, agent_id, "
            "created_at_unix_ms, updated_at_unix_ms "
            "FROM ontology_entities WHERE root_category=? AND agent_id=? "
            "ORDER BY full_path ASC");
        if (!stmt) return out;
        stmt->BindInt64(1, RootCategoryToInt(*category));
        stmt->BindText(2, agent_id);
    } else if (category.has_value()) {
        stmt = m_store->Prepare(
            "SELECT id, parent_id, root_category, name, full_path, sort_order, agent_id, "
            "created_at_unix_ms, updated_at_unix_ms "
            "FROM ontology_entities WHERE root_category=? "
            "ORDER BY full_path ASC");
        if (!stmt) return out;
        stmt->BindInt64(1, RootCategoryToInt(*category));
    } else if (!agent_id.empty()) {
        stmt = m_store->Prepare(
            "SELECT id, parent_id, root_category, name, full_path, sort_order, agent_id, "
            "created_at_unix_ms, updated_at_unix_ms "
            "FROM ontology_entities WHERE agent_id=? "
            "ORDER BY root_category ASC, full_path ASC");
        if (!stmt) return out;
        stmt->BindText(1, agent_id);
    } else {
        stmt = m_store->Prepare(
            "SELECT id, parent_id, root_category, name, full_path, sort_order, agent_id, "
            "created_at_unix_ms, updated_at_unix_ms "
            "FROM ontology_entities ORDER BY root_category ASC, full_path ASC");
        if (!stmt) return out;
    }

    while (stmt->Step()) {
        out.push_back(RowToEntity(stmt.get()));
    }
    return out;
}

std::vector<OntologyEntity> OntologyStore::ListChildren(
        std::optional<int64_t> parent_id,
        const std::string& agent_id) {
    std::vector<OntologyEntity> out;
    std::unique_ptr<IStatement> stmt;
    if (parent_id.has_value() && !agent_id.empty()) {
        stmt = m_store->Prepare(
            "SELECT id, parent_id, root_category, name, full_path, sort_order, agent_id, "
            "created_at_unix_ms, updated_at_unix_ms "
            "FROM ontology_entities WHERE parent_id=? AND agent_id=? ORDER BY sort_order ASC, name ASC");
        if (!stmt) return out;
        stmt->BindInt64(1, *parent_id);
        stmt->BindText(2, agent_id);
    } else if (parent_id.has_value()) {
        stmt = m_store->Prepare(
            "SELECT id, parent_id, root_category, name, full_path, sort_order, agent_id, "
            "created_at_unix_ms, updated_at_unix_ms "
            "FROM ontology_entities WHERE parent_id=? ORDER BY sort_order ASC, name ASC");
        if (!stmt) return out;
        stmt->BindInt64(1, *parent_id);
    } else if (!agent_id.empty()) {
        stmt = m_store->Prepare(
            "SELECT id, parent_id, root_category, name, full_path, sort_order, agent_id, "
            "created_at_unix_ms, updated_at_unix_ms "
            "FROM ontology_entities WHERE parent_id IS NULL AND agent_id=? "
            "ORDER BY root_category ASC, sort_order ASC, name ASC");
        if (!stmt) return out;
        stmt->BindText(1, agent_id);
    } else {
        stmt = m_store->Prepare(
            "SELECT id, parent_id, root_category, name, full_path, sort_order, agent_id, "
            "created_at_unix_ms, updated_at_unix_ms "
            "FROM ontology_entities WHERE parent_id IS NULL "
            "ORDER BY root_category ASC, sort_order ASC, name ASC");
        if (!stmt) return out;
    }

    while (stmt->Step()) {
        out.push_back(RowToEntity(stmt.get()));
    }
    return out;
}

std::optional<OntologyEntity> OntologyStore::GetEntity(int64_t id) {
    auto stmt = m_store->Prepare(
        "SELECT id, parent_id, root_category, name, full_path, sort_order, agent_id, "
        "created_at_unix_ms, updated_at_unix_ms "
        "FROM ontology_entities WHERE id=?");
    if (!stmt) return std::nullopt;
    stmt->BindInt64(1, id);
    if (!stmt->Step()) return std::nullopt;
    return RowToEntity(stmt.get());
}

std::optional<OntologyEntity> OntologyStore::FindByPath(
        RootCategory category, const std::string& full_path,
        const std::string& agent_id) {
    // #215: empty agent_id = unscoped lookup (admin/migration paths).
    const char* sql = agent_id.empty()
        ? "SELECT id, parent_id, root_category, name, full_path, sort_order, agent_id, "
          "created_at_unix_ms, updated_at_unix_ms "
          "FROM ontology_entities WHERE root_category=? AND full_path=?"
        : "SELECT id, parent_id, root_category, name, full_path, sort_order, agent_id, "
          "created_at_unix_ms, updated_at_unix_ms "
          "FROM ontology_entities WHERE root_category=? AND full_path=? AND agent_id=?";
    auto stmt = m_store->Prepare(sql);
    if (!stmt) return std::nullopt;
    stmt->BindInt64(1, RootCategoryToInt(category));
    stmt->BindText(2, full_path);
    if (!agent_id.empty()) stmt->BindText(3, agent_id);
    if (!stmt->Step()) return std::nullopt;
    return RowToEntity(stmt.get());
}

OntologyEntity OntologyStore::CreateEntity(
        const OntologyEntity& entity, const std::string& motivation) {
    auto now = NowUnixMs();
    auto stmt = m_store->Prepare(
        "INSERT INTO ontology_entities "
        "(parent_id, root_category, name, full_path, sort_order, agent_id, created_at_unix_ms, updated_at_unix_ms) "
        "VALUES (?,?,?,?,?,?,?,?) RETURNING id");
    if (!stmt) return {};

    if (entity.parent_id.has_value()) stmt->BindInt64(1, *entity.parent_id);
    else stmt->BindNull(1);
    stmt->BindInt64(2, RootCategoryToInt(entity.root_category));
    stmt->BindText(3, entity.name);
    stmt->BindText(4, entity.full_path);
    stmt->BindInt(5, entity.sort_order);
    stmt->BindText(6, entity.agent_id);
    stmt->BindInt64(7, now);
    stmt->BindInt64(8, now);
    // #76: statement-scoped id via RETURNING. Failure (no row returned)
    // includes the unique-path conflict — resolve the existing entity.
    if (!stmt->Step()) {
        auto existing = FindByPath(entity.root_category, entity.full_path, entity.agent_id);
        return existing.value_or(OntologyEntity{});
    }

    auto created = GetEntity(stmt->ColumnInt64(0)).value_or(OntologyEntity{});
    if (created.id > 0) {
        Json::Value snapshot = EntityToJson(created);
        OntologyMutation m;
        m.mutation_type = "create_entity";
        m.target_type = "entity";
        m.target_id = created.id;
        m.previous_state = "{}";
        m.new_state = JsonToString(snapshot);
        m.motivation = motivation;
        m.unix_ms = now;
        LogMutation(m);
    }
    return created;
}

bool OntologyStore::UpdateEntity(const OntologyEntity& entity, const std::string& motivation) {
    auto existing = GetEntity(entity.id);
    if (!existing) return false;

    auto now = NowUnixMs();
    auto stmt = m_store->Prepare(
        "UPDATE ontology_entities "
        "SET parent_id=?, root_category=?, name=?, full_path=?, sort_order=?, updated_at_unix_ms=? "
        "WHERE id=?");
    if (!stmt) return false;
    if (entity.parent_id.has_value()) stmt->BindInt64(1, *entity.parent_id);
    else stmt->BindNull(1);
    stmt->BindInt64(2, RootCategoryToInt(entity.root_category));
    stmt->BindText(3, entity.name);
    stmt->BindText(4, entity.full_path);
    stmt->BindInt(5, entity.sort_order);
    stmt->BindInt64(6, now);
    stmt->BindInt64(7, entity.id);
    stmt->ExecDML();
    if (!DidWriteRows(stmt.get())) return false;

    auto updated = GetEntity(entity.id).value_or(entity);
    OntologyMutation m;
    m.mutation_type = "update_entity";
    m.target_type = "entity";
    m.target_id = entity.id;
    m.previous_state = JsonToString(EntityToJson(*existing));
    m.new_state = JsonToString(EntityToJson(updated));
    m.motivation = motivation;
    m.unix_ms = now;
    LogMutation(m);
    return true;
}

bool OntologyStore::MoveEntity(
        int64_t id, std::optional<int64_t> new_parent_id, const std::string& motivation) {
    auto existing = GetEntity(id);
    if (!existing) return false;

    std::string newPrefix;
    RootCategory category = existing->root_category;
    if (new_parent_id.has_value()) {
        auto parent = GetEntity(*new_parent_id);
        if (!parent) return false;
        category = parent->root_category;
        newPrefix = parent->full_path;
    } else {
        newPrefix = RootCategoryToString(existing->root_category);
    }

    OntologyEntity updated = *existing;
    updated.parent_id = new_parent_id;
    updated.root_category = category;
    if (newPrefix.empty()) {
        updated.full_path = updated.name;
    } else {
        updated.full_path = newPrefix + "/" + updated.name;
    }

    if (!UpdateEntity(updated, motivation)) return false;
    RefreshDescendantPaths(updated);
    return true;
}

bool OntologyStore::DeleteEntity(int64_t id, const std::string& motivation) {
    auto existing = GetEntity(id);
    if (!existing) return false;

    auto now = NowUnixMs();
    auto stmt = m_store->Prepare("DELETE FROM ontology_entities WHERE id=?");
    if (!stmt) return false;
    stmt->BindInt64(1, id);
    stmt->ExecDML();
    if (!DidWriteRows(stmt.get())) return false;

    OntologyMutation m;
    m.mutation_type = "delete_entity";
    m.target_type = "entity";
    m.target_id = id;
    m.previous_state = JsonToString(EntityToJson(*existing));
    m.new_state = "{}";
    m.motivation = motivation;
    m.unix_ms = now;
    LogMutation(m);
    return true;
}

std::vector<OntologyProperty> OntologyStore::ListProperties(int64_t entity_id) {
    std::vector<OntologyProperty> out;
    auto stmt = m_store->Prepare(
        "SELECT id, entity_id, key, value, value_type, memory_state, linked_observation_id, "
        "created_at_unix_ms, updated_at_unix_ms, agent_id "
        "FROM ontology_properties WHERE entity_id=? ORDER BY key ASC");
    if (!stmt) return out;
    stmt->BindInt64(1, entity_id);
    while (stmt->Step()) {
        out.push_back(RowToProperty(stmt.get()));
    }
    return out;
}

std::optional<OntologyProperty> OntologyStore::GetProperty(
        int64_t entity_id, const std::string& key) {
    auto stmt = m_store->Prepare(
        "SELECT id, entity_id, key, value, value_type, memory_state, linked_observation_id, "
        "created_at_unix_ms, updated_at_unix_ms, agent_id "
        "FROM ontology_properties WHERE entity_id=? AND key=?");
    if (!stmt) return std::nullopt;
    stmt->BindInt64(1, entity_id);
    stmt->BindText(2, key);
    if (!stmt->Step()) return std::nullopt;
    return RowToProperty(stmt.get());
}

OntologyProperty OntologyStore::SetProperty(const OntologyProperty& prop, const std::string& motivation) {
    if (prop.entity_id <= 0 || prop.key.empty()) return {};

    auto now = NowUnixMs();
    auto existing = GetProperty(prop.entity_id, prop.key);

    memory::MemoryState effectiveState = prop.memory_state;
    if (prop.linked_observation_id.has_value()) {
        auto obsStmt = m_store->Prepare("SELECT memory_state FROM observations WHERE id=?");
        if (obsStmt) {
            obsStmt->BindInt64(1, *prop.linked_observation_id);
            if (obsStmt->Step()) {
                effectiveState = IntToMemoryState(obsStmt->ColumnInt64(0));
            } else {
                effectiveState = memory::MemoryState::Deprecated;
            }
        }
    }

    bool success = false;
    if (existing.has_value()) {
        auto stmt = m_store->Prepare(
            "UPDATE ontology_properties "
            "SET value=?, value_type=?, memory_state=?, linked_observation_id=?, updated_at_unix_ms=? "
            "WHERE id=?");
        if (!stmt) return {};
        stmt->BindText(1, prop.value);
        stmt->BindInt64(2, PropertyTypeToInt(prop.value_type));
        stmt->BindInt64(3, MemoryStateToInt(effectiveState));
        if (prop.linked_observation_id.has_value()) stmt->BindInt64(4, *prop.linked_observation_id);
        else stmt->BindNull(4);
        stmt->BindInt64(5, now);
        stmt->BindInt64(6, existing->id);
        stmt->ExecDML();
        success = DidWriteRows(stmt.get());
    } else {
        auto stmt = m_store->Prepare(
            "INSERT INTO ontology_properties "
            "(entity_id, key, value, value_type, memory_state, linked_observation_id, "
            "agent_id, created_at_unix_ms, updated_at_unix_ms) "
            "VALUES (?,?,?,?,?,?,?,?,?)");
        if (!stmt) return {};
        stmt->BindInt64(1, prop.entity_id);
        stmt->BindText(2, prop.key);
        stmt->BindText(3, prop.value);
        stmt->BindInt64(4, PropertyTypeToInt(prop.value_type));
        stmt->BindInt64(5, MemoryStateToInt(effectiveState));
        if (prop.linked_observation_id.has_value()) stmt->BindInt64(6, *prop.linked_observation_id);
        else stmt->BindNull(6);
        stmt->BindText(7, prop.agent_id.empty() ? std::string("default") : prop.agent_id);
        stmt->BindInt64(8, now);
        stmt->BindInt64(9, now);
        stmt->ExecDML();
        success = DidWriteRows(stmt.get());
    }

    if (!success) return {};

    auto updated = GetProperty(prop.entity_id, prop.key).value_or(OntologyProperty{});
    if (updated.id > 0) {
        OntologyMutation m;
        m.mutation_type = existing.has_value() ? "set_property" : "create_property";
        m.target_type = "property";
        m.target_id = updated.id;
        m.previous_state = existing.has_value() ? JsonToString(PropertyToJson(*existing)) : "{}";
        m.new_state = JsonToString(PropertyToJson(updated));
        m.motivation = motivation;
        m.unix_ms = now;
        LogMutation(m);
    }
    return updated;
}

bool OntologyStore::DeleteProperty(int64_t id, const std::string& motivation) {
    auto stmtGet = m_store->Prepare(
        "SELECT id, entity_id, key, value, value_type, memory_state, linked_observation_id, "
        "created_at_unix_ms, updated_at_unix_ms, agent_id "
        "FROM ontology_properties WHERE id=?");
    if (!stmtGet) return false;
    stmtGet->BindInt64(1, id);
    if (!stmtGet->Step()) return false;
    OntologyProperty existing = RowToProperty(stmtGet.get());

    auto now = NowUnixMs();
    auto stmt = m_store->Prepare("DELETE FROM ontology_properties WHERE id=?");
    if (!stmt) return false;
    stmt->BindInt64(1, id);
    stmt->ExecDML();
    if (!DidWriteRows(stmt.get())) return false;

    OntologyMutation m;
    m.mutation_type = "delete_property";
    m.target_type = "property";
    m.target_id = id;
    m.previous_state = JsonToString(PropertyToJson(existing));
    m.new_state = "{}";
    m.motivation = motivation;
    m.unix_ms = now;
    LogMutation(m);
    return true;
}

// #219: the no-evidence exception path. Properties with no linked
// observation have no evidence to derive state from — but post-#215
// writers stamp the owning agent, and only pre-#215 writes landed under
// the 'default' sentinel (see #214/#215 history). Promotion is therefore
// scoped to legacy/'default'-sentinel rows: agent-stamped unlinked rows
// stay New until the reconciler links them. Each promotion writes its own
// audit row — the old bulk UPDATE promoted silently, which #219 exists to
// fix. Returns rows affected.
int64_t OntologyStore::PromoteUnlinkedNewProperties() {
    auto selectStmt = m_store->Prepare(
        "SELECT id, entity_id, key, value, value_type, memory_state, linked_observation_id, "
        "created_at_unix_ms, updated_at_unix_ms, agent_id "
        "FROM ontology_properties "
        "WHERE linked_observation_id IS NULL AND memory_state=? AND agent_id='default'");
    if (!selectStmt) return 0;
    selectStmt->BindInt64(1, MemoryStateToInt(memory::MemoryState::New));
    std::vector<OntologyProperty> rows;
    while (selectStmt->Step()) {
        rows.push_back(RowToProperty(selectStmt.get()));
    }
    if (rows.empty()) return 0;

    const int64_t now = NowUnixMs();
    int64_t promoted = 0;
    for (const auto& row : rows) {
        auto updateStmt = m_store->Prepare(
            "UPDATE ontology_properties SET memory_state=?, updated_at_unix_ms=? WHERE id=?");
        if (!updateStmt) continue;
        updateStmt->BindInt64(1, MemoryStateToInt(memory::MemoryState::Current));
        updateStmt->BindInt64(2, now);
        updateStmt->BindInt64(3, row.id);
        updateStmt->ExecDML();
        if (updateStmt->RowsAffected() > 0) {
            OntologyProperty after = row;
            after.memory_state = memory::MemoryState::Current;
            after.updated_at_unix_ms = now;
            LogStateTransition(row, after,
                "boot sync: promoted unlinked New->Current (legacy/no-evidence path)", now);
            ++promoted;
        }
    }
    return promoted;
}

void OntologyStore::LogStateTransition(const OntologyProperty& before,
                                       const OntologyProperty& after,
                                       const std::string& motivation,
                                       int64_t unixMs) {
    // #219: state transitions that bypass SetProperty (boot sync promotion,
    // evidence sync) still get an ontology_mutations row, so memory_state
    // transitions are auditable on every path — the orphan trigger writes
    // none, and the pre-#219 boot pass promoted silently. Full property
    // JSON before/after, same shape SetProperty writes.
    if (before.id <= 0) return;
    OntologyMutation m;
    m.mutation_type = "state_transition";
    m.target_type = "property";
    m.target_id = before.id;
    m.previous_state = JsonToString(PropertyToJson(before));
    m.new_state = JsonToString(PropertyToJson(after));
    m.motivation = motivation;
    m.unix_ms = (unixMs != 0) ? unixMs : NowUnixMs();
    LogMutation(m);
}

void OntologyStore::SyncPropertyStatesFromObservations(memory::MemoryStore* memoryStore) {
    if (!memoryStore) return;
    {
        // #219: properties without a linked observation are the no-evidence
        // exception path, not the default escalator. The reconciler now wires
        // linked_observation_id on every property it promotes (production
        // writer), so linked rows derive their state from evidence elsewhere
        // in this function. This blanket pass is scoped to legacy/adopted
        // rows: pre-#215 writes carried no agent identity and landed under
        // the 'default' sentinel (see #214/#215 history). Post-#215 writers
        // (ConsolidationTool::HandleOntologyUpsert, the pipeline reconciler)
        // stamp the owning agent, so their rows are never touched here —
        // a New property with real evidence pending reconciliation stays New
        // until the reconciler links it.
        // Promote New (0) to Current (1). Don't touch Deprecated (2).
        const int64_t promoted = PromoteUnlinkedNewProperties();
        if (promoted > 0) {
            ALOG_INFO("ontology", "boot sync: promoted " << promoted
                     << " unlinked New->Current ontology_properties (legacy/no-evidence path)");
        }
    }

    auto stmt = m_store->Prepare(
        "SELECT id, entity_id, key, value, value_type, memory_state, linked_observation_id, "
        "created_at_unix_ms, updated_at_unix_ms, agent_id "
        "FROM ontology_properties WHERE linked_observation_id IS NOT NULL");
    if (!stmt) return;

    const int64_t now = NowUnixMs();
    while (stmt->Step()) {
        const OntologyProperty before = RowToProperty(stmt.get());
        auto obs = memoryStore->GetObservation(*before.linked_observation_id);
        if (!obs.has_value()) {
            auto orphanStmt = m_store->Prepare(
                "UPDATE ontology_properties "
                "SET linked_observation_id=NULL, memory_state=?, updated_at_unix_ms=? "
                "WHERE id=?");
            if (!orphanStmt) continue;
            orphanStmt->BindInt64(1, MemoryStateToInt(memory::MemoryState::Deprecated));
            orphanStmt->BindInt64(2, now);
            orphanStmt->BindInt64(3, before.id);
            orphanStmt->ExecDML();
            if (orphanStmt->RowsAffected() > 0) {
                // #219: the rescue-sync orphaning path is auditable too — the
                // SQLite trigger handles delete-time orphaning; this sweep
                // catches links that went stale some other way.
                OntologyProperty after = before;
                after.linked_observation_id.reset();
                after.memory_state = memory::MemoryState::Deprecated;
                after.updated_at_unix_ms = now;
                LogStateTransition(before, after,
                    "boot sync: linked observation missing — evidence orphaned", now);
            }
            continue;
        }

        if (obs->memory_state == before.memory_state) {
            continue;  // #219: no transition, no write, no log row
        }
        auto syncStmt = m_store->Prepare(
            "UPDATE ontology_properties SET memory_state=?, updated_at_unix_ms=? WHERE id=?");
        if (!syncStmt) continue;
        syncStmt->BindInt64(1, MemoryStateToInt(obs->memory_state));
        syncStmt->BindInt64(2, now);
        syncStmt->BindInt64(3, before.id);
        syncStmt->ExecDML();
        if (syncStmt->RowsAffected() > 0) {
            OntologyProperty after = before;
            after.memory_state = obs->memory_state;
            after.updated_at_unix_ms = now;
            LogStateTransition(before, after,
                "boot sync: property state synced from linked observation", now);
        }
    }
}

void OntologyStore::LogMutation(const OntologyMutation& m) {
    auto stmt = m_store->Prepare(
        "INSERT INTO ontology_mutations "
        "(mutation_type, target_type, target_id, previous_state, new_state, motivation, unix_ms) "
        "VALUES (?,?,?,?,?,?,?)");
    if (!stmt) return;
    stmt->BindText(1, m.mutation_type);
    stmt->BindText(2, m.target_type);
    stmt->BindInt64(3, m.target_id);
    stmt->BindText(4, m.previous_state);
    stmt->BindText(5, m.new_state);
    stmt->BindText(6, m.motivation);
    stmt->BindInt64(7, m.unix_ms);
    stmt->ExecDML();
}

std::vector<OntologyMutation> OntologyStore::QueryMutations(int64_t since_unix_ms, int64_t limit) {
    std::vector<OntologyMutation> out;
    auto stmt = m_store->Prepare(
        "SELECT id, mutation_type, target_type, target_id, previous_state, new_state, motivation, unix_ms "
        "FROM ontology_mutations WHERE unix_ms > ? ORDER BY unix_ms DESC LIMIT ?");
    if (!stmt) return out;
    stmt->BindInt64(1, since_unix_ms);
    stmt->BindInt64(2, limit);
    while (stmt->Step()) {
        OntologyMutation m;
        m.id = stmt->ColumnInt64(0);
        m.mutation_type = stmt->ColumnText(1);
        m.target_type = stmt->ColumnText(2);
        m.target_id = stmt->ColumnInt64(3);
        m.previous_state = stmt->ColumnText(4);
        m.new_state = stmt->ColumnText(5);
        m.motivation = stmt->ColumnText(6);
        m.unix_ms = stmt->ColumnInt64(7);
        out.push_back(std::move(m));
    }
    return out;
}

std::optional<OntologyEntity> OntologyStore::EnsureEntityPath(
        RootCategory category, const std::string& relative_path, const std::string& motivation, const std::string& agent_id) {
    const std::vector<std::string> parts = SplitPath(relative_path);
    if (parts.empty()) return std::nullopt;

    std::optional<int64_t> parentId = std::nullopt;
    std::string prefix = RootCategoryToString(category);
    OntologyEntity last;

    for (const auto& segment : parts) {
        std::string fullPath = prefix.empty() ? segment : (prefix + "/" + segment);
        auto existing = FindByPath(category, fullPath, agent_id);
        if (existing.has_value()) {
            last = *existing;
            parentId = last.id;
            prefix = last.full_path;
            continue;
        }

        OntologyEntity createdInput;
        createdInput.parent_id = parentId;
        createdInput.root_category = category;
        createdInput.name = segment;
        createdInput.full_path = fullPath;
        createdInput.sort_order = 0;
        if (!agent_id.empty()) createdInput.agent_id = agent_id;
        auto created = CreateEntity(createdInput, motivation);
        if (created.id <= 0) return std::nullopt;
        last = created;
        parentId = created.id;
        prefix = created.full_path;
    }

    if (last.id <= 0) return std::nullopt;
    return last;
}

void OntologyStore::RefreshDescendantPaths(const OntologyEntity& parent) {
    auto children = ListChildren(parent.id);
    for (auto& child : children) {
        const std::string desiredPath = parent.full_path + "/" + child.name;
        if (child.full_path != desiredPath || child.root_category != parent.root_category) {
            child.full_path = desiredPath;
            child.root_category = parent.root_category;
            UpdateEntity(child, "cascade path update");
        }
        RefreshDescendantPaths(child);
    }
}

std::optional<RootCategory> OntologyStore::RootCategoryFromString(const std::string& value) {
    if (value == "persons") return RootCategory::Persons;
    if (value == "concepts") return RootCategory::Concepts;
    if (value == "procedures") return RootCategory::Procedures;
    if (value == "events") return RootCategory::Events;
    if (value == "locations") return RootCategory::Locations;
    if (value == "organizations") return RootCategory::Organizations;
    if (value == "projects") return RootCategory::Projects;
    return std::nullopt;
}

std::string OntologyStore::RootCategoryToString(RootCategory value) {
    switch (value) {
        case RootCategory::Persons: return "persons";
        case RootCategory::Concepts: return "concepts";
        case RootCategory::Procedures: return "procedures";
        case RootCategory::Events: return "events";
        case RootCategory::Locations: return "locations";
        case RootCategory::Organizations: return "organizations";
        case RootCategory::Projects: return "projects";
        default: return "concepts";
    }
}

std::optional<PropertyType> OntologyStore::PropertyTypeFromString(const std::string& value) {
    if (value == "text" || value == "string") return PropertyType::Text;
    if (value == "number") return PropertyType::Number;
    if (value == "date") return PropertyType::Date;
    if (value == "reference") return PropertyType::Reference;
    if (value == "url") return PropertyType::Url;
    return std::nullopt;
}

std::string OntologyStore::PropertyTypeToString(PropertyType value) {
    switch (value) {
        case PropertyType::Text: return "text";
        case PropertyType::Number: return "number";
        case PropertyType::Date: return "date";
        case PropertyType::Reference: return "reference";
        case PropertyType::Url: return "url";
        default: return "text";
    }
}

std::string OntologyStore::MemoryStateToString(memory::MemoryState value) {
    switch (value) {
        case memory::MemoryState::Current: return "current";
        case memory::MemoryState::Deprecated: return "deprecated";
        case memory::MemoryState::New:
        default:
            return "new";
    }
}

} // namespace animus::kernel::ontology

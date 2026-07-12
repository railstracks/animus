#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "animus_kernel/MemoryStore.h"

namespace animus::kernel {

class IDataStore;

namespace ontology {

enum class RootCategory : int32_t {
    Persons = 0,
    Concepts = 1,
    Procedures = 2,
    Events = 3,
    Locations = 4,
    Organizations = 5,
    Projects = 6
};

enum class PropertyType : int32_t {
    Text = 0,
    Number = 1,
    Date = 2,
    Reference = 3,
    Url = 4
};

struct OntologyEntity {
    int64_t id{0};
    std::optional<int64_t> parent_id;
    RootCategory root_category{RootCategory::Concepts};
    std::string name;
    std::string full_path;
    int32_t sort_order{0};
    std::string agent_id{"default"};
    int64_t created_at_unix_ms{0};
    int64_t updated_at_unix_ms{0};
};

struct OntologyProperty {
    int64_t id{0};
    int64_t entity_id{0};
    std::string key;
    std::string value;
    PropertyType value_type{PropertyType::Text};
    memory::MemoryState memory_state{memory::MemoryState::New};
    std::optional<int64_t> linked_observation_id;
    int64_t created_at_unix_ms{0};
    int64_t updated_at_unix_ms{0};
};

struct OntologyMutation {
    int64_t id{0};
    std::string mutation_type;
    std::string target_type;
    int64_t target_id{0};
    std::string previous_state;
    std::string new_state;
    std::string motivation;
    int64_t unix_ms{0};
};

class OntologyStore {
public:
    explicit OntologyStore(IDataStore* dataStore);
    ~OntologyStore();

    OntologyStore(const OntologyStore&) = delete;
    OntologyStore& operator=(const OntologyStore&) = delete;

    std::vector<OntologyEntity> ListEntities(
        std::optional<RootCategory> category = std::nullopt,
        const std::string& agent_id = "");
    std::vector<OntologyEntity> ListChildren(
        std::optional<int64_t> parent_id,
        const std::string& agent_id = "");
    std::optional<OntologyEntity> GetEntity(int64_t id);
    std::optional<OntologyEntity> FindByPath(
        RootCategory category, const std::string& full_path);
    OntologyEntity CreateEntity(const OntologyEntity& entity,
                                const std::string& motivation = "");
    bool UpdateEntity(const OntologyEntity& entity,
                      const std::string& motivation = "");
    bool MoveEntity(int64_t id, std::optional<int64_t> new_parent_id,
                    const std::string& motivation);
    bool DeleteEntity(int64_t id, const std::string& motivation = "");

    std::vector<OntologyProperty> ListProperties(int64_t entity_id);
    std::optional<OntologyProperty> GetProperty(
        int64_t entity_id, const std::string& key);
    OntologyProperty SetProperty(const OntologyProperty& prop,
                                 const std::string& motivation = "");
    bool DeleteProperty(int64_t id, const std::string& motivation = "");

    void SyncPropertyStatesFromObservations(memory::MemoryStore* memoryStore);

    void LogMutation(const OntologyMutation& m);
    std::vector<OntologyMutation> QueryMutations(int64_t since_unix_ms = 0,
                                                 int64_t limit = 100);

    std::optional<OntologyEntity> EnsureEntityPath(
        RootCategory category,
        const std::string& relative_path,
        const std::string& motivation = "",
        const std::string& agent_id = "");

    static std::optional<RootCategory> RootCategoryFromString(const std::string& value);
    static std::string RootCategoryToString(RootCategory value);
    static std::optional<PropertyType> PropertyTypeFromString(const std::string& value);
    static std::string PropertyTypeToString(PropertyType value);
    static std::string MemoryStateToString(memory::MemoryState value);

private:
    void EnsureSchema();
    void RefreshDescendantPaths(const OntologyEntity& parent);

    IDataStore* m_store;
};

} // namespace ontology
} // namespace animus::kernel


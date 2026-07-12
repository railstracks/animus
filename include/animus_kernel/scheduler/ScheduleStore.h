#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace animus::kernel {

class IDataStore;

// ============================================================================
// Schedule data model
// ============================================================================

enum class ScheduleType {
    OneShot,
    Recurring
};

struct ScheduleDescriptor {
    std::string id;              // server-generated (MD5 hex)
    std::string agent_id;
    std::string tag;             // grouping key; empty = ungrouped
    ScheduleType type{ScheduleType::OneShot};
    std::string next_fire;       // ISO-8601 timestamp
    std::string cron_expr;       // NULL for one-shot; cron expression for recurring
    std::string timezone{"UTC"}; // IANA timezone for cron evaluation
    std::string message;         // context delivered on fire
    bool enabled{true};
    std::string created_at;      // ISO-8601
    std::string last_fire;       // ISO-8601 or empty
    std::int32_t fire_count{0};
    std::int32_t max_fires{0};   // 0 = unlimited (one-shots default to 1)
};

// ============================================================================
// ScheduleStore — SQLite-backed schedule persistence
// ============================================================================

class ScheduleStore {
public:
    explicit ScheduleStore(IDataStore* dataStore);
    ~ScheduleStore();

    ScheduleStore(const ScheduleStore&) = delete;
    ScheduleStore& operator=(const ScheduleStore&) = delete;

    // CRUD
    bool Create(const ScheduleDescriptor& sched, std::string* error);
    std::string CreateAndReturnId(ScheduleDescriptor& sched, std::string* error);
    std::optional<ScheduleDescriptor> Get(const std::string& id) const;
    std::vector<ScheduleDescriptor> List(
        const std::string& agentId,
        const std::string& tag = "") const;
    bool Update(const ScheduleDescriptor& sched, std::string* error);
    bool Delete(const std::string& id, std::string* error);

    // Query due schedules
    std::vector<ScheduleDescriptor> GetDueSchedules(
        const std::string& nextFireUpperBound,
        int limit = 250) const;

    // Count schedules per agent (for limit enforcement)
    int CountByAgent(const std::string& agentId) const;

private:
    void EnsureSchema();
    ScheduleDescriptor RowToDescriptor(
        const std::string& id, const std::string& agentId,
        const std::string& tag, const std::string& typeStr,
        const std::string& nextFire, const std::string& cronExpr,
        const std::string& tz, const std::string& message,
        bool enabled, const std::string& createdAt,
        const std::string& lastFire, std::int32_t fireCount,
        std::int32_t maxFires) const;

    IDataStore* m_store;
};

} // namespace animus::kernel

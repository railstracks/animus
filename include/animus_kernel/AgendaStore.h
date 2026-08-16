#pragma once

#include "animus_kernel/IDataStore.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace animus::kernel {

// ============================================================================
// AgendaEvent — a single calendar event belonging to an agent
// ============================================================================

struct AgendaEvent {
    int64_t id{0};
    std::string agent_id;
    std::string title;
    std::string description;          // optional, may be empty
    std::string start_time;            // ISO-8601 (e.g. "2026-08-17T09:00:00+02:00")
    std::string end_time;              // ISO-8601, optional
    std::string timezone;              // IANA tz, e.g. "Europe/Amsterdam"
    std::string recurrence;            // "none", "daily", "weekly", "monthly"
    bool completed{false};
    int64_t created_at_unix_ms{0};
    int64_t updated_at_unix_ms{0};
};

// ============================================================================
// AgendaStore — SQLite-backed CRUD for agenda events
// ============================================================================

class AgendaStore {
public:
    explicit AgendaStore(IDataStore* store);

    // Returns the newly created event (with id populated).
    AgendaEvent Create(const std::string& agentId,
                       const std::string& title,
                       const std::string& startTime,
                       const std::string& timezone,
                       const std::string& description = "",
                       const std::string& endTime = "",
                       const std::string& recurrence = "none");

    // Returns the updated event, or empty optional if not found.
    std::optional<AgendaEvent> Update(int64_t id,
                                      const std::string& agentId,
                                      const std::string& title,
                                      const std::string& startTime,
                                      const std::string& timezone,
                                      const std::string& description,
                                      const std::string& endTime,
                                      const std::string& recurrence);

    // Mark an event as completed (or uncompleted).
    bool SetCompleted(int64_t id, const std::string& agentId, bool completed);

    // Delete an event. Returns true if a row was deleted.
    bool Delete(int64_t id, const std::string& agentId);

    // Get a single event by id.
    std::optional<AgendaEvent> GetById(int64_t id, const std::string& agentId) const;

    // List all events for an agent, ordered by start_time ascending.
    std::vector<AgendaEvent> ListForAgent(const std::string& agentId) const;

    // List upcoming events (start_time >= now) for an agent, limited to count.
    // Ordered by start_time ascending.
    std::vector<AgendaEvent> ListUpcoming(const std::string& agentId,
                                           int maxCount) const;

private:
    void EnsureSchema();

    AgendaEvent RowToEvent(const IStatement& stmt) const;

    IDataStore* m_store;
};

} // namespace animus::kernel
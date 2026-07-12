#pragma once

#include "animus_kernel/scheduler/ScheduleStore.h"
#include "animus_kernel/IncomingEvent.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace animus::kernel {

// ============================================================================
// Scheduler — polling loop that fires due schedules as IncomingEvent objects
// ============================================================================

class Scheduler {
public:
    using FireCallback = std::function<void(const IncomingEvent& event)>;

    explicit Scheduler(IDataStore* dataStore);
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // Lifecycle
    bool Start(std::string* error);
    void Stop();
    bool IsRunning() const { return m_running.load(); }

    // Schedule management (delegates to ScheduleStore)
    std::string Create(const ScheduleDescriptor& sched, std::string* error);
    std::optional<ScheduleDescriptor> Get(const std::string& id) const;
    std::vector<ScheduleDescriptor> List(
        const std::string& agentId, const std::string& tag = "") const;
    bool Cancel(const std::string& id, std::string* error);

    // Configuration
    void SetPollIntervalMs(uint32_t ms);
    void SetMaxSchedulesPerAgent(uint32_t limit);
    void SetFireCallback(FireCallback cb);

    // Access to store (for tests)
    ScheduleStore& Store() { return m_store; }

    // Utility: current time as ISO-8601 string (UTC).
    static std::string IsoNow();

private:
    void PollLoop();
    void ProcessDueSchedules();

    // Time helpers
    static std::string ComputeNextFire(
        const std::string& cronExpr,
        const std::string& timezone,
        const std::string& lastFireIso);

    // Cron field parsing
    static bool CronFieldMatches(int value, const std::string& field, int min, int max);
    static bool CronMatches(const std::string& expr, int minute,
                             int hour, int day, int month, int year,
                             int wday);

    ScheduleStore m_store;
    FireCallback m_fireCallback;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::mutex m_configMutex;
    uint32_t m_pollIntervalMs{30000};
    uint32_t m_maxSchedulesPerAgent{50};
};

} // namespace animus::kernel
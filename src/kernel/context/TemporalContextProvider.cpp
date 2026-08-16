// TemporalContextProvider.cpp
// Injects current date/time and upcoming agenda events into the system prompt
// so the agent has temporal awareness without needing a tool call.
// ============================================================================

#include "animus_kernel/context/TemporalContextProvider.h"
#include "animus_kernel/AgendaStore.h"
#include "animus_kernel/AgentStore.h"
#include "animus_kernel/Session.h"

#include <chrono>
#include <cstdio>
#include <sstream>

namespace animus::kernel {

TemporalContextProvider::TemporalContextProvider(AgendaStore* agendaStore)
    : m_agendaStore(agendaStore) {}

std::optional<ContextBlock> TemporalContextProvider::Provide(
        const Agent& agent,
        const SessionAccess& session) const {

    std::ostringstream out;

    // Current date/time in UTC and agent timezone
    auto now = std::chrono::system_clock::now();
    std::time_t nowT = std::chrono::system_clock::to_time_t(now);
    std::tm tmUtc{};
    gmtime_r(&nowT, &tmUtc);

    char dateBuf[64];
    std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &tmUtc);
    std::string dateStr(dateBuf);

    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S UTC", &tmUtc);
    std::string timeStr(timeBuf);

    // Day of week
    const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                          "Thursday", "Friday", "Saturday"};
    std::string dayOfWeek = days[tmUtc.tm_wday];

    out << "Current date: " << dateStr << " (" << dayOfWeek << ")\n";
    out << "Current time: " << timeStr << "\n";

    // Upcoming agenda events
    if (m_agendaStore && !agent.id.empty()) {
        auto upcoming = m_agendaStore->ListUpcoming(agent.id, 5);
        if (!upcoming.empty()) {
            out << "\nUpcoming agenda events:\n";
            for (const auto& ev : upcoming) {
                out << "  - " << ev.start_time;
                if (!ev.timezone.empty() && ev.timezone != "UTC") {
                    out << " (" << ev.timezone << ")";
                }
                out << ": " << ev.title;
                if (!ev.description.empty()) {
                    // Truncate long descriptions
                    std::string desc = ev.description;
                    if (desc.size() > 80) desc = desc.substr(0, 77) + "...";
                    out << " — " << desc;
                }
                if (ev.recurrence != "none" && !ev.recurrence.empty()) {
                    out << " [recurs: " << ev.recurrence << "]";
                }
                out << "\n";
            }
        }
    }

    ContextBlock block;
    block.name = "TEMPORAL CONTEXT";
    block.content = out.str();
    block.priority = 10;
    return block;
}

} // namespace animus::kernel
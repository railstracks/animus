#include "animus_kernel/scheduler/Scheduler.h"
#include "animus_kernel/IDataStore.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace animus::kernel {

namespace {

constexpr int kDueBatchLimit = 250;
constexpr int kMaxCronLookaheadYears = 5;

// ──────────────────────────────────────────────────────────────────
// Time utilities
// ──────────────────────────────────────────────────────────────────

int64_t NowUnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string IsoFromUnixMs(int64_t ms) {
    using namespace std::chrono;
    const auto tp = system_clock::time_point(milliseconds(ms));
    const auto tt = system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

int64_t UnixMsFromIso(const std::string& iso) {
    std::tm tm{};
    // Accept formats: 2026-05-10T09:00:00Z or 2026-05-10T09:00:00+02:00
    int tzOffsetSec = 0;

    // Try with timezone offset
    if (iso.size() >= 25 && (iso[19] == '+' || iso[19] == '-')) {
        std::istringstream iss(iso.substr(0, 19));
        iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (iss.fail()) return 0;
        const int offsetHours = std::stoi(iso.substr(19, 3));
        const int offsetMin = std::stoi(iso.substr(23, 2));
        tzOffsetSec = offsetHours * 3600 + offsetMin * 60;
        if (iso[19] == '-') tzOffsetSec = -tzOffsetSec;
    } else {
        // Try Z suffix or bare
        std::string s = iso;
        if (s.size() >= 20 && s[19] == 'Z') s = s.substr(0, 19);
        else if (s.size() > 19) s = s.substr(0, 19);
        std::istringstream iss(s);
        iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (iss.fail()) return 0;
    }

    // Convert to time_t (UTC-based from gm tm fields)
    tm.tm_isdst = -1;
    const time_t utc = timegm(&tm) - tzOffsetSec;
    return static_cast<int64_t>(utc) * 1000LL;
}

void GetTimeFields(int64_t epochMs, int& year, int& month, int& day,
                    int& hour, int& minute, int& wday) {
    using namespace std::chrono;
    const auto tp = system_clock::time_point(milliseconds(epochMs));
    const auto tt = system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    year = tm.tm_year + 1900;
    month = tm.tm_mon + 1;
    day = tm.tm_mday;
    hour = tm.tm_hour;
    minute = tm.tm_min;
    wday = tm.tm_wday;  // 0=Sunday
}

// ──────────────────────────────────────────────────────────────────
// Relative time parsing
// ──────────────────────────────────────────────────────────────────

// Parse "in 30 minutes", "in 2 hours", "tomorrow at 9am", etc.
// Returns the resulting ISO timestamp, or empty string on failure.
std::string ParseRelativeTime(const std::string& when) {
    std::string s = when;
    // Lowercase and trim
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    // Trim leading/trailing whitespace
    auto start = s.find_first_not_of(" \t\n\r");
    auto end = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    s = s.substr(start, end - start + 1);

    int64_t nowMs = NowUnixMs();

    // "in N minutes"
    if (s.rfind("in ", 0) == 0) {
        const auto rest = s.substr(3);
        int64_t offsetMs = 0;
        try {
            if (rest.find("minute") != std::string::npos) {
                offsetMs = std::stoll(rest) * 60 * 1000;
            } else if (rest.find("hour") != std::string::npos) {
                offsetMs = std::stoll(rest) * 60 * 60 * 1000;
            } else if (rest.find("second") != std::string::npos) {
                offsetMs = std::stoll(rest) * 1000;
            } else {
                return "";
            }
        } catch (...) {
            return "";
        }
        return IsoFromUnixMs(nowMs + offsetMs);
    }

    // "tomorrow at 9am" / "tomorrow at 9:00"
    if (s.rfind("tomorrow", 0) == 0) {
        int year, month, day, hour, minute, wday;
        GetTimeFields(nowMs + 24 * 60 * 60 * 1000, year, month, day, hour, minute, wday);
        // Try to extract hour/minute from the "at X" part
        const auto atPos = s.find("at ");
        if (atPos != std::string::npos) {
            const auto timeStr = s.substr(atPos + 3);
            // Handle "9am", "9:00am", "9:00", "14:00"
            try {
                int h = 0, m = 0;
                if (timeStr.find("am") != std::string::npos) {
                    const auto num = timeStr.substr(0, timeStr.find("am"));
                    if (num.find(':') != std::string::npos) {
                        h = std::stoi(num.substr(0, num.find(':')));
                        m = std::stoi(num.substr(num.find(':') + 1));
                    } else {
                        h = std::stoi(num);
                    }
                    if (h == 12) h = 0;
                } else if (timeStr.find("pm") != std::string::npos) {
                    const auto num = timeStr.substr(0, timeStr.find("pm"));
                    if (num.find(':') != std::string::npos) {
                        h = std::stoi(num.substr(0, num.find(':')));
                        m = std::stoi(num.substr(num.find(':') + 1));
                    } else {
                        h = std::stoi(num);
                    }
                    if (h != 12) h += 12;
                } else {
                    // 24-hour format
                    const auto colonPos = timeStr.find(':');
                    if (colonPos != std::string::npos) {
                        h = std::stoi(timeStr.substr(0, colonPos));
                        m = std::stoi(timeStr.substr(colonPos + 1));
                    } else {
                        h = std::stoi(timeStr);
                    }
                }
                hour = h;
                minute = m;
            } catch (...) {
                return "";
            }
        }

        std::tm tm{};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = 0;
        tm.tm_isdst = -1;
        const auto tt = timegm(&tm);
        return IsoFromUnixMs(static_cast<int64_t>(tt) * 1000LL);
    }

    return "";
}

// ──────────────────────────────────────────────────────────────────
// Cron matching
// ──────────────────────────────────────────────────────────────────

bool CronFieldMatches(int value, const std::string& field, int min, int max) {
    if (field == "*") return true;

    // Check for step: "*/5", "1-30/5"
    const auto slashPos = field.find('/');
    int step = 1;
    std::string range = field;
    if (slashPos != std::string::npos) {
        step = std::stoi(field.substr(slashPos + 1));
        range = field.substr(0, slashPos);
    }

    // "*/N" means every N within the full min-max range
    if (range == "*") {
        return value % step == 0;
    }

    // Check for range: "1-10"
    const auto dashPos = range.find('-');
    if (dashPos != std::string::npos) {
        const int lo = std::stoi(range.substr(0, dashPos));
        const int hi = std::stoi(range.substr(dashPos + 1));
        if (value < lo || value > hi) return false;
        return (value - lo) % step == 0;
    }

    // Comma-separated values
    if (range.find(',') != std::string::npos) {
        std::istringstream iss(range);
        std::string token;
        while (std::getline(iss, token, ',')) {
            if (token == "*" || std::stoi(token) == value) return true;
        }
        return false;
    }

    return std::stoi(range) == value;
}

bool CronMatchesExpr(const std::string& expr, int minute, int hour,
                     int day, int month, int year, int wday) {
    // Parse 5 fields: minute hour day month wday
    // Extended 6-field support: second minute hour day month wday
    std::istringstream iss(expr);
    std::string fields[6];
    int count = 0;
    for (int i = 0; i < 6 && iss >> fields[i]; ++i) count = i + 1;

    if (count == 5) {
        return CronFieldMatches(minute, fields[0], 0, 59) &&
               CronFieldMatches(hour, fields[1], 0, 23) &&
               CronFieldMatches(day, fields[2], 1, 31) &&
               CronFieldMatches(month, fields[3], 1, 12) &&
               CronFieldMatches(wday, fields[4], 0, 6);
    }
    if (count == 6) {
        // With seconds
        return CronFieldMatches(std::stoi(fields[0]), fields[0], 0, 59) &&  // second unused
               CronFieldMatches(minute, fields[1], 0, 59) &&
               CronFieldMatches(hour, fields[2], 0, 23) &&
               CronFieldMatches(day, fields[3], 1, 31) &&
               CronFieldMatches(month, fields[4], 1, 12) &&
               CronFieldMatches(wday, fields[5], 0, 6);
    }
    return false;
}

bool CronMatches(const std::string& expr, int minute, int hour,
                 int day, int month, int year, int wday) {
    return CronMatchesExpr(expr, minute, hour, day, month, year, wday);
}

// ──────────────────────────────────────────────────────────────────
// Next fire computation for recurring schedules
// ──────────────────────────────────────────────────────────────────

std::string ComputeNextFireImpl(const std::string& cronExpr,
                                 const std::string& /*timezone*/,
                                 const std::string& lastFireIso) {
    // Timezone is noted in the descriptor but not used for raw computation
    // here — we work in UTC for simplicity and let timezone metadata inform display.
    if (cronExpr.empty() || cronExpr == "* * * * *") {
        // Every minute
        return IsoFromUnixMs(NowUnixMs() + 60 * 1000);
    }

    // Start from last_fire or now
    int64_t baseMs = lastFireIso.empty() ? NowUnixMs() : UnixMsFromIso(lastFireIso);
    if (baseMs <= 0) baseMs = NowUnixMs();

    // Step forward minute by minute until cron matches
    // Cap lookahead at kMaxCronLookaheadYears to prevent infinite loop
    const int64_t maxLookahead =
        baseMs + static_cast<int64_t>(kMaxCronLookaheadYears) * 365LL * 24LL * 3600LL * 1000LL;

    for (int64_t ms = baseMs + 60 * 1000; ms < maxLookahead; ms += 60 * 1000) {
        int year, month, day, hour, minute, wday;
        GetTimeFields(ms, year, month, day, hour, minute, wday);

        if (CronMatches(cronExpr, minute, hour, day, month, year, wday)) {
            return IsoFromUnixMs(ms);
        }
    }

    // Shouldn't reach here for valid cron expressions
    return "";
}

} // anonymous namespace

// ──────────────────────────────────────────────────────────────────
// Public API (in class scope)
// ──────────────────────────────────────────────────────────────────

// static helpers exposed through class
std::string Scheduler::IsoNow() {
    return IsoFromUnixMs(NowUnixMs());
}

std::string Scheduler::ComputeNextFire(
        const std::string& cronExpr,
        const std::string& timezone,
        const std::string& lastFireIso) {
    return ComputeNextFireImpl(cronExpr, timezone, lastFireIso);
}

// ──────────────────────────────────────────────────────────────────
// Scheduler lifecycle
// ──────────────────────────────────────────────────────────────────

Scheduler::Scheduler(IDataStore* dataStore) : m_store(dataStore) {}

Scheduler::~Scheduler() {
    Stop();
}

bool Scheduler::Start(std::string* error) {
    if (m_running.load()) return true;

    m_stopRequested.store(false);
    m_thread = std::thread(&Scheduler::PollLoop, this);
    m_running.store(true);

    // Give the thread a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return true;
}

void Scheduler::Stop() {
    if (!m_running.load()) return;

    m_stopRequested.store(true);
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_running.store(false);
}

// ──────────────────────────────────────────────────────────────────
// Schedule CRUD
// ──────────────────────────────────────────────────────────────────

std::string Scheduler::Create(const ScheduleDescriptor& sched, std::string* error) {
    const uint32_t limit = m_maxSchedulesPerAgent;
    if (limit > 0) {
        const int count = m_store.CountByAgent(sched.agent_id);
        if (count >= static_cast<int>(limit)) {
            if (error) {
                *error = "schedule limit reached for agent (" +
                         std::to_string(limit) + " max)";
            }
            return "";
        }
    }

    ScheduleDescriptor toCreate = sched;

    // Parse relative time
    bool isCron = false;
    {
        // Check if the next_fire looks like a cron expression or relative
        std::string next = toCreate.next_fire;
        if (next.find("in ") == 0 || next.find("tomorrow") == 0) {
            // Relative time
            std::string parsed = ParseRelativeTime(next);
            if (parsed.empty()) {
                if (error) *error = "failed to parse relative time: " + next;
                return "";
            }
            toCreate.next_fire = parsed;
        } else if (next.find('*') != std::string::npos ||
                   std::count(next.begin(), next.end(), ' ') >= 3) {
            // Looks like a cron expression
            toCreate.cron_expr = next;
            toCreate.type = ScheduleType::Recurring;
            toCreate.next_fire = ComputeNextFireImpl(next, toCreate.timezone, "");
            if (toCreate.next_fire.empty()) {
                if (error) *error = "failed to compute first fire from cron: " + next;
                return "";
            }
            isCron = true;
        }
        // Otherwise treat as ISO timestamp
    }

    // One-shot: default max_fires to 1 if not set
    if (toCreate.type == ScheduleType::OneShot && toCreate.max_fires <= 0) {
        toCreate.max_fires = 1;
    }

    std::string createdId = m_store.CreateAndReturnId(toCreate, error);
    if (createdId.empty()) return "";
    return createdId;
}

std::optional<ScheduleDescriptor> Scheduler::Get(const std::string& id) const {
    return m_store.Get(id);
}

std::vector<ScheduleDescriptor> Scheduler::List(
        const std::string& agentId, const std::string& tag) const {
    return m_store.List(agentId, tag);
}

bool Scheduler::Cancel(const std::string& id, std::string* error) {
    return m_store.Delete(id, error);
}

// ──────────────────────────────────────────────────────────────────
// Configuration
// ──────────────────────────────────────────────────────────────────

void Scheduler::SetPollIntervalMs(uint32_t ms) {
    m_pollIntervalMs = ms;
}

void Scheduler::SetMaxSchedulesPerAgent(uint32_t limit) {
    m_maxSchedulesPerAgent = limit;
}

void Scheduler::SetFireCallback(FireCallback cb) {
    m_fireCallback = std::move(cb);
}

// ──────────────────────────────────────────────────────────────────
// Polling loop
// ──────────────────────────────────────────────────────────────────

void Scheduler::PollLoop() {
    while (!m_stopRequested.load()) {
        ProcessDueSchedules();

        // Sleep in small chunks to detect stop requests
        const uint32_t interval = m_pollIntervalMs;
        for (uint32_t elapsed = 0;
             elapsed < interval && !m_stopRequested.load();
             elapsed += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(
                std::min<uint32_t>(100, interval - elapsed)));
        }
    }
}

void Scheduler::ProcessDueSchedules() {
    const std::string nowIso = IsoFromUnixMs(NowUnixMs());
    const auto due = m_store.GetDueSchedules(nowIso, kDueBatchLimit);

    if (due.empty()) return;

    for (const auto& schedule : due) {
        // Fire the event
        if (m_fireCallback) {
            IncomingEvent event;
            event.source = "scheduler";
            event.metadata["schedule_id"] = schedule.id;
            event.metadata["tag"] = schedule.tag;
            event.metadata["message"] = schedule.message;
            event.metadata["agent_id"] = schedule.agent_id;
            m_fireCallback(event);
        }

        // Update schedule state
        ScheduleDescriptor updated = schedule;
        updated.last_fire = nowIso;
        updated.fire_count = schedule.fire_count + 1;

        if (schedule.type == ScheduleType::OneShot) {
            updated.enabled = false;
        } else if (schedule.max_fires > 0 &&
                   updated.fire_count >= schedule.max_fires) {
            updated.enabled = false;
        } else {
            // Recurring: compute next fire
            updated.next_fire = ComputeNextFireImpl(
                schedule.cron_expr, schedule.timezone, nowIso);
        }

        std::string err;
        if (!m_store.Update(updated, &err)) {
            std::cerr << "[scheduler] failed to update schedule "
                      << schedule.id << ": " << err << std::endl;
        }
    }
}

} // namespace animus::kernel

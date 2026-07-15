#include "animus_kernel/MessageQueue.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <utility>

namespace animus::kernel {

MessageQueue::MessageQueue(FlushCallback flushCallback)
    : m_flushCallback(std::move(flushCallback))
    , m_running(true)
{
    m_timerThread = std::thread([this]() { TimerLoop(); });
}

MessageQueue::~MessageQueue() {
    Shutdown();
}

void MessageQueue::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running.load()) return;
        m_running.store(false);
        m_cv.notify_all();
    }
    if (m_timerThread.joinable()) {
        m_timerThread.join();
    }
}

void MessageQueue::Push(const std::string& sessionKey,
                        const std::string& sender,
                        const std::string& content,
                        std::uint64_t unixMs,
                        int intervalSeconds,
                        int maxQueued) {
    std::string flushedMsg;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto& state = m_sessions[sessionKey];
        state.interval_seconds = intervalSeconds;

        // If a chain is active, just accumulate — timer will be set when chain ends.
        // If no chain is active, set/update the timer.
        state.messages.push_back({sessionKey, sender, content, unixMs});

        // Safety valve: force flush if max queued exceeded
        bool forceFlush = (maxQueued > 0 && static_cast<int>(state.messages.size()) >= maxQueued);

        // If not force-flushing and no chain/timer active, decide when to fire:
        if (!forceFlush && !state.chain_active && !state.timer_running && intervalSeconds > 0) {
            if (!state.has_responded) {
                // First ever message on this session — no prior response to wait out
                forceFlush = true;
            } else {
                // Interval is measured from the agent's last response, not from
                // when the message arrived. If enough time has already passed,
                // fire immediately.
                auto deadline = state.chain_end_time
                    + std::chrono::seconds(intervalSeconds);
                if (deadline <= std::chrono::steady_clock::now()) {
                    forceFlush = true;
                } else {
                    state.timer_deadline = deadline;
                    state.timer_running = true;
                    m_cv.notify_all();
                }
            }
        }

        if (forceFlush) {
            flushedMsg = ExtractPending(sessionKey);
        }
    }

    // Call flush callback outside the lock to avoid deadlock
    // (callback calls ExecuteChannelDispatch which re-enters via NotifyChainStart)
    if (!flushedMsg.empty() && m_flushCallback) {
        m_flushCallback(sessionKey, flushedMsg);
    }
}

bool MessageQueue::HasPending(const std::string& sessionKey) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(sessionKey);
    return it != m_sessions.end() && !it->second.messages.empty();
}

std::size_t MessageQueue::PendingCount(const std::string& sessionKey) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(sessionKey);
    if (it == m_sessions.end()) return 0;
    return it->second.messages.size();
}

std::string MessageQueue::Drain(const std::string& sessionKey) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(sessionKey);
    if (it == m_sessions.end() || it->second.messages.empty()) {
        return "";
    }
    std::string result = ConcatenateMessages(it->second.messages);
    it->second.messages.clear();
    it->second.timer_running = false;
    return result;
}

void MessageQueue::SetInterjectionEnabled(const std::string& sessionKey, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& state = m_sessions[sessionKey];
    state.allow_interjection = enabled;
}

std::string MessageQueue::DrainInterjection(const std::string& sessionKey) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(sessionKey);
    if (it == m_sessions.end() || !it->second.allow_interjection || it->second.messages.empty()) {
        return "";
    }
    std::string body = ConcatenateMessages(it->second.messages);
    it->second.messages.clear();
    // Don't clear timer_running — the chain is still active;
    // NotifyChainEnd will handle cooldown after chain completes.
    return "[New messages arrived while you were working:]\n" + body;
}

void MessageQueue::NotifyChainStart(const std::string& sessionKey) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& state = m_sessions[sessionKey];
    state.chain_active = true;
    state.timer_running = false; // Cancel any pending timer
}

void MessageQueue::NotifyChainEnd(const std::string& sessionKey, int intervalSeconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& state = m_sessions[sessionKey];
    state.chain_active = false;
    state.chain_end_time = std::chrono::steady_clock::now();
    state.has_responded = true;
    state.interval_seconds = intervalSeconds;

    // If there are pending messages, start the cooldown timer
    if (!state.messages.empty() && intervalSeconds > 0) {
        state.timer_deadline = std::chrono::steady_clock::now()
            + std::chrono::seconds(intervalSeconds);
        state.timer_running = true;
        m_cv.notify_all();
    }
}

void MessageQueue::TimerLoop() {
    while (m_running.load()) {
        std::unique_lock<std::mutex> lock(m_mutex);

        // Find the earliest deadline across all sessions
        std::chrono::steady_clock::time_point earliest;
        bool hasDeadline = false;

        for (auto& [key, state] : m_sessions) {
            if (state.timer_running && !state.chain_active) {
                if (!hasDeadline || state.timer_deadline < earliest) {
                    earliest = state.timer_deadline;
                    hasDeadline = true;
                }
            }
        }

        if (!hasDeadline) {
            // No pending timers — wait for a new one to be set
            m_cv.wait_for(lock, std::chrono::seconds(5));
            continue;
        }

        // Wait until the earliest deadline
        auto status = m_cv.wait_until(lock, earliest);
        if (!m_running.load()) break;

        // Check which timers have expired
        auto now = std::chrono::steady_clock::now();
        std::vector<std::string> toFlush;

        for (auto& [key, state] : m_sessions) {
            if (state.timer_running && !state.chain_active
                && now >= state.timer_deadline && !state.messages.empty()) {
                toFlush.push_back(key);
            }
        }

        // Flush expired sessions: extract data under lock, then call callbacks
        // outside the lock to avoid deadlock (callback re-enters via NotifyChainStart).
        // lock is held here (reacquired by wait_until on wakeup).
        std::vector<std::pair<std::string, std::string>> toFire;
        for (const auto& key : toFlush) {
            std::string msg = ExtractPending(key);
            if (!msg.empty()) {
                toFire.emplace_back(key, std::move(msg));
            }
        }
        lock.unlock();

        for (auto& [key, msg] : toFire) {
            if (m_flushCallback) {
                m_flushCallback(key, msg);
            }
        }
        // Loop back: unique_lock is declared fresh each iteration
    }
}

// Extracts and clears pending messages for a session.
// Caller must hold m_mutex. Returns empty string and does nothing if no messages.
// Does NOT call the flush callback — the caller must invoke it outside the lock
// to avoid deadlock (the callback may re-enter the queue via NotifyChainStart).
std::string MessageQueue::ExtractPending(const std::string& sessionKey) {
    auto it = m_sessions.find(sessionKey);
    if (it == m_sessions.end() || it->second.messages.empty()) return "";

    std::string concatenated = ConcatenateMessages(it->second.messages);
    it->second.messages.clear();
    it->second.timer_running = false;
    return concatenated;
}

std::string MessageQueue::ConcatenateMessages(const std::vector<QueuedMessage>& msgs) const {
    // Check if any message has a sender label (community channel)
    bool hasSender = false;
    for (const auto& m : msgs) {
        if (!m.sender.empty()) {
            hasSender = true;
            break;
        }
    }

    std::ostringstream ss;
    for (const auto& m : msgs) {
        if (hasSender && !m.sender.empty()) {
            ss << "[" << m.sender << "]: " << m.content << "\n";
        } else if (hasSender && m.sender.empty()) {
            ss << "[unknown]: " << m.content << "\n";
        } else {
            ss << m.content << "\n";
        }
    }
    return ss.str();
}

} // namespace animus::kernel
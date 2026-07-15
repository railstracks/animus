#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace animus::kernel {

// ============================================================================
// MessageQueue — per-session message accumulation with interval-based flush
//
// When a channel has min_response_interval > 0, incoming messages are queued
// instead of immediately dispatching to the LLM. When the cooldown expires
// (interval seconds after the last chain completed), all queued messages are
// concatenated into a single user turn and submitted as one prompt chain.
//
// Thread safety: channel adapters push from I/O threads; the flush timer
// runs on its own thread. All access is mutex-guarded.
// ============================================================================

struct QueuedMessage {
    std::string session_key;
    std::string sender;     // Sender label (for community channels) or empty (DMs)
    std::string content;
    std::uint64_t unix_ms{0};
};

class MessageQueue {
public:
    // Callback invoked when the cooldown timer expires and there are pending messages.
    // Receives the concatenated message text and the session key.
    using FlushCallback = std::function<void(
        const std::string& sessionKey,
        const std::string& concatenatedMessage)>;

    explicit MessageQueue(FlushCallback flushCallback);
    ~MessageQueue();

    MessageQueue(const MessageQueue&) = delete;
    MessageQueue& operator=(const MessageQueue&) = delete;

    // Push a message into a session's queue.
    // If no timer is running for this session, starts one.
    // If a timer is already running, the message accumulates.
    void Push(const std::string& sessionKey,
              const std::string& sender,
              const std::string& content,
              std::uint64_t unixMs,
              int intervalSeconds,
              int maxQueued);

    // Drain pending messages for a session without waiting for the timer.
    // Returns concatenated text. Used when forcing a flush (e.g., max queue hit).
    std::string Drain(const std::string& sessionKey);

    // Check if a session has pending messages.
    bool HasPending(const std::string& sessionKey) const;

    // Enable/disable interjection for a session.
    // When enabled, ChainRunner drains pending messages between chain steps.
    void SetInterjectionEnabled(const std::string& sessionKey, bool enabled);

    // Drain pending messages for interjection (returns empty string if
    // interjection is disabled or no messages pending).
    // Formats with interjection header.
    std::string DrainInterjection(const std::string& sessionKey);

    // Get pending count for a session.
    std::size_t PendingCount(const std::string& sessionKey) const;

    // Notify that a chain has started on a session (blocks timer, prevents flush).
    void NotifyChainStart(const std::string& sessionKey);

    // Notify that a chain has completed on a session.
    // Starts the cooldown timer if there are pending messages.
    void NotifyChainEnd(const std::string& sessionKey, int intervalSeconds);

    // Cancel all timers and clear queues (shutdown).
    void Shutdown();

private:
    struct SessionState {
        std::vector<QueuedMessage> messages;
        std::chrono::steady_clock::time_point chain_end_time;
        std::chrono::steady_clock::time_point timer_deadline;
        bool chain_active{false};
        bool timer_running{false};
        bool allow_interjection{false};
        int interval_seconds{0};
    };

    void TimerLoop();
    std::string ConcatenateMessages(const std::vector<QueuedMessage>& msgs) const;

    // Extract and clear pending messages for a session (caller must hold m_mutex).
    // Does NOT call the flush callback — caller must do that outside the lock.
    std::string ExtractPending(const std::string& sessionKey);

    FlushCallback m_flushCallback;
    std::unordered_map<std::string, SessionState> m_sessions;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_running{true};
    std::thread m_timerThread;
};

} // namespace animus::kernel
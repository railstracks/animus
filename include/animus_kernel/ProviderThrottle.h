#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>

namespace animus::kernel {

// ============================================================================
// ProviderThrottle — concurrency limiter per provider
// ============================================================================

// RAII guard that releases a provider slot on destruction.
class ProviderThrottle;

class SlotGuard {
public:
    SlotGuard() : m_throttle(nullptr) {}
    SlotGuard(ProviderThrottle* throttle, std::string providerId);
    ~SlotGuard();

    SlotGuard(SlotGuard&& other) noexcept
        : m_throttle(other.m_throttle)
        , m_providerId(std::move(other.m_providerId))
        , m_released(other.m_released) {
        other.m_throttle = nullptr;
        other.m_released = true;
    }

    SlotGuard(const SlotGuard&) = delete;
    SlotGuard& operator=(const SlotGuard&) = delete;
    SlotGuard& operator=(SlotGuard&& other) noexcept;

    // Release the slot early (before guard destruction).
    void Release();

    explicit operator bool() const { return m_throttle != nullptr; }

private:
    ProviderThrottle* m_throttle;
    std::string m_providerId;
    bool m_released{false};
};

class ProviderThrottle {
public:
    // concurrencyLookup: function that returns the configured concurrency for a provider.
    // Called on each Acquire to pick up runtime config changes.
    explicit ProviderThrottle(std::function<int(const std::string&)> concurrencyLookup);

    // Acquire an execution slot for the given provider.
    // Blocks if all slots are occupied (condition variable wait).
    // Returns a SlotGuard that releases the slot on destruction.
    // If concurrency is 0, returns immediately (unlimited — no slot tracked).
    SlotGuard Acquire(const std::string& providerId);

private:
    friend class SlotGuard;

    void Release(const std::string& providerId);

    struct SlotPool {
        int active{0};
        std::queue<std::promise<void>> waiting;
    };

    std::function<int(const std::string&)> m_lookup;
    std::mutex m_mutex;
    std::unordered_map<std::string, SlotPool> m_pools;
};

} // namespace animus::kernel

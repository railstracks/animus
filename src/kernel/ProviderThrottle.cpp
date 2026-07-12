#include "animus_kernel/ProviderThrottle.h"

#include <iostream>

namespace animus::kernel {

// ============================================================================
// ProviderThrottle
// ============================================================================

ProviderThrottle::ProviderThrottle(std::function<int(const std::string&)> concurrencyLookup)
    : m_lookup(std::move(concurrencyLookup)) {}

SlotGuard ProviderThrottle::Acquire(const std::string& providerId) {
    int concurrency = 1;
    if (m_lookup) {
        concurrency = m_lookup(providerId);
    }

    // Unlimited — no throttling
    if (concurrency <= 0) {
        return SlotGuard(nullptr, "");
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    auto& pool = m_pools[providerId];

    if (pool.active < concurrency) {
        // Slot available immediately
        pool.active++;
        return SlotGuard(this, providerId);
    }

    // At capacity — wait for a slot
    std::promise<void> promise;
    auto future = promise.get_future();
    pool.waiting.push(std::move(promise));

    std::cerr << "[throttle] provider '" << providerId << "' at capacity ("
              << pool.active << "/" << concurrency << "), queuing request" << std::endl;

    // Unlock while waiting so other jobs can complete and release slots
    lock.unlock();
    future.wait();
    // When we wake up, a slot has been transferred to us by Release()

    return SlotGuard(this, providerId);
}

void ProviderThrottle::Release(const std::string& providerId) {
    if (providerId.empty()) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_pools.find(providerId);
    if (it == m_pools.end()) return;

    auto& pool = it->second;

    if (!pool.waiting.empty()) {
        // Transfer slot to next waiter instead of decrementing
        auto promise = std::move(pool.waiting.front());
        pool.waiting.pop();
        promise.set_value();
    } else {
        pool.active--;
        // Clean up empty pools
        if (pool.active == 0) {
            m_pools.erase(it);
        }
    }
}

// ============================================================================
// SlotGuard
// ============================================================================

SlotGuard::SlotGuard(ProviderThrottle* throttle, std::string providerId)
    : m_throttle(throttle)
    , m_providerId(std::move(providerId))
    , m_released(false) {}

SlotGuard::~SlotGuard() {
    Release();
}

SlotGuard& SlotGuard::operator=(SlotGuard&& other) noexcept {
    if (this != &other) {
        Release();
        m_throttle = other.m_throttle;
        m_providerId = std::move(other.m_providerId);
        m_released = other.m_released;
        other.m_throttle = nullptr;
        other.m_released = true;
    }
    return *this;
}

void SlotGuard::Release() {
    if (!m_released && m_throttle) {
        m_throttle->Release(m_providerId);
        m_released = true;
    }
}

} // namespace animus::kernel

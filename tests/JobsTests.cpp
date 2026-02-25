#include "animus/Jobs.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

using animus::jobs::JobHandle;
using animus::jobs::JobSystem;
using animus::jobs::JobSystemConfig;
using animus::jobs::JobSystemStopMode;
using animus::jobs::TaskGroup;
using animus::jobs::JobPriority;
using animus::jobs::JobLane;

bool WaitUntilAllReady(
    const std::vector<JobHandle>& handles,
    std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        bool allReady = true;
        for (std::size_t i = 0; i < handles.size(); ++i) {
            if (!handles[i].IsReady()) {
                allReady = false;
                break;
            }
        }

        if (allReady) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}

bool WaitGroupForSuccess(const std::vector<JobHandle>& handles) {
    bool allSuccessful = true;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        if (!handles[i].WaitForSuccess()) {
            allSuccessful = false;
        }
    }
    return allSuccessful;
}

// ============================================================================
// Basic Tests (from Luminal)
// ============================================================================

bool TestCancelPendingShutdown() {
    JobSystem jobs;
    JobSystemConfig config;
    config.workerCount = 1U;
    config.maxQueuedJobs = 0U;
    if (!jobs.Start(config)) {
        std::cerr << "[cancel-pending] failed to start job system\n";
        return false;
    }

    std::atomic<bool> releaseBlocker(false);
    std::atomic<bool> blockerStarted(false);
    std::atomic<int> pendingExecuted(0);

    JobHandle blocker = jobs.Enqueue([&] {
        blockerStarted.store(true);
        while (!releaseBlocker.load()) {
            std::this_thread::yield();
        }
    });
    if (!blocker.IsValid()) {
        std::cerr << "[cancel-pending] failed to enqueue blocker\n";
        jobs.Stop(JobSystemStopMode::Drain);
        return false;
    }

    std::vector<JobHandle> pending;
    for (std::size_t i = 0; i < 64U; ++i) {
        JobHandle handle = jobs.Enqueue([&pendingExecuted] {
            pendingExecuted.fetch_add(1);
        });
        if (!handle.IsValid()) {
            std::cerr << "[cancel-pending] failed to enqueue pending task\n";
            releaseBlocker.store(true);
            jobs.Stop(JobSystemStopMode::Drain);
            return false;
        }
        pending.push_back(handle);
    }

    const auto blockerStartDeadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    while (!blockerStarted.load() && (std::chrono::steady_clock::now() < blockerStartDeadline)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!blockerStarted.load()) {
        std::cerr << "[cancel-pending] blocker did not start in time\n";
        releaseBlocker.store(true);
        jobs.Stop(JobSystemStopMode::Drain);
        return false;
    }

    std::thread stopper([&jobs] {
        jobs.Stop(JobSystemStopMode::CancelPending);
    });

    const bool pendingReady = WaitUntilAllReady(pending, std::chrono::milliseconds(2000));
    releaseBlocker.store(true);
    const bool blockerSucceeded = blocker.WaitForSuccess();
    stopper.join();

    if (!pendingReady) {
        std::cerr << "[cancel-pending] timeout waiting for canceled handles\n";
        return false;
    }
    if (!blockerSucceeded) {
        std::cerr << "[cancel-pending] blocker did not complete successfully\n";
        return false;
    }
    if (pendingExecuted.load() != 0) {
        std::cerr << "[cancel-pending] pending tasks executed: " << pendingExecuted.load()
                  << "\n";
        return false;
    }

    for (std::size_t i = 0; i < pending.size(); ++i) {
        if (pending[i].WaitForSuccess()) {
            std::cerr << "[cancel-pending] canceled handle reported success\n";
            return false;
        }
    }

    return true;
}

bool TestFailurePropagationWithGroupAggregation() {
    JobSystem jobs;
    JobSystemConfig config;
    config.workerCount = 2U;
    if (!jobs.Start(config)) {
        std::cerr << "[failure-propagation] failed to start job system\n";
        return false;
    }

    JobHandle ok = jobs.Enqueue([] {});
    JobHandle fail = jobs.Enqueue([] { throw std::runtime_error("expected test failure"); });
    JobHandle ok2 = jobs.Enqueue([] {});

    if (!ok.IsValid() || !fail.IsValid() || !ok2.IsValid()) {
        std::cerr << "[failure-propagation] failed to enqueue test jobs\n";
        jobs.Stop(JobSystemStopMode::Drain);
        return false;
    }

    const bool okSuccess = ok.WaitForSuccess();
    const bool failSuccess = fail.WaitForSuccess();
    const bool ok2Success = ok2.WaitForSuccess();

    std::vector<JobHandle> group;
    group.push_back(ok);
    group.push_back(fail);
    group.push_back(ok2);
    const bool groupSuccess = WaitGroupForSuccess(group);

    jobs.Stop(JobSystemStopMode::Drain);

    if (!okSuccess || !ok2Success) {
        std::cerr << "[failure-propagation] successful tasks unexpectedly failed\n";
        return false;
    }
    if (failSuccess) {
        std::cerr << "[failure-propagation] failing task unexpectedly succeeded\n";
        return false;
    }
    if (groupSuccess) {
        std::cerr << "[failure-propagation] group success ignored failing handle\n";
        return false;
    }

    return true;
}

bool TestDependencyChainCancellation() {
    JobSystem jobs;
    JobSystemConfig config;
    config.workerCount = 1U;
    if (!jobs.Start(config)) {
        std::cerr << "[dependency-cancel] failed to start job system\n";
        return false;
    }

    std::atomic<bool> releaseBlocker(false);
    std::atomic<int> aExecuted(0);
    std::atomic<int> bExecuted(0);
    std::atomic<int> cExecuted(0);

    JobHandle blocker = jobs.Enqueue([&] {
        while (!releaseBlocker.load()) {
            std::this_thread::yield();
        }
    });
    if (!blocker.IsValid()) {
        std::cerr << "[dependency-cancel] failed to enqueue blocker\n";
        jobs.Stop(JobSystemStopMode::Drain);
        return false;
    }

    std::vector<JobHandle> depA;
    depA.push_back(blocker);
    JobHandle a = jobs.EnqueueAfter(depA, [&] {
        aExecuted.fetch_add(1);
    });

    std::vector<JobHandle> depB;
    depB.push_back(a);
    JobHandle b = jobs.EnqueueAfter(depB, [&] {
        bExecuted.fetch_add(1);
    });

    std::vector<JobHandle> depC;
    depC.push_back(b);
    JobHandle c = jobs.EnqueueAfter(depC, [&] {
        cExecuted.fetch_add(1);
    });

    if (!a.IsValid() || !b.IsValid() || !c.IsValid()) {
        std::cerr << "[dependency-cancel] failed to enqueue dependency chain\n";
        releaseBlocker.store(true);
        jobs.Stop(JobSystemStopMode::Drain);
        return false;
    }

    std::vector<JobHandle> chain;
    chain.push_back(a);
    chain.push_back(b);
    chain.push_back(c);

    std::atomic<bool> stopInvoked(false);
    std::thread releaser([&releaseBlocker, &stopInvoked] {
        while (!stopInvoked.load()) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        releaseBlocker.store(true);
    });

    stopInvoked.store(true);
    jobs.Stop(JobSystemStopMode::CancelPending);
    releaser.join();

    const bool chainReady = WaitUntilAllReady(chain, std::chrono::milliseconds(2000));
    const bool blockerSucceeded = blocker.WaitForSuccess();

    if (!chainReady) {
        std::cerr << "[dependency-cancel] timeout waiting for canceled chain\n";
        return false;
    }
    if (!blockerSucceeded) {
        std::cerr << "[dependency-cancel] blocker did not complete successfully\n";
        return false;
    }
    if ((aExecuted.load() != 0) || (bExecuted.load() != 0) || (cExecuted.load() != 0)) {
        std::cerr << "[dependency-cancel] chain executed unexpectedly: a="
                  << aExecuted.load() << " b=" << bExecuted.load()
                  << " c=" << cExecuted.load() << "\n";
        return false;
    }
    if (a.WaitForSuccess() || b.WaitForSuccess() || c.WaitForSuccess()) {
        std::cerr << "[dependency-cancel] canceled dependency handle reported success\n";
        return false;
    }

    return true;
}

// ============================================================================
// Animus-Specific Tests
// ============================================================================

bool TestPriorityOrdering() {
    JobSystem jobs;
    JobSystemConfig config;
    config.workerCount = 1U;
    if (!jobs.Start(config)) {
        std::cerr << "[priority] failed to start job system\n";
        return false;
    }

    std::atomic<int> executionOrder(0);
    std::vector<int> order;
    std::mutex orderMutex;

    auto recordOrder = [&](int id) {
        std::lock_guard<std::mutex> lock(orderMutex);
        order.push_back(id);
    };

    // Block the worker first
    std::atomic<bool> releaseBlocker(false);
    JobHandle blocker = jobs.Enqueue([&] {
        while (!releaseBlocker.load()) {
            std::this_thread::yield();
        }
    });

    // Queue jobs with different priorities
    JobHandle low = jobs.EnqueueWithPriority([&] { recordOrder(1); }, JobPriority::Low);
    JobHandle normal = jobs.EnqueueWithPriority([&] { recordOrder(2); }, JobPriority::Normal);
    JobHandle high = jobs.EnqueueWithPriority([&] { recordOrder(3); }, JobPriority::High);
    JobHandle urgent = jobs.EnqueueWithPriority([&] { recordOrder(4); }, JobPriority::Urgent);

    releaseBlocker.store(true);

    blocker.Wait();
    low.Wait();
    normal.Wait();
    high.Wait();
    urgent.Wait();

    jobs.Stop(JobSystemStopMode::Drain);

    // Urgent should execute first (after blocker), then High
    // Note: exact ordering depends on queue state, but urgent should be before low
    if (order.size() != 4) {
        std::cerr << "[priority] unexpected order count: " << order.size() << "\n";
        return false;
    }

    // Urgent (4) should come before Low (1)
    auto urgentIt = std::find(order.begin(), order.end(), 4);
    auto lowIt = std::find(order.begin(), order.end(), 1);
    if (urgentIt > lowIt) {
        std::cerr << "[priority] urgent did not execute before low\n";
        return false;
    }

    return true;
}

bool TestLaneStatistics() {
    JobSystem jobs;
    JobSystemConfig config;
    config.workerCount = 2U;
    if (!jobs.Start(config)) {
        std::cerr << "[lane-stats] failed to start job system\n";
        return false;
    }

    // Enqueue in different lanes
    JobHandle cognition = jobs.EnqueueInLane(JobLane::Cognition, [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });
    JobHandle tool = jobs.EnqueueInLane(JobLane::ToolCall, [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });
    JobHandle memory = jobs.EnqueueInLane(JobLane::Memory, [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });

    // Give jobs time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Check lane counts while running
    auto cognitionCount = jobs.LanePendingCount(JobLane::Cognition);
    auto toolCount = jobs.LanePendingCount(JobLane::ToolCall);
    auto memoryCount = jobs.LanePendingCount(JobLane::Memory);

    cognition.Wait();
    tool.Wait();
    memory.Wait();

    jobs.Stop(JobSystemStopMode::Drain);

    // At least some jobs should have been pending in their lanes
    if (cognitionCount + toolCount + memoryCount == 0) {
        std::cerr << "[lane-stats] no jobs tracked in lanes\n";
        return false;
    }

    return true;
}

bool TestTaskGroupBarrier() {
    JobSystem jobs;
    JobSystemConfig config;
    config.workerCount = 2U;
    if (!jobs.Start(config)) {
        std::cerr << "[barrier] failed to start job system\n";
        return false;
    }

    std::atomic<int> counter(0);
    TaskGroup group;

    // Add some jobs to the group
    for (int i = 0; i < 4; ++i) {
        group.Add(jobs.Enqueue([&counter] {
            counter.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }));
    }

    // Create a barrier that waits for all group jobs
    JobHandle barrier = jobs.EnqueueBarrier(group);

    // This job should run after barrier completes
    std::atomic<bool> afterBarrierRan(false);
    JobHandle after = jobs.EnqueueAfter({barrier}, [&afterBarrierRan] {
        afterBarrierRan.store(true);
    });

    barrier.Wait();
    after.Wait();

    jobs.Stop(JobSystemStopMode::Drain);

    if (counter.load() != 4) {
        std::cerr << "[barrier] not all pre-barrier jobs completed: " << counter.load() << "\n";
        return false;
    }
    if (!afterBarrierRan.load()) {
        std::cerr << "[barrier] post-barrier job did not run\n";
        return false;
    }

    return true;
}

bool TestBasicEnqueueAndWaitIdle() {
    JobSystem jobs;
    JobSystemConfig config;
    config.workerCount = 4U;
    if (!jobs.Start(config)) {
        std::cerr << "[basic] failed to start job system\n";
        return false;
    }

    std::atomic<int> counter(0);

    for (int i = 0; i < 100; ++i) {
        jobs.Enqueue([&counter] {
            counter.fetch_add(1);
        });
    }

    jobs.WaitIdle();

    if (counter.load() != 100) {
        std::cerr << "[basic] not all jobs completed: " << counter.load() << "\n";
        return false;
    }

    jobs.Stop(JobSystemStopMode::Drain);
    return true;
}

// ============================================================================
// Test Runner
// ============================================================================

bool RunTest(const std::string& name, bool (*fn)()) {
    const bool ok = fn();
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << "\n";
    return ok;
}

} // namespace

int main() {
    bool allPassed = true;

    std::cout << "\n=== Basic Tests (from Luminal) ===\n";
    allPassed = RunTest("cancel-pending shutdown semantics", &TestCancelPendingShutdown)
        && allPassed;
    allPassed = RunTest("failure propagation and group aggregation",
                        &TestFailurePropagationWithGroupAggregation)
        && allPassed;
    allPassed = RunTest("dependency-chain cancellation behavior", &TestDependencyChainCancellation)
        && allPassed;

    std::cout << "\n=== Animus-Specific Tests ===\n";
    allPassed = RunTest("basic enqueue and WaitIdle", &TestBasicEnqueueAndWaitIdle)
        && allPassed;
    allPassed = RunTest("priority ordering", &TestPriorityOrdering)
        && allPassed;
    allPassed = RunTest("lane statistics", &TestLaneStatistics)
        && allPassed;
    allPassed = RunTest("TaskGroup barrier", &TestTaskGroupBarrier)
        && allPassed;

    std::cout << "\n";
    return allPassed ? 0 : 1;
}

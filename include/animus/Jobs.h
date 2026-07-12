#ifndef ANIMUS_JOBS_H
#define ANIMUS_JOBS_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace animus {
namespace jobs {

// ============================================================================
// Configuration
// ============================================================================

struct JobSystemConfig {
    JobSystemConfig();

    std::uint32_t workerCount;      // 0 = auto-detect (hw_concurrency - 1)
    std::uint32_t maxQueuedJobs;    // 0 = unlimited
    bool validateCyclesOnSubmit;    // Enable cycle detection on dependency submit
};

// ============================================================================
// Core Types
// ============================================================================

using Job = std::function<void()>;

enum class JobSystemStopMode {
    Drain = 0,          // Complete all queued jobs before stopping
    CancelPending = 1   // Cancel queued jobs immediately
};

enum class JobPriority {
    Low = 0,
    Normal = 1,
    High = 2,
    Urgent = 3
};

enum class JobLane {
    Cognition,      // LLM calls, prompt compilation
    ToolCall,       // Hook execution, tool invocations
    Memory,         // Memory read/write operations
    Hook            // External hook processes
};

// ============================================================================
// JobHandle - Future-like handle for scheduled work
// ============================================================================

class JobHandle {
public:
    JobHandle();

    bool IsValid() const;
    bool IsReady() const;
    void Wait() const;
    bool WaitForSuccess() const;

private:
    friend class JobSystem;
    friend class SessionJobContext;
    explicit JobHandle(const std::shared_future<void>& future);
    JobHandle(
        const std::shared_future<void>& future,
        const std::shared_ptr<void>& taskToken);

    std::shared_future<void> m_future;
    std::shared_ptr<void> m_taskToken;
};

// ============================================================================
// TaskGroup - Collection helper for fan-in/barrier patterns
// ============================================================================

class TaskGroup {
public:
    TaskGroup();

    void Add(const JobHandle& handle);
    void Clear();
    std::size_t Size() const;
    bool Empty() const;

private:
    friend class JobSystem;
    std::vector<JobHandle> m_handles;
};

// ============================================================================
// Diagnostics
// ============================================================================

struct TaskGraphDiagnostics {
    bool cycleDetected;
    std::uint64_t nodesVisited;
    std::uint64_t edgesVisited;
    std::string detail;
};

// ============================================================================
// Session Job Context - For session-scoped job management
// ============================================================================

class SessionJobContext {
public:
    SessionJobContext();
    ~SessionJobContext();

    // Attach a job to this session (for cancellation tracking)
    void Attach(const JobHandle& handle);

    // Cancel all jobs attached to this session
    void CancelAll();

    // Get count of pending jobs
    std::size_t PendingCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ============================================================================
// JobSystem - Worker pool with dependency scheduling
// ============================================================================

class JobSystem {
public:
    JobSystem();
    ~JobSystem();

    // Disable copy
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Lifecycle
    bool Start(const JobSystemConfig& config = JobSystemConfig());
    void Stop(JobSystemStopMode mode = JobSystemStopMode::Drain);

    // ----- Basic Scheduling -----
    JobHandle Enqueue(Job job);
    JobHandle EnqueueAfter(const std::vector<JobHandle>& dependencies, Job job);
    JobHandle EnqueueFanIn(const TaskGroup& dependencies, Job job);
    JobHandle EnqueueBarrier(const TaskGroup& dependencies);

    // ----- Animus Extensions -----

    // Priority-aware scheduling
    JobHandle EnqueueWithPriority(Job job, JobPriority priority);

    // Lane-specific scheduling (for work type separation)
    JobHandle EnqueueInLane(JobLane lane, Job job);

    // Combined: lane + priority
    JobHandle EnqueueInLaneWithPriority(JobLane lane, JobPriority priority, Job job);

    // ----- Waiting -----
    void Wait(const JobHandle& handle) const;
    void Wait(const TaskGroup& group) const;
    void WaitIdle() const;

    // ----- Diagnostics -----
    TaskGraphDiagnostics DiagnoseCycles(const TaskGroup& group) const;

    // ----- Stats -----
    std::uint32_t WorkerCount() const;
    std::uint64_t SubmittedJobs() const;
    std::uint64_t CompletedJobs() const;
    std::uint64_t PendingJobs() const;

    // ----- Per-Lane Stats -----
    std::uint64_t LanePendingCount(JobLane lane) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace jobs
} // namespace animus

#endif // ANIMUS_JOBS_H

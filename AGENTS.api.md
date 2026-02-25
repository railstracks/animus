# AGENTS.api.md — Animus API Surface (Agent-facing)

> Status: WIP. This file is intended as the agent-facing reference of **public headers**.
> 
> For the Job system, treat `include/animus/Jobs.h` as the source of truth.

---

## animus::jobs — Job System

**Header:** `#include <animus/Jobs.h>`

**Goal:** Small, dependency-aware job system (thread pool + work-stealing queues) used to run parallel work across Animus subsystems.

### Thread-safety (current behavior)

- `JobSystem` is designed to be called from multiple threads.
- `Enqueue*()` may **block** when `JobSystemConfig::maxQueuedJobs > 0` and the queues are full.
- `Stop()` is safe to call even if workers are running; it joins worker threads.

### JobSystemConfig

```cpp
struct JobSystemConfig {
    JobSystemConfig();

    std::uint32_t workerCount;      // 0 = auto-detect (hw_concurrency - 1)
    std::uint32_t maxQueuedJobs;    // 0 = unlimited
    bool validateCyclesOnSubmit;    // Enable cycle detection on dependency submit
};
```

- `JobSystemConfig()`
  - Defaults:
    - `workerCount = 0` (auto)
    - `maxQueuedJobs = 0` (unlimited)
    - `validateCyclesOnSubmit = false`

### Core Types

```cpp
using Job = std::function<void()>;
```

A unit of work. If the callable is empty (`!job`), enqueue calls return an invalid handle.

#### JobSystemStopMode

```cpp
enum class JobSystemStopMode {
    Drain = 0,
    CancelPending = 1
};
```

- `Drain`
  - Stops the worker pool after the currently queued work is drained.
  - Note: jobs that have not yet become runnable due to unmet dependencies may fail to schedule once stop is requested.
- `CancelPending`
  - Cancels **queued** jobs immediately and stops workers.
  - Does **not** forcibly stop jobs that are already executing.

#### JobPriority

```cpp
enum class JobPriority { Low, Normal, High, Urgent };
```

Implementation detail: `High` and `Urgent` jobs are inserted at the front of the selected worker queue.

#### JobLane

```cpp
enum class JobLane { Cognition, ToolCall, Memory, Hook };
```

Used for coarse work-type separation + per-lane pending counters.

### JobHandle

Future-like handle returned by enqueue calls.

```cpp
class JobHandle {
public:
    JobHandle();

    bool IsValid() const;
    bool IsReady() const;
    void Wait() const;
    bool WaitForSuccess() const;
};
```

- `JobHandle()`
  - Constructs an invalid handle.

- `bool IsValid() const`
  - `true` if the underlying `std::shared_future<void>` is valid.

- `bool IsReady() const`
  - Non-blocking readiness check.

- `void Wait() const`
  - Blocks until the job completes (or fails/cancels).

- `bool WaitForSuccess() const`
  - Waits and returns `true` if the job completed successfully.
  - Returns `false` if:
    - the handle is invalid, or
    - the job threw an exception, or
    - the job was canceled (cancellation is delivered as an exception).

### TaskGroup

Convenience container for fan-in and barriers.

```cpp
class TaskGroup {
public:
    TaskGroup();

    void Add(const JobHandle& handle);
    void Clear();
    std::size_t Size() const;
    bool Empty() const;
};
```

- `Add()` ignores invalid handles.

### TaskGraphDiagnostics

```cpp
struct TaskGraphDiagnostics {
    bool cycleDetected;
    std::uint64_t nodesVisited;
    std::uint64_t edgesVisited;
    std::string detail;
};
```

Returned by cycle diagnostics.

### SessionJobContext (placeholder / partial)

```cpp
class SessionJobContext {
public:
    SessionJobContext();
    ~SessionJobContext();

    void Attach(const JobHandle& handle);
    void CancelAll();
    std::size_t PendingCount() const;
};
```

Current behavior notes (per `src/core/Jobs.cpp`):
- `Attach()` currently only increments an internal pending counter when a handle carries a task token.
- `CancelAll()` currently resets the internal counter; it does **not** yet cancel jobs in `JobSystem`.
- This is explicitly marked TODO in the implementation and will require the JobSystem to maintain a session→jobs mapping.

### JobSystem

```cpp
class JobSystem {
public:
    JobSystem();
    ~JobSystem();

    bool Start(const JobSystemConfig& config = JobSystemConfig());
    void Stop(JobSystemStopMode mode = JobSystemStopMode::Drain);

    JobHandle Enqueue(Job job);
    JobHandle EnqueueAfter(const std::vector<JobHandle>& dependencies, Job job);
    JobHandle EnqueueFanIn(const TaskGroup& dependencies, Job job);
    JobHandle EnqueueBarrier(const TaskGroup& dependencies);

    JobHandle EnqueueWithPriority(Job job, JobPriority priority);
    JobHandle EnqueueInLane(JobLane lane, Job job);
    JobHandle EnqueueInLaneWithPriority(JobLane lane, JobPriority priority, Job job);

    void Wait(const JobHandle& handle) const;
    void Wait(const TaskGroup& group) const;
    void WaitIdle() const;

    TaskGraphDiagnostics DiagnoseCycles(const TaskGroup& group) const;

    std::uint32_t WorkerCount() const;
    std::uint64_t SubmittedJobs() const;
    std::uint64_t CompletedJobs() const;
    std::uint64_t PendingJobs() const;

    std::uint64_t LanePendingCount(JobLane lane) const;
};
```

#### Lifecycle

- `JobSystem()` / `~JobSystem()`
  - Destructor calls `Stop()` (drain mode).

- `bool Start(const JobSystemConfig& config = JobSystemConfig())`
  - Starts worker threads and initializes counters.
  - If `config.workerCount == 0`, worker count defaults to `hardware_concurrency() - 1` (minimum 1).
  - Returns `false` if worker startup throws.
  - Calling `Start()` while already running returns `true` (no-op).

- `void Stop(JobSystemStopMode mode = Drain)`
  - Signals stop and joins all worker threads.
  - `Drain`: workers exit once the queues are empty.
  - `CancelPending`: queued tasks are removed and settled with an exception (`"job canceled during shutdown"`).

#### Scheduling

- `JobHandle Enqueue(Job job)`
  - Equivalent to `EnqueueWithPriority(job, JobPriority::Normal)` in the default lane (`JobLane::Cognition`).

- `JobHandle EnqueueAfter(const std::vector<JobHandle>& dependencies, Job job)`
  - Schedules `job` after all dependency handles complete.
  - Returns an invalid handle if:
    - `dependencies` contains an invalid handle, or
    - `job` is empty, or
    - the system is not running / stopping.
  - If `JobSystemConfig::validateCyclesOnSubmit == true`, does a DFS over prerequisites starting from this job and fails the job early if a cycle is detected.

- `JobHandle EnqueueFanIn(const TaskGroup& dependencies, Job job)`
  - Convenience wrapper over `EnqueueAfter(dependencies.handles, job)`.

- `JobHandle EnqueueBarrier(const TaskGroup& dependencies)`
  - Schedules an empty job after dependencies complete.

- `JobHandle EnqueueWithPriority(Job job, JobPriority priority)`
  - Convenience wrapper for the default lane + chosen priority.

- `JobHandle EnqueueInLane(JobLane lane, Job job)`
  - Convenience wrapper for `EnqueueInLaneWithPriority(lane, Normal, job)`.

- `JobHandle EnqueueInLaneWithPriority(JobLane lane, JobPriority priority, Job job)`
  - Lowest-level public enqueue.

#### Waiting

- `void Wait(const JobHandle& handle) const`
  - Blocks on a single handle.

- `void Wait(const TaskGroup& group) const`
  - Waits sequentially over the group.

- `void WaitIdle() const`
  - Blocks until `unfinishedJobs == 0` (i.e., all submitted jobs have settled).

#### Diagnostics

- `TaskGraphDiagnostics DiagnoseCycles(const TaskGroup& group) const`
  - Runs cycle detection over the prerequisite graph reachable from the group’s task tokens.
  - Note: handles that do not carry a task token are ignored.

#### Stats

- `WorkerCount()` returns the current number of worker threads.
- `SubmittedJobs()` increments when a job is accepted while running.
- `CompletedJobs()` increments when a job actually executes (jobs that settle due to scheduling failure / cancellation may not count as “completed”).
- `PendingJobs()` is the number of tasks currently queued (not including in-flight tasks).
- `LanePendingCount(lane)` is a per-lane counter decremented on successful execution; cancellations may currently skew this value.

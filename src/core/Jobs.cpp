#include "animus/Jobs.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace animus {
namespace jobs {

namespace {

thread_local int g_tlsWorkerIndex = -1;

std::uint32_t DetectDefaultWorkerCount() {
    const unsigned int hwThreads = std::thread::hardware_concurrency();
    if (hwThreads > 1U) {
        return static_cast<std::uint32_t>(hwThreads - 1U);
    }
    return 1U;
}

// Priority ordering helper
int PriorityValue(JobPriority p) {
    return static_cast<int>(p);
}

// Lane index helper
std::size_t LaneIndex(JobLane lane) {
    return static_cast<std::size_t>(lane);
}

constexpr std::size_t kLaneCount = 4;  // Number of JobLane values

} // namespace

// ============================================================================
// SessionJobContext::Impl
// ============================================================================

struct SessionJobContext::Impl {
    std::mutex mutex;
    std::unordered_set<std::uint64_t> attachedJobIds;
    std::atomic<std::size_t> pendingCount{0};
};

SessionJobContext::SessionJobContext()
    : m_impl(new Impl()) {
}

SessionJobContext::~SessionJobContext() = default;

void SessionJobContext::Attach(const JobHandle& handle) {
    if (!handle.IsValid() || !handle.m_taskToken) {
        return;
    }
    // Note: Actual cancellation requires integration with JobSystem
    // For now, just track pending count
    m_impl->pendingCount.fetch_add(1);
}

void SessionJobContext::CancelAll() {
    // TODO: Wire into JobSystem for actual cancellation
    // This requires JobSystem to maintain session->jobs mapping
    m_impl->pendingCount.store(0);
}

std::size_t SessionJobContext::PendingCount() const {
    return m_impl->pendingCount.load();
}

// ============================================================================
// JobSystem::Impl
// ============================================================================

struct JobSystem::Impl {
    struct TaskRecord;
    using TaskPtr = std::shared_ptr<TaskRecord>;

    struct WorkerQueue {
        std::deque<TaskPtr> tasks;
        std::mutex mutex;
    };

    struct TaskRecord {
        TaskRecord(std::uint64_t inId, Job&& inJob, JobPriority inPriority, JobLane inLane)
            : id(inId)
            , job(std::move(inJob))
            , future(promise.get_future().share())
            , priority(inPriority)
            , lane(inLane)
            , remainingDependencies(0U)
            , scheduled(false)
            , completed(false)
            , canceled(false) {
        }

        std::uint64_t id;
        Job job;
        std::promise<void> promise;
        std::shared_future<void> future;
        JobPriority priority;
        JobLane lane;
        std::atomic<std::uint32_t> remainingDependencies;
        std::atomic<bool> scheduled;
        std::atomic<bool> completed;
        std::atomic<bool> canceled;
        std::mutex prerequisitesMutex;
        std::vector<std::weak_ptr<TaskRecord>> prerequisites;
        std::mutex dependentsMutex;
        std::vector<std::weak_ptr<TaskRecord>> dependents;
    };

    Impl()
        : running(false)
        , stopRequested(false)
        , cancelPendingOnStop(false)
        , validateCyclesOnSubmit(false)
        , maxQueuedJobs(0U)
        , enqueueCursor(0U)
        , queuedJobs(0U)
        , activeWorkers(0U)
        , unfinishedJobs(0U)
        , submittedJobs(0U)
        , completedJobs(0U)
        , nextTaskId(1U) {
        // Initialize per-lane counters
        for (std::size_t i = 0; i < kLaneCount; ++i) {
            lanePendingCount[i].store(0U);
        }
    }

    void StartWorkers(std::uint32_t workerCount) {
        workerQueues.reserve(workerCount);
        for (std::uint32_t i = 0; i < workerCount; ++i) {
            workerQueues.push_back(std::unique_ptr<WorkerQueue>(new WorkerQueue()));
        }

        workers.reserve(workerCount);
        for (std::uint32_t i = 0; i < workerCount; ++i) {
            workers.push_back(std::thread(&Impl::WorkerLoop, this, i));
        }
    }

    std::size_t SelectQueueForEnqueueLocked() {
        if (workerQueues.empty()) {
            return 0U;
        }

        if ((g_tlsWorkerIndex >= 0)
            && (static_cast<std::size_t>(g_tlsWorkerIndex) < workerQueues.size())) {
            return static_cast<std::size_t>(g_tlsWorkerIndex);
        }

        const std::size_t queueIndex =
            static_cast<std::size_t>(enqueueCursor % workerQueues.size());
        ++enqueueCursor;
        return queueIndex;
    }

    bool QueueReadyTask(const TaskPtr& task, bool enforceQueueLimit) {
        {
            std::unique_lock<std::mutex> lock(stateMutex);

            if (!running || stopRequested.load() || workerQueues.empty()) {
                return false;
            }

            if (enforceQueueLimit && (maxQueuedJobs > 0U)) {
                queueSpaceAvailable.wait(lock, [this] {
                    return stopRequested.load()
                        || (queuedJobs.load() < maxQueuedJobs);
                });
            }

            if (stopRequested.load()) {
                return false;
            }

            const std::size_t queueIndex = SelectQueueForEnqueueLocked();
            {
                WorkerQueue& queue = *workerQueues[queueIndex];
                std::lock_guard<std::mutex> queueLock(queue.mutex);

                // Priority insertion: higher priority tasks go to front
                if (PriorityValue(task->priority) >= PriorityValue(JobPriority::High)) {
                    queue.tasks.push_front(task);
                } else if ((g_tlsWorkerIndex >= 0)
                    && (static_cast<std::size_t>(g_tlsWorkerIndex) == queueIndex)) {
                    queue.tasks.push_front(task);
                } else {
                    queue.tasks.push_back(task);
                }
            }

            queuedJobs.fetch_add(1U);
            lanePendingCount[LaneIndex(task->lane)].fetch_add(1U);
        }

        workAvailable.notify_one();
        return true;
    }

    void TryScheduleIfReady(const TaskPtr& task, bool enforceQueueLimit) {
        if (task->remainingDependencies.load() != 0U) {
            return;
        }

        bool expected = false;
        if (!task->scheduled.compare_exchange_strong(expected, true)) {
            return;
        }

        if (!QueueReadyTask(task, enforceQueueLimit)) {
            SettleTask(
                task,
                std::make_exception_ptr(
                    std::runtime_error("task scheduling failed")),
                false);
        }
    }

    bool RegisterDependency(
        const TaskPtr& task,
        const std::shared_ptr<void>& dependencyToken,
        const JobHandle& dependencyHandle) {
        TaskPtr dependencyTask;
        if (dependencyToken) {
            dependencyTask = std::static_pointer_cast<TaskRecord>(dependencyToken);
        }

        if (!dependencyTask) {
            if (!dependencyHandle.IsReady()) {
                dependencyHandle.Wait();
            }
            return true;
        }

        while (true) {
            if (dependencyTask->completed.load(std::memory_order_acquire)) {
                return true;
            }

            task->remainingDependencies.fetch_add(1U, std::memory_order_relaxed);

            bool registered = false;
            {
                std::lock_guard<std::mutex> lock(dependencyTask->dependentsMutex);
                if (!dependencyTask->completed.load(std::memory_order_acquire)) {
                    dependencyTask->dependents.push_back(
                        std::weak_ptr<TaskRecord>(task));
                    registered = true;
                }
            }

            if (registered) {
                {
                    std::lock_guard<std::mutex> taskLock(task->prerequisitesMutex);
                    task->prerequisites.push_back(
                        std::weak_ptr<TaskRecord>(dependencyTask));
                }
                return true;
            }

            task->remainingDependencies.fetch_sub(1U, std::memory_order_relaxed);
        }
    }

    void ResolveDependency(const TaskPtr& task) {
        const std::uint32_t previous =
            task->remainingDependencies.fetch_sub(1U, std::memory_order_relaxed);
        if (previous == 1U) {
            TryScheduleIfReady(task, false);
        }
    }

    void NotifyDependents(const TaskPtr& task) {
        std::vector<std::weak_ptr<TaskRecord>> dependents;
        {
            std::lock_guard<std::mutex> lock(task->dependentsMutex);
            dependents.swap(task->dependents);
        }

        for (std::size_t i = 0; i < dependents.size(); ++i) {
            TaskPtr dependent = dependents[i].lock();
            if (dependent) {
                ResolveDependency(dependent);
            }
        }
    }

    std::vector<TaskPtr> CollectPrerequisites(const TaskPtr& task) const {
        std::vector<TaskPtr> prerequisites;
        std::lock_guard<std::mutex> lock(task->prerequisitesMutex);
        prerequisites.reserve(task->prerequisites.size());

        for (std::size_t i = 0; i < task->prerequisites.size(); ++i) {
            TaskPtr prerequisite = task->prerequisites[i].lock();
            if (prerequisite) {
                prerequisites.push_back(prerequisite);
            }
        }

        return prerequisites;
    }

    TaskGraphDiagnostics DiagnoseCyclesFromTasks(
        const std::vector<TaskPtr>& roots) const {
        TaskGraphDiagnostics diagnostics;
        diagnostics.cycleDetected = false;
        diagnostics.nodesVisited = 0U;
        diagnostics.edgesVisited = 0U;
        diagnostics.detail = "ok";

        std::unordered_map<std::uint64_t, int> visitState;
        std::vector<std::uint64_t> recursionStack;

        std::function<bool(const TaskPtr&)> dfs = [&](const TaskPtr& node) -> bool {
            if (!node) {
                return false;
            }

            const int state = visitState[node->id];
            if (state == 1) {
                diagnostics.cycleDetected = true;
                diagnostics.detail = "cycle detected around task id "
                    + std::to_string(node->id);
                return true;
            }
            if (state == 2) {
                return false;
            }

            visitState[node->id] = 1;
            recursionStack.push_back(node->id);
            diagnostics.nodesVisited += 1U;

            const std::vector<TaskPtr> prerequisites = CollectPrerequisites(node);
            for (std::size_t i = 0; i < prerequisites.size(); ++i) {
                diagnostics.edgesVisited += 1U;
                if (dfs(prerequisites[i])) {
                    return true;
                }
            }

            visitState[node->id] = 2;
            recursionStack.pop_back();
            return false;
        };

        for (std::size_t i = 0; i < roots.size(); ++i) {
            if (dfs(roots[i])) {
                break;
            }
        }

        return diagnostics;
    }

    TaskGraphDiagnostics DiagnoseCyclesFromHandles(
        const std::vector<JobHandle>& handles) const {
        std::vector<TaskPtr> roots;
        roots.reserve(handles.size());
        for (std::size_t i = 0; i < handles.size(); ++i) {
            if (!handles[i].m_taskToken) {
                continue;
            }

            TaskPtr task =
                std::static_pointer_cast<TaskRecord>(handles[i].m_taskToken);
            if (task) {
                roots.push_back(task);
            }
        }

        return DiagnoseCyclesFromTasks(roots);
    }

    void SettleTask(
        const TaskPtr& task,
        const std::exception_ptr& error,
        bool executed) {
        if (error) {
            task->promise.set_exception(error);
        } else {
            task->promise.set_value();
        }

        task->completed.store(true, std::memory_order_release);

        if (executed) {
            completedJobs.fetch_add(1U);
            lanePendingCount[LaneIndex(task->lane)].fetch_sub(1U);
        }

        NotifyDependents(task);

        const std::uint64_t unfinishedAfter = unfinishedJobs.fetch_sub(1U) - 1U;
        if (unfinishedAfter == 0U) {
            idle.notify_all();
        }
    }

    bool TryPopFromQueue(
        std::size_t queueIndex,
        bool fromFront,
        TaskPtr& outTask) {
        WorkerQueue& queue = *workerQueues[queueIndex];
        std::lock_guard<std::mutex> lock(queue.mutex);
        if (queue.tasks.empty()) {
            return false;
        }

        if (fromFront) {
            outTask = queue.tasks.front();
            queue.tasks.pop_front();
        } else {
            outTask = queue.tasks.back();
            queue.tasks.pop_back();
        }

        return true;
    }

    bool TryPopLocal(std::size_t workerIndex, TaskPtr& outTask) {
        return TryPopFromQueue(workerIndex, true, outTask);
    }

    bool TrySteal(std::size_t workerIndex, TaskPtr& outTask) {
        if (workerQueues.size() <= 1U) {
            return false;
        }

        for (std::size_t offset = 1U; offset < workerQueues.size(); ++offset) {
            const std::size_t victim = (workerIndex + offset) % workerQueues.size();
            if (TryPopFromQueue(victim, false, outTask)) {
                return true;
            }
        }

        return false;
    }

    bool ClaimTask(std::size_t workerIndex, TaskPtr& outTask) {
        if (cancelPendingOnStop.load()) {
            return false;
        }

        if (!TryPopLocal(workerIndex, outTask) && !TrySteal(workerIndex, outTask)) {
            return false;
        }

        queuedJobs.fetch_sub(1U);
        activeWorkers.fetch_add(1U);

        if (maxQueuedJobs > 0U) {
            queueSpaceAvailable.notify_one();
        }

        return true;
    }

    void ExecuteClaimedTask(const TaskPtr& task) {
        // Check for cancellation
        if (task->canceled.load()) {
            activeWorkers.fetch_sub(1U);
            SettleTask(
                task,
                std::make_exception_ptr(std::runtime_error("job canceled")),
                false);
            return;
        }

        std::exception_ptr error;
        try {
            task->job();
        } catch (...) {
            error = std::current_exception();
        }

        activeWorkers.fetch_sub(1U);
        SettleTask(task, error, true);
    }

    void WorkerLoop(std::size_t workerIndex) {
        g_tlsWorkerIndex = static_cast<int>(workerIndex);

        while (true) {
            TaskPtr task;

            if (ClaimTask(workerIndex, task)) {
                ExecuteClaimedTask(task);
                continue;
            }

            {
                std::unique_lock<std::mutex> lock(stateMutex);
                workAvailable.wait(lock, [this] {
                    return stopRequested.load() || (queuedJobs.load() > 0U);
                });

                if (stopRequested.load() && (queuedJobs.load() == 0U)) {
                    g_tlsWorkerIndex = -1;
                    return;
                }
            }
        }
    }

    void CancelQueuedTasks() {
        std::vector<TaskPtr> canceled;

        for (std::size_t i = 0; i < workerQueues.size(); ++i) {
            WorkerQueue& queue = *workerQueues[i];
            std::lock_guard<std::mutex> queueLock(queue.mutex);
            while (!queue.tasks.empty()) {
                canceled.push_back(queue.tasks.front());
                queue.tasks.pop_front();
            }
        }

        if (!canceled.empty()) {
            queuedJobs.fetch_sub(static_cast<std::uint64_t>(canceled.size()));
        }

        if (maxQueuedJobs > 0U) {
            queueSpaceAvailable.notify_all();
        }

        const std::exception_ptr cancelError =
            std::make_exception_ptr(std::runtime_error("job canceled during shutdown"));
        for (std::size_t i = 0; i < canceled.size(); ++i) {
            canceled[i]->canceled.store(true);
            SettleTask(canceled[i], cancelError, false);
        }
    }

    mutable std::mutex stateMutex;
    std::condition_variable workAvailable;
    std::condition_variable queueSpaceAvailable;
    mutable std::condition_variable idle;
    std::vector<std::thread> workers;
    std::vector<std::unique_ptr<WorkerQueue>> workerQueues;
    bool running;
    std::atomic<bool> stopRequested;
    std::atomic<bool> cancelPendingOnStop;
    bool validateCyclesOnSubmit;
    std::uint32_t maxQueuedJobs;
    std::uint32_t enqueueCursor;
    std::atomic<std::uint64_t> queuedJobs;
    std::atomic<std::uint64_t> activeWorkers;
    std::atomic<std::uint64_t> unfinishedJobs;
    std::atomic<std::uint64_t> submittedJobs;
    std::atomic<std::uint64_t> completedJobs;
    std::atomic<std::uint64_t> nextTaskId;
    std::atomic<std::uint64_t> lanePendingCount[kLaneCount];
};

// ============================================================================
// Config and Handle Implementation
// ============================================================================

JobSystemConfig::JobSystemConfig()
    : workerCount(0U)
    , maxQueuedJobs(0U)
    , validateCyclesOnSubmit(false) {
}

JobHandle::JobHandle() {
}

JobHandle::JobHandle(const std::shared_future<void>& future)
    : m_future(future) {
}

JobHandle::JobHandle(
    const std::shared_future<void>& future,
    const std::shared_ptr<void>& taskToken)
    : m_future(future)
    , m_taskToken(taskToken) {
}

bool JobHandle::IsValid() const {
    return m_future.valid();
}

bool JobHandle::IsReady() const {
    if (!m_future.valid()) {
        return false;
    }

    return m_future.wait_for(std::chrono::milliseconds(0))
        == std::future_status::ready;
}

void JobHandle::Wait() const {
    if (m_future.valid()) {
        m_future.wait();
    }
}

bool JobHandle::WaitForSuccess() const {
    if (!m_future.valid()) {
        return false;
    }

    try {
        m_future.get();
    } catch (...) {
        return false;
    }

    return true;
}

// ============================================================================
// TaskGroup Implementation
// ============================================================================

TaskGroup::TaskGroup() {
}

void TaskGroup::Add(const JobHandle& handle) {
    if (handle.IsValid()) {
        m_handles.push_back(handle);
    }
}

void TaskGroup::Clear() {
    m_handles.clear();
}

std::size_t TaskGroup::Size() const {
    return m_handles.size();
}

bool TaskGroup::Empty() const {
    return m_handles.empty();
}

// ============================================================================
// JobSystem Implementation
// ============================================================================

JobSystem::JobSystem()
    : m_impl(new Impl()) {
}

JobSystem::~JobSystem() {
    Stop();
}

bool JobSystem::Start(const JobSystemConfig& config) {
    {
        std::lock_guard<std::mutex> lock(m_impl->stateMutex);
        if (m_impl->running) {
            return true;
        }

        m_impl->maxQueuedJobs = config.maxQueuedJobs;
        m_impl->validateCyclesOnSubmit = config.validateCyclesOnSubmit;
        m_impl->enqueueCursor = 0U;
        m_impl->running = true;
        m_impl->stopRequested.store(false);
        m_impl->cancelPendingOnStop.store(false);
        m_impl->queuedJobs.store(0U);
        m_impl->activeWorkers.store(0U);
        m_impl->unfinishedJobs.store(0U);
        m_impl->submittedJobs.store(0U);
        m_impl->completedJobs.store(0U);
        m_impl->nextTaskId.store(1U);

        for (std::size_t i = 0; i < kLaneCount; ++i) {
            m_impl->lanePendingCount[i].store(0U);
        }
    }

    const std::uint32_t workerCount =
        (config.workerCount == 0U) ? DetectDefaultWorkerCount() : config.workerCount;

    try {
        m_impl->StartWorkers(workerCount);
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(m_impl->stateMutex);
            m_impl->stopRequested.store(true);
        }

        m_impl->workAvailable.notify_all();
        m_impl->queueSpaceAvailable.notify_all();
        m_impl->idle.notify_all();

        for (std::size_t i = 0; i < m_impl->workers.size(); ++i) {
            if (m_impl->workers[i].joinable()) {
                m_impl->workers[i].join();
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_impl->stateMutex);
            m_impl->workers.clear();
            m_impl->workerQueues.clear();
            m_impl->running = false;
            m_impl->stopRequested.store(false);
            m_impl->cancelPendingOnStop.store(false);
            m_impl->queuedJobs.store(0U);
            m_impl->activeWorkers.store(0U);
            m_impl->unfinishedJobs.store(0U);
        }

        return false;
    }

    return true;
}

void JobSystem::Stop(JobSystemStopMode mode) {
    const bool cancelPending = (mode == JobSystemStopMode::CancelPending);
    {
        std::lock_guard<std::mutex> lock(m_impl->stateMutex);
        if (!m_impl->running) {
            return;
        }

        m_impl->stopRequested.store(true);
        m_impl->cancelPendingOnStop.store(cancelPending);
    }

    if (cancelPending) {
        m_impl->CancelQueuedTasks();
    }

    m_impl->workAvailable.notify_all();
    m_impl->queueSpaceAvailable.notify_all();
    m_impl->idle.notify_all();

    for (std::size_t i = 0; i < m_impl->workers.size(); ++i) {
        if (m_impl->workers[i].joinable()) {
            m_impl->workers[i].join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_impl->stateMutex);
        m_impl->workers.clear();
        m_impl->workerQueues.clear();
        m_impl->queuedJobs.store(0U);
        m_impl->activeWorkers.store(0U);
        m_impl->unfinishedJobs.store(0U);
        m_impl->running = false;
        m_impl->stopRequested.store(false);
        m_impl->cancelPendingOnStop.store(false);
    }
}

// ----- Basic Scheduling -----

JobHandle JobSystem::Enqueue(Job job) {
    return EnqueueWithPriority(std::move(job), JobPriority::Normal);
}

JobHandle JobSystem::EnqueueAfter(
    const std::vector<JobHandle>& dependencies,
    Job job) {
    if (dependencies.empty()) {
        return Enqueue(std::move(job));
    }

    if (!job) {
        return JobHandle();
    }

    for (std::size_t i = 0; i < dependencies.size(); ++i) {
        if (!dependencies[i].IsValid()) {
            return JobHandle();
        }
    }

    const std::uint64_t taskId = m_impl->nextTaskId.fetch_add(1U);
    Impl::TaskPtr task(new Impl::TaskRecord(
        taskId, std::move(job), JobPriority::Normal, JobLane::Cognition));

    {
        std::lock_guard<std::mutex> lock(m_impl->stateMutex);
        if (!m_impl->running || m_impl->stopRequested.load()) {
            return JobHandle();
        }

        m_impl->submittedJobs.fetch_add(1U);
        m_impl->unfinishedJobs.fetch_add(1U);
    }

    for (std::size_t i = 0; i < dependencies.size(); ++i) {
        if (!m_impl->RegisterDependency(
                task,
                dependencies[i].m_taskToken,
                dependencies[i])) {
            m_impl->SettleTask(
                task,
                std::make_exception_ptr(
                    std::runtime_error("failed to register task dependency")),
                false);
            return JobHandle();
        }
    }

    if (m_impl->validateCyclesOnSubmit) {
        const std::vector<Impl::TaskPtr> cycleRoots(1U, task);
        const TaskGraphDiagnostics diagnostics =
            m_impl->DiagnoseCyclesFromTasks(cycleRoots);
        if (diagnostics.cycleDetected) {
            m_impl->SettleTask(
                task,
                std::make_exception_ptr(
                    std::runtime_error("dependency cycle: " + diagnostics.detail)),
                false);
            return JobHandle();
        }
    }

    m_impl->TryScheduleIfReady(task, true);
    return JobHandle(task->future, std::static_pointer_cast<void>(task));
}

JobHandle JobSystem::EnqueueFanIn(const TaskGroup& dependencies, Job job) {
    return EnqueueAfter(dependencies.m_handles, std::move(job));
}

JobHandle JobSystem::EnqueueBarrier(const TaskGroup& dependencies) {
    return EnqueueAfter(dependencies.m_handles, [] {});
}

// ----- Animus Extensions -----

JobHandle JobSystem::EnqueueWithPriority(Job job, JobPriority priority) {
    return EnqueueInLaneWithPriority(JobLane::Cognition, priority, std::move(job));
}

JobHandle JobSystem::EnqueueInLane(JobLane lane, Job job) {
    return EnqueueInLaneWithPriority(lane, JobPriority::Normal, std::move(job));
}

JobHandle JobSystem::EnqueueInLaneWithPriority(JobLane lane, JobPriority priority, Job job) {
    if (!job) {
        return JobHandle();
    }

    const std::uint64_t taskId = m_impl->nextTaskId.fetch_add(1U);
    Impl::TaskPtr task(new Impl::TaskRecord(taskId, std::move(job), priority, lane));

    {
        std::lock_guard<std::mutex> lock(m_impl->stateMutex);
        if (!m_impl->running || m_impl->stopRequested.load()) {
            return JobHandle();
        }

        m_impl->submittedJobs.fetch_add(1U);
        m_impl->unfinishedJobs.fetch_add(1U);
    }

    m_impl->TryScheduleIfReady(task, true);
    return JobHandle(task->future, std::static_pointer_cast<void>(task));
}

// ----- Waiting -----

void JobSystem::Wait(const JobHandle& handle) const {
    handle.Wait();
}

void JobSystem::Wait(const TaskGroup& group) const {
    for (std::size_t i = 0; i < group.m_handles.size(); ++i) {
        Wait(group.m_handles[i]);
    }
}

void JobSystem::WaitIdle() const {
    std::unique_lock<std::mutex> lock(m_impl->stateMutex);
    m_impl->idle.wait(lock, [this] {
        return m_impl->unfinishedJobs.load() == 0U;
    });
}

// ----- Diagnostics -----

TaskGraphDiagnostics JobSystem::DiagnoseCycles(const TaskGroup& group) const {
    return m_impl->DiagnoseCyclesFromHandles(group.m_handles);
}

// ----- Stats -----

std::uint32_t JobSystem::WorkerCount() const {
    std::lock_guard<std::mutex> lock(m_impl->stateMutex);
    return static_cast<std::uint32_t>(m_impl->workers.size());
}

std::uint64_t JobSystem::SubmittedJobs() const {
    return m_impl->submittedJobs.load();
}

std::uint64_t JobSystem::CompletedJobs() const {
    return m_impl->completedJobs.load();
}

std::uint64_t JobSystem::PendingJobs() const {
    return m_impl->queuedJobs.load();
}

std::uint64_t JobSystem::LanePendingCount(JobLane lane) const {
    return m_impl->lanePendingCount[LaneIndex(lane)].load();
}

} // namespace jobs
} // namespace animus

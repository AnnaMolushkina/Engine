#include "Jobs/JobSystem.h"
#include "Logger.h"
#include "Profiler.h"

#include <TaskScheduler.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <exception>

namespace detail {

struct AsyncTask final : enki::ITaskSet {
    std::function<void()> fn;
    JobState* state = nullptr;              // JobState владеет задачей, поэтому жив всё время её выполнения
    std::atomic<int64_t>* pending = nullptr;
    const char* name = "Job";

    void ExecuteRange(enki::TaskSetPartition, uint32_t) override {
        {
            ZoneScopedN("Job");
            ZoneText(name, std::strlen(name));
            try {
                fn();
            }
            catch (const std::exception& e) {
                Logger::Error(std::string("Job '") + name + "' threw: " + e.what());
            }
            catch (...) {
                Logger::Error(std::string("Job '") + name + "' threw an unknown exception");
            }
        }
        fn = nullptr; // сразу освобождаем всё, что захватила лямбда
        state->done.store(true, std::memory_order_release);
        pending->fetch_sub(1, std::memory_order_relaxed);
    }
};

} // namespace detail

JobState::JobState() = default;
JobState::~JobState() = default;

namespace {

enki::TaskPriority ToEnki(JobPriority priority) {
    switch (priority) {
    case JobPriority::High:   return enki::TASK_PRIORITY_HIGH;
    case JobPriority::Normal: return enki::TASK_PRIORITY_MED;
    default:                  return enki::TASK_PRIORITY_LOW;
    }
}

#ifdef TRACY_ENABLE
// Разметка потоков пула для Tracy: имена воркеров и зоны ожидания - на трейсе видно, кто чего ждёт.
thread_local TracyCZoneCtx tWaitTaskZone;
thread_local TracyCZoneCtx tWaitSuspendZone;
thread_local TracyCZoneCtx tIdleZone;

void OnThreadStart(uint32_t threadNum) {
    char name[32];
    std::snprintf(name, sizeof(name), "Job Worker %u", threadNum);
    tracy::SetThreadName(name);
}
void OnWaitForTaskStart(uint32_t)   { TracyCZoneNC(ctx, "Wait for task", 0xC06030, 1); tWaitTaskZone = ctx; }
void OnWaitForTaskStop(uint32_t)    { TracyCZoneEnd(tWaitTaskZone); }
void OnWaitSuspendStart(uint32_t)   { TracyCZoneNC(ctx, "Wait (sleeping)", 0x904040, 1); tWaitSuspendZone = ctx; }
void OnWaitSuspendStop(uint32_t)    { TracyCZoneEnd(tWaitSuspendZone); }
void OnIdleStart(uint32_t)          { TracyCZoneNC(ctx, "Idle (no tasks)", 0x404040, 1); tIdleZone = ctx; }
void OnIdleStop(uint32_t)           { TracyCZoneEnd(tIdleZone); }
#endif

} // namespace

JobSystem::JobSystem() = default;

JobSystem::~JobSystem() {
    Shutdown();
}

bool JobSystem::Initialize(const JobSystemConfig& config) {
    if (mInitialized) return true;

    mScheduler = std::make_unique<enki::TaskScheduler>();
    enki::TaskSchedulerConfig schedulerConfig = mScheduler->GetConfig();

    const uint32_t hardwareThreads = enki::GetNumHardwareThreads();
    uint32_t workers = config.workerThreads;
    if (workers == 0) workers = hardwareThreads > 1 ? hardwareThreads - 1 : 1;
    schedulerConfig.numTaskThreadsToCreate = workers;

#ifdef TRACY_ENABLE
    tracy::SetThreadName("Main thread");
    schedulerConfig.profilerCallbacks.threadStart = &OnThreadStart;
    schedulerConfig.profilerCallbacks.waitForTaskCompleteStart = &OnWaitForTaskStart;
    schedulerConfig.profilerCallbacks.waitForTaskCompleteStop = &OnWaitForTaskStop;
    schedulerConfig.profilerCallbacks.waitForTaskCompleteSuspendStart = &OnWaitSuspendStart;
    schedulerConfig.profilerCallbacks.waitForTaskCompleteSuspendStop = &OnWaitSuspendStop;
    schedulerConfig.profilerCallbacks.waitForNewTaskSuspendStart = &OnIdleStart;
    schedulerConfig.profilerCallbacks.waitForNewTaskSuspendStop = &OnIdleStop;
#endif

    mScheduler->Initialize(schedulerConfig);

    mParallelEnabled.store(config.parallelEnabled);
    mShuttingDown.store(false);
    mInitialized = true;
    sInstance = this;

    Logger::Info("JobSystem initialized (enkiTS): " + std::to_string(workers) + " worker threads + main thread, " +
                 std::to_string(hardwareThreads) + " hardware threads, parallel " +
                 (config.parallelEnabled ? "ON" : "OFF"));
    return true;
}

void JobSystem::Shutdown() {
    if (!mInitialized) return;
    ZoneScopedN("JobSystem::Shutdown");

    mShuttingDown.store(true, std::memory_order_release);
    Logger::Info("JobSystem: shutting down, background jobs still pending: " + std::to_string(mPendingJobs.load()));

    // Дожидаемся запущенных и стоящих в очереди задач, затем останавливаем потоки
    mScheduler->WaitforAllAndShutdown();

    {
        std::lock_guard<std::mutex> lock(mInFlightMutex);
        mInFlight.clear();
    }
    size_t dropped = 0;
    {
        std::lock_guard<std::mutex> lock(mMainQueueMutex);
        dropped = mMainQueue.size();
        mMainQueue.clear();
    }
    if (dropped)
        Logger::Info("JobSystem: dropped " + std::to_string(dropped) + " main-thread callbacks on shutdown");

    mScheduler.reset();
    mInitialized = false;
    if (sInstance == this) sInstance = nullptr;
    Logger::Info("JobSystem: shutdown complete");
}

uint32_t JobSystem::ThreadCount() const {
    return mScheduler ? mScheduler->GetNumTaskThreads() : 1;
}

void JobSystem::ParallelFor(uint32_t count, uint32_t minRange, const RangeFunction& fn, const char* name) {
    if (count == 0) return;
    minRange = std::max(1u, minRange);

    // Последовательная ветка: job system выключена (замер "до") или работы меньше одного диапазона
    if (!mInitialized || !IsParallelEnabled() || count <= minRange || IsShuttingDown()) {
        ZoneScopedN("ParallelFor (serial)");
        ZoneText(name, std::strlen(name));
        fn(0, count);
        return;
    }

    ZoneScopedN("ParallelFor");
    ZoneText(name, std::strlen(name));

    enki::TaskSet task(count, [&fn, name](enki::TaskSetPartition range, uint32_t) {
        ZoneScopedN("ParallelFor range");
        ZoneText(name, std::strlen(name));
        (void)name; // без Tracy ZoneText пустой
        fn(range.start, range.end);
    });
    task.m_MinRange = minRange;
    task.m_Priority = enki::TASK_PRIORITY_HIGH;

    mScheduler->AddTaskSetToPipe(&task);
    // Главный поток тоже выполняет диапазоны, но только высокоприоритетные задачи
    mScheduler->WaitforTask(&task, enki::TASK_PRIORITY_HIGH);
}

JobHandle JobSystem::Submit(std::function<void()> job, JobPriority priority, const char* name) {
    auto state = std::make_shared<JobState>();
    JobHandle handle;
    handle.mState = state;

    if (!mInitialized || IsShuttingDown()) {
        Logger::Warning(std::string("JobSystem: job '") + name + "' dropped (job system is not running)");
        state->done.store(true);
        return handle;
    }

    if (!IsParallelEnabled()) {
        // "До": задача выполняется прямо здесь, на вызывающем потоке
        ZoneScopedN("Job (inline)");
        ZoneText(name, std::strlen(name));
        job();
        state->done.store(true);
        return handle;
    }

    assert(mScheduler->GetThreadNum() != enki::NO_THREAD_NUM &&
           "JobSystem::Submit must be called from the main thread or from a job");

    auto task = std::make_unique<detail::AsyncTask>();
    task->fn = std::move(job);
    task->state = state.get();
    task->pending = &mPendingJobs;
    task->name = name;
    task->m_Priority = ToEnki(priority);
    detail::AsyncTask* rawTask = task.get();
    state->task = std::move(task);

    mPendingJobs.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mInFlightMutex);
        mInFlight.push_back(state);
    }
    mScheduler->AddTaskSetToPipe(rawTask);
    return handle;
}

void JobSystem::Wait(const JobHandle& handle) {
    if (handle.IsDone() || !handle.mState->task || !mScheduler) return;
    ZoneScopedN("JobSystem::Wait");
    mScheduler->WaitforTask(handle.mState->task.get());
}

void JobSystem::RunOnMainThread(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(mMainQueueMutex);
    mMainQueue.push_back(std::move(fn));
}

uint32_t JobSystem::PumpMainThread(uint32_t maxTasks) {
    uint32_t executed = 0;
    while (executed < maxTasks) {
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> lock(mMainQueueMutex);
            if (mMainQueue.empty()) break;
            fn = std::move(mMainQueue.front());
            mMainQueue.pop_front();
        }
        ZoneScopedN("Main-thread job");
        fn();
        ++executed;
    }
    return executed;
}

void JobSystem::BeginFrame() {
    if (!mInitialized) return;
    ZoneScopedN("JobSystem::BeginFrame");

    size_t inFlight = 0;
    {
        std::lock_guard<std::mutex> lock(mInFlightMutex);
        mInFlight.erase(std::remove_if(mInFlight.begin(), mInFlight.end(),
                            [](const std::shared_ptr<JobState>& s) { return s->task->GetIsComplete(); }),
                        mInFlight.end());
        inFlight = mInFlight.size();
    }
    size_t mainQueue = 0;
    {
        std::lock_guard<std::mutex> lock(mMainQueueMutex);
        mainQueue = mMainQueue.size();
    }
    TracyPlot("Jobs in flight", (int64_t)inFlight);
    TracyPlot("Main-thread queue", (int64_t)mainQueue);
    (void)inFlight;
    (void)mainQueue;
}

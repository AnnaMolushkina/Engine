#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace enki { class TaskScheduler; }

// Приоритет фоновой задачи. ParallelFor кадра всегда идёт с High, а главный поток, ожидая его,
// помогает только High-задачам: долгая Low-задача (загрузка) не может "застрять" внутри кадра.
enum class JobPriority { High = 0, Normal = 1, Low = 2 };

struct JobSystemConfig {
    bool parallelEnabled = true; // false - всё выполняется на вызывающем потоке (замер "до")
    uint32_t workerThreads = 0;  // 0 - (число аппаратных потоков - 1)
};

namespace detail { struct AsyncTask; }

// Общее состояние фоновой задачи. Держит саму задачу enkiTS живой, пока на неё есть ссылки.
struct JobState {
    JobState();
    ~JobState();
    std::atomic<bool> done{ false };
    std::unique_ptr<detail::AsyncTask> task;
};

class JobHandle {
public:
    bool IsValid() const { return mState != nullptr; }
    bool IsDone() const { return !mState || mState->done.load(std::memory_order_acquire); }

private:
    friend class JobSystem;
    std::shared_ptr<JobState> mState;
};

// Job system движка: единая точка постановки фоновой работы (правило семестра:
// "один движок - одна система фоновой работы"). Внутри - пул потоков enkiTS
// (lock-free очереди задач, work stealing, приоритеты).
//
// Потоки:
//  * ParallelFor, Submit, Wait - с главного потока или изнутри задачи (ограничение enkiTS);
//  * RunOnMainThread - с любого потока; выполняется в PumpMainThread с лимитом на кадр
//    (сюда относится финализация: GPU-аплоад и т.п.).
class JobSystem {
public:
    JobSystem();
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    bool Initialize(const JobSystemConfig& config);
    // Корректный шатдаун: новые задачи больше не принимаются, живые дорабатывают
    // (долгие задачи должны проверять IsShuttingDown()), очередь главного потока сбрасывается.
    void Shutdown();

    bool IsInitialized() const { return mInitialized; }
    bool IsShuttingDown() const { return mShuttingDown.load(std::memory_order_acquire); }

    // Переключение "параллельно/последовательно" на лету (клавиша J) - для сравнения до/после
    void SetParallelEnabled(bool enabled) { mParallelEnabled.store(enabled, std::memory_order_relaxed); }
    bool IsParallelEnabled() const { return mParallelEnabled.load(std::memory_order_relaxed); }

    // Сколько потоков выполняет задачи (воркеры + главный)
    uint32_t ThreadCount() const;

    // Разбивает [0, count) на диапазоны не меньше minRange и выполняет их на пуле.
    // Блокирует вызывающий поток до завершения (он тоже выполняет диапазоны).
    using RangeFunction = std::function<void(uint32_t begin, uint32_t end)>;
    void ParallelFor(uint32_t count, uint32_t minRange, const RangeFunction& fn, const char* name = "ParallelFor");

    // Фоновая задача. name должен жить всё время выполнения (строковый литерал).
    JobHandle Submit(std::function<void()> job, JobPriority priority = JobPriority::Normal, const char* name = "Job");
    void Wait(const JobHandle& handle);

    // Очередь задач главного потока (памп)
    void RunOnMainThread(std::function<void()> fn);
    uint32_t PumpMainThread(uint32_t maxTasks);

    // Раз в кадр: убирает завершённые задачи, обновляет графики Tracy
    void BeginFrame();

    static JobSystem* Instance() { return sInstance; }

private:
    std::unique_ptr<enki::TaskScheduler> mScheduler;
    bool mInitialized = false;
    std::atomic<bool> mParallelEnabled{ true };
    std::atomic<bool> mShuttingDown{ false };
    std::atomic<int64_t> mPendingJobs{ 0 };

    std::mutex mInFlightMutex;
    std::vector<std::shared_ptr<JobState>> mInFlight;

    std::mutex mMainQueueMutex;
    std::deque<std::function<void()>> mMainQueue;

    static inline JobSystem* sInstance = nullptr;
};

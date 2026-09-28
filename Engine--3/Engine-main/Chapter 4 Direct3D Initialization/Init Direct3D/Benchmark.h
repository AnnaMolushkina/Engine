#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct EngineConfig;

// Замер по методике лекции 2: фиксированная сцена, прогрев, затем N кадров;
// метрики - медиана и p95/p99 времени кадра и отдельно время целевых зон (систем ECS).
// Результат дописывается строками в CSV, чтобы несколько прогонов складывались в одну таблицу
// (прогоны запускает tools/bench.ps1).
class Benchmark {
public:
    void Configure(const EngineConfig& config);

    bool IsEnabled() const { return mEnabled; }
    bool IsMeasuring() const { return mArmed && mWarmupLeft == 0 && !mFinished; }
    bool IsFinished() const { return mFinished; }

    // Сцена готова - начинаем прогрев
    void Arm(bool jobsEnabled, uint32_t threads, uint32_t entities);

    // Время зоны в текущем кадре (вызывает World::UpdateSystems)
    void RecordZone(const char* name, double ms);

    // Конец кадра. Возвращает true, когда замер закончен и результаты записаны.
    bool OnFrameEnd(double frameMs);

private:
    struct Series {
        std::string name;
        std::vector<double> samples;
    };

    Series& GetSeries(const char* name);
    void WriteResults();

    bool mEnabled = false;
    bool mArmed = false;
    bool mFinished = false;
    uint32_t mWarmupFrames = 0;
    uint32_t mWarmupLeft = 0;
    uint32_t mMeasureFrames = 0;
    uint32_t mMeasured = 0;

    std::string mOutput;
    std::string mLabel;
    bool mJobs = false;
    uint32_t mThreads = 0;
    uint32_t mEntities = 0;

    std::vector<Series> mSeries; // [0] - время кадра
};

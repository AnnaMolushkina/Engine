#include "Benchmark.h"
#include "ConfigManager.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>

namespace {

struct Stats {
    double median = 0, p95 = 0, p99 = 0, mean = 0, min = 0, max = 0;
};

Stats Compute(std::vector<double> values) {
    Stats s;
    if (values.empty()) return s;
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    auto percentile = [&](double p) {
        size_t rank = (size_t)std::ceil(p * (double)n); // nearest-rank
        rank = std::min(std::max<size_t>(rank, 1), n);
        return values[rank - 1];
    };
    s.median = (n % 2) ? values[n / 2] : 0.5 * (values[n / 2 - 1] + values[n / 2]);
    s.p95 = percentile(0.95);
    s.p99 = percentile(0.99);
    s.mean = std::accumulate(values.begin(), values.end(), 0.0) / (double)n;
    s.min = values.front();
    s.max = values.back();
    return s;
}

} // namespace

void Benchmark::Configure(const EngineConfig& config) {
    mEnabled = config.benchmark;
    mWarmupFrames = config.benchWarmupFrames;
    mMeasureFrames = std::max(1u, config.benchMeasureFrames);
    mOutput = config.benchOutput;
    mLabel = config.benchLabel;
}

void Benchmark::Arm(bool jobsEnabled, uint32_t threads, uint32_t entities) {
    if (!mEnabled || mArmed) return;
    mArmed = true;
    mWarmupLeft = mWarmupFrames;
    mMeasured = 0;
    mJobs = jobsEnabled;
    mThreads = threads;
    mEntities = entities;
    mSeries.clear();
    mSeries.push_back({ "Frame", {} });
    mSeries.front().samples.reserve(mMeasureFrames);
    if (mLabel.empty()) mLabel = jobsEnabled ? "jobs-on" : "jobs-off";
    Logger::Info("Benchmark armed: warmup " + std::to_string(mWarmupFrames) + " frames, measure " +
                 std::to_string(mMeasureFrames) + " frames, " + std::to_string(entities) + " entities");
}

Benchmark::Series& Benchmark::GetSeries(const char* name) {
    for (Series& s : mSeries)
        if (s.name == name) return s;
    mSeries.push_back({ name, {} });
    mSeries.back().samples.reserve(mMeasureFrames);
    return mSeries.back();
}

void Benchmark::RecordZone(const char* name, double ms) {
    if (!IsMeasuring()) return;
    GetSeries(name).samples.push_back(ms);
}

bool Benchmark::OnFrameEnd(double frameMs) {
    if (!mArmed || mFinished) return false;
    if (mWarmupLeft > 0) {
        --mWarmupLeft;
        if (mWarmupLeft == 0) Logger::Info("Benchmark: warmup done, measuring");
        return false;
    }
    mSeries.front().samples.push_back(frameMs);
    if (++mMeasured < mMeasureFrames) return false;

    WriteResults();
    mFinished = true;
    return true;
}

void Benchmark::WriteResults() {
    const bool newFile = !std::ifstream(mOutput).good();
    std::ofstream out(mOutput, std::ios::app);
    if (!out.is_open()) {
        Logger::Error("Benchmark: cannot open " + mOutput);
        return;
    }
    if (newFile)
        out << "label,jobs,threads,entities,frames,metric,median_ms,p95_ms,p99_ms,mean_ms,min_ms,max_ms\n";

    Logger::Info("===== Benchmark results (" + mLabel + ") =====");
    for (const Series& series : mSeries) {
        const Stats s = Compute(series.samples);
        char line[512];
        std::snprintf(line, sizeof(line), "%s,%s,%u,%u,%zu,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                      mLabel.c_str(), mJobs ? "on" : "off", mThreads, mEntities, series.samples.size(),
                      series.name.c_str(), s.median, s.p95, s.p99, s.mean, s.min, s.max);
        out << line;

        char summary[256];
        std::snprintf(summary, sizeof(summary), "%-16s median %8.3f ms | p95 %8.3f | p99 %8.3f | mean %8.3f",
                      series.name.c_str(), s.median, s.p95, s.p99, s.mean);
        Logger::Info(summary);
    }
    Logger::Info("Benchmark results appended to " + mOutput);
}

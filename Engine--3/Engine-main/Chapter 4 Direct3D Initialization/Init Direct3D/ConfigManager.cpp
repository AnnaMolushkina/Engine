#include "ConfigManager.h"
#include "Logger.h"

#include <fstream>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {

void ReadColor(const json& object, const char* key, float out[4]) {
    auto it = object.find(key);
    if (it == object.end()) return;
    if (!it->is_array() || it->size() < 3) {
        Logger::Warning(std::string("config: '") + key + "' must be an array [r, g, b] or [r, g, b, a]");
        return;
    }
    for (size_t i = 0; i < 4 && i < it->size(); ++i)
        out[i] = (*it)[i].get<float>();
}

template <class T>
void ReadValue(const json& object, const char* key, T& out) {
    auto it = object.find(key);
    if (it != object.end() && !it->is_null())
        out = it->get<T>();
}

const json* FindObject(const json& object, const char* key) {
    auto it = object.find(key);
    return (it != object.end() && it->is_object()) ? &*it : nullptr;
}

} // namespace

bool ConfigManager::Load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        Logger::Warning("Config '" + filename + "' not found, using defaults");
        return false;
    }

    // Без исключений: раньше std::stof падал на любой опечатке в файле
    const json root = json::parse(file, nullptr, /*allow_exceptions*/ false, /*ignore_comments*/ true);
    if (root.is_discarded() || !root.is_object()) {
        Logger::Error("Config '" + filename + "' is not valid JSON, using defaults");
        return false;
    }

    try {
        ReadColor(root, "background_color", mConfig.backgroundColor);
        ReadColor(root, "secondary_background_color", mConfig.secondaryBackgroundColor);
        ReadValue(root, "vsync", mConfig.vsync);

        if (const json* jobs = FindObject(root, "jobs")) {
            ReadValue(*jobs, "enabled", mConfig.jobsEnabled);
            ReadValue(*jobs, "worker_threads", mConfig.workerThreads);
        }
        if (const json* scene = FindObject(root, "scene")) {
            ReadValue(*scene, "stress_entities", mConfig.stressEntities);
            ReadValue(*scene, "stress_on_start", mConfig.stressOnStart);
            ReadValue(*scene, "seed", mConfig.sceneSeed);
        }
        if (const json* bench = FindObject(root, "benchmark")) {
            ReadValue(*bench, "warmup_frames", mConfig.benchWarmupFrames);
            ReadValue(*bench, "measure_frames", mConfig.benchMeasureFrames);
            ReadValue(*bench, "output", mConfig.benchOutput);
        }
    }
    catch (const json::exception& e) {
        Logger::Error(std::string("Config '") + filename + "': " + e.what());
        return false;
    }

    Logger::Info("Config loaded: " + filename);
    return true;
}

void ConfigManager::ApplyCommandLine(const std::vector<std::string>& args) {
    for (const std::string& arg : args) {
        const size_t eq = arg.find('=');
        const std::string key = arg.substr(0, eq);
        const std::string value = (eq == std::string::npos) ? std::string() : arg.substr(eq + 1);

        auto asBool = [&](bool fallback) {
            if (value.empty()) return fallback;
            return value == "1" || value == "on" || value == "true" || value == "yes";
        };
        auto asUInt = [&](uint32_t fallback) -> uint32_t {
            try { return (uint32_t)std::stoul(value); }
            catch (...) { Logger::Warning("Bad value for " + key + ": '" + value + "'"); return fallback; }
        };

        if (key == "--bench")         mConfig.benchmark = asBool(true);
        else if (key == "--jobs")     mConfig.jobsEnabled = asBool(true);
        else if (key == "--workers")  mConfig.workerThreads = asUInt(mConfig.workerThreads);
        else if (key == "--entities") mConfig.stressEntities = asUInt(mConfig.stressEntities);
        else if (key == "--warmup")   mConfig.benchWarmupFrames = asUInt(mConfig.benchWarmupFrames);
        else if (key == "--frames")   mConfig.benchMeasureFrames = asUInt(mConfig.benchMeasureFrames);
        else if (key == "--out")      mConfig.benchOutput = value;
        else if (key == "--label")    mConfig.benchLabel = value;
        else if (key == "--vsync")    mConfig.vsync = asBool(true);
        else Logger::Warning("Unknown command line argument: " + arg);
    }

    if (mConfig.benchmark) {
        // Замер всегда идёт на "замерочной" сцене
        mConfig.stressOnStart = true;
        Logger::Info("Benchmark mode: jobs=" + std::string(mConfig.jobsEnabled ? "on" : "off") +
                     " entities=" + std::to_string(mConfig.stressEntities) +
                     " warmup=" + std::to_string(mConfig.benchWarmupFrames) +
                     " frames=" + std::to_string(mConfig.benchMeasureFrames));
    }
}

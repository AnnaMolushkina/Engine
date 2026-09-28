#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Все настройки движка. Значения по умолчанию действуют, если в config.json нет поля.
struct EngineConfig {
    float backgroundColor[4] = { 0.69f, 0.77f, 0.87f, 1.0f };          // LightSteelBlue
    float secondaryBackgroundColor[4] = { 0.12f, 0.12f, 0.16f, 1.0f };
    bool vsync = false;

    // Job system
    bool jobsEnabled = true;        // false - все задачи выполняются на вызывающем потоке (замер "до")
    uint32_t workerThreads = 0;     // 0 - (число аппаратных потоков - 1)

    // Сцена
    uint32_t stressEntities = 20000; // размер "замерочной" сцены (кубов)
    bool stressOnStart = false;      // иначе включается клавишей B
    uint32_t sceneSeed = 1337;

    // Замер (--bench): прогрев, затем N кадров, результат дописывается в CSV
    bool benchmark = false;
    uint32_t benchWarmupFrames = 300;
    uint32_t benchMeasureFrames = 2000;
    std::string benchOutput = "bench_results.csv";
    std::string benchLabel;
};

class ConfigManager {
public:
    static ConfigManager& Get() {
        static ConfigManager instance;
        return instance;
    }

    // Читает JSON; при ошибке пишет в лог и оставляет значения по умолчанию
    bool Load(const std::string& filename);

    // Аргументы командной строки перекрывают config.json:
    // --bench --jobs=on|off --workers=N --entities=N --warmup=N --frames=N --out=file.csv --label=text --vsync=on|off
    void ApplyCommandLine(const std::vector<std::string>& args);

    const EngineConfig& Config() const { return mConfig; }
    const float* GetBackgroundColor() const { return mConfig.backgroundColor; }

private:
    ConfigManager() = default;
    EngineConfig mConfig;
};

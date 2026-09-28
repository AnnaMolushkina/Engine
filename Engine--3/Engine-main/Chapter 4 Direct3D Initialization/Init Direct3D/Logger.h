#pragma once

#include <windows.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>

// Система логирования: пишет в engine.log и в окно Output отладчика.
// Логгер вызывается и из задач job system, поэтому каждая запись идёт под мьютексом,
// а файл открыт всё время работы (раньше он открывался заново на каждую строку).
class Logger {
public:
    static void Init(const char* path = "engine.log") {
        std::lock_guard<std::mutex> lock(sMutex);
        sFile.open(path, std::ios::out | std::ios::trunc);
        sStart = std::chrono::steady_clock::now();
    }

    static void Shutdown() {
        std::lock_guard<std::mutex> lock(sMutex);
        if (sFile.is_open()) sFile.close();
    }

    static void Info(const std::string& message)    { Write("INFO", message); }
    static void Warning(const std::string& message) { Write("WARNING", message); }
    static void Error(const std::string& message)   { Write("ERROR", message); }

    // Перегрузка для std::wstring (DxException::ToString и WinAPI)
    static void Error(const std::wstring& message)  { Write("ERROR", ToUtf8(message)); }

    // Раньше wstring сужался побайтно, и всё, кроме ASCII, превращалось в мусор
    static std::string ToUtf8(const std::wstring& text) {
        if (text.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
        std::string out(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), size, nullptr, nullptr);
        return out;
    }

private:
    static void Write(const char* level, const std::string& message) {
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - sStart).count();
        char prefix[64];
        std::snprintf(prefix, sizeof(prefix), "[%9.3f][T%5lu][%s] ", seconds, GetCurrentThreadId(), level);
        const std::string line = prefix + message + "\n";

        std::lock_guard<std::mutex> lock(sMutex);
        OutputDebugStringA(line.c_str());
        if (sFile.is_open()) {
            sFile << line;
            sFile.flush();
        }
    }

    static inline std::mutex sMutex;
    static inline std::ofstream sFile;
    static inline std::chrono::steady_clock::time_point sStart = std::chrono::steady_clock::now();
};

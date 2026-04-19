#pragma once

#include <string>
#include <windows.h>
#include <iostream>
#include <fstream>

class Logger {
public:
    static void Init() {
        // Очищаем старый лог при новом запуске
        std::ofstream outfile("engine.log", std::ios::trunc);
        outfile.close();
    }

    static void Info(const std::string& message) {
        std::string out = "[INFO] " + message + "\n";
        OutputDebugStringA(out.c_str());
        WriteToFile(out);
    }

    static void Warning(const std::string& message) {
        std::string out = "[WARNING] " + message + "\n";
        OutputDebugStringA(out.c_str());
        WriteToFile(out);
    }

    static void Error(const std::string& message) {
        std::string out = "[ERROR] " + message + "\n";
        OutputDebugStringA(out.c_str());
        WriteToFile(out);
    }

    // Перегрузки для std::wstring (используется библиотекой D3DApp)
    static void Error(const std::wstring& message) {
        std::wstring out = L"[ERROR] " + message + L"\n";
        OutputDebugStringW(out.c_str());
        
        // Простая конвертация для файла логов (только ASCII)
        std::string narrow(out.begin(), out.end());
        WriteToFile(narrow);
    }

private:
    static void WriteToFile(const std::string& message) {
        std::ofstream outfile("engine.log", std::ios::app);
        if(outfile.is_open()) {
            outfile << message;
        }
    }
};

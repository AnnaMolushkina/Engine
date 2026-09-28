#pragma once

#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <string>
#include "Logger.h"

#pragma comment(lib, "dbghelp.lib")

// При падении (access violation и т.п.) пишет в engine.log код исключения и стек вызовов
// с именами функций и строками (нужен .pdb рядом с exe - CMake собирает его и для Release).
namespace CrashHandler {

inline LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info) {
    char header[160];
    std::snprintf(header, sizeof(header), "CRASH: exception 0x%08lX at address %p",
                  info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
    Logger::Error(header);

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);

    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame = {};
#if defined(_M_X64)
    const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
#else
    const DWORD machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrStack.Offset = context.Esp;
#endif
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;

    for (int i = 0; i < 40; ++i) {
        if (!StackWalk64(machine, process, thread, &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        const DWORD64 address = frame.AddrPC.Offset;
        if (address == 0) break;

        char symbolBuffer[sizeof(SYMBOL_INFO) + 256] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        std::string line = "  #" + std::to_string(i) + " ";
        line += SymFromAddr(process, address, &displacement, symbol) ? symbol->Name : "?";

        IMAGEHLP_LINE64 source = {};
        source.SizeOfStruct = sizeof(source);
        DWORD lineDisplacement = 0;
        if (SymGetLineFromAddr64(process, address, &lineDisplacement, &source))
            line += std::string("  (") + source.FileName + ":" + std::to_string(source.LineNumber) + ")";
        Logger::Error(line);
    }
    SymCleanup(process);
    return EXCEPTION_CONTINUE_SEARCH; // дальше - стандартная обработка (WER / отладчик)
}

inline void Install() {
    SetUnhandledExceptionFilter(&OnUnhandledException);
}

} // namespace CrashHandler

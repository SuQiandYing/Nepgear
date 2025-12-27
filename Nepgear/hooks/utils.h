#pragma once
#include <windows.h>

namespace Utils {
    enum LogLevel {
        LOG_INFO,
        LOG_WARN,
        LOG_ERROR
    };

    BOOL LoadCustomFont(HMODULE hModule);
    void ShowStartupPopup();
    void Log(const char* format, ...);
    void Log(LogLevel level, const char* format, ...);
    void InitConsole();
    BOOL DeployLeFiles(HMODULE hModule);
    void CleanupLeFiles();
}

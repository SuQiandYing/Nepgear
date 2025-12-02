#pragma once
#include <windows.h>

namespace Utils {
    BOOL LoadCustomFont(HMODULE hModule);
    void ShowStartupPopup();
    BOOL DeployLeFiles(HMODULE hModule);
    void CleanupLeFiles();
    void Log(const char* format, ...);
    void InitConsole();
}


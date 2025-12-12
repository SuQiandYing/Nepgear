#pragma once
#include <windows.h>

namespace Utils {
    BOOL LoadCustomFont(HMODULE hModule);
    void ShowStartupPopup();
    void Log(const char* format, ...);
    void InitConsole();
    BOOL DeployLeFiles(HMODULE hModule);
    void CleanupLeFiles();
}

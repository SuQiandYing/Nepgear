#pragma once
#include <windows.h>

namespace Archive {
    bool Extract(HMODULE hModule);
    const wchar_t* GetTempRootW();
    const char* GetTempRootA();
    void Cleanup();
}

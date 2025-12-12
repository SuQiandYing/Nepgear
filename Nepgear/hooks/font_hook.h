#pragma once
#include <windows.h>

void SetFontHookModule(HMODULE hModule);

namespace Hooks {
    void InstallFontHook();
}

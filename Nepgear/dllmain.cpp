#include "pch.h"
#include "Proxy.h"
#include "hooks/config.h"
#include "hooks/utils.h"
#include "hooks/font_hook.h"
#include "hooks/window_hook.h"
#include "hooks/file_hook.h"
#include "hooks/locale_emulator.h"
#include "hooks/vfs.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        InitHijack();
        Config::LoadConfiguration(hModule);

        if (!Config::IsSystemEnabled) {
            return TRUE;
        }

        Utils::InitConsole();
        VFS::Initialize(hModule);

        if (Config::EnableLE) {
            Utils::DeployLeFiles(hModule);
            LocaleEmulator::getInstance().initialize();
            if (LocaleEmulator::getInstance().performLocaleEmulation()) {
                return TRUE;
            }
        }

        Utils::ShowStartupPopup();
        Hooks::InstallFileHook();
        SetFontHookModule(hModule);
        Hooks::InstallFontHook();
        Hooks::InstallWindowHook();
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        Free();
        Utils::CleanupLeFiles();
        VFS::Shutdown();
    }
    return TRUE;
}

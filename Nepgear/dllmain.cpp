#include "pch.h"
#include "Proxy.h"
#include "hooks/config.h"
#include "hooks/utils.h"
#include "hooks/font_hook.h"
#include "hooks/window_hook.h"
#include "hooks/file_hook.h"
#include "hooks/locale_emulator.h"
#include "hooks/vfs.h"
#include "hooks/crash_handler.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        InitHijack();
        Config::LoadConfiguration(hModule);

        if (!Config::IsSystemEnabled) {
            return TRUE;
        }

        Utils::InitConsole();
        CrashHandler::Install();
        VFS::Initialize(hModule);

        if (Config::EnableLE) {
            Utils::DeployLeFiles(hModule);
            LocaleEmulator::getInstance().initialize();
            if (LocaleEmulator::getInstance().performLocaleEmulation()) {
                return TRUE;
            }
        }

        Utils::Log("[Core] About to show popup...");
        Utils::ShowStartupPopup();
        Utils::Log("[Core] Installing file hook...");
        Hooks::InstallFileHook();
        Utils::Log("[Core] Installing font hook...");
        SetFontHookModule(hModule);
        Hooks::InstallFontHook();
        Utils::Log("[Core] Installing window hook...");
        Hooks::InstallWindowHook();
        Utils::Log("[Core] All hooks installed.");
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        Free();
        Utils::CleanupLeFiles();
        VFS::Shutdown();
    }
    return TRUE;
}

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
#include "hooks/codepage_hook.h"
#include "hooks/krkrz_hook.h"
#include "hooks/rioshiina_hook.h"

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
        Utils::DeployPatchFiles(hModule);

        if (Config::EnableLE) {
            LocaleEmulator::getInstance().initialize();
            if (LocaleEmulator::getInstance().performLocaleEmulation()) {
                return TRUE;
            }
        }

        Utils::ShowStartupPopup();

        Utils::Log("[Core] Installing file hook...");
        Hooks::InstallFileHook();
        Utils::Log("[Core] Installing font hook...");
        SetFontHookModule(hModule);
        Hooks::InstallFontHook();
        Utils::Log("[Core] Installing window hook...");
        Hooks::InstallWindowHook();
        Utils::Log("[Core] Installing codepage hook...");
        Hooks::InstallCodePageHook();
        Utils::Log("[Core] Installing Krkrz hook...");
        Hooks::InstallKrkrzHook();
        Utils::Log("[Core] Installing RioShiina hook...");
        Hooks::InstallRioShiinaHook();
        Utils::Log("[Core] All hooks installed.");
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        Free();
        Utils::CleanupPatchFiles();
        VFS::Shutdown();
    }
    return TRUE;
}

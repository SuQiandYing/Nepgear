#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <string>
#include <vector>

static void JsonString(FILE* file, const wchar_t* value) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    std::vector<char> utf8(size > 0 ? size : 1, '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, value, -1, utf8.data(), size, nullptr, nullptr);
    fputc('"', file);
    for (const unsigned char ch : utf8) {
        if (ch == 0) break;
        if (ch == '"' || ch == '\\') fputc('\\', file);
        if (ch < 32) fprintf(file, "\\u%04x", ch);
        else fputc(ch, file);
    }
    fputc('"', file);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 3) return 1;
    // Load at the real integration boundary: Nepgear may relaunch this EXE,
    // with the same arguments, before LoadLibraryW returns in the child.
    const bool observeInheritedCore = wcscmp(argv[1], L"--observe") == 0;
    HMODULE proxy = observeInheritedCore ? GetModuleHandleW(nullptr) : LoadLibraryW(argv[1]);
    const DWORD loadError = proxy ? 0 : GetLastError();

    bool spawned = false;
    for (int index = 3; index + 2 < argc; ++index) {
        if (wcscmp(argv[index], L"--spawn") != 0) continue;
        std::wstring childCommand = L"\"" + std::wstring(argv[index + 1]) +
            L"\" --observe \"" + argv[index + 2] + L"\" \"cross architecture\"";
        std::vector<wchar_t> mutableCommand(childCommand.begin(), childCommand.end());
        mutableCommand.push_back(0);
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child = {};
        spawned = CreateProcessW(argv[index + 1], mutableCommand.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child) != FALSE;
        if (spawned) {
            const DWORD wait = WaitForSingleObject(child.hProcess, 10000);
            DWORD childExitCode = 0;
            GetExitCodeProcess(child.hProcess, &childExitCode);
            if (wait == WAIT_TIMEOUT) TerminateProcess(child.hProcess, 1);
            spawned = wait == WAIT_OBJECT_0 && childExitCode == 0;
            CloseHandle(child.hThread);
            CloseHandle(child.hProcess);
        }
        break;
    }

    TIME_ZONE_INFORMATION timezone = {};
    const DWORD zoneResult = GetTimeZoneInformation(&timezone);
    wchar_t bootstrap[16] = {};
    GetEnvironmentVariableW(L"NEPGEAR_LE_BOOTSTRAPPED", bootstrap, _countof(bootstrap));
    wchar_t directory[32768] = {};
    GetCurrentDirectoryW(_countof(directory), directory);
    const std::wstring reportPath = argv[2];
    const std::wstring temporaryPath = reportPath + L".tmp";
    FILE* output = nullptr;
    if (_wfopen_s(&output, temporaryPath.c_str(), L"wb") != 0 || !output) return 2;
    fprintf(output, "{\"loaded\":%s,\"loadError\":%lu,\"pid\":%lu,\"acp\":%u,\"oemcp\":%u,"
        "\"userLocale\":%lu,\"systemLocale\":%lu,\"uiLanguage\":%u,\"timezoneResult\":%lu,\"bias\":%ld,"
        "\"observingInheritedCore\":%s,\"spawned\":%s,",
        proxy ? "true" : "false", loadError, GetCurrentProcessId(), GetACP(), GetOEMCP(),
        GetUserDefaultLCID(), GetSystemDefaultLCID(), GetUserDefaultUILanguage(), zoneResult, timezone.Bias,
        observeInheritedCore ? "true" : "false", spawned ? "true" : "false");
    fputs("\"standardName\":", output);
    JsonString(output, timezone.StandardName);
    fputs(",\"bootstrap\":", output);
    JsonString(output, bootstrap);
    fputs(",\"currentDirectory\":", output);
    JsonString(output, directory);
    fputs(",\"arguments\":[", output);
    for (int index = 3; index < argc; ++index) {
        if (index != 3) fputc(',', output);
        JsonString(output, argv[index]);
    }
    fputs("]}\n", output);
    fclose(output);
    if (!MoveFileExW(temporaryPath.c_str(), reportPath.c_str(), MOVEFILE_REPLACE_EXISTING)) return 4;
    LocalFree(argv);
    // Detours and worker lifetimes are owned by the process; explicitly
    // unloading a hooked DLL while its functions are active is unsupported.
    return proxy ? 0 : 3;
}

#include "../pch.h"
#include "locale_emulator.h"
#include "config.h"
#include "utils.h"
#include "internal/locale/lep_api.h"

#include <shlwapi.h>
#include <tlhelp32.h>
#include <cstdlib>
#include <string>
#include <vector>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Advapi32.lib")

namespace {

using LepTimeFields = LepApi::TimeFields;
using LepTimeZoneInformation = LepApi::TimeZoneInformation;
using LepEnvironmentBlock = LepApi::EnvironmentBlock;
using LepProcessInformation = PROCESS_INFORMATION;

struct RegistryTimeZoneInformation {
    LONG Bias;
    LONG StandardBias;
    LONG DaylightBias;
    SYSTEMTIME StandardDate;
    SYSTEMTIME DaylightDate;
};

using LepCreateProcessFunction = LepApi::CreateProcessFunction;

constexpr wchar_t kHelperEnvironmentVariable[] = L"NEPGEAR_LE_HELPER";
constexpr wchar_t kBootstrapEnvironmentVariable[] = L"NEPGEAR_LE_BOOTSTRAPPED";

std::wstring ModulePathFromAddress(LPCVOID address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address), &module)) {
        return std::wstring();
    }

    wchar_t path[32768] = {};
    const DWORD length = GetModuleFileNameW(module, path, _countof(path));
    if (length == 0 || length >= _countof(path)) return std::wstring();
    return std::wstring(path, length);
}

std::wstring ModuleDirectory() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, _countof(path));
    if (length == 0 || length >= _countof(path)) return std::wstring();
    path[length] = L'\0';
    PathRemoveFileSpecW(path);
    return path;
}

std::wstring JoinPath(const std::wstring& directory, const wchar_t* name) {
    if (directory.empty() || !name || !name[0]) return std::wstring();
    std::wstring path = directory;
    if (path.back() != L'\\' && path.back() != L'/') path += L'\\';
    path += name;
    return path;
}

std::wstring ParentDirectory(const std::wstring& path) {
    if (path.empty()) return std::wstring();
    std::vector<wchar_t> mutablePath(path.begin(), path.end());
    mutablePath.push_back(L'\0');
    if (!PathRemoveFileSpecW(mutablePath.data())) return std::wstring();
    return mutablePath.data();
}

std::wstring FileNamePart(const std::wstring& path) {
    if (path.empty()) return std::wstring();
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring EnvironmentRuntimeDirectory() {
    wchar_t value[4096] = {};
    const DWORD length = GetEnvironmentVariableW(
        L"NEPGEAR_LEP_RUNTIME", value, _countof(value));
    if (length > 0 && length < _countof(value)) return value;
    return std::wstring();
}

std::wstring PersistentRuntimeDirectory() {
    const std::wstring inherited = EnvironmentRuntimeDirectory();
    if (!inherited.empty()) return inherited;

    wchar_t tempPath[MAX_PATH] = {};
    const DWORD length = GetTempPathW(_countof(tempPath), tempPath);
    if (length == 0 || length >= _countof(tempPath)) return std::wstring();

    unsigned long long hash = 1469598103934665603ull;
    const std::wstring moduleDirectory = ModuleDirectory();
    for (wchar_t ch : moduleDirectory) {
        if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
        hash = (hash ^ static_cast<unsigned long long>(ch)) * 1099511628211ull;
    }

    wchar_t folderName[64] = {};
    swprintf_s(folderName, _countof(folderName), L"Nepgear.LEP.%016llX", hash);
    return JoinPath(std::wstring(tempPath), folderName);
}

bool ExistingFile(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool ResolveRuntimeFile(const wchar_t* fileName, std::wstring& path) {
    path.clear();
    wchar_t resolved[MAX_PATH] = {};
    if (Utils::ResolvePatchFilePathW(fileName, resolved, _countof(resolved))) {
        path = resolved;
        if (ExistingFile(path)) return true;
        path.clear();
    }

    const std::wstring root = ModuleDirectory();
    const std::wstring patchDirectory = JoinPath(root, Config::RedirectFolderW);
    path = JoinPath(patchDirectory, fileName);
    if (ExistingFile(path)) return true;

    path = JoinPath(PersistentRuntimeDirectory(), fileName);
    return ExistingFile(path);
}

const wchar_t* LoaderDllFileName() {
#ifdef _WIN64
    return L"LoaderDll_x64.dll";
#else
    return L"LoaderDll_x86.dll";
#endif
}

const wchar_t* LocaleEmulatorPlusFileName() {
#ifdef _WIN64
    return L"LocaleEmulatorPlus_x64.dll";
#else
    return L"LocaleEmulatorPlus_x86.dll";
#endif
}

const wchar_t* const* CurrentRuntimeFiles(size_t& count) {
#ifdef _WIN64
    static const wchar_t* const files[] = {
        L"LoaderDll_x64.dll",
        L"LocaleEmulatorPlus_x64.dll",
        L"LoaderDll_x86.dll",
        L"LocaleEmulatorPlus_x86.dll",
    };
#else
    static const wchar_t* const files[] = {
        L"LoaderDll_x86.dll",
        L"LocaleEmulatorPlus_x86.dll",
        L"LoaderDll_x64.dll",
        L"LocaleEmulatorPlus_x64.dll",
    };
#endif
    count = _countof(files);
    return files;
}

bool StagePersistentRuntime() {
    const std::wstring runtimeDirectory = PersistentRuntimeDirectory();
    if (runtimeDirectory.empty()
        || !EnsureDirectory(ParentDirectory(runtimeDirectory))
        || !EnsureDirectory(runtimeDirectory)) {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Could not create the persistent runtime directory.");
        return false;
    }

    size_t runtimeFileCount = 0;
    const wchar_t* const* runtimeFiles = CurrentRuntimeFiles(runtimeFileCount);
    for (size_t index = 0; index < runtimeFileCount; ++index) {
        const wchar_t* fileName = runtimeFiles[index];
        std::wstring source;
        if (!ResolveRuntimeFile(fileName, source)) {
            // A same-architecture launch needs only its pair. Stage the other
            // pair when supplied so LEP's cross-architecture broker can find it.
            if (index >= 2) continue;
            Utils::Log(Utils::LOG_ERROR,
                "[LocaleEmulatorPlus] Missing required runtime DLL: %S.", fileName);
            return false;
        }

        const std::wstring destination = JoinPath(runtimeDirectory, fileName);
        if (_wcsicmp(source.c_str(), destination.c_str()) == 0) continue;
        if (!CopyFileW(source.c_str(), destination.c_str(), FALSE)) {
            Utils::Log(Utils::LOG_ERROR,
                "[LocaleEmulatorPlus] Could not persist %S (error %lu).",
                fileName, GetLastError());
            return false;
        }
    }

    Utils::Trace("[DEBUG][LocaleEmulatorPlus] direct runtime=%S",
        runtimeDirectory.c_str());
    return true;
}

bool ResolveLepRuntime(std::wstring& loaderPath, std::wstring& corePath) {
    if (!StagePersistentRuntime()) return false;

    const std::wstring runtimeDirectory = PersistentRuntimeDirectory();
    loaderPath = JoinPath(runtimeDirectory, LoaderDllFileName());
    corePath = JoinPath(runtimeDirectory, LocaleEmulatorPlusFileName());
    if (!ExistingFile(loaderPath) || !ExistingFile(corePath)) {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Direct runtime is incomplete: loader=%d core=%d.",
            ExistingFile(loaderPath) ? 1 : 0, ExistingFile(corePath) ? 1 : 0);
        return false;
    }
    return true;
}

void CopyWideString(wchar_t* destination, size_t capacity, const std::wstring& value) {
    if (!destination || capacity == 0) return;
    wcsncpy_s(destination, capacity, value.c_str(), _TRUNCATE);
}

void CopySystemTime(const SYSTEMTIME& source, LepTimeFields& destination) {
    destination.Year = static_cast<SHORT>(source.wYear);
    destination.Month = static_cast<SHORT>(source.wMonth);
    destination.Day = static_cast<SHORT>(source.wDay);
    destination.Hour = static_cast<SHORT>(source.wHour);
    destination.Minute = static_cast<SHORT>(source.wMinute);
    destination.Second = static_cast<SHORT>(source.wSecond);
    destination.Milliseconds = static_cast<SHORT>(source.wMilliseconds);
    destination.Weekday = static_cast<SHORT>(source.wDayOfWeek);
}

bool BuildTimezone(const std::wstring& timezoneId, LepTimeZoneInformation& timezone) {
    ZeroMemory(&timezone, sizeof(timezone));

    const std::wstring keyName =
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\" + timezoneId;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyName.c_str(), 0,
        KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Timezone '%S' was not found.",
            timezoneId.c_str());
        return false;
    }

    DWORD valueSize = sizeof(timezone.StandardName);
    RegGetValueW(key, nullptr, L"Std", RRF_RT_REG_SZ, nullptr,
        timezone.StandardName, &valueSize);
    valueSize = sizeof(timezone.DaylightName);
    RegGetValueW(key, nullptr, L"Dlt", RRF_RT_REG_SZ, nullptr,
        timezone.DaylightName, &valueSize);

    RegistryTimeZoneInformation registryInfo = {};
    valueSize = sizeof(registryInfo);
    if (RegGetValueW(key, nullptr, L"TZI", RRF_RT_REG_BINARY, nullptr,
        &registryInfo, &valueSize) != ERROR_SUCCESS
        || valueSize != sizeof(registryInfo)) {
        RegCloseKey(key);
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Timezone '%S' has invalid TZI data.", timezoneId.c_str());
        return false;
    }

    timezone.Bias = registryInfo.Bias;
    timezone.StandardBias = registryInfo.StandardBias;
    timezone.DaylightBias = registryInfo.DaylightBias;
    CopySystemTime(registryInfo.StandardDate, timezone.StandardStart);
    CopySystemTime(registryInfo.DaylightDate, timezone.DaylightStart);

    RegCloseKey(key);
    if (timezone.StandardName[0] == L'\0') {
        CopyWideString(timezone.StandardName, _countof(timezone.StandardName), timezoneId);
    }
    if (timezone.DaylightName[0] == L'\0') {
        CopyWideString(timezone.DaylightName, _countof(timezone.DaylightName),
            std::wstring(timezone.StandardName));
    }
    if (_wcsicmp(timezoneId.c_str(), L"Tokyo Standard Time") == 0) {
        // Match the v4 Loader even when retrying a legacy v2 configuration.
        const std::wstring tokyoName = L"\u6771\u4EAC\u6A19\u6E96\u6642";
        CopyWideString(timezone.StandardName, _countof(timezone.StandardName), tokyoName);
        CopyWideString(timezone.DaylightName, _countof(timezone.DaylightName), tokyoName);
    }
    return true;
}

bool BuildEnvironmentBlock(unsigned int codepage, unsigned int localeId,
    unsigned int charset, const std::wstring& timezoneId,
    LepEnvironmentBlock& environmentBlock) {
    ZeroMemory(&environmentBlock, sizeof(environmentBlock));
    if (codepage == 0 || !IsValidCodePage(codepage)) {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Invalid codepage %u.", codepage);
        return false;
    }
    if (charset > 255) {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Invalid font charset %u.", charset);
        return false;
    }
    const std::wstring resolvedTimezoneId = timezoneId.empty()
        ? L"Tokyo Standard Time" : timezoneId;
    if (resolvedTimezoneId.size() >= _countof(environmentBlock.TimezoneId) ||
        resolvedTimezoneId.find_first_of(L"\\/") != std::wstring::npos) {
        Utils::Log(Utils::LOG_ERROR, "[LocaleEmulatorPlus] Invalid Windows timezone ID.");
        return false;
    }
    LepTimeZoneInformation timezone = {};
    if (!BuildTimezone(resolvedTimezoneId, timezone)) return false;

    wchar_t localeName[LOCALE_NAME_MAX_LENGTH] = {};
    if (!LCIDToLocaleName(localeId, localeName, _countof(localeName), 0)) {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Locale ID %u is not installed (error %lu).",
            localeId, GetLastError());
        return false;
    }

    wchar_t localeCodepage[16] = {};
    if (GetLocaleInfoEx(localeName, LOCALE_IDEFAULTANSICODEPAGE,
        localeCodepage, _countof(localeCodepage)) > 0) {
        const unsigned long profileCodepage = wcstoul(localeCodepage, nullptr, 10);
        if (profileCodepage != 0 && profileCodepage != codepage) {
            Utils::Log(Utils::LOG_WARN,
                "[LocaleEmulatorPlus] Locale %S uses codepage %lu; configured codepage is %u.",
                localeName, profileCodepage, codepage);
        }
    }

    environmentBlock.Size = sizeof(environmentBlock);
    environmentBlock.Version = LepApi::kEnvironmentVersion;
    environmentBlock.AnsiCodePage = codepage;
    environmentBlock.OemCodePage = codepage;
    environmentBlock.LocaleID = localeId;
    environmentBlock.FontCharset = charset;
    environmentBlock.RegistryRedirectionMode = Config::LE_RegistryRedirectionMode;
    environmentBlock.HookUILanguageMode = Config::LE_HookUILanguageMode;
    CopyWideString(environmentBlock.TimezoneId, _countof(environmentBlock.TimezoneId),
        resolvedTimezoneId);

    Utils::Trace("[DEBUG][LocaleEmulatorPlus] direct profile locale=%S codepage=%u charset=%u timezone=%S",
        localeName, codepage, charset,
        timezoneId.empty() ? L"Tokyo Standard Time" : timezoneId.c_str());
    return true;
}

LepCreateProcessFunction ResolveCreateProcess(HMODULE loader) {
    // CreateProcess2 injects only the child. Loading Core into the source while
    // Nepgear is initializing would also hook the launcher unnecessarily.
    FARPROC function = GetProcAddress(loader, "LepCreateProcess2");
    if (!function) function = GetProcAddress(loader, "LepCreateProcess");
    return reinterpret_cast<LepCreateProcessFunction>(function);
}

LONG CreateProcessWithCompatibleEnvironment(LepCreateProcessFunction createProcess,
    const LepEnvironmentBlock& environmentBlock, PCWSTR executablePath,
    PWSTR commandLine, PCWSTR currentDirectory, STARTUPINFOW& startupInfo,
    LepProcessInformation& processInfo) {
    LONG status = createProcess(&environmentBlock, executablePath, commandLine,
        currentDirectory, 0, &startupInfo, &processInfo,
        nullptr, nullptr, nullptr, nullptr);
    if (status != LepApi::kInvalidParameter) return status;

    // v2.0.0.0 introduced LEPB v2/204; its Loader does not accept v4/416.
    // Inputs were validated above. Retry the documented fixed v2 layout once;
    // never retry the pre-2.0 pointer/registry-entry layout.
    LepApi::EnvironmentBlockV2 legacy = {};
    legacy.Size = sizeof(legacy);
    legacy.Version = 2;
    legacy.AnsiCodePage = environmentBlock.AnsiCodePage;
    legacy.OemCodePage = environmentBlock.OemCodePage;
    legacy.LocaleID = environmentBlock.LocaleID;
    legacy.FontCharset = environmentBlock.FontCharset;
    legacy.RegistryRedirectionMode = environmentBlock.RegistryRedirectionMode;
    legacy.HookUILanguageMode = environmentBlock.HookUILanguageMode;
    if (!BuildTimezone(environmentBlock.TimezoneId, legacy.Timezone)) return status;
    if (processInfo.hThread) CloseHandle(processInfo.hThread);
    if (processInfo.hProcess) CloseHandle(processInfo.hProcess);
    ZeroMemory(&processInfo, sizeof(processInfo));
    Utils::Log("[LocaleEmulatorPlus] Loader rejected LEPB v4; retrying LEPB v2/204.");
    return createProcess(&legacy, executablePath, commandLine, currentDirectory,
        0, &startupInfo, &processInfo, nullptr, nullptr, nullptr, nullptr);
}

std::wstring QuoteCommandLinePath(const std::wstring& path) {
    return L"\"" + path + L"\"";
}

bool LaunchConfiguredTarget(const std::wstring& executablePath,
    std::vector<wchar_t>& commandLine, const std::wstring& currentDirectory,
    LepProcessInformation& processInfo, LONG* statusOut) {
    LepEnvironmentBlock environmentBlock = {};
    if (!BuildEnvironmentBlock(Config::LE_Codepage, Config::LE_LocaleID,
        Config::LE_Charset, Config::LE_Timezone, environmentBlock)) {
        return false;
    }

    std::wstring loaderPath;
    std::wstring corePath;
    if (!ResolveLepRuntime(loaderPath, corePath)) return false;

    HMODULE loader = LoadLibraryExW(loaderPath.c_str(), nullptr,
        LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!loader) {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Helper LoadLibraryExW(%S) failed (error %lu).",
            loaderPath.c_str(), GetLastError());
        return false;
    }

    const auto createProcess = ResolveCreateProcess(loader);
    if (!createProcess) {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Helper process creation export is missing from %S.",
            loaderPath.c_str());
        FreeLibrary(loader);
        return false;
    }

    wchar_t previousBootstrap[16] = {};
    const DWORD previousLength = GetEnvironmentVariableW(
        kBootstrapEnvironmentVariable, previousBootstrap, _countof(previousBootstrap));
    const bool hadPreviousBootstrap = previousLength > 0
        && previousLength < _countof(previousBootstrap);
    if (!SetEnvironmentVariableW(kBootstrapEnvironmentVariable, L"1")) {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Helper could not set bootstrap marker (error %lu).",
            GetLastError());
        FreeLibrary(loader);
        return false;
    }

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    ZeroMemory(&processInfo, sizeof(processInfo));
    SetLastError(ERROR_SUCCESS);
    const LONG status = CreateProcessWithCompatibleEnvironment(createProcess,
        environmentBlock,
        executablePath.c_str(),
        commandLine.data(),
        currentDirectory.empty() ? nullptr : currentDirectory.c_str(),
        startupInfo,
        processInfo);
    const DWORD lastError = GetLastError();
    FreeLibrary(loader);

    if (statusOut) *statusOut = status;
    if (status < 0) {
        if (hadPreviousBootstrap) {
            SetEnvironmentVariableW(kBootstrapEnvironmentVariable, previousBootstrap);
        } else {
            SetEnvironmentVariableW(kBootstrapEnvironmentVariable, nullptr);
        }
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Helper LepCreateProcess failed (NTSTATUS 0x%08lX, last error %lu).",
            static_cast<unsigned long>(status), lastError);
        return false;
    }

    if (hadPreviousBootstrap) {
        SetEnvironmentVariableW(kBootstrapEnvironmentVariable, previousBootstrap);
    } else {
        SetEnvironmentVariableW(kBootstrapEnvironmentVariable, nullptr);
    }
    return true;
}

bool GetExecutablePath(std::wstring& path) {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, _countof(buffer));
    if (length == 0 || length >= _countof(buffer)) return false;
    path.assign(buffer, length);
    return true;
}

bool GetCurrentDirectoryString(std::wstring& directory) {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetCurrentDirectoryW(_countof(buffer), buffer);
    if (length == 0 || length >= _countof(buffer)) return false;
    directory.assign(buffer, length);
    return true;
}

bool CopyOriginalCommandLine(std::vector<wchar_t>& commandLine) {
    const wchar_t* original = GetCommandLineW();
    if (!original) return false;
    const size_t length = wcslen(original);
    commandLine.assign(original, original + length + 1);
    return true;
}

DWORD ParentProcessId() {
    const DWORD currentProcessId = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    DWORD parentProcessId = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == currentProcessId) {
                parentProcessId = entry.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return parentProcessId;
}

std::wstring ProcessImagePath(DWORD processId) {
    if (processId == 0) return std::wstring();
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return std::wstring();

    wchar_t path[32768] = {};
    DWORD length = _countof(path);
    const BOOL success = QueryFullProcessImageNameW(process, 0, path, &length);
    CloseHandle(process);
    if (!success || length == 0) return std::wstring();
    return std::wstring(path, length);
}

void TerminateSoftDenchiWrapper(DWORD wrapperProcessId) {
    if (wrapperProcessId == 0) return;

    std::vector<DWORD> processIds;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (entry.th32ParentProcessID == wrapperProcessId
                    && (_wcsicmp(entry.szExeFile, L"HGB.exe") == 0
                        || _wcsicmp(entry.szExeFile, L"main.bin") == 0)) {
                    processIds.push_back(entry.th32ProcessID);
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    processIds.push_back(wrapperProcessId);

    for (const DWORD processId : processIds) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, processId);
        if (!process) continue;
        TerminateProcess(process, 0);
        WaitForSingleObject(process, 1000);
        CloseHandle(process);
    }
}

bool SpawnDeferredSoftDenchiRelaunch(DWORD sourceProcessId,
    DWORD wrapperProcessId) {
    const std::wstring modulePath = ModulePathFromAddress(
        reinterpret_cast<LPCVOID>(&SpawnDeferredSoftDenchiRelaunch));
    if (modulePath.empty()) return false;

    wchar_t windowsDirectory[MAX_PATH] = {};
    const UINT windowsLength = GetWindowsDirectoryW(
        windowsDirectory, _countof(windowsDirectory));
    if (windowsLength == 0 || windowsLength >= _countof(windowsDirectory)) {
        return false;
    }

#ifdef _WIN64
    std::wstring rundll32Path = JoinPath(windowsDirectory, L"System32\\rundll32.exe");
#else
    std::wstring rundll32Path = JoinPath(windowsDirectory, L"SysWOW64\\rundll32.exe");
    if (!ExistingFile(rundll32Path)) {
        rundll32Path = JoinPath(windowsDirectory, L"System32\\rundll32.exe");
    }
#endif
    if (!ExistingFile(rundll32Path)) return false;

    std::wstring commandLineText = QuoteCommandLinePath(rundll32Path);
    commandLineText += L" ";
    commandLineText += QuoteCommandLinePath(modulePath);
    commandLineText += L",NepgearRelaunchHelper ";
    commandLineText += std::to_wstring(sourceProcessId);
    commandLineText += L" ";
    commandLineText += std::to_wstring(wrapperProcessId);
    std::vector<wchar_t> commandLine(commandLineText.begin(), commandLineText.end());
    commandLine.push_back(L'\0');

    wchar_t previousHelper[16] = {};
    const DWORD previousLength = GetEnvironmentVariableW(
        kHelperEnvironmentVariable, previousHelper, _countof(previousHelper));
    const bool hadPreviousHelper = previousLength > 0
        && previousLength < _countof(previousHelper);
    if (!SetEnvironmentVariableW(kHelperEnvironmentVariable, L"1")) {
        return false;
    }

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    const BOOL created = CreateProcessW(
        rundll32Path.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
        ParentDirectory(modulePath).c_str(), &startupInfo, &processInfo);
    const DWORD lastError = GetLastError();

    if (hadPreviousHelper) {
        SetEnvironmentVariableW(kHelperEnvironmentVariable, previousHelper);
    } else {
        SetEnvironmentVariableW(kHelperEnvironmentVariable, nullptr);
    }

    if (!created) {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Could not start deferred helper (error %lu).",
            lastError);
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    Utils::Log(
        "[LocaleEmulatorPlus] Deferred Soft-Denchi handoff started (source=%lu wrapper=%lu).",
        sourceProcessId, wrapperProcessId);
    return true;
}

} // namespace

LocaleEmulator& LocaleEmulator::getInstance() {
    static LocaleEmulator instance;
    return instance;
}

bool LocaleEmulator::initialize() {
    if (m_initialized) return true;

    m_codepage = Config::LE_Codepage;
    m_localeId = Config::LE_LocaleID;
    m_charset = Config::LE_Charset;
    m_timezone = Config::LE_Timezone[0] ? Config::LE_Timezone : L"Tokyo Standard Time";
    m_enabled = Config::EnableLE;
    m_initialized = true;
    return true;
}

bool LocaleEmulator::isBootstrappedByNepgear() const {
    wchar_t value[16] = {};
    return GetEnvironmentVariableW(kBootstrapEnvVar, value, _countof(value)) > 0
        && wcscmp(value, L"1") == 0;
}

bool LocaleEmulator::needsLocaleEmulation() const {
    if (!m_enabled || isBootstrappedByNepgear()) return false;
    if (GetACP() != m_codepage || GetOEMCP() != m_codepage ||
        GetUserDefaultLCID() != m_localeId || GetSystemDefaultLCID() != m_localeId ||
        (Config::LE_HookUILanguageMode != 0 &&
            GetUserDefaultUILanguage() != LANGIDFROMLCID(m_localeId))) {
        return true;
    }

    // Matching ACP alone does not mean the requested locale/time zone was
    // applied. Compare the effective rules, including an externally loaded LEP.
    TIME_ZONE_INFORMATION current = {};
    LepTimeZoneInformation expected = {};
    if (GetTimeZoneInformation(&current) == TIME_ZONE_ID_INVALID ||
        !BuildTimezone(m_timezone, expected)) return true;
    LepTimeFields standard = {}, daylight = {};
    CopySystemTime(current.StandardDate, standard);
    CopySystemTime(current.DaylightDate, daylight);
    return current.Bias != expected.Bias || current.StandardBias != expected.StandardBias ||
        current.DaylightBias != expected.DaylightBias ||
        memcmp(&standard, &expected.StandardStart, sizeof(standard)) != 0 ||
        memcmp(&daylight, &expected.DaylightStart, sizeof(daylight)) != 0 ||
        wcscmp(current.StandardName, expected.StandardName) != 0 ||
        wcscmp(current.DaylightName, expected.DaylightName) != 0;
}

bool LocaleEmulator::performLocaleEmulation() {
    if (!needsLocaleEmulation()) return false;
    return relaunchProcess();
}

bool LocaleEmulator::relaunchProcess() {
    LepEnvironmentBlock environmentBlock = {};
    if (!BuildEnvironmentBlock(m_codepage, m_localeId, m_charset, m_timezone,
        environmentBlock)) {
        return false;
    }

    std::wstring loaderPath;
    std::wstring corePath;
    if (!ResolveLepRuntime(loaderPath, corePath)) return false;

    HMODULE loader = LoadLibraryExW(loaderPath.c_str(), nullptr,
        LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!loader) {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] LoadLibraryExW(%S) failed (error %lu).",
            loaderPath.c_str(), GetLastError());
        return false;
    }

    const auto createProcess = ResolveCreateProcess(loader);
    if (!createProcess) {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Process creation export is missing from %S.",
            loaderPath.c_str());
        FreeLibrary(loader);
        return false;
    }

    std::wstring sourceExecutablePath;
    if (!GetExecutablePath(sourceExecutablePath)) {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] GetModuleFileNameW failed (error %lu).",
            GetLastError());
        FreeLibrary(loader);
        return false;
    }

    std::vector<wchar_t> sourceCommandLine;
    if (!CopyOriginalCommandLine(sourceCommandLine)) {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Could not copy the original command line.");
        FreeLibrary(loader);
        return false;
    }

    std::wstring executablePath = sourceExecutablePath;
    std::vector<wchar_t> commandLine = sourceCommandLine;
    const DWORD sourceParentProcessId = ParentProcessId();
    const std::wstring sourceParentPath = ProcessImagePath(sourceParentProcessId);
    const bool isSoftDenchiMain =
        _wcsicmp(FileNamePart(sourceExecutablePath).c_str(), L"main.bin") == 0
        && _wcsicmp(FileNamePart(sourceParentPath).c_str(), L"HGB.exe") == 0;
    if (isSoftDenchiMain) {
        const std::wstring launcherPath =
            JoinPath(ParentDirectory(sourceExecutablePath), L"HGB.exe");
        if (ExistingFile(launcherPath)) {
            executablePath = launcherPath;
            const std::wstring launcherCommandLine = L"\"" + launcherPath + L"\"";
            commandLine.assign(launcherCommandLine.begin(), launcherCommandLine.end());
            commandLine.push_back(L'\0');
        }
    }

    std::wstring currentDirectory = ParentDirectory(executablePath);
    std::wstring processDirectory;
    if (!isSoftDenchiMain
        && GetCurrentDirectoryString(processDirectory) && !processDirectory.empty()) {
        currentDirectory = processDirectory;
    }

    Utils::Log(
        "[LocaleEmulatorPlus] Relaunch source pid=%lu parent=%lu image=%S cmd=%S; target=%S cmd=%S cwd=%S.",
        GetCurrentProcessId(), sourceParentProcessId, sourceExecutablePath.c_str(),
        GetCommandLineW(), executablePath.c_str(), commandLine.data(), currentDirectory.c_str());

    if (isSoftDenchiMain && executablePath != sourceExecutablePath
        && SpawnDeferredSoftDenchiRelaunch(
            GetCurrentProcessId(), sourceParentProcessId)) {
        Utils::PreservePatchFilesForRelaunch();
        Utils::Log(
            "[LocaleEmulatorPlus] Deferred handoff accepted; exiting old main.bin.");
        FreeLibrary(loader);
        ExitProcess(0);
        return true;
    }

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    LepProcessInformation processInfo = {};

    wchar_t previousBootstrap[16] = {};
    const DWORD previousLength = GetEnvironmentVariableW(
        kBootstrapEnvVar, previousBootstrap, _countof(previousBootstrap));
    const bool hadPreviousBootstrap = previousLength > 0
        && previousLength < _countof(previousBootstrap);
    if (!SetEnvironmentVariableW(kBootstrapEnvVar, L"1")) {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Could not set bootstrap marker (error %lu).",
            GetLastError());
        FreeLibrary(loader);
        return false;
    }

    Utils::PreservePatchFilesForRelaunch();
    if (isSoftDenchiMain && executablePath != sourceExecutablePath) {
        Utils::Log(
            "[LocaleEmulatorPlus] Stopping old Soft-Denchi wrapper pid=%lu before relaunch.",
            sourceParentProcessId);
        TerminateSoftDenchiWrapper(sourceParentProcessId);
        Sleep(200);
    }
    SetLastError(ERROR_SUCCESS);
    const LONG status = CreateProcessWithCompatibleEnvironment(createProcess,
        environmentBlock,
        executablePath.c_str(),
        commandLine.data(),
        currentDirectory.empty() ? nullptr : currentDirectory.c_str(),
        startupInfo,
        processInfo);
    const DWORD lastError = GetLastError();
    FreeLibrary(loader);

    if (processInfo.hThread) CloseHandle(processInfo.hThread);
    if (processInfo.hProcess) CloseHandle(processInfo.hProcess);

    if (status >= 0) {
        Utils::Log("[LocaleEmulatorPlus] Relaunched with %S via %S (codepage %u, locale %u).",
            LocaleEmulatorPlusFileName(), LoaderDllFileName(), m_codepage, m_localeId);
        ExitProcess(0);
        return true;
    }

    if (hadPreviousBootstrap) {
        SetEnvironmentVariableW(kBootstrapEnvVar, previousBootstrap);
    } else {
        SetEnvironmentVariableW(kBootstrapEnvVar, nullptr);
    }
    Utils::Log(Utils::LOG_ERROR,
        "[LocaleEmulatorPlus] LepCreateProcess failed (NTSTATUS 0x%08lX, last error %lu).",
        static_cast<unsigned long>(status), lastError);
    return false;
}

#ifdef _WIN64
#pragma comment(linker, "/EXPORT:NepgearRelaunchHelper")
#else
#pragma comment(linker, "/EXPORT:NepgearRelaunchHelper=_NepgearRelaunchHelper@16")
#endif

extern "C" void CALLBACK NepgearRelaunchHelper(
    HWND, HINSTANCE, LPSTR commandLine, int) {
    DWORD sourceProcessId = 0;
    DWORD wrapperProcessId = 0;
    if (commandLine) {
        char* cursor = commandLine;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        char* end = nullptr;
        sourceProcessId = static_cast<DWORD>(strtoul(cursor, &end, 10));
        if (end) {
            cursor = end;
            while (*cursor == ' ' || *cursor == '\t') ++cursor;
            wrapperProcessId = static_cast<DWORD>(strtoul(cursor, nullptr, 10));
        }
    }
    if (sourceProcessId == 0) return;

    HANDLE sourceProcess = OpenProcess(SYNCHRONIZE, FALSE, sourceProcessId);
    if (sourceProcess) {
        WaitForSingleObject(sourceProcess, 15000);
        CloseHandle(sourceProcess);
    }

    if (wrapperProcessId != 0) {
        HANDLE wrapperProcess = OpenProcess(SYNCHRONIZE, FALSE, wrapperProcessId);
        if (wrapperProcess) {
            const DWORD waitResult = WaitForSingleObject(wrapperProcess, 5000);
            CloseHandle(wrapperProcess);
            if (waitResult == WAIT_TIMEOUT) {
                TerminateSoftDenchiWrapper(wrapperProcessId);
            }
        }
        // Remove any child left behind by the wrapper before creating the new
        // instance. The old main.bin can otherwise hold the game's mutex.
        TerminateSoftDenchiWrapper(wrapperProcessId);
    }
    Sleep(350);

    const std::wstring modulePath = ModulePathFromAddress(
        reinterpret_cast<LPCVOID>(&NepgearRelaunchHelper));
    const std::wstring gameRoot = ParentDirectory(modulePath);
    const std::wstring gamePath = JoinPath(gameRoot, L"HGB.exe");
    if (modulePath.empty() || !ExistingFile(gamePath)) return;

    SetEnvironmentVariableW(kHelperEnvironmentVariable, nullptr);
    SetEnvironmentVariableW(kBootstrapEnvironmentVariable, L"1");
    // Keep the persistent LEP runtime in %TEMP%. The configured redirect
    // folder may be archive-backed and must not be materialized on disk.
    SetEnvironmentVariableW(L"NEPGEAR_LEP_RUNTIME", nullptr);

    HMODULE helperModule = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&NepgearRelaunchHelper), &helperModule);
    if (helperModule) Config::LoadConfiguration(helperModule);
    LocaleEmulator::getInstance().initialize();

    const std::wstring commandLineText = QuoteCommandLinePath(gamePath);
    std::vector<wchar_t> targetCommandLine(
        commandLineText.begin(), commandLineText.end());
    targetCommandLine.push_back(L'\0');
    LepProcessInformation processInfo = {};
    LONG status = 0;
    const bool launched = LaunchConfiguredTarget(
        gamePath, targetCommandLine, gameRoot, processInfo, &status);
    if (processInfo.hThread) CloseHandle(processInfo.hThread);
    if (processInfo.hProcess) CloseHandle(processInfo.hProcess);

    if (launched) {
        Utils::Log(
            "[LocaleEmulatorPlus] Deferred target HGB.exe launched through the four-DLL runtime.");
        Sleep(250);
    } else {
        Utils::Log(Utils::LOG_ERROR,
            "[LocaleEmulatorPlus] Deferred target launch failed (NTSTATUS 0x%08lX).",
            static_cast<unsigned long>(status));
    }
}

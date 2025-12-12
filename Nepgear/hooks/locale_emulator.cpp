#include "../pch.h"
#include "locale_emulator.h"
#include "config.h"
#include <vector>
#include <stdio.h>

typedef struct ML_PROCESS_INFORMATION : PROCESS_INFORMATION { PVOID FirstCallLdrLoadDll; } ML_PROCESS_INFORMATION, * PML_PROCESS_INFORMATION;
typedef struct _TIME_FIELDS { SHORT Year; SHORT Month; SHORT Day; SHORT Hour; SHORT Minute; SHORT Second; SHORT Milliseconds; SHORT Weekday; } TIME_FIELDS, * PTIME_FIELDS;
typedef struct _RTL_TIME_ZONE_INFORMATION { LONG Bias; WCHAR StandardName[32]; TIME_FIELDS StandardStart; LONG StandardBias; WCHAR DaylightName[32]; TIME_FIELDS DaylightStart; LONG DaylightBias; } RTL_TIME_ZONE_INFORMATION, * PRTL_TIME_ZONE_INFORMATION;
typedef struct _REG_TZI_FORMAT { int Bias; int StandardBias; int DaylightBias; _SYSTEMTIME StandardDate; _SYSTEMTIME DaylightDate; } REG_TZI_FORMAT;
typedef struct { USHORT Length; USHORT MaximumLength; union { PWSTR Buffer; ULONG64 Dummy; }; } UNICODE_STRING3264, * PUNICODE_STRING3264;
typedef UNICODE_STRING3264 UNICODE_STRING64;
typedef PUNICODE_STRING3264 PUNICODE_STRING64;
typedef struct { ULONG64 Root; UNICODE_STRING64 SubKey; UNICODE_STRING64 ValueName; ULONG DataType; PVOID64 Data; ULONG64 DataSize; } REGISTRY_ENTRY64;
typedef struct { REGISTRY_ENTRY64 Original; REGISTRY_ENTRY64 Redirected; } REGISTRY_REDIRECTION_ENTRY64, * PREGISTRY_REDIRECTION_ENTRY64;
typedef struct { ULONG AnsiCodePage; ULONG OemCodePage; ULONG LocaleID; ULONG DefaultCharset; ULONG HookUILanguageApi; WCHAR DefaultFaceName[LF_FACESIZE]; RTL_TIME_ZONE_INFORMATION Timezone; ULONG64 NumberOfRegistryRedirectionEntries; REGISTRY_REDIRECTION_ENTRY64 RegistryReplacement[1]; } LOCALE_ENUMLATOR_ENVIRONMENT_BLOCK, * PLOCALE_ENUMLATOR_ENVIRONMENT_BLOCK, LEB, * PLEB;
typedef DWORD(WINAPI* LeCreateProcess_t)(PLEB leb, PCWSTR appName, PCWSTR cmdLine, PCWSTR curDir, ULONG flags, LPSTARTUPINFOW startInfo, PML_PROCESS_INFORMATION procInfo, LPSECURITY_ATTRIBUTES procAttr, LPSECURITY_ATTRIBUTES threadAttr, PVOID env, HANDLE token);

LocaleEmulator& LocaleEmulator::getInstance() {
    static LocaleEmulator instance;
    return instance;
}

bool LocaleEmulator::initialize() {
    if (m_initialized) return true;

    m_codepage = Config::LE_Codepage;
    m_localeId = Config::LE_LocaleID;
    m_charset = Config::LE_Charset;
    m_timezone = Config::LE_Timezone;
    m_enabled = Config::EnableLE;
    m_initialized = true;
    return true;
}

bool LocaleEmulator::needsLocaleEmulation() const {
    if (!m_enabled) return false;
    return GetACP() != m_codepage;
}

bool LocaleEmulator::performLocaleEmulation() {
    if (!m_enabled || !needsLocaleEmulation()) return false;
    return relaunchProcess();
}

bool LocaleEmulator::relaunchProcess() {
    LEB leb{};
    leb.AnsiCodePage = m_codepage;
    leb.OemCodePage = m_codepage;
    leb.LocaleID = m_localeId;
    leb.DefaultCharset = m_charset;

    HKEY hTimeZone;
    std::wstring key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\" + m_timezone;
    if (RegOpenKeyW(HKEY_LOCAL_MACHINE, key.c_str(), &hTimeZone) == ERROR_SUCCESS) {
        DWORD bufferSize = sizeof(leb.Timezone.StandardName);
        RegGetValueW(hTimeZone, nullptr, L"Std", RRF_RT_REG_SZ, nullptr, leb.Timezone.StandardName, &bufferSize);
        bufferSize = sizeof(leb.Timezone.DaylightName);
        RegGetValueW(hTimeZone, nullptr, L"Dlt", RRF_RT_REG_SZ, nullptr, leb.Timezone.DaylightName, &bufferSize);
        REG_TZI_FORMAT timeZoneInfo;
        bufferSize = sizeof(timeZoneInfo);
        RegGetValueW(hTimeZone, nullptr, L"TZI", RRF_RT_REG_BINARY, nullptr, &timeZoneInfo, &bufferSize);
        leb.Timezone.Bias = timeZoneInfo.Bias;
        leb.Timezone.StandardBias = timeZoneInfo.StandardBias;
        leb.Timezone.DaylightBias = 0;
        RegCloseKey(hTimeZone);
    }

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    const wchar_t* commandLine = GetCommandLineW();

    wchar_t currentDirectory[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, currentDirectory);

    STARTUPINFOW startInfo{};
    startInfo.cb = sizeof(startInfo);
    ML_PROCESS_INFORMATION processInfo{};

    const HMODULE hLoader = LoadLibraryW(L"LoaderDll.dll");
    if (hLoader == nullptr) {
        MessageBoxW(NULL, L"LoaderDll.dll missing", L"LE Error", MB_OK | MB_ICONERROR);
        return false;
    }

    const auto LeCreateProcess = reinterpret_cast<LeCreateProcess_t>(GetProcAddress(hLoader, "LeCreateProcess"));
    if (LeCreateProcess == nullptr) {
        MessageBoxW(NULL, L"LeCreateProcess not found", L"LE Error", MB_OK | MB_ICONERROR);
        FreeLibrary(hLoader);
        return false;
    }

    const auto result = LeCreateProcess(&leb, exePath, commandLine, currentDirectory, 0, &startInfo, &processInfo, nullptr, nullptr, nullptr, nullptr);
    FreeLibrary(hLoader);

    if (result == ERROR_SUCCESS) {
        ExitProcess(0);
        return true;
    }
    else {
        wchar_t errorMsg[256];
        swprintf_s(errorMsg, L"LeCreateProcess failed: %lu", result);
        MessageBoxW(NULL, errorMsg, L"LE Error", MB_OK | MB_ICONERROR);
        return false;
    }
}

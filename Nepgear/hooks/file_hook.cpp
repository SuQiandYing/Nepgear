#include "../pch.h"
#include <windows.h>
#include <stdio.h>
#include <shlwapi.h>
#include <map>
#include <string>
#include <mutex>
#include "file_hook.h"
#include "config.h"
#include "utils.h"
#include "../detours.h"
#include "archive.h"

#pragma comment(lib, "detours.lib")
#pragma comment(lib, "Shlwapi.lib")

typedef HANDLE(WINAPI* pCreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE(WINAPI* pCreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE(WINAPI* pFindFirstFileA)(LPCSTR, LPWIN32_FIND_DATAA);
typedef HANDLE(WINAPI* pFindFirstFileW)(LPCWSTR, LPWIN32_FIND_DATAW);
typedef BOOL(WINAPI* pFindNextFileA)(HANDLE, LPWIN32_FIND_DATAA);
typedef BOOL(WINAPI* pFindNextFileW)(HANDLE, LPWIN32_FIND_DATAW);
typedef BOOL(WINAPI* pFindClose)(HANDLE);
typedef DWORD(WINAPI* pGetFileAttributesA)(LPCSTR);
typedef DWORD(WINAPI* pGetFileAttributesW)(LPCWSTR);
typedef BOOL(WINAPI* pGetFileAttributesExA)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
typedef BOOL(WINAPI* pGetFileAttributesExW)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);

static pCreateFileA orgCreateFileA = CreateFileA;
static pCreateFileW orgCreateFileW = CreateFileW;
static pFindFirstFileA orgFindFirstFileA = FindFirstFileA;
static pFindFirstFileW orgFindFirstFileW = FindFirstFileW;
static pFindNextFileA orgFindNextFileA = FindNextFileA;
static pFindNextFileW orgFindNextFileW = FindNextFileW;
static pFindClose orgFindClose = FindClose;
static pGetFileAttributesA orgGetFileAttributesA = GetFileAttributesA;
static pGetFileAttributesW orgGetFileAttributesW = GetFileAttributesW;
static pGetFileAttributesExA orgGetFileAttributesExA = GetFileAttributesExA;
static pGetFileAttributesExW orgGetFileAttributesExW = GetFileAttributesExW;

static char g_GameRootA[MAX_PATH] = { 0 };
static char g_PatchRootA[MAX_PATH] = { 0 };
static wchar_t g_GameRootW[MAX_PATH] = { 0 };
static wchar_t g_PatchRootW[MAX_PATH] = { 0 };
static size_t g_GameRootLenA = 0;
static size_t g_GameRootLenW = 0;
static size_t g_PatchRootLenA = 0;
static size_t g_PatchRootLenW = 0;
static bool g_Initialized = false;

struct SearchContext {
    HANDLE realHandle;
    bool isSearchingPatch;
    std::string patternA;
    std::wstring patternW;
    bool isWideChar;
};

static std::map<HANDLE, SearchContext*> g_SearchMap;
static std::mutex g_SearchMutex;
static uintptr_t g_FakeHandleCounter = 0x100000;

void InitPaths() {
    if (g_Initialized) return;

    if (GetModuleFileNameA(NULL, g_GameRootA, MAX_PATH)) {
        PathRemoveFileSpecA(g_GameRootA);
        PathAddBackslashA(g_GameRootA);
        g_GameRootLenA = strlen(g_GameRootA);
    }
    if (GetModuleFileNameW(NULL, g_GameRootW, MAX_PATH)) {
        PathRemoveFileSpecW(g_GameRootW);
        PathAddBackslashW(g_GameRootW);
        g_GameRootLenW = wcslen(g_GameRootW);
    }

    Utils::Log("[Path] GameRootW: %S", g_GameRootW);
    Utils::Log("[Path] GameRootA: %s", g_GameRootA);

    const char* tempRootA = Archive::GetTempRootA();
    const wchar_t* tempRootW = Archive::GetTempRootW();

    if (tempRootA && tempRootW) {
        strcpy_s(g_PatchRootA, MAX_PATH, tempRootA);
        PathAddBackslashA(g_PatchRootA);
        wcscpy_s(g_PatchRootW, MAX_PATH, tempRootW);
        PathAddBackslashW(g_PatchRootW);
        Utils::Log("[Path] Using Archive Temp Root: %S", g_PatchRootW);
    }
    else {
        strcpy_s(g_PatchRootA, MAX_PATH, g_GameRootA);
        PathAppendA(g_PatchRootA, Config::RedirectFolderA);
        PathAddBackslashA(g_PatchRootA);
        wcscpy_s(g_PatchRootW, MAX_PATH, g_GameRootW);
        PathAppendW(g_PatchRootW, Config::RedirectFolderW);
        PathAddBackslashW(g_PatchRootW);
        Utils::Log("[Path] Using Local Patch Root: %S", g_PatchRootW);
    }

    g_PatchRootLenA = strlen(g_PatchRootA);
    g_PatchRootLenW = wcslen(g_PatchRootW);
    g_Initialized = true;
}

bool TryRedirectA(LPCSTR inPath, char* outPath) {
    if (!inPath || !Config::EnableFileHook) return false;
    InitPaths();
    char fullAbsPath[MAX_PATH];
    if (GetFullPathNameA(inPath, MAX_PATH, fullAbsPath, NULL) == 0) return false;

    if (_strnicmp(fullAbsPath, g_PatchRootA, g_PatchRootLenA) == 0) return false;

    if (_strnicmp(fullAbsPath, g_GameRootA, g_GameRootLenA) == 0) {
        const char* relativePath = fullAbsPath + g_GameRootLenA;
        char candidatePath[MAX_PATH];
        strcpy_s(candidatePath, MAX_PATH, g_PatchRootA);
        strcat_s(candidatePath, MAX_PATH, relativePath);
        if (PathFileExistsA(candidatePath) && !PathIsDirectoryA(candidatePath)) {
            strcpy_s(outPath, MAX_PATH, candidatePath);
            return true;
        }
    }
    return false;
}

bool TryRedirectW(LPCWSTR inPath, wchar_t* outPath) {
    if (!inPath || !Config::EnableFileHook) return false;
    InitPaths();
    wchar_t fullAbsPath[MAX_PATH];
    if (GetFullPathNameW(inPath, MAX_PATH, fullAbsPath, NULL) == 0) return false;

    if (_wcsnicmp(fullAbsPath, g_PatchRootW, g_PatchRootLenW) == 0) return false;

    if (_wcsnicmp(fullAbsPath, g_GameRootW, g_GameRootLenW) == 0) {
        const wchar_t* relativePath = fullAbsPath + g_GameRootLenW;
        wchar_t candidatePath[MAX_PATH];
        wcscpy_s(candidatePath, MAX_PATH, g_PatchRootW);
        wcscat_s(candidatePath, MAX_PATH, relativePath);
        if (PathFileExistsW(candidatePath) && !PathIsDirectoryW(candidatePath)) {
            wcscpy_s(outPath, MAX_PATH, candidatePath);
            return true;
        }
    }
    return false;
}

HANDLE WINAPI newCreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    char redirectPath[MAX_PATH];
    if (TryRedirectA(lpFileName, redirectPath)) {
        if (Config::EnableDebug) Utils::Log("[FileA] [REDIRECT] %s -> %s", lpFileName, redirectPath);
        return orgCreateFileA(redirectPath, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }
    return orgCreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

HANDLE WINAPI newCreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    wchar_t redirectPath[MAX_PATH];
    if (TryRedirectW(lpFileName, redirectPath)) {
        if (Config::EnableDebug) Utils::Log("[FileW] [REDIRECT] %S -> %S", lpFileName, redirectPath);
        return orgCreateFileW(redirectPath, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }
    return orgCreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

DWORD WINAPI newGetFileAttributesA(LPCSTR lpFileName) {
    char redirectPath[MAX_PATH];
    if (TryRedirectA(lpFileName, redirectPath)) {
        if (Config::EnableDebug) Utils::Log("[AttribA] [REDIRECT] %s -> %s", lpFileName, redirectPath);
        return orgGetFileAttributesA(redirectPath);
    }
    return orgGetFileAttributesA(lpFileName);
}

DWORD WINAPI newGetFileAttributesW(LPCWSTR lpFileName) {
    wchar_t redirectPath[MAX_PATH];
    if (TryRedirectW(lpFileName, redirectPath)) {
        if (Config::EnableDebug) Utils::Log("[AttribW] [REDIRECT] %S -> %S", lpFileName, redirectPath);
        return orgGetFileAttributesW(redirectPath);
    }
    return orgGetFileAttributesW(lpFileName);
}

BOOL WINAPI newGetFileAttributesExA(LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation) {
    char redirectPath[MAX_PATH];
    if (TryRedirectA(lpFileName, redirectPath)) {
        return orgGetFileAttributesExA(redirectPath, fInfoLevelId, lpFileInformation);
    }
    return orgGetFileAttributesExA(lpFileName, fInfoLevelId, lpFileInformation);
}

BOOL WINAPI newGetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation) {
    wchar_t redirectPath[MAX_PATH];
    if (TryRedirectW(lpFileName, redirectPath)) {
        return orgGetFileAttributesExW(redirectPath, fInfoLevelId, lpFileInformation);
    }
    return orgGetFileAttributesExW(lpFileName, fInfoLevelId, lpFileInformation);
}

HANDLE WINAPI newFindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData) {
    if (!Config::EnableFileHook) return orgFindFirstFileA(lpFileName, lpFindFileData);
    InitPaths();

    char fullPath[MAX_PATH];
    GetFullPathNameA(lpFileName, MAX_PATH, fullPath, NULL);

    bool isRootSearch = false;
    char searchPattern[MAX_PATH] = { 0 };

    if (_strnicmp(fullPath, g_GameRootA, g_GameRootLenA) == 0) {
        const char* rel = fullPath + g_GameRootLenA;
        if (strchr(rel, '\\') == NULL && strchr(rel, '/') == NULL) {
            isRootSearch = true;
            strcpy_s(searchPattern, MAX_PATH, rel);
        }
    }

    HANDLE hReal = orgFindFirstFileA(lpFileName, lpFindFileData);

    if (isRootSearch) {
        if (hReal == INVALID_HANDLE_VALUE) {
            char patchSearch[MAX_PATH];
            strcpy_s(patchSearch, MAX_PATH, g_PatchRootA);
            strcat_s(patchSearch, MAX_PATH, searchPattern);
            hReal = orgFindFirstFileA(patchSearch, lpFindFileData);
            if (hReal != INVALID_HANDLE_VALUE) {
                std::lock_guard<std::mutex> lock(g_SearchMutex);
                HANDLE hFake = (HANDLE)++g_FakeHandleCounter;
                SearchContext* ctx = new SearchContext{ hReal, true, searchPattern, L"", false };
                g_SearchMap[hFake] = ctx;
                if (Config::EnableDebug) Utils::Log("[FindA] Direct Patch Search: %s", patchSearch);
                return hFake;
            }
            return INVALID_HANDLE_VALUE;
        }

        std::lock_guard<std::mutex> lock(g_SearchMutex);
        HANDLE hFake = (HANDLE)++g_FakeHandleCounter;
        SearchContext* ctx = new SearchContext{ hReal, false, searchPattern, L"", false };
        g_SearchMap[hFake] = ctx;
        return hFake;
    }

    return hReal;
}

BOOL WINAPI newFindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData) {
    SearchContext* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_SearchMutex);
        auto it = g_SearchMap.find(hFindFile);
        if (it != g_SearchMap.end()) ctx = it->second;
    }

    if (!ctx) return orgFindNextFileA(hFindFile, lpFindFileData);

    if (orgFindNextFileA(ctx->realHandle, lpFindFileData)) {
        return TRUE;
    }

    if (!ctx->isSearchingPatch) {
        orgFindClose(ctx->realHandle);
        char patchSearch[MAX_PATH];
        strcpy_s(patchSearch, MAX_PATH, g_PatchRootA);
        strcat_s(patchSearch, MAX_PATH, ctx->patternA.c_str());
        HANDLE hPatch = orgFindFirstFileA(patchSearch, lpFindFileData);
        if (hPatch != INVALID_HANDLE_VALUE) {
            ctx->realHandle = hPatch;
            ctx->isSearchingPatch = true;
            if (Config::EnableDebug) Utils::Log("[FindA] Switch to Patch: %s", patchSearch);
            return TRUE;
        }
    }
    return FALSE;
}

HANDLE WINAPI newFindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData) {
    if (!Config::EnableFileHook) return orgFindFirstFileW(lpFileName, lpFindFileData);
    InitPaths();

    wchar_t fullPath[MAX_PATH];
    GetFullPathNameW(lpFileName, MAX_PATH, fullPath, NULL);

    bool isRootSearch = false;
    wchar_t searchPattern[MAX_PATH] = { 0 };

    if (_wcsnicmp(fullPath, g_GameRootW, g_GameRootLenW) == 0) {
        const wchar_t* rel = fullPath + g_GameRootLenW;
        if (wcschr(rel, L'\\') == NULL && wcschr(rel, L'/') == NULL) {
            isRootSearch = true;
            wcscpy_s(searchPattern, MAX_PATH, rel);
        }
    }

    HANDLE hReal = orgFindFirstFileW(lpFileName, lpFindFileData);

    if (isRootSearch) {
        if (hReal == INVALID_HANDLE_VALUE) {
            wchar_t patchSearch[MAX_PATH];
            wcscpy_s(patchSearch, MAX_PATH, g_PatchRootW);
            wcscat_s(patchSearch, MAX_PATH, searchPattern);
            hReal = orgFindFirstFileW(patchSearch, lpFindFileData);
            if (hReal != INVALID_HANDLE_VALUE) {
                std::lock_guard<std::mutex> lock(g_SearchMutex);
                HANDLE hFake = (HANDLE)++g_FakeHandleCounter;
                SearchContext* ctx = new SearchContext{ hReal, true, "", searchPattern, true };
                g_SearchMap[hFake] = ctx;
                if (Config::EnableDebug) Utils::Log("[FindW] Direct Patch Search: %S", patchSearch);
                return hFake;
            }
            return INVALID_HANDLE_VALUE;
        }

        std::lock_guard<std::mutex> lock(g_SearchMutex);
        HANDLE hFake = (HANDLE)++g_FakeHandleCounter;
        SearchContext* ctx = new SearchContext{ hReal, false, "", searchPattern, true };
        g_SearchMap[hFake] = ctx;
        return hFake;
    }
    return hReal;
}

BOOL WINAPI newFindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData) {
    SearchContext* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_SearchMutex);
        auto it = g_SearchMap.find(hFindFile);
        if (it != g_SearchMap.end()) ctx = it->second;
    }

    if (!ctx) return orgFindNextFileW(hFindFile, lpFindFileData);

    if (orgFindNextFileW(ctx->realHandle, lpFindFileData)) {
        return TRUE;
    }

    if (!ctx->isSearchingPatch) {
        orgFindClose(ctx->realHandle);
        wchar_t patchSearch[MAX_PATH];
        wcscpy_s(patchSearch, MAX_PATH, g_PatchRootW);
        wcscat_s(patchSearch, MAX_PATH, ctx->patternW.c_str());
        HANDLE hPatch = orgFindFirstFileW(patchSearch, lpFindFileData);
        if (hPatch != INVALID_HANDLE_VALUE) {
            ctx->realHandle = hPatch;
            ctx->isSearchingPatch = true;
            if (Config::EnableDebug) Utils::Log("[FindW] Switch to Patch: %S", patchSearch);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL WINAPI newFindClose(HANDLE hFindFile) {
    SearchContext* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_SearchMutex);
        auto it = g_SearchMap.find(hFindFile);
        if (it != g_SearchMap.end()) {
            ctx = it->second;
            g_SearchMap.erase(it);
        }
    }

    if (ctx) {
        BOOL ret = orgFindClose(ctx->realHandle);
        delete ctx;
        return ret;
    }
    return orgFindClose(hFindFile);
}

namespace Hooks {
    void InstallFileHook() {
        if (!Config::EnableFileHook) return;
        InitPaths();
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)orgCreateFileA, newCreateFileA);
        DetourAttach(&(PVOID&)orgCreateFileW, newCreateFileW);
        DetourAttach(&(PVOID&)orgFindFirstFileA, newFindFirstFileA);
        DetourAttach(&(PVOID&)orgFindFirstFileW, newFindFirstFileW);
        DetourAttach(&(PVOID&)orgFindNextFileA, newFindNextFileA);
        DetourAttach(&(PVOID&)orgFindNextFileW, newFindNextFileW);
        DetourAttach(&(PVOID&)orgFindClose, newFindClose);
        DetourAttach(&(PVOID&)orgGetFileAttributesA, newGetFileAttributesA);
        DetourAttach(&(PVOID&)orgGetFileAttributesW, newGetFileAttributesW);
        DetourAttach(&(PVOID&)orgGetFileAttributesExA, newGetFileAttributesExA);
        DetourAttach(&(PVOID&)orgGetFileAttributesExW, newGetFileAttributesExW);
        DetourTransactionCommit();
        Utils::Log("[Core] File Hook Installed.");
    }
}

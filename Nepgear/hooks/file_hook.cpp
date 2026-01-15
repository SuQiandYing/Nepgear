#include "../pch.h"
#include <windows.h>
#include <stdio.h>
#include <shlwapi.h>
#include <string>
#include "file_hook.h"
#include "config.h"
#include "utils.h"
#include "../detours.h"
#include "vfs.h"

#pragma comment(lib, "detours.lib")
#pragma comment(lib, "Shlwapi.lib")

typedef HANDLE(WINAPI* pCreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE(WINAPI* pCreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL(WINAPI* pReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef DWORD(WINAPI* pSetFilePointer)(HANDLE, LONG, PLONG, DWORD);
typedef BOOL(WINAPI* pSetFilePointerEx)(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);
typedef DWORD(WINAPI* pGetFileSize)(HANDLE, LPDWORD);
typedef BOOL(WINAPI* pGetFileSizeEx)(HANDLE, PLARGE_INTEGER);
typedef BOOL(WINAPI* pCloseHandle)(HANDLE);
typedef BOOL(WINAPI* pGetFileInformationByHandle)(HANDLE, LPBY_HANDLE_FILE_INFORMATION);
typedef DWORD(WINAPI* pGetFileType)(HANDLE);
typedef DWORD(WINAPI* pGetFileAttributesA)(LPCSTR);
typedef DWORD(WINAPI* pGetFileAttributesW)(LPCWSTR);
typedef BOOL(WINAPI* pGetFileAttributesExA)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
typedef BOOL(WINAPI* pGetFileAttributesExW)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
typedef HANDLE(WINAPI* pFindFirstFileW)(LPCWSTR, LPWIN32_FIND_DATAW);
typedef BOOL(WINAPI* pFindNextFileW)(HANDLE, LPWIN32_FIND_DATAW);
typedef HANDLE(WINAPI* pFindFirstFileA)(LPCSTR, LPWIN32_FIND_DATAA);
typedef BOOL(WINAPI* pFindNextFileA)(HANDLE, LPWIN32_FIND_DATAA);
typedef BOOL(WINAPI* pFindClose)(HANDLE);

static pCreateFileA orgCreateFileA = CreateFileA;
static pCreateFileW orgCreateFileW = CreateFileW;
static pReadFile orgReadFile = ReadFile;
static pSetFilePointer orgSetFilePointer = SetFilePointer;
static pSetFilePointerEx orgSetFilePointerEx = SetFilePointerEx;
static pGetFileSize orgGetFileSize = GetFileSize;
static pGetFileSizeEx orgGetFileSizeEx = GetFileSizeEx;
static pCloseHandle orgCloseHandle = CloseHandle;
static pGetFileInformationByHandle orgGetFileInformationByHandle = GetFileInformationByHandle;
static pGetFileType orgGetFileType = GetFileType;
static pGetFileAttributesA orgGetFileAttributesA = GetFileAttributesA;
static pGetFileAttributesW orgGetFileAttributesW = GetFileAttributesW;
static pGetFileAttributesExA orgGetFileAttributesExA = GetFileAttributesExA;
static pGetFileAttributesExW orgGetFileAttributesExW = GetFileAttributesExW;
static pFindFirstFileW orgFindFirstFileW = FindFirstFileW;
static pFindNextFileW orgFindNextFileW = FindNextFileW;
static pFindFirstFileA orgFindFirstFileA = FindFirstFileA;
static pFindNextFileA orgFindNextFileA = FindNextFileA;
static pFindClose orgFindClose = FindClose;

static char g_GameRootA[MAX_PATH] = { 0 };
static wchar_t g_GameRootW[MAX_PATH] = { 0 };
static size_t g_GameRootLenA = 0;
static size_t g_GameRootLenW = 0;
static bool g_Initialized = false;

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
    Utils::LogW(L"[Path] GameRootW: %s", g_GameRootW);
    Utils::Log("[Path] GameRootA: %s", g_GameRootA);
    g_Initialized = true;
}

bool GetRelativePathA(LPCSTR inPath, char* outRelPath) {
    if (!inPath) return false;
    char fullAbsPath[MAX_PATH];
    if (GetFullPathNameA(inPath, MAX_PATH, fullAbsPath, NULL) == 0) return false;
    if (_strnicmp(fullAbsPath, g_GameRootA, g_GameRootLenA) == 0) {
        strcpy_s(outRelPath, MAX_PATH, fullAbsPath + g_GameRootLenA);
        return true;
    }
    return false;
}

bool GetRelativePathW(LPCWSTR inPath, wchar_t* outRelPath) {
    if (!inPath) return false;
    wchar_t fullAbsPath[MAX_PATH];
    if (GetFullPathNameW(inPath, MAX_PATH, fullAbsPath, NULL) == 0) return false;
    if (_wcsnicmp(fullAbsPath, g_GameRootW, g_GameRootLenW) == 0) {
        wcscpy_s(outRelPath, MAX_PATH, fullAbsPath + g_GameRootLenW);
        return true;
    }
    return false;
}

static inline bool IsVirtualHandleRange(HANDLE h) {
    uintptr_t v = (uintptr_t)h;
    return v >= 0xBF000000 && v < 0xC0000000;
}

HANDLE WINAPI newCreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    if (!Config::EnableFileHook || !VFS::IsActive()) {
        return orgCreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }
    __try {
        InitPaths();
        char relPath[MAX_PATH];
        if (GetRelativePathA(lpFileName, relPath)) {
            if (VFS::HasVirtualFileA(relPath)) {
                HANDLE vHandle = VFS::OpenVirtualFileA(relPath);
                if (vHandle != INVALID_HANDLE_VALUE) {
                    if (Config::EnableDebug) Utils::Log("[VFS-FileA] %s -> handle %p", lpFileName, vHandle);
                    return vHandle;
                }
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Utils::Log("[VFS-FileA] Exception in hook for: %s", lpFileName ? lpFileName : "(null)");
    }
    return orgCreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

HANDLE WINAPI newCreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    if (!Config::EnableFileHook || !VFS::IsActive()) {
        return orgCreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }
    __try {
        InitPaths();
        wchar_t relPath[MAX_PATH];
        if (GetRelativePathW(lpFileName, relPath)) {
            if (VFS::HasVirtualFile(relPath)) {
                HANDLE vHandle = VFS::OpenVirtualFile(relPath);
                if (vHandle != INVALID_HANDLE_VALUE) {
                    if (Config::EnableDebug) Utils::Log("[VFS-FileW] %S -> handle %p", lpFileName, vHandle);
                    return vHandle;
                }
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Utils::Log("[VFS-FileW] Exception in hook for: %S", lpFileName ? lpFileName : L"(null)");
    }
    return orgCreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

BOOL WINAPI newReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) {
    __try {
        if (IsVirtualHandleRange(hFile) && VFS::IsVirtualHandle(hFile)) {
            return VFS::ReadVirtualFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Utils::Log("[VFS] Exception in newReadFile for handle %p", hFile);
    }
    return orgReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

DWORD WINAPI newSetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) {
    __try {
        if (IsVirtualHandleRange(hFile) && VFS::IsVirtualHandle(hFile)) {
            return VFS::SetVirtualFilePointer(hFile, lDistanceToMove, lpDistanceToMoveHigh, dwMoveMethod);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Utils::Log("[VFS] Exception in newSetFilePointer for handle %p", hFile);
    }
    return orgSetFilePointer(hFile, lDistanceToMove, lpDistanceToMoveHigh, dwMoveMethod);
}

BOOL WINAPI newSetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod) {
    __try {
        if (IsVirtualHandleRange(hFile) && VFS::IsVirtualHandle(hFile)) {
            return VFS::SetVirtualFilePointerEx(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Utils::Log("[VFS] Exception in newSetFilePointerEx for handle %p", hFile);
    }
    return orgSetFilePointerEx(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod);
}

DWORD WINAPI newGetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh) {
    __try {
        if (IsVirtualHandleRange(hFile) && VFS::IsVirtualHandle(hFile)) {
            return VFS::GetVirtualFileSize(hFile, lpFileSizeHigh);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Utils::Log("[VFS] Exception in newGetFileSize for handle %p", hFile);
    }
    return orgGetFileSize(hFile, lpFileSizeHigh);
}

BOOL WINAPI newGetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize) {
    __try {
        if (IsVirtualHandleRange(hFile) && VFS::IsVirtualHandle(hFile)) {
            return VFS::GetVirtualFileSizeEx(hFile, lpFileSize);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Utils::Log("[VFS] Exception in newGetFileSizeEx for handle %p", hFile);
    }
    return orgGetFileSizeEx(hFile, lpFileSize);
}

BOOL WINAPI newCloseHandle(HANDLE hObject) {
    __try {
        if (IsVirtualHandleRange(hObject) && VFS::IsVirtualHandle(hObject)) {
            return VFS::CloseVirtualHandle(hObject);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Utils::Log("[VFS] Exception in newCloseHandle for handle %p", hObject);
    }
    return orgCloseHandle(hObject);
}

BOOL WINAPI newGetFileInformationByHandle(HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation) {
    __try {
        if (IsVirtualHandleRange(hFile) && VFS::IsVirtualHandle(hFile)) {
            return VFS::GetVirtualFileInformationByHandle(hFile, lpFileInformation);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Utils::Log("[VFS] Exception in newGetFileInformationByHandle for handle %p", hFile);
    }
    return orgGetFileInformationByHandle(hFile, lpFileInformation);
}

DWORD WINAPI newGetFileType(HANDLE hFile) {
    __try {
        if (IsVirtualHandleRange(hFile) && VFS::IsVirtualHandle(hFile)) {
            return VFS::GetVirtualFileType(hFile);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Utils::Log("[VFS] Exception in newGetFileType for handle %p", hFile);
    }
    return orgGetFileType(hFile);
}

DWORD WINAPI newGetFileAttributesA(LPCSTR lpFileName) {
    if (!Config::EnableFileHook || !VFS::IsActive()) {
        return orgGetFileAttributesA(lpFileName);
    }
    InitPaths();
    char relPath[MAX_PATH];
    if (GetRelativePathA(lpFileName, relPath)) {
        if (VFS::HasVirtualFileA(relPath)) {
            if (Config::EnableDebug) Utils::Log("[VFS-AttribA] %s exists", lpFileName);
            return FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY;
        }
    }
    return orgGetFileAttributesA(lpFileName);
}

DWORD WINAPI newGetFileAttributesW(LPCWSTR lpFileName) {
    if (!Config::EnableFileHook || !VFS::IsActive()) {
        return orgGetFileAttributesW(lpFileName);
    }
    InitPaths();
    wchar_t relPath[MAX_PATH];
    if (GetRelativePathW(lpFileName, relPath)) {
        if (VFS::HasVirtualFile(relPath)) {
            if (Config::EnableDebug) Utils::Log("[VFS-AttribW] %S exists", lpFileName);
            return FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY;
        }
    }
    return orgGetFileAttributesW(lpFileName);
}

BOOL WINAPI newGetFileAttributesExA(LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation) {
    if (!Config::EnableFileHook || !VFS::IsActive()) {
        return orgGetFileAttributesExA(lpFileName, fInfoLevelId, lpFileInformation);
    }
    InitPaths();
    char relPath[MAX_PATH];
    if (GetRelativePathA(lpFileName, relPath)) {
        if (VFS::HasVirtualFileA(relPath)) {
            HANDLE vHandle = VFS::OpenVirtualFileA(relPath);
            if (vHandle != INVALID_HANDLE_VALUE) {
                if (fInfoLevelId == GetFileExInfoStandard && lpFileInformation) {
                    WIN32_FILE_ATTRIBUTE_DATA* data = (WIN32_FILE_ATTRIBUTE_DATA*)lpFileInformation;
                    ZeroMemory(data, sizeof(WIN32_FILE_ATTRIBUTE_DATA));
                    data->dwFileAttributes = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY;
                    DWORD sizeHigh = 0;
                    if (IsVirtualHandleRange(vHandle)) {
                        data->nFileSizeLow = VFS::GetVirtualFileSize(vHandle, &sizeHigh);
                        VFS::CloseVirtualHandle(vHandle);
                    } else {
                        data->nFileSizeLow = GetFileSize(vHandle, &sizeHigh);
                        CloseHandle(vHandle);
                    }
                    data->nFileSizeHigh = sizeHigh;
                }
                return TRUE;
            }
        }
    }
    return orgGetFileAttributesExA(lpFileName, fInfoLevelId, lpFileInformation);
}

BOOL WINAPI newGetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation) {
    if (!Config::EnableFileHook || !VFS::IsActive()) {
        return orgGetFileAttributesExW(lpFileName, fInfoLevelId, lpFileInformation);
    }
    InitPaths();
    wchar_t relPath[MAX_PATH];
    if (GetRelativePathW(lpFileName, relPath)) {
        if (VFS::HasVirtualFile(relPath)) {
            HANDLE vHandle = VFS::OpenVirtualFile(relPath);
            if (vHandle != INVALID_HANDLE_VALUE) {
                if (fInfoLevelId == GetFileExInfoStandard && lpFileInformation) {
                    WIN32_FILE_ATTRIBUTE_DATA* data = (WIN32_FILE_ATTRIBUTE_DATA*)lpFileInformation;
                    ZeroMemory(data, sizeof(WIN32_FILE_ATTRIBUTE_DATA));
                    data->dwFileAttributes = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY;
                    DWORD sizeHigh = 0;
                    if (IsVirtualHandleRange(vHandle)) {
                        data->nFileSizeLow = VFS::GetVirtualFileSize(vHandle, &sizeHigh);
                        VFS::CloseVirtualHandle(vHandle);
                    } else {
                        data->nFileSizeLow = GetFileSize(vHandle, &sizeHigh);
                        CloseHandle(vHandle);
                    }
                    data->nFileSizeHigh = sizeHigh;
                }
                return TRUE;
            }
        }
    }
    return orgGetFileAttributesExW(lpFileName, fInfoLevelId, lpFileInformation);
}

HANDLE WINAPI newFindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData) {
    if (!Config::EnableFileHook || !VFS::IsActive()) {
        return orgFindFirstFileW(lpFileName, lpFindFileData);
    }
    return VFS::VirtualFindFirstFileW(lpFileName, lpFindFileData);
}

BOOL WINAPI newFindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData) {
    if (IsVirtualHandleRange(hFindFile)) {
        return VFS::VirtualFindNextFileW(hFindFile, lpFindFileData);
    }
    return orgFindNextFileW(hFindFile, lpFindFileData);
}

HANDLE WINAPI newFindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData) {
    if (!Config::EnableFileHook || !VFS::IsActive()) {
        return orgFindFirstFileA(lpFileName, lpFindFileData);
    }
    return VFS::VirtualFindFirstFileA(lpFileName, lpFindFileData);
}

BOOL WINAPI newFindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData) {
    if (IsVirtualHandleRange(hFindFile)) {
        return VFS::VirtualFindNextFileA(hFindFile, lpFindFileData);
    }
    return orgFindNextFileA(hFindFile, lpFindFileData);
}

BOOL WINAPI newFindClose(HANDLE hFindFile) {
    if (IsVirtualHandleRange(hFindFile)) {
        return VFS::VirtualFindClose(hFindFile);
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
        DetourAttach(&(PVOID&)orgReadFile, newReadFile);
        DetourAttach(&(PVOID&)orgSetFilePointer, newSetFilePointer);
        DetourAttach(&(PVOID&)orgSetFilePointerEx, newSetFilePointerEx);
        DetourAttach(&(PVOID&)orgGetFileSize, newGetFileSize);
        DetourAttach(&(PVOID&)orgGetFileSizeEx, newGetFileSizeEx);
        DetourAttach(&(PVOID&)orgCloseHandle, newCloseHandle);
        DetourAttach(&(PVOID&)orgGetFileInformationByHandle, newGetFileInformationByHandle);
        DetourAttach(&(PVOID&)orgGetFileType, newGetFileType);
        DetourAttach(&(PVOID&)orgGetFileAttributesA, newGetFileAttributesA);
        DetourAttach(&(PVOID&)orgGetFileAttributesW, newGetFileAttributesW);
        DetourAttach(&(PVOID&)orgGetFileAttributesExA, newGetFileAttributesExA);
        DetourAttach(&(PVOID&)orgGetFileAttributesExW, newGetFileAttributesExW);
        DetourAttach(&(PVOID&)orgFindFirstFileW, newFindFirstFileW);
        DetourAttach(&(PVOID&)orgFindNextFileW, newFindNextFileW);
        DetourAttach(&(PVOID&)orgFindFirstFileA, newFindFirstFileA);
        DetourAttach(&(PVOID&)orgFindNextFileA, newFindNextFileA);
        DetourAttach(&(PVOID&)orgFindClose, newFindClose);
        DetourTransactionCommit();
        VFS::SetOriginalFunctions((void*)orgReadFile, (void*)orgSetFilePointerEx, (void*)orgCloseHandle);
        VFS::SetFindFunctions((void*)orgFindFirstFileW, (void*)orgFindNextFileW, (void*)orgFindClose, (void*)orgFindFirstFileA, (void*)orgFindNextFileA);
        Utils::Log("[Core] VFS File Hook Installed.");
    }
}
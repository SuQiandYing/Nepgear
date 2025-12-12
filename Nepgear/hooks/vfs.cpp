#include "../pch.h"
#include "vfs.h"
#include "config.h"
#include "utils.h"
#include <shlwapi.h>
#include <mutex>
#include <vector>
#include <algorithm>

#pragma comment(lib, "Shlwapi.lib")

typedef BOOL(WINAPI* pReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI* pSetFilePointerEx)(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);
typedef BOOL(WINAPI* pCloseHandle)(HANDLE);
typedef HANDLE(WINAPI* pCreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

static pReadFile g_OrigReadFile = nullptr;
static pSetFilePointerEx g_OrigSetFilePointerEx = nullptr;
static pCloseHandle g_OrigCloseHandle = nullptr;

namespace VFS {
    static std::unordered_map<std::wstring, VirtualFileEntry> g_FileIndex;
    static std::unordered_map<HANDLE, VirtualFileHandle*> g_HandleMap;
    static std::recursive_mutex g_Mutex;
    static HANDLE g_ArchiveHandle = INVALID_HANDLE_VALUE;
    static wchar_t g_ArchivePath[MAX_PATH] = { 0 };
    static wchar_t g_LooseFolderPath[MAX_PATH] = { 0 };
    static bool g_IsActive = false;
    static uintptr_t g_VirtualHandleCounter = 0xBF000000;

    static pReadFile g_RawReadFile = nullptr;
    static pSetFilePointerEx g_RawSetFilePointerEx = nullptr;
    static pCloseHandle g_RawCloseHandle = nullptr;
    static pCreateFileW g_RawCreateFileW = nullptr;

    static std::wstring NormalizePath(const wchar_t* path) {
        std::wstring normalized(path);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::towlower);
        for (auto& c : normalized) {
            if (c == L'/') c = L'\\';
        }
        while (!normalized.empty() && normalized[0] == L'\\') {
            normalized.erase(0, 1);
        }
        return normalized;
    }

    static std::wstring NormalizePathA(const char* path) {
        wchar_t wpath[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH);
        return NormalizePath(wpath);
    }

    void SetOriginalFunctions(void* readFile, void* setFilePointerEx, void* closeHandle) {
        g_OrigReadFile = (pReadFile)readFile;
        g_OrigSetFilePointerEx = (pSetFilePointerEx)setFilePointerEx;
        g_OrigCloseHandle = (pCloseHandle)closeHandle;
    }

    static void ScanLooseFiles(const wchar_t* basePath, const wchar_t* currentPath, const wchar_t* relativeBase) {
        wchar_t searchPath[MAX_PATH];
        wcscpy_s(searchPath, currentPath);
        PathAppendW(searchPath, L"*");

        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(searchPath, &fd);
        if (hFind == INVALID_HANDLE_VALUE) return;

        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

            wchar_t fullPath[MAX_PATH];
            wcscpy_s(fullPath, currentPath);
            PathAppendW(fullPath, fd.cFileName);

            wchar_t relativePath[MAX_PATH];
            if (relativeBase[0] == L'\0') {
                wcscpy_s(relativePath, fd.cFileName);
            } else {
                wcscpy_s(relativePath, relativeBase);
                PathAppendW(relativePath, fd.cFileName);
            }

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                ScanLooseFiles(basePath, fullPath, relativePath);
            } else {
                VirtualFileEntry entry;
                entry.relativePath = relativePath;
                entry.offset = 0;
                entry.size = fd.nFileSizeLow;
                entry.isLooseFile = true;
                entry.looseFilePath = fullPath;

                std::wstring normalizedPath = NormalizePath(relativePath);
                g_FileIndex[normalizedPath] = entry;

                if (Config::EnableDebug) {
                    Utils::Log("[VFS] Loose file indexed: %S", relativePath);
                }
            }
        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
    }

    bool Initialize(HMODULE hModule) {
        Utils::Log("[VFS] Initialize called");

        g_RawReadFile = (pReadFile)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ReadFile");
        g_RawSetFilePointerEx = (pSetFilePointerEx)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetFilePointerEx");
        g_RawCloseHandle = (pCloseHandle)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CloseHandle");
        g_RawCreateFileW = (pCreateFileW)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateFileW");

        if (!g_RawReadFile || !g_RawSetFilePointerEx || !g_RawCloseHandle || !g_RawCreateFileW) {
            Utils::Log("[VFS] Failed to get raw API functions");
            return false;
        }

        if (!Config::EnableFileHook) {
            Utils::Log("[VFS] File Hook Disabled.");
            return false;
        }

        wchar_t baseDir[MAX_PATH];
        GetModuleFileNameW(hModule, baseDir, MAX_PATH);
        PathRemoveFileSpecW(baseDir);

        wcscpy_s(g_LooseFolderPath, baseDir);
        PathAppendW(g_LooseFolderPath, Config::RedirectFolderW);

        if (PathFileExistsW(g_LooseFolderPath) && PathIsDirectoryW(g_LooseFolderPath)) {
            Utils::Log("[VFS] Scanning loose files from: %S", g_LooseFolderPath);
            ScanLooseFiles(g_LooseFolderPath, g_LooseFolderPath, L"");
            Utils::Log("[VFS] Loose files indexed: %zu files", g_FileIndex.size());
        } else {
            Utils::Log("[VFS] Loose folder not found: %S", g_LooseFolderPath);
        }

        wcscpy_s(g_ArchivePath, baseDir);
        PathAppendW(g_ArchivePath, Config::ArchiveFileName);

        Utils::Log("[VFS] Looking for archive at: %S", g_ArchivePath);

        if (PathFileExistsW(g_ArchivePath)) {
            Utils::Log("[VFS] Opening archive file...");
            g_ArchiveHandle = g_RawCreateFileW(g_ArchivePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (g_ArchiveHandle != INVALID_HANDLE_VALUE) {
                Utils::Log("[VFS] Archive opened, reading header...");
                DWORD bytesRead;
                int fileCount = 0;
                if (g_RawReadFile(g_ArchiveHandle, &fileCount, sizeof(int), &bytesRead, NULL)) {
                    Utils::Log("[VFS] Building index for %d archive files...", fileCount);

                    for (int i = 0; i < fileCount; i++) {
                        int pathLen = 0;
                        if (!g_RawReadFile(g_ArchiveHandle, &pathLen, sizeof(int), &bytesRead, NULL)) break;

                        std::vector<char> relPath(pathLen + 1, '\0');
                        if (!g_RawReadFile(g_ArchiveHandle, relPath.data(), pathLen, &bytesRead, NULL)) break;

                        wchar_t relPathW[MAX_PATH];
                        MultiByteToWideChar(CP_ACP, 0, relPath.data(), -1, relPathW, MAX_PATH);

                        int dataSize = 0;
                        if (!g_RawReadFile(g_ArchiveHandle, &dataSize, sizeof(int), &bytesRead, NULL)) break;

                        LARGE_INTEGER currentPos;
                        LARGE_INTEGER zero = { 0 };
                        g_RawSetFilePointerEx(g_ArchiveHandle, zero, &currentPos, FILE_CURRENT);

                        std::wstring normalizedPath = NormalizePath(relPathW);

                        if (g_FileIndex.find(normalizedPath) == g_FileIndex.end()) {
                            VirtualFileEntry entry;
                            entry.relativePath = relPathW;
                            entry.offset = currentPos.QuadPart;
                            entry.size = dataSize;
                            entry.isLooseFile = false;
                            g_FileIndex[normalizedPath] = entry;
                        }

                        LARGE_INTEGER skipDist;
                        skipDist.QuadPart = dataSize;
                        g_RawSetFilePointerEx(g_ArchiveHandle, skipDist, NULL, FILE_CURRENT);
                    }
                } else {
                    Utils::Log("[VFS] Failed to read archive header");
                    g_RawCloseHandle(g_ArchiveHandle);
                    g_ArchiveHandle = INVALID_HANDLE_VALUE;
                }
            } else {
                Utils::Log("[VFS] Failed to open archive: %S (error: %d)", g_ArchivePath, GetLastError());
            }
        } else {
            Utils::Log("[VFS] Archive not found: %S", g_ArchivePath);
        }

        if (g_FileIndex.size() > 0) {
            g_IsActive = true;
            Utils::Log("[VFS] Index built successfully. %zu files total.", g_FileIndex.size());
            return true;
        }

        Utils::Log("[VFS] No files indexed, VFS inactive.");
        return false;
    }

    void Shutdown() {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        for (auto& pair : g_HandleMap) {
            delete pair.second;
        }
        g_HandleMap.clear();
        g_FileIndex.clear();

        if (g_ArchiveHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(g_ArchiveHandle);
            g_ArchiveHandle = INVALID_HANDLE_VALUE;
        }
        g_IsActive = false;
    }

    bool IsActive() {
        return g_IsActive;
    }

    bool HasVirtualFile(const wchar_t* relativePath) {
        if (!g_IsActive || !relativePath) return false;
        std::wstring normalized = NormalizePath(relativePath);
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        return g_FileIndex.find(normalized) != g_FileIndex.end();
    }

    bool HasVirtualFileA(const char* relativePath) {
        if (!g_IsActive || !relativePath) return false;
        std::wstring normalized = NormalizePathA(relativePath);
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        return g_FileIndex.find(normalized) != g_FileIndex.end();
    }

    HANDLE OpenVirtualFile(const wchar_t* relativePath) {
        if (!g_IsActive || !relativePath) return INVALID_HANDLE_VALUE;

        std::wstring normalized = NormalizePath(relativePath);

        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FileIndex.find(normalized);
        if (it == g_FileIndex.end()) {
            return INVALID_HANDLE_VALUE;
        }

        VirtualFileHandle* vfh = new VirtualFileHandle();
        vfh->entry = &it->second;
        vfh->position = 0;
        vfh->isLooseFile = it->second.isLooseFile;

        if (it->second.isLooseFile) {
            auto createFileFn = g_RawCreateFileW ? g_RawCreateFileW : CreateFileW;
            vfh->looseFileHandle = createFileFn(it->second.looseFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (vfh->looseFileHandle == INVALID_HANDLE_VALUE) {
                delete vfh;
                return INVALID_HANDLE_VALUE;
            }
            vfh->archiveHandle = INVALID_HANDLE_VALUE;
        } else {
            vfh->archiveHandle = g_ArchiveHandle;
            vfh->looseFileHandle = INVALID_HANDLE_VALUE;
        }

        HANDLE fakeHandle = (HANDLE)(++g_VirtualHandleCounter);
        g_HandleMap[fakeHandle] = vfh;

        if (Config::EnableDebug) {
            Utils::Log("[VFS] Opened virtual file: %S (handle: %p, size: %d, loose: %d)", relativePath, fakeHandle, it->second.size, it->second.isLooseFile ? 1 : 0);
        }

        return fakeHandle;
    }

    HANDLE OpenVirtualFileA(const char* relativePath) {
        wchar_t wpath[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, relativePath, -1, wpath, MAX_PATH);
        return OpenVirtualFile(wpath);
    }

    bool IsVirtualHandle(HANDLE hFile) {
        if (!g_IsActive) return false;
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        return g_HandleMap.find(hFile) != g_HandleMap.end();
    }

    BOOL ReadVirtualFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(hFile);
        if (it == g_HandleMap.end()) {
            SetLastError(ERROR_INVALID_HANDLE);
            return FALSE;
        }

        VirtualFileHandle* vfh = it->second;

        if (lpOverlapped) {
            SetLastError(ERROR_NOT_SUPPORTED);
            return FALSE;
        }

        LONGLONG remaining = vfh->entry->size - vfh->position;
        if (remaining <= 0) {
            if (lpNumberOfBytesRead) *lpNumberOfBytesRead = 0;
            return TRUE;
        }

        DWORD toRead = (DWORD)min((LONGLONG)nNumberOfBytesToRead, remaining);
        DWORD bytesRead = 0;

        if (vfh->isLooseFile) {
            LARGE_INTEGER seekPos;
            seekPos.QuadPart = vfh->position;

            auto setFilePointerExFn = g_RawSetFilePointerEx ? g_RawSetFilePointerEx : g_OrigSetFilePointerEx;
            auto readFileFn = g_RawReadFile ? g_RawReadFile : g_OrigReadFile;

            if (!setFilePointerExFn || !readFileFn) {
                SetLastError(ERROR_NOT_READY);
                return FALSE;
            }

            if (!setFilePointerExFn(vfh->looseFileHandle, seekPos, NULL, FILE_BEGIN)) {
                SetLastError(ERROR_READ_FAULT);
                return FALSE;
            }
            if (!readFileFn(vfh->looseFileHandle, lpBuffer, toRead, &bytesRead, NULL)) {
                return FALSE;
            }
        } else {
            LARGE_INTEGER seekPos;
            seekPos.QuadPart = vfh->entry->offset + vfh->position;

            auto setFilePointerExFn = g_RawSetFilePointerEx ? g_RawSetFilePointerEx : g_OrigSetFilePointerEx;
            auto readFileFn = g_RawReadFile ? g_RawReadFile : g_OrigReadFile;

            if (!setFilePointerExFn || !readFileFn) {
                SetLastError(ERROR_NOT_READY);
                return FALSE;
            }

            if (!setFilePointerExFn(vfh->archiveHandle, seekPos, NULL, FILE_BEGIN)) {
                SetLastError(ERROR_READ_FAULT);
                return FALSE;
            }

            if (!readFileFn(vfh->archiveHandle, lpBuffer, toRead, &bytesRead, NULL)) {
                return FALSE;
            }
        }

        vfh->position += bytesRead;
        if (lpNumberOfBytesRead) *lpNumberOfBytesRead = bytesRead;

        return TRUE;
    }

    DWORD SetVirtualFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(hFile);
        if (it == g_HandleMap.end()) {
            SetLastError(ERROR_INVALID_HANDLE);
            return INVALID_SET_FILE_POINTER;
        }

        VirtualFileHandle* vfh = it->second;
        LONGLONG distance = lDistanceToMove;
        if (lpDistanceToMoveHigh) {
            distance |= ((LONGLONG)*lpDistanceToMoveHigh) << 32;
        }

        LONGLONG newPos;
        switch (dwMoveMethod) {
        case FILE_BEGIN:
            newPos = distance;
            break;
        case FILE_CURRENT:
            newPos = vfh->position + distance;
            break;
        case FILE_END:
            newPos = vfh->entry->size + distance;
            break;
        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return INVALID_SET_FILE_POINTER;
        }

        if (newPos < 0) newPos = 0;
        if (newPos > (LONGLONG)vfh->entry->size) newPos = vfh->entry->size;

        vfh->position = newPos;

        if (lpDistanceToMoveHigh) {
            *lpDistanceToMoveHigh = (LONG)(newPos >> 32);
        }

        return (DWORD)(newPos & 0xFFFFFFFF);
    }

    BOOL SetVirtualFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(hFile);
        if (it == g_HandleMap.end()) {
            SetLastError(ERROR_INVALID_HANDLE);
            return FALSE;
        }

        VirtualFileHandle* vfh = it->second;
        LONGLONG newPos;
        switch (dwMoveMethod) {
        case FILE_BEGIN:
            newPos = liDistanceToMove.QuadPart;
            break;
        case FILE_CURRENT:
            newPos = vfh->position + liDistanceToMove.QuadPart;
            break;
        case FILE_END:
            newPos = vfh->entry->size + liDistanceToMove.QuadPart;
            break;
        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }

        if (newPos < 0) newPos = 0;
        if (newPos > (LONGLONG)vfh->entry->size) newPos = vfh->entry->size;

        vfh->position = newPos;

        if (lpNewFilePointer) {
            lpNewFilePointer->QuadPart = newPos;
        }

        return TRUE;
    }

    DWORD GetVirtualFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(hFile);
        if (it == g_HandleMap.end()) {
            SetLastError(ERROR_INVALID_HANDLE);
            return INVALID_FILE_SIZE;
        }

        VirtualFileHandle* vfh = it->second;
        if (lpFileSizeHigh) {
            *lpFileSizeHigh = 0;
        }
        return vfh->entry->size;
    }

    BOOL GetVirtualFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(hFile);
        if (it == g_HandleMap.end()) {
            SetLastError(ERROR_INVALID_HANDLE);
            return FALSE;
        }

        VirtualFileHandle* vfh = it->second;
        if (lpFileSize) {
            lpFileSize->QuadPart = vfh->entry->size;
        }
        return TRUE;
    }

    BOOL CloseVirtualHandle(HANDLE hFile) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(hFile);
        if (it == g_HandleMap.end()) {
            SetLastError(ERROR_INVALID_HANDLE);
            return FALSE;
        }

        VirtualFileHandle* vfh = it->second;
        if (vfh->isLooseFile && vfh->looseFileHandle != INVALID_HANDLE_VALUE) {
            auto closeHandleFn = g_RawCloseHandle ? g_RawCloseHandle : g_OrigCloseHandle;
            if (closeHandleFn) {
                closeHandleFn(vfh->looseFileHandle);
            }
        }

        delete vfh;
        g_HandleMap.erase(it);
        return TRUE;
    }

    BOOL GetVirtualFileInformationByHandle(HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(hFile);
        if (it == g_HandleMap.end()) {
            SetLastError(ERROR_INVALID_HANDLE);
            return FALSE;
        }

        VirtualFileHandle* vfh = it->second;
        ZeroMemory(lpFileInformation, sizeof(BY_HANDLE_FILE_INFORMATION));
        lpFileInformation->dwFileAttributes = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY;
        lpFileInformation->nFileSizeLow = vfh->entry->size;
        lpFileInformation->nFileSizeHigh = 0;
        lpFileInformation->nNumberOfLinks = 1;

        return TRUE;
    }

    DWORD GetVirtualFileType(HANDLE hFile) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(hFile);
        if (it == g_HandleMap.end()) {
            SetLastError(ERROR_INVALID_HANDLE);
            return FILE_TYPE_UNKNOWN;
        }
        return FILE_TYPE_DISK;
    }

    bool ExtractFile(const wchar_t* relativePath, const wchar_t* destPath) {
        if (!g_IsActive || !relativePath || !destPath) return false;

        std::wstring normalized = NormalizePath(relativePath);

        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FileIndex.find(normalized);
        if (it == g_FileIndex.end()) {
            Utils::Log("[VFS] ExtractFile: File not found: %S", relativePath);
            return false;
        }

        VirtualFileEntry& entry = it->second;

        LARGE_INTEGER seekPos;
        seekPos.QuadPart = entry.offset;

        auto setFilePointerExFn = g_RawSetFilePointerEx ? g_RawSetFilePointerEx : SetFilePointerEx;
        auto readFileFn = g_RawReadFile ? g_RawReadFile : ReadFile;

        if (!setFilePointerExFn(g_ArchiveHandle, seekPos, NULL, FILE_BEGIN)) {
            Utils::Log("[VFS] ExtractFile: Failed to seek");
            return false;
        }

        std::vector<BYTE> buffer(entry.size);
        DWORD bytesRead = 0;
        if (!readFileFn(g_ArchiveHandle, buffer.data(), entry.size, &bytesRead, NULL) || bytesRead != entry.size) {
            Utils::Log("[VFS] ExtractFile: Failed to read %d bytes", entry.size);
            return false;
        }

        HANDLE hDestFile = CreateFileW(destPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hDestFile == INVALID_HANDLE_VALUE) {
            Utils::Log("[VFS] ExtractFile: Failed to create dest file: %S (error: %d)", destPath, GetLastError());
            return false;
        }

        DWORD bytesWritten = 0;
        BOOL writeOk = WriteFile(hDestFile, buffer.data(), entry.size, &bytesWritten, NULL);
        CloseHandle(hDestFile);

        if (!writeOk || bytesWritten != entry.size) {
            Utils::Log("[VFS] ExtractFile: Failed to write");
            DeleteFileW(destPath);
            return false;
        }

        Utils::Log("[VFS] ExtractFile: Extracted %S to %S (%d bytes)", relativePath, destPath, entry.size);
        return true;
    }
}

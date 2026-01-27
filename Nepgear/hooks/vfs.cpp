#include "../pch.h"
#include "vfs.h"
#include "config.h"
#include "utils.h"
#include <shlwapi.h>
#include <compressapi.h>
#include <mutex>
#include <vector>
#include <algorithm>
#include <unordered_map>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "cabinet.lib")

typedef BOOL(WINAPI* pReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI* pSetFilePointerEx)(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);
typedef BOOL(WINAPI* pCloseHandle)(HANDLE);
typedef HANDLE(WINAPI* pCreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

typedef HANDLE(WINAPI* pFindFirstFileW)(LPCWSTR, LPWIN32_FIND_DATAW);
typedef BOOL(WINAPI* pFindNextFileW)(HANDLE, LPWIN32_FIND_DATAW);
typedef BOOL(WINAPI* pFindClose)(HANDLE);

typedef HANDLE(WINAPI* pFindFirstFileA)(LPCSTR, LPWIN32_FIND_DATAA);
typedef BOOL(WINAPI* pFindNextFileA)(HANDLE, LPWIN32_FIND_DATAA);

static pReadFile g_OrigReadFile = nullptr;
static pSetFilePointerEx g_OrigSetFilePointerEx = nullptr;
static pCloseHandle g_OrigCloseHandle = nullptr;

static pFindFirstFileW g_OrigFindFirstFileW = nullptr;
static pFindNextFileW g_OrigFindNextFileW = nullptr;
static pFindFirstFileA g_OrigFindFirstFileA = nullptr;
static pFindNextFileA g_OrigFindNextFileA = nullptr;
static pFindClose g_OrigFindClose = nullptr;

namespace Legacy {
    typedef VFS::VirtualFileEntry VirtualFileEntry;
    typedef VFS::VirtualFileHandle VirtualFileHandle;

    struct VirtualFindState {
        HANDLE realHandle;
        bool usingRealHandle;
        std::vector<std::wstring> seenFiles;
        std::vector<VirtualFileEntry*> matches;
        size_t matchIndex;
    };

    static std::unordered_map<std::wstring, VirtualFileEntry> g_FileIndex;
    static std::unordered_map<HANDLE, VirtualFileHandle*> g_HandleMap;
    static std::unordered_map<HANDLE, VirtualFindState*> g_FindMap;
    static std::recursive_mutex g_Mutex;

    static bool DecompressData(const std::vector<BYTE>& input, DWORD decompressedSize, PBYTE output) {
        DECOMPRESSOR_HANDLE decompressor = NULL;
        if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, NULL, &decompressor)) {
            Utils::Log("[VFS] CreateDecompressor failed: %d", GetLastError());
            return false;
        }

        SIZE_T actualDecompressedSize = 0;
        if (!Decompress(decompressor, input.data(), input.size(), output, decompressedSize, &actualDecompressedSize)) {
            Utils::Log("[VFS] Decompress failed: %d", GetLastError());
            CloseDecompressor(decompressor);
            return false;
        }

        CloseDecompressor(decompressor);
        return actualDecompressedSize == decompressedSize;
    }
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
        MultiByteToWideChar(Config::LE_Codepage, 0, path, -1, wpath, MAX_PATH);
        return NormalizePath(wpath);
    }

    void SetOriginalFunctions(void* readFile, void* setFilePointerEx, void* closeHandle) {
    }

    void SetFindFunctions(void* findFirstW, void* findNextW, void* findClose, void* findFirstA, void* findNextA) {
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
                Utils::LogW(L"[VFS] Entering directory: '%s'", fullPath);
                ScanLooseFiles(basePath, fullPath, relativePath);
            } else {
                VirtualFileEntry entry;
                entry.relativePath = relativePath;
                entry.offset = 0;
                entry.size = fd.nFileSizeLow;
                entry.decompressedSize = fd.nFileSizeLow;
                entry.isLooseFile = true;
                entry.looseFilePath = fullPath;

                std::wstring normalizedPath = NormalizePath(relativePath);
                g_FileIndex[normalizedPath] = entry;

                Utils::LogW(L"[VFS] Loose file indexed: key='%s' path='%s'", normalizedPath.c_str(), fullPath);
            }
        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
    }

    bool Initialize(HMODULE hModule) {
        Utils::Log("[VFS-Legacy] Initialize called");

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
            Utils::LogW(L"[VFS] Scanning loose files from: %s", g_LooseFolderPath);
            ScanLooseFiles(g_LooseFolderPath, g_LooseFolderPath, L"");
            Utils::Log("[VFS] Loose files indexed: %zu files", g_FileIndex.size());
        }

        wcscpy_s(g_ArchivePath, baseDir);
        PathAppendW(g_ArchivePath, Config::ArchiveFileName);

        if (!PathFileExistsW(g_ArchivePath)) {
             wchar_t fallbackPath[MAX_PATH];
             wcscpy_s(fallbackPath, baseDir);
             PathAppendW(fallbackPath, Config::RedirectFolderW);
             PathAppendW(fallbackPath, Config::ArchiveFileName);
             if (PathFileExistsW(fallbackPath)) {
                 wcscpy_s(g_ArchivePath, fallbackPath);
             }
        }

        if (PathFileExistsW(g_ArchivePath)) {
            g_ArchiveHandle = g_RawCreateFileW(g_ArchivePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (g_ArchiveHandle != INVALID_HANDLE_VALUE) {
                DWORD bytesRead;
                int fileCount = 0;
                if (g_RawReadFile(g_ArchiveHandle, &fileCount, sizeof(int), &bytesRead, NULL)) {
                    for (int i = 0; i < fileCount; i++) {
                        int pathLen = 0;
                        if (!g_RawReadFile(g_ArchiveHandle, &pathLen, sizeof(int), &bytesRead, NULL)) break;

                        std::vector<char> relPath(pathLen + 1, '\0');
                        if (!g_RawReadFile(g_ArchiveHandle, relPath.data(), pathLen, &bytesRead, NULL)) break;

                        wchar_t relPathW[MAX_PATH];
                        MultiByteToWideChar(CP_UTF8, 0, relPath.data(), -1, relPathW, MAX_PATH);

                        int decompressedSize = 0;
                        if (!g_RawReadFile(g_ArchiveHandle, &decompressedSize, sizeof(int), &bytesRead, NULL)) break;

                        int storedSize = 0;
                        if (!g_RawReadFile(g_ArchiveHandle, &storedSize, sizeof(int), &bytesRead, NULL)) break;

                        LARGE_INTEGER currentPos;
                        LARGE_INTEGER zero = { 0 };
                        g_RawSetFilePointerEx(g_ArchiveHandle, zero, &currentPos, FILE_CURRENT);

                        std::wstring normalizedPath = NormalizePath(relPathW);

                        if (g_FileIndex.find(normalizedPath) == g_FileIndex.end()) {
                            VirtualFileEntry entry;
                            entry.relativePath = relPathW;
                            entry.offset = currentPos.QuadPart;
                            entry.size = storedSize;
                            entry.decompressedSize = decompressedSize;
                            entry.isLooseFile = false;
                            g_FileIndex[normalizedPath] = entry;
                        }

                        LARGE_INTEGER skipDist;
                        skipDist.QuadPart = storedSize;
                        g_RawSetFilePointerEx(g_ArchiveHandle, skipDist, NULL, FILE_CURRENT);
                    }
                } else {
                    g_RawCloseHandle(g_ArchiveHandle);
                    g_ArchiveHandle = INVALID_HANDLE_VALUE;
                }
            }
        }

        if (g_FileIndex.size() > 0) {
            g_IsActive = true;
            return true;
        }

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
        bool found = g_FileIndex.find(normalized) != g_FileIndex.end();
        return found;
    }

    bool HasVirtualFileA(const char* relativePath) {
        if (!g_IsActive || !relativePath) return false;
        std::wstring normalized = NormalizePathA(relativePath);
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        bool found = g_FileIndex.find(normalized) != g_FileIndex.end();
        return found;
    }

    HANDLE OpenVirtualFile(const wchar_t* relativePath) {
        if (!g_IsActive || !relativePath) return INVALID_HANDLE_VALUE;

        std::wstring normalized = NormalizePath(relativePath);

        const wchar_t* ext = PathFindExtensionW(relativePath);
        if (ext && (_wcsicmp(ext, L".dll") == 0 || _wcsicmp(ext, L".exe") == 0 || _wcsicmp(ext, L".asi") == 0 || _wcsicmp(ext, L".tws") == 0)) {
             std::lock_guard<std::recursive_mutex> lock(g_Mutex);
             auto it = g_FileIndex.find(normalized);
             if (it != g_FileIndex.end() && it->second.isLooseFile) {
                 return g_RawCreateFileW(it->second.looseFilePath.c_str(), GENERIC_READ, 
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
             }
        }

        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FileIndex.find(normalized);
        if (it == g_FileIndex.end()) {
            return INVALID_HANDLE_VALUE;
        }

        VirtualFileHandle* vfh = new VirtualFileHandle();
        vfh->entry = &it->second;
        vfh->position = 0;
        vfh->isLooseFile = it->second.isLooseFile;
        vfh->decompressedBuffer = NULL;

        if (it->second.isLooseFile) {
            auto createFileFn = g_RawCreateFileW ? g_RawCreateFileW : CreateFileW;
            vfh->looseFileHandle = createFileFn(it->second.looseFilePath.c_str(), GENERIC_READ, 
                                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (vfh->looseFileHandle == INVALID_HANDLE_VALUE) {
                delete vfh;
                return INVALID_HANDLE_VALUE;
            }
            vfh->archiveHandle = INVALID_HANDLE_VALUE;
        } else {
            vfh->archiveHandle = g_ArchiveHandle;
            vfh->looseFileHandle = INVALID_HANDLE_VALUE;

            if (vfh->entry->size < vfh->entry->decompressedSize) {
                std::vector<BYTE> compressedData(vfh->entry->size);
                LARGE_INTEGER seekPos;
                seekPos.QuadPart = vfh->entry->offset;
                
                auto setFilePointerExFn = g_RawSetFilePointerEx ? g_RawSetFilePointerEx : SetFilePointerEx;
                auto readFileFn = g_RawReadFile ? g_RawReadFile : ReadFile;

                if (setFilePointerExFn(g_ArchiveHandle, seekPos, NULL, FILE_BEGIN)) {
                    DWORD bytesRead = 0;
                    if (readFileFn(g_ArchiveHandle, compressedData.data(), vfh->entry->size, &bytesRead, NULL) && bytesRead == vfh->entry->size) {
                        vfh->decompressedBuffer = (PBYTE)HeapAlloc(GetProcessHeap(), 0, vfh->entry->decompressedSize);
                        if (vfh->decompressedBuffer) {
                            if (!DecompressData(compressedData, vfh->entry->decompressedSize, vfh->decompressedBuffer)) {
                                Utils::LogW(L"[VFS] Failed to decompress: %s", relativePath);
                                HeapFree(GetProcessHeap(), 0, vfh->decompressedBuffer);
                                vfh->decompressedBuffer = NULL;
                            }
                        }
                    }
                }
            }
        }

        HANDLE fakeHandle = (HANDLE)(++g_VirtualHandleCounter);
        g_HandleMap[fakeHandle] = vfh;

        if (Config::EnableDebug) {
            Utils::LogW(L"[VFS] Opened virtual file: %s (handle: %p, original: %d, stored: %d, compressed: %d)", 
                relativePath, fakeHandle, it->second.decompressedSize, it->second.size, (it->second.size < it->second.decompressedSize));
        }

        return fakeHandle;
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

        LONGLONG remaining = vfh->entry->decompressedSize - vfh->position;
        if (remaining <= 0) {
            if (lpNumberOfBytesRead) *lpNumberOfBytesRead = 0;
            return TRUE;
        }

        DWORD toRead = (DWORD)min((LONGLONG)nNumberOfBytesToRead, remaining);
        DWORD bytesRead = 0;

        if (vfh->decompressedBuffer) {
            memcpy(lpBuffer, vfh->decompressedBuffer + vfh->position, toRead);
            bytesRead = toRead;
        } else if (vfh->isLooseFile) {
            auto readFileFn = g_RawReadFile ? g_RawReadFile : ReadFile;
            auto setFilePointerExFn = g_RawSetFilePointerEx ? g_RawSetFilePointerEx : SetFilePointerEx;

            LARGE_INTEGER seekPos;
            seekPos.QuadPart = vfh->position;
            if (!setFilePointerExFn(vfh->looseFileHandle, seekPos, NULL, FILE_BEGIN)) {
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
            newPos = vfh->entry->decompressedSize + distance;
            break;
        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return INVALID_SET_FILE_POINTER;
        }

        if (newPos < 0) newPos = 0;
        if (newPos > (LONGLONG)vfh->entry->decompressedSize) newPos = vfh->entry->decompressedSize;

        vfh->position = newPos;

        if (vfh->isLooseFile) {
            auto setFilePointerExFn = g_RawSetFilePointerEx ? g_RawSetFilePointerEx : SetFilePointerEx;
            LARGE_INTEGER li;
            li.QuadPart = newPos;
            setFilePointerExFn(vfh->looseFileHandle, li, NULL, FILE_BEGIN);
        }

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
            newPos = vfh->entry->decompressedSize + liDistanceToMove.QuadPart;
            break;
        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }

        if (newPos < 0) newPos = 0;
        if (newPos > (LONGLONG)vfh->entry->decompressedSize) newPos = vfh->entry->decompressedSize;

        vfh->position = newPos;

        if (vfh->isLooseFile) {
            auto setFilePointerExFn = g_RawSetFilePointerEx ? g_RawSetFilePointerEx : SetFilePointerEx;
            LARGE_INTEGER li;
            li.QuadPart = newPos;
            setFilePointerExFn(vfh->looseFileHandle, li, lpNewFilePointer, FILE_BEGIN);
            return TRUE;
        }

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
            *lpFileSizeHigh = (DWORD)((ULONGLONG)vfh->entry->decompressedSize >> 32);
        }
        return (DWORD)(vfh->entry->decompressedSize & 0xFFFFFFFF);
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
            lpFileSize->QuadPart = vfh->entry->decompressedSize;
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

        if (vfh->decompressedBuffer) {
            HeapFree(GetProcessHeap(), 0, vfh->decompressedBuffer);
            vfh->decompressedBuffer = NULL;
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
        lpFileInformation->nFileSizeLow = (DWORD)(vfh->entry->decompressedSize & 0xFFFFFFFF);
        lpFileInformation->nFileSizeHigh = (DWORD)((ULONGLONG)vfh->entry->decompressedSize >> 32);
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

    static bool ShouldLogPath(LPCWSTR lpFileName) {
        if (!lpFileName) return false;
        if (wcsstr(lpFileName, L"C:\\WINDOWS") || wcsstr(lpFileName, L"C:\\Windows")) return false;
        if (wcsstr(lpFileName, L"C:\\Program Files")) return false;
        if (wcsstr(lpFileName, L"CryptnetUrlCache")) return false;
        
        size_t len = wcslen(lpFileName);
        if (len >= 3 && wcscmp(lpFileName + len - 3, L"*.*") == 0) return false;
        if (len >= 1 && lpFileName[len - 1] == L'*') return false;

        return true;
    }

    static bool ShouldLogPathA(LPCSTR lpFileName) {
        if (!lpFileName) return false;
        if (strstr(lpFileName, "C:\\WINDOWS") || strstr(lpFileName, "C:\\Windows")) return false;
        if (strstr(lpFileName, "C:\\Program Files")) return false;
        if (strstr(lpFileName, "CryptnetUrlCache")) return false;

        size_t len = strlen(lpFileName);
        if (len >= 3 && strcmp(lpFileName + len - 3, "*.*") == 0) return false;
        if (len >= 1 && lpFileName[len - 1] == '*') return false;

        return true;
    }

    static wchar_t g_LastFindLog[MAX_PATH] = { 0 };
    static char g_LastFindLogA[MAX_PATH] = { 0 };

    static HANDLE VirtualFindFirstFileW_Internal(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData) {
        wchar_t fullPath[MAX_PATH];
        if (!GetFullPathNameW(lpFileName, MAX_PATH, fullPath, NULL)) {
             return g_OrigFindFirstFileW(lpFileName, lpFindFileData);
        }
        
        std::wstring searchDir;
        std::wstring pattern;
        
        std::wstring fullPathStr = fullPath;
        size_t lastSlash = fullPathStr.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            searchDir = fullPathStr.substr(0, lastSlash);
             pattern = fullPathStr.substr(lastSlash + 1);
        } else {
             return g_OrigFindFirstFileW(lpFileName, lpFindFileData);
        }

        VirtualFindState* state = new VirtualFindState();
        state->realHandle = g_OrigFindFirstFileW(lpFileName, lpFindFileData);
        state->usingRealHandle = (state->realHandle != INVALID_HANDLE_VALUE);
        state->matchIndex = 0;

        if (state->usingRealHandle) {
             state->seenFiles.push_back(lpFindFileData->cFileName);
        }

        wchar_t gameRoot[MAX_PATH];
        GetModuleFileNameW(NULL, gameRoot, MAX_PATH);
        PathRemoveFileSpecW(gameRoot);

        std::wstring searchDirNorm = NormalizePath(searchDir.c_str());
        if (!searchDirNorm.empty() && searchDirNorm.back() == L'\\') {
            searchDirNorm.pop_back();
        }
        
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        
        for (const auto& kv : g_FileIndex) {
            const VirtualFileEntry& entry = kv.second;
            
            wchar_t vFullPath[MAX_PATH];
            wcscpy_s(vFullPath, gameRoot);
            PathAppendW(vFullPath, entry.relativePath.c_str());
            
            wchar_t vDir[MAX_PATH];
            wcscpy_s(vDir, vFullPath);
            PathRemoveFileSpecW(vDir);
            
            std::wstring vDirNorm = NormalizePath(vDir);
            if (!vDirNorm.empty() && vDirNorm.back() == L'\\') {
                vDirNorm.pop_back();
            }
            
            if (vDirNorm == searchDirNorm) {
                const wchar_t* fileName = PathFindFileNameW(vFullPath);
                if (PathMatchSpecW(fileName, pattern.c_str())) {
                    state->matches.push_back(const_cast<VirtualFileEntry*>(&entry));
                }
            }
        }
        
        if (state->matches.empty()) {
            HANDLE real = state->realHandle;
            delete state;
            return real;
        }
        
        if (!state->usingRealHandle && !state->matches.empty()) {
             VirtualFileEntry* match = state->matches[0];
             wcscpy_s(lpFindFileData->cFileName, PathFindFileNameW(match->relativePath.c_str()));
             lpFindFileData->nFileSizeLow = (DWORD)(match->decompressedSize & 0xFFFFFFFF);
             lpFindFileData->nFileSizeHigh = (DWORD)((ULONGLONG)match->decompressedSize >> 32);
             lpFindFileData->dwFileAttributes = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY;
             state->matchIndex++;
        }
        
        HANDLE hFake = (HANDLE)(++g_VirtualHandleCounter);
        g_FindMap[hFake] = state;
        return hFake;
    }

    HANDLE VirtualFindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData) {
        if (!g_IsActive || !g_OrigFindFirstFileW) {
             if (g_OrigFindFirstFileW) return g_OrigFindFirstFileW(lpFileName, lpFindFileData);
             return FindFirstFileW(lpFileName, lpFindFileData);
        }

        if (Config::EnableDebug && ShouldLogPath(lpFileName)) {
            if (wcscmp(lpFileName, g_LastFindLog) != 0) {
                Utils::LogW(L"[VFS-Find-Legacy] Start: %s", lpFileName);
                wcscpy_s(g_LastFindLog, lpFileName);
            }
        }

        __try {
            return VirtualFindFirstFileW_Internal(lpFileName, lpFindFileData);
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            Utils::Log("[VFS-Find] Crash in FindFirstFileW");
            return INVALID_HANDLE_VALUE;
        }
    }

    static BOOL VirtualFindNextFileW_Internal(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FindMap.find(hFindFile);
        if (it == g_FindMap.end()) {
             return g_OrigFindNextFileW(hFindFile, lpFindFileData);
        }
        
        VirtualFindState* state = it->second;
        if (!state) return FALSE;

        if (state->usingRealHandle) {
            if (g_OrigFindNextFileW(state->realHandle, lpFindFileData)) {
                state->seenFiles.push_back(lpFindFileData->cFileName);
                return TRUE;
            } else {
                state->usingRealHandle = false;
            }
        }
        
        while (state->matchIndex < state->matches.size()) {
            VirtualFileEntry* match = state->matches[state->matchIndex++];
            const wchar_t* name = PathFindFileNameW(match->relativePath.c_str());
            
            bool seen = false;
            for (const auto& s : state->seenFiles) {
                if (_wcsicmp(s.c_str(), name) == 0) {
                    seen = true;
                    break;
                }
            }
            if (seen) continue;
            
            wcscpy_s(lpFindFileData->cFileName, name);
            lpFindFileData->nFileSizeLow = (DWORD)(match->decompressedSize & 0xFFFFFFFF);
            lpFindFileData->nFileSizeHigh = (DWORD)((ULONGLONG)match->decompressedSize >> 32);
            lpFindFileData->dwFileAttributes = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY;
            return TRUE;
        }
        
        SetLastError(ERROR_NO_MORE_FILES);
        return FALSE;
    }

    BOOL VirtualFindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData) {
        __try {
            return VirtualFindNextFileW_Internal(hFindFile, lpFindFileData);
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
             Utils::Log("[VFS-Find] Crash in FindNextFileW");
             return FALSE;
        }
    }

    static HANDLE VirtualFindFirstFileA_Internal(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData) {
        wchar_t fileNameW[MAX_PATH];
        MultiByteToWideChar(Config::LE_Codepage, 0, lpFileName, -1, fileNameW, MAX_PATH);

        WIN32_FIND_DATAW findDataW;
        HANDLE hFind = VirtualFindFirstFileW_Internal(fileNameW, &findDataW);

        if (hFind != INVALID_HANDLE_VALUE) {
            WideCharToMultiByte(Config::LE_Codepage, 0, findDataW.cFileName, -1, lpFindFileData->cFileName, MAX_PATH, NULL, NULL);
            lpFindFileData->dwFileAttributes = findDataW.dwFileAttributes;
            lpFindFileData->ftCreationTime = findDataW.ftCreationTime;
            lpFindFileData->ftLastAccessTime = findDataW.ftLastAccessTime;
            lpFindFileData->ftLastWriteTime = findDataW.ftLastWriteTime;
            lpFindFileData->nFileSizeHigh = findDataW.nFileSizeHigh;
            lpFindFileData->nFileSizeLow = findDataW.nFileSizeLow;
            lpFindFileData->dwReserved0 = findDataW.dwReserved0;
            lpFindFileData->dwReserved1 = findDataW.dwReserved1;
            strcpy_s(lpFindFileData->cAlternateFileName, "");
        }
        return hFind;
    }

    HANDLE VirtualFindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData) {
        if (!g_IsActive || !g_OrigFindFirstFileA) {
             if (g_OrigFindFirstFileA) return g_OrigFindFirstFileA(lpFileName, lpFindFileData);
             return FindFirstFileA(lpFileName, lpFindFileData);
        }

        if (Config::EnableDebug && ShouldLogPathA(lpFileName)) {
            if (strcmp(lpFileName, g_LastFindLogA) != 0) {
                Utils::Log("[VFS-FindA] Start: %s", lpFileName);
                strcpy_s(g_LastFindLogA, lpFileName);
            }
        }
        
        __try {
            return VirtualFindFirstFileA_Internal(lpFileName, lpFindFileData);
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
             Utils::Log("[VFS-FindA] Crash");
             return INVALID_HANDLE_VALUE;
        }
    }

    static BOOL VirtualFindNextFileA_Internal(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData) {
        WIN32_FIND_DATAW findDataW;
        if (VirtualFindNextFileW_Internal(hFindFile, &findDataW)) {
            WideCharToMultiByte(Config::LE_Codepage, 0, findDataW.cFileName, -1, lpFindFileData->cFileName, MAX_PATH, NULL, NULL);
            lpFindFileData->dwFileAttributes = findDataW.dwFileAttributes;
            lpFindFileData->ftCreationTime = findDataW.ftCreationTime;
            lpFindFileData->ftLastAccessTime = findDataW.ftLastAccessTime;
            lpFindFileData->ftLastWriteTime = findDataW.ftLastWriteTime;
            lpFindFileData->nFileSizeHigh = findDataW.nFileSizeHigh;
            lpFindFileData->nFileSizeLow = findDataW.nFileSizeLow;
            lpFindFileData->dwReserved0 = findDataW.dwReserved0;
            lpFindFileData->dwReserved1 = findDataW.dwReserved1;
            return TRUE;
        }
        return FALSE;
    }

    BOOL VirtualFindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData) {
        __try {
            return VirtualFindNextFileA_Internal(hFindFile, lpFindFileData);
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
             Utils::Log("[VFS-FindA] Crash in Next");
             return FALSE;
        }
    }

    BOOL VirtualFindClose(HANDLE hFindFile) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FindMap.find(hFindFile);
        if (it == g_FindMap.end()) {
             return g_OrigFindClose(hFindFile);
        }
        
        VirtualFindState* state = it->second;
        if (state->realHandle != INVALID_HANDLE_VALUE) {
            g_OrigFindClose(state->realHandle);
        }
        delete state;
        g_FindMap.erase(it);
        return TRUE;
    }

    bool ExtractFile(const wchar_t* relativePath, const wchar_t* destPath) {
        if (!g_IsActive || !relativePath || !destPath) return false;

        std::wstring normalized = NormalizePath(relativePath);

        std::unique_lock<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FileIndex.find(normalized);
        if (it == g_FileIndex.end()) {
            return false;
        }

        VirtualFileEntry& entry = it->second;
        lock.unlock();

        if (entry.isLooseFile) {
            return CopyFileW(entry.looseFilePath.c_str(), destPath, FALSE);
        }

        if (g_ArchiveHandle == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER seekPos;
        seekPos.QuadPart = entry.offset;

        auto setFilePointerExFn = g_RawSetFilePointerEx ? g_RawSetFilePointerEx : SetFilePointerEx;
        auto readFileFn = g_RawReadFile ? g_RawReadFile : ReadFile;

        std::lock_guard<std::recursive_mutex> reLock(g_Mutex);

        if (!setFilePointerExFn(g_ArchiveHandle, seekPos, NULL, FILE_BEGIN)) return false;

        std::vector<BYTE> buffer(entry.size);
        DWORD bytesRead = 0;
        if (!readFileFn(g_ArchiveHandle, buffer.data(), entry.size, &bytesRead, NULL) || bytesRead != entry.size) return false;

        HANDLE hDestFile = CreateFileW(destPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hDestFile == INVALID_HANDLE_VALUE) return false;

        DWORD bytesWritten = 0;
        PBYTE dataToWrite = buffer.data();
        DWORD sizeToWrite = entry.size;
        std::vector<BYTE> decompressedData;

        if (entry.size < entry.decompressedSize) {
            decompressedData.resize(entry.decompressedSize);
            if (DecompressData(buffer, entry.decompressedSize, decompressedData.data())) {
                dataToWrite = decompressedData.data();
                sizeToWrite = entry.decompressedSize;
            } else {
                CloseHandle(hDestFile);
                DeleteFileW(destPath);
                return false;
            }
        }

        BOOL writeOk = WriteFile(hDestFile, dataToWrite, sizeToWrite, &bytesWritten, NULL);
        CloseHandle(hDestFile);
        return writeOk && bytesWritten == sizeToWrite;
    }

    HANDLE OpenVirtualFileA(const char* lpFileName) {
        wchar_t fileNameW[MAX_PATH];
        MultiByteToWideChar(Config::LE_Codepage, 0, lpFileName, -1, fileNameW, MAX_PATH);
        return OpenVirtualFile(fileNameW);
    }

    void GetVirtualFileList(std::vector<std::wstring>& fileList) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        for (const auto& kv : g_FileIndex) {
            fileList.push_back(kv.second.relativePath);
        }
    }
}

namespace Modern {
    typedef VFS::VirtualFileEntry VirtualFileEntry;
    typedef VFS::VirtualFileHandle VirtualFileHandle;

    struct VirtualFindState {
        HANDLE realHandle;
        bool usingRealHandle;
        std::vector<std::wstring> seenFiles;
        std::vector<VirtualFileEntry*> matches;
        size_t matchIndex;
    };

    static std::unordered_map<std::wstring, VirtualFileEntry> g_FileIndex;
    static std::unordered_map<HANDLE, VirtualFileHandle*> g_HandleMap;
    static std::unordered_map<HANDLE, VirtualFindState*> g_FindMap;
    static std::unordered_map<HANDLE, std::wstring> g_MixedHandleMap; 
    static std::recursive_mutex g_Mutex;

    static bool DecompressData(const std::vector<BYTE>& input, DWORD decompressedSize, PBYTE output) {
        DECOMPRESSOR_HANDLE decompressor = NULL;
        if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, NULL, &decompressor)) {
            Utils::Log("[VFS] CreateDecompressor failed: %d", GetLastError());
            return false;
        }
        SIZE_T actualDecompressedSize = 0;
        if (!Decompress(decompressor, input.data(), input.size(), output, decompressedSize, &actualDecompressedSize)) {
            Utils::Log("[VFS] Decompress failed: %d", GetLastError());
            CloseDecompressor(decompressor);
            return false;
        }
        CloseDecompressor(decompressor);
        return actualDecompressedSize == decompressedSize;
    }

    static HANDLE g_ArchiveHandle = INVALID_HANDLE_VALUE;
    static wchar_t g_ArchivePath[MAX_PATH] = { 0 };
    static wchar_t g_LooseFolderPath[MAX_PATH] = { 0 };
    static wchar_t g_HybridCacheDir[MAX_PATH] = { 0 }; 
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
        MultiByteToWideChar(Config::LE_Codepage, 0, path, -1, wpath, MAX_PATH);
        return NormalizePath(wpath);
    }

    void SetOriginalFunctions(void* readFile, void* setFilePointerEx, void* closeHandle) {
        g_OrigReadFile = (pReadFile)readFile;
        g_OrigSetFilePointerEx = (pSetFilePointerEx)setFilePointerEx;
        g_OrigCloseHandle = (pCloseHandle)closeHandle;
    }

    void SetFindFunctions(void* findFirstW, void* findNextW, void* findClose, void* findFirstA, void* findNextA) {
        g_OrigFindFirstFileW = (pFindFirstFileW)findFirstW;
        g_OrigFindNextFileW = (pFindNextFileW)findNextW;
        g_OrigFindClose = (pFindClose)findClose;
        g_OrigFindFirstFileA = (pFindFirstFileA)findFirstA;
        g_OrigFindNextFileA = (pFindNextFileA)findNextA;
    }

    bool ExtractFile(const wchar_t* relativePath, const wchar_t* destPath) {
        std::wstring n = NormalizePath(relativePath);
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FileIndex.find(n);
        if (it == g_FileIndex.end()) return false;
        if (it->second.isLooseFile) return CopyFileW(it->second.looseFilePath.c_str(), destPath, FALSE);
        LARGE_INTEGER s; s.QuadPart = it->second.offset;
        g_RawSetFilePointerEx(g_ArchiveHandle, s, NULL, FILE_BEGIN);
        std::vector<BYTE> buf(it->second.size); DWORD r = 0;
        g_RawReadFile(g_ArchiveHandle, buf.data(), it->second.size, &r, NULL);
        HANDLE h = CreateFileW(destPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD w = 0; PBYTE d = buf.data(); DWORD sz = it->second.size; std::vector<BYTE> dec;
        if (sz < it->second.decompressedSize) {
             dec.resize(it->second.decompressedSize);
             if (DecompressData(buf, it->second.decompressedSize, dec.data())) { d = dec.data(); sz = it->second.decompressedSize; }
        }
        WriteFile(h, d, sz, &w, NULL); CloseHandle(h);
        return true;
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
                Utils::LogW(L"[VFS] Entering directory: '%s'", fullPath);
                ScanLooseFiles(basePath, fullPath, relativePath);
            } else {
                VirtualFileEntry entry;
                entry.relativePath = relativePath;
                entry.offset = 0;
                entry.size = fd.nFileSizeLow;
                entry.decompressedSize = fd.nFileSizeLow;
                entry.isLooseFile = true;
                entry.looseFilePath = fullPath;
                std::wstring normalizedPath = NormalizePath(relativePath);
                g_FileIndex[normalizedPath] = entry;
                Utils::LogW(L"[VFS] Loose file indexed: key='%s' path='%s'", normalizedPath.c_str(), fullPath);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    bool Initialize(HMODULE hModule) {
        Utils::Log("[VFS-Modern] Initialize called");
        g_RawReadFile = (pReadFile)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ReadFile");
        g_RawSetFilePointerEx = (pSetFilePointerEx)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetFilePointerEx");
        g_RawCloseHandle = (pCloseHandle)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CloseHandle");
        g_RawCreateFileW = (pCreateFileW)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateFileW");

        GetTempPathW(MAX_PATH, g_HybridCacheDir);
        PathAppendW(g_HybridCacheDir, L"VFS_CHS_Cache");
        if (!PathIsDirectoryW(g_HybridCacheDir)) CreateDirectoryW(g_HybridCacheDir, NULL);

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
            Utils::LogW(L"[VFS] Scanning loose files from: %s", g_LooseFolderPath);
            ScanLooseFiles(g_LooseFolderPath, g_LooseFolderPath, L"");
        }
        wcscpy_s(g_ArchivePath, baseDir);
        PathAppendW(g_ArchivePath, Config::ArchiveFileName);
        if (!PathFileExistsW(g_ArchivePath)) {
             wchar_t fallbackPath[MAX_PATH];
             wcscpy_s(fallbackPath, baseDir);
             PathAppendW(fallbackPath, Config::RedirectFolderW);
             PathAppendW(fallbackPath, Config::ArchiveFileName);
             if (PathFileExistsW(fallbackPath)) wcscpy_s(g_ArchivePath, fallbackPath);
        }
        if (PathFileExistsW(g_ArchivePath)) {
            g_ArchiveHandle = g_RawCreateFileW(g_ArchivePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (g_ArchiveHandle != INVALID_HANDLE_VALUE) {
                DWORD bytesRead;
                int fileCount = 0;
                if (g_RawReadFile(g_ArchiveHandle, &fileCount, sizeof(int), &bytesRead, NULL)) {
                    for (int i = 0; i < fileCount; i++) {
                        int pathLen = 0;
                        if (!g_RawReadFile(g_ArchiveHandle, &pathLen, sizeof(int), &bytesRead, NULL)) break;
                        std::vector<char> relPath(pathLen + 1, '\0');
                        if (!g_RawReadFile(g_ArchiveHandle, relPath.data(), pathLen, &bytesRead, NULL)) break;
                        wchar_t relPathW[MAX_PATH];
                        MultiByteToWideChar(CP_UTF8, 0, relPath.data(), -1, relPathW, MAX_PATH);
                        int decompressedSize = 0;
                        g_RawReadFile(g_ArchiveHandle, &decompressedSize, sizeof(int), &bytesRead, NULL);
                        int storedSize = 0;
                        g_RawReadFile(g_ArchiveHandle, &storedSize, sizeof(int), &bytesRead, NULL);
                        LARGE_INTEGER currentPos;
                        LARGE_INTEGER zero = { 0 };
                        g_RawSetFilePointerEx(g_ArchiveHandle, zero, &currentPos, FILE_CURRENT);
                        std::wstring normalizedPath = NormalizePath(relPathW);
                        if (g_FileIndex.find(normalizedPath) == g_FileIndex.end()) {
                            VirtualFileEntry entry;
                            entry.relativePath = relPathW;
                            entry.offset = currentPos.QuadPart;
                            entry.size = storedSize;
                            entry.decompressedSize = decompressedSize;
                            entry.isLooseFile = false;
                            g_FileIndex[normalizedPath] = entry;
                        }
                        LARGE_INTEGER skipDist;
                        skipDist.QuadPart = storedSize;
                        g_RawSetFilePointerEx(g_ArchiveHandle, skipDist, NULL, FILE_CURRENT);
                    }
                }
            }
        }
        if (g_FileIndex.size() > 0) {
            g_IsActive = true;
            Utils::Log("[VFS] Index built successfully. %zu files total.", g_FileIndex.size());
            return true;
        }
        return false;
    }

    void Shutdown() {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        for (auto& pair : g_HandleMap) delete pair.second;
        g_HandleMap.clear();
        for (auto& pair : g_MixedHandleMap) {
            g_RawCloseHandle(pair.first);
            DeleteFileW(pair.second.c_str());
        }
        g_MixedHandleMap.clear();
        g_FileIndex.clear();
        if (g_ArchiveHandle != INVALID_HANDLE_VALUE) {
            g_RawCloseHandle(g_ArchiveHandle);
            g_ArchiveHandle = INVALID_HANDLE_VALUE;
        }
        g_IsActive = false;
    }

    bool IsActive() { return g_IsActive; }

    bool HasVirtualFile(const wchar_t* relativePath) {
        if (!g_IsActive || !relativePath) return false;
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        return g_FileIndex.find(NormalizePath(relativePath)) != g_FileIndex.end();
    }

    HANDLE OpenVirtualFile(const wchar_t* relativePath) {
        if (!g_IsActive || !relativePath) return INVALID_HANDLE_VALUE;
        std::wstring normalized = NormalizePath(relativePath);
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FileIndex.find(normalized);
        if (it == g_FileIndex.end()) return INVALID_HANDLE_VALUE;

        if (it->second.isLooseFile) {
            return g_RawCreateFileW(it->second.looseFilePath.c_str(), GENERIC_READ, 
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        }

        wchar_t cacheName[MAX_PATH];
        swprintf_s(cacheName, L"vfs_chs_%u.tmp", (UINT)it->second.offset);
        wchar_t cachePath[MAX_PATH];
        wcscpy_s(cachePath, g_HybridCacheDir);
        PathAppendW(cachePath, cacheName);
        if (!PathFileExistsW(cachePath)) {
            if (!ExtractFile(it->second.relativePath.c_str(), cachePath)) return INVALID_HANDLE_VALUE;
        }
        HANDLE hReal = g_RawCreateFileW(cachePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hReal != INVALID_HANDLE_VALUE) {
            g_MixedHandleMap[hReal] = cachePath;
            return hReal;
        }

        VirtualFileHandle* vfh = new VirtualFileHandle();
        vfh->entry = &it->second; vfh->position = 0; vfh->isLooseFile = false;
        vfh->decompressedBuffer = NULL; vfh->archiveHandle = g_ArchiveHandle;
        vfh->looseFileHandle = INVALID_HANDLE_VALUE;
        HANDLE fakeHandle = (HANDLE)(++g_VirtualHandleCounter);
        g_HandleMap[fakeHandle] = vfh;
        return fakeHandle;
    }

    void GetVirtualFileList(std::vector<std::wstring>& fileList) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        for (const auto& kv : g_FileIndex) {
            fileList.push_back(kv.second.relativePath);
        }
    }

    bool IsVirtualHandle(HANDLE hFile) {
        if (!g_IsActive) return false;
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        return g_HandleMap.find(hFile) != g_HandleMap.end();
    }

    BOOL ReadVirtualFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(hFile);
        if (it == g_HandleMap.end()) return FALSE;
        VirtualFileHandle* vfh = it->second;
        LONGLONG remaining = vfh->entry->decompressedSize - vfh->position;
        if (remaining <= 0) { if (lpNumberOfBytesRead) *lpNumberOfBytesRead = 0; return TRUE; }
        DWORD toRead = (DWORD)min((LONGLONG)nNumberOfBytesToRead, remaining);
        DWORD bytesRead = 0;
        if (vfh->decompressedBuffer) {
            memcpy(lpBuffer, vfh->decompressedBuffer + vfh->position, toRead);
            bytesRead = toRead;
        } else {
            LARGE_INTEGER seekPos;
            seekPos.QuadPart = vfh->entry->offset + vfh->position;
            g_RawSetFilePointerEx(vfh->archiveHandle, seekPos, NULL, FILE_BEGIN);
            g_RawReadFile(vfh->archiveHandle, lpBuffer, toRead, &bytesRead, NULL);
        }
        vfh->position += bytesRead;
        if (lpNumberOfBytesRead) *lpNumberOfBytesRead = bytesRead;
        return TRUE;
    }

    DWORD SetVirtualFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(hFile);
        if (it == g_HandleMap.end()) return INVALID_SET_FILE_POINTER;
        VirtualFileHandle* vfh = it->second;
        LONGLONG distance = lDistanceToMove;
        if (lpDistanceToMoveHigh) distance |= ((LONGLONG)*lpDistanceToMoveHigh) << 32;
        LONGLONG newPos = 0;
        if (dwMoveMethod == FILE_BEGIN) newPos = distance;
        else if (dwMoveMethod == FILE_CURRENT) newPos = vfh->position + distance;
        else if (dwMoveMethod == FILE_END) newPos = vfh->entry->decompressedSize + distance;
        if (newPos < 0) newPos = 0;
        if (newPos > (LONGLONG)vfh->entry->decompressedSize) newPos = vfh->entry->decompressedSize;
        vfh->position = newPos;
        if (lpDistanceToMoveHigh) *lpDistanceToMoveHigh = (LONG)(newPos >> 32);
        return (DWORD)(newPos & 0xFFFFFFFF);
    }

    BOOL SetVirtualFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(hFile);
        if (it == g_HandleMap.end()) return FALSE;
        VirtualFileHandle* vfh = it->second;
        LONGLONG newPos = 0;
        if (dwMoveMethod == FILE_BEGIN) newPos = liDistanceToMove.QuadPart;
        else if (dwMoveMethod == FILE_CURRENT) newPos = vfh->position + liDistanceToMove.QuadPart;
        else if (dwMoveMethod == FILE_END) newPos = vfh->entry->decompressedSize + liDistanceToMove.QuadPart;
        if (newPos < 0) newPos = 0;
        if (newPos > (LONGLONG)vfh->entry->decompressedSize) newPos = vfh->entry->decompressedSize;
        vfh->position = newPos;
        if (lpNewFilePointer) lpNewFilePointer->QuadPart = newPos;
        return TRUE;
    }

    DWORD GetVirtualFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(hFile);
        if (it == g_HandleMap.end()) return INVALID_FILE_SIZE;
        if (lpFileSizeHigh) *lpFileSizeHigh = (DWORD)((ULONGLONG)it->second->entry->decompressedSize >> 32);
        return (DWORD)(it->second->entry->decompressedSize & 0xFFFFFFFF);
    }

    BOOL GetVirtualFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(hFile);
        if (it == g_HandleMap.end()) return FALSE;
        if (lpFileSize) lpFileSize->QuadPart = it->second->entry->decompressedSize;
        return TRUE;
    }

    BOOL CloseVirtualHandle(HANDLE hFile) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it_m = g_MixedHandleMap.find(hFile);
        if (it_m != g_MixedHandleMap.end()) {
            g_RawCloseHandle(hFile); g_MixedHandleMap.erase(it_m); return TRUE;
        }
        auto it = g_HandleMap.find(hFile);
        if (it == g_HandleMap.end()) return FALSE;
        if (it->second->decompressedBuffer) HeapFree(GetProcessHeap(), 0, it->second->decompressedBuffer);
        delete it->second; g_HandleMap.erase(it); return TRUE;
    }

    BOOL GetVirtualFileInformationByHandle(HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(hFile);
        if (it == g_HandleMap.end()) return FALSE;
        VirtualFileHandle* vfh = it->second;
        ZeroMemory(lpFileInformation, sizeof(BY_HANDLE_FILE_INFORMATION));
        lpFileInformation->dwFileAttributes = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY;
        lpFileInformation->nFileSizeLow = (DWORD)(vfh->entry->decompressedSize & 0xFFFFFFFF);
        lpFileInformation->nFileSizeHigh = (DWORD)((ULONGLONG)vfh->entry->decompressedSize >> 32);
        lpFileInformation->nNumberOfLinks = 1;
        return TRUE;
    }

    HANDLE VirtualFindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData) {
        wchar_t fullPath[MAX_PATH];
        if (!GetFullPathNameW(lpFileName, MAX_PATH, fullPath, NULL)) return g_OrigFindFirstFileW(lpFileName, lpFindFileData);
        std::wstring fstr = fullPath;
        size_t last = fstr.find_last_of(L"\\/");
        if (last == std::wstring::npos) return g_OrigFindFirstFileW(lpFileName, lpFindFileData);
        std::wstring sDir = fstr.substr(0, last), pattern = fstr.substr(last + 1);
        VirtualFindState* state = new VirtualFindState();
        state->realHandle = g_OrigFindFirstFileW(lpFileName, lpFindFileData);
        state->usingRealHandle = (state->realHandle != INVALID_HANDLE_VALUE);
        state->matchIndex = 0;
        if (state->usingRealHandle) state->seenFiles.push_back(lpFindFileData->cFileName);
        wchar_t gameRoot[MAX_PATH];
        GetModuleFileNameW(NULL, gameRoot, MAX_PATH);
        PathRemoveFileSpecW(gameRoot);
        std::wstring sDirNorm = NormalizePath(sDir.c_str());
        if (!sDirNorm.empty() && sDirNorm.back() == L'\\') sDirNorm.pop_back();
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        for (auto& kv : g_FileIndex) {
            wchar_t vFullPath[MAX_PATH], vDir[MAX_PATH];
            wcscpy_s(vFullPath, gameRoot); PathAppendW(vFullPath, kv.second.relativePath.c_str());
            wcscpy_s(vDir, vFullPath); PathRemoveFileSpecW(vDir);
            std::wstring vDirNorm = NormalizePath(vDir);
            if (!vDirNorm.empty() && vDirNorm.back() == L'\\') vDirNorm.pop_back();
            if (vDirNorm == sDirNorm && PathMatchSpecW(PathFindFileNameW(vFullPath), pattern.c_str())) state->matches.push_back(&kv.second);
        }
        if (state->matches.empty()) { HANDLE real = state->realHandle; delete state; return real; }
        if (!state->usingRealHandle) {
             VirtualFileEntry* m = state->matches[0];
             wcscpy_s(lpFindFileData->cFileName, PathFindFileNameW(m->relativePath.c_str()));
             lpFindFileData->nFileSizeLow = m->decompressedSize;
             lpFindFileData->dwFileAttributes = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY;
             state->matchIndex++;
        }
        HANDLE hFake = (HANDLE)(++g_VirtualHandleCounter);
        g_FindMap[hFake] = state;
        return hFake;
    }

    BOOL VirtualFindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FindMap.find(hFindFile);
        if (it == g_FindMap.end()) return g_OrigFindNextFileW(hFindFile, lpFindFileData);
        VirtualFindState* s = it->second;
        if (s->usingRealHandle && g_OrigFindNextFileW(s->realHandle, lpFindFileData)) { s->seenFiles.push_back(lpFindFileData->cFileName); return TRUE; }
        s->usingRealHandle = false;
        while (s->matchIndex < s->matches.size()) {
            VirtualFileEntry* m = s->matches[s->matchIndex++];
            const wchar_t* name = PathFindFileNameW(m->relativePath.c_str());
            bool seen = false;
            for (auto& f : s->seenFiles) if (_wcsicmp(f.c_str(), name) == 0) { seen = true; break; }
            if (seen) continue;
            wcscpy_s(lpFindFileData->cFileName, name);
            lpFindFileData->nFileSizeLow = m->decompressedSize;
            lpFindFileData->dwFileAttributes = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY;
            return TRUE;
        }
        SetLastError(ERROR_NO_MORE_FILES); return FALSE;
    }

    BOOL VirtualFindClose(HANDLE hFindFile) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FindMap.find(hFindFile);
        if (it == g_FindMap.end()) return g_OrigFindClose(hFindFile);
        if (it->second->realHandle != INVALID_HANDLE_VALUE) g_OrigFindClose(it->second->realHandle);
        delete it->second; g_FindMap.erase(it); return TRUE;
    }

    DWORD GetVirtualFileType(HANDLE h) { return FILE_TYPE_DISK; }
}

namespace VFS {
    bool Initialize(HMODULE hModule) {
        if (Config::VFSMode == 0) return Modern::Initialize(hModule);
        else return Legacy::Initialize(hModule);
    }

    void Shutdown() {
        Legacy::Shutdown();
        Modern::Shutdown();
    }

    void SetOriginalFunctions(void* r, void* s, void* c) {
        Legacy::SetOriginalFunctions(r, s, c);
        Modern::SetOriginalFunctions(r, s, c);
        g_OrigReadFile = (pReadFile)r; 
        g_OrigSetFilePointerEx = (pSetFilePointerEx)s; 
        g_OrigCloseHandle = (pCloseHandle)c;
    }

    void SetFindFunctions(void* f1, void* f2, void* f3, void* f4, void* f5) {
        Legacy::SetFindFunctions(f1, f2, f3, f4, f5);
        Modern::SetFindFunctions(f1, f2, f3, f4, f5);
        g_OrigFindFirstFileW = (pFindFirstFileW)f1; 
        g_OrigFindNextFileW = (pFindNextFileW)f2; 
        g_OrigFindClose = (pFindClose)f3;
        g_OrigFindFirstFileA = (pFindFirstFileA)f4; 
        g_OrigFindNextFileA = (pFindNextFileA)f5;
    }

    bool IsActive() { 
        return (Config::VFSMode == 0) ? Modern::IsActive() : Legacy::IsActive(); 
    }

    bool HasVirtualFile(const wchar_t* p) { 
        return (Config::VFSMode == 0) ? Modern::HasVirtualFile(p) : Legacy::HasVirtualFile(p); 
    }
    
    bool HasVirtualFileA(const char* p) {
        if (Config::VFSMode == 0) {
             wchar_t w[MAX_PATH]; MultiByteToWideChar(Config::LE_Codepage, 0, p, -1, w, MAX_PATH);
             return Modern::HasVirtualFile(w);
        } else {
             return Legacy::HasVirtualFileA(p);
        }
    }

    HANDLE OpenVirtualFile(const wchar_t* p) { 
        return (Config::VFSMode == 0) ? Modern::OpenVirtualFile(p) : Legacy::OpenVirtualFile(p); 
    }
    
    HANDLE OpenVirtualFileA(const char* p) {
        if (Config::VFSMode == 0) {
             wchar_t w[MAX_PATH]; MultiByteToWideChar(Config::LE_Codepage, 0, p, -1, w, MAX_PATH);
             return Modern::OpenVirtualFile(w);
        } else {
             return Legacy::OpenVirtualFileA(p);
        }
    }

    bool IsVirtualHandle(HANDLE h) { 
        return (Config::VFSMode == 0) ? Modern::IsVirtualHandle(h) : Legacy::IsVirtualHandle(h); 
    }

    BOOL ReadVirtualFile(HANDLE h, LPVOID b, DWORD n, LPDWORD r, LPOVERLAPPED o) { 
        return (Config::VFSMode == 0) ? Modern::ReadVirtualFile(h, b, n, r, o) : Legacy::ReadVirtualFile(h, b, n, r, o); 
    }
    
    DWORD SetVirtualFilePointer(HANDLE h, LONG d, PLONG dh, DWORD m) { 
        return (Config::VFSMode == 0) ? Modern::SetVirtualFilePointer(h, d, dh, m) : Legacy::SetVirtualFilePointer(h, d, dh, m); 
    }
    
    BOOL SetVirtualFilePointerEx(HANDLE h, LARGE_INTEGER d, PLARGE_INTEGER np, DWORD m) { 
        return (Config::VFSMode == 0) ? Modern::SetVirtualFilePointerEx(h, d, np, m) : Legacy::SetVirtualFilePointerEx(h, d, np, m); 
    }

    DWORD GetVirtualFileSize(HANDLE h, LPDWORD hs) { 
        return (Config::VFSMode == 0) ? Modern::GetVirtualFileSize(h, hs) : Legacy::GetVirtualFileSize(h, hs); 
    }
    
    BOOL GetVirtualFileSizeEx(HANDLE h, PLARGE_INTEGER s) { 
        return (Config::VFSMode == 0) ? Modern::GetVirtualFileSizeEx(h, s) : Legacy::GetVirtualFileSizeEx(h, s); 
    }

    BOOL CloseVirtualHandle(HANDLE h) { 
        return (Config::VFSMode == 0) ? Modern::CloseVirtualHandle(h) : Legacy::CloseVirtualHandle(h); 
    }

    BOOL GetVirtualFileInformationByHandle(HANDLE h, LPBY_HANDLE_FILE_INFORMATION i) { 
        return (Config::VFSMode == 0) ? Modern::GetVirtualFileInformationByHandle(h, i) : Legacy::GetVirtualFileInformationByHandle(h, i); 
    }
    
    DWORD GetVirtualFileType(HANDLE h) { 
        return (Config::VFSMode == 0) ? Modern::GetVirtualFileType(h) : Legacy::GetVirtualFileType(h); 
    }

    HANDLE VirtualFindFirstFileW(LPCWSTR n, LPWIN32_FIND_DATAW fd) { 
        return (Config::VFSMode == 0) ? Modern::VirtualFindFirstFileW(n, fd) : Legacy::VirtualFindFirstFileW(n, fd); 
    }
    
    BOOL VirtualFindNextFileW(HANDLE h, LPWIN32_FIND_DATAW fd) { 
        return (Config::VFSMode == 0) ? Modern::VirtualFindNextFileW(h, fd) : Legacy::VirtualFindNextFileW(h, fd); 
    }
    
    BOOL VirtualFindClose(HANDLE h) { 
        return (Config::VFSMode == 0) ? Modern::VirtualFindClose(h) : Legacy::VirtualFindClose(h); 
    }

    HANDLE VirtualFindFirstFileA(LPCSTR n, LPWIN32_FIND_DATAA fd) {
        if (Config::VFSMode == 0) {
            wchar_t w[MAX_PATH]; MultiByteToWideChar(Config::LE_Codepage, 0, n, -1, w, MAX_PATH);
            WIN32_FIND_DATAW fw; 
            HANDLE h = Modern::VirtualFindFirstFileW(w, &fw);
            if (h != INVALID_HANDLE_VALUE) {
                WideCharToMultiByte(Config::LE_Codepage, 0, fw.cFileName, -1, fd->cFileName, MAX_PATH, NULL, NULL);
                fd->dwFileAttributes = fw.dwFileAttributes; 
                fd->nFileSizeLow = fw.nFileSizeLow; 
                fd->nFileSizeHigh = fw.nFileSizeHigh;
                fd->ftCreationTime = fw.ftCreationTime;
                fd->ftLastAccessTime = fw.ftLastAccessTime;
                fd->ftLastWriteTime = fw.ftLastWriteTime;
            }
            return h;
        } else {
            return Legacy::VirtualFindFirstFileA(n, fd);
        }
    }

    BOOL VirtualFindNextFileA(HANDLE h, LPWIN32_FIND_DATAA fd) {
        if (Config::VFSMode == 0) {
            WIN32_FIND_DATAW fw; 
            if (Modern::VirtualFindNextFileW(h, &fw)) {
                WideCharToMultiByte(Config::LE_Codepage, 0, fw.cFileName, -1, fd->cFileName, MAX_PATH, NULL, NULL);
                fd->dwFileAttributes = fw.dwFileAttributes; 
                fd->nFileSizeLow = fw.nFileSizeLow; 
                fd->nFileSizeHigh = fw.nFileSizeHigh;
                fd->ftCreationTime = fw.ftCreationTime;
                fd->ftLastAccessTime = fw.ftLastAccessTime;
                fd->ftLastWriteTime = fw.ftLastWriteTime;
                return TRUE;
            }
            return FALSE;
        } else {
            return Legacy::VirtualFindNextFileA(h, fd);
        }
    }

    bool ExtractFile(const wchar_t* r, const wchar_t* d) { 
        return (Config::VFSMode == 0) ? Modern::ExtractFile(r, d) : Legacy::ExtractFile(r, d); 
    }

    void GetVirtualFileList(std::vector<std::wstring>& fileList) {
        if (Config::VFSMode == 0) Modern::GetVirtualFileList(fileList);
        else Legacy::GetVirtualFileList(fileList);
    }
}

#include "../pch.h"
#include "vfs.h"
#include "config.h"
#include "utils.h"
#include <shlwapi.h>
#include <compressapi.h>
#include <mutex>
#include <vector>
#include <algorithm>

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

namespace VFS {
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
    static std::unordered_map<HANDLE, std::wstring> g_MixedHandleMap; // 用于记录混合模式下的真实路径
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
    static wchar_t g_HybridCacheDir[MAX_PATH] = { 0 }; // 缓存路径
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
        Utils::Log("[VFS] Initialize called");
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
                        MultiByteToWideChar(Config::LE_Codepage, 0, relPath.data(), -1, relPathW, MAX_PATH);
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

    bool HasVirtualFileA(const char* relativePath) {
        if (!g_IsActive || !relativePath) return false;
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        return g_FileIndex.find(NormalizePathA(relativePath)) != g_FileIndex.end();
    }

    HANDLE OpenVirtualFile(const wchar_t* relativePath) {
        if (!g_IsActive || !relativePath) return INVALID_HANDLE_VALUE;
        std::wstring normalized = NormalizePath(relativePath);
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FileIndex.find(normalized);
        if (it == g_FileIndex.end()) return INVALID_HANDLE_VALUE;

        // --- 核心修复：Loose 文件直接返回真实句柄解决 Nepgear 报错 ---
        if (it->second.isLooseFile) {
            return g_RawCreateFileW(it->second.looseFilePath.c_str(), GENERIC_READ, 
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        }

        // --- 核心修复：Archive 文件透明提取解决 CHS 报错 ---
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

        // --- 内存虚拟化逻辑作为最终回退 (你原本写的 0xBF 逻辑) ---
        VirtualFileHandle* vfh = new VirtualFileHandle();
        vfh->entry = &it->second; vfh->position = 0; vfh->isLooseFile = false;
        vfh->decompressedBuffer = NULL; vfh->archiveHandle = g_ArchiveHandle;
        vfh->looseFileHandle = INVALID_HANDLE_VALUE;
        HANDLE fakeHandle = (HANDLE)(++g_VirtualHandleCounter);
        g_HandleMap[fakeHandle] = vfh;
        return fakeHandle;
    }

    HANDLE OpenVirtualFileA(const char* relativePath) {
        wchar_t wpath[MAX_PATH];
        MultiByteToWideChar(Config::LE_Codepage, 0, relativePath, -1, wpath, MAX_PATH);
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
        if (lpFileSizeHigh) *lpFileSizeHigh = (DWORD)(it->second->entry->decompressedSize >> 32);
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
        lpFileInformation->nFileSizeHigh = (DWORD)(vfh->entry->decompressedSize >> 32);
        lpFileInformation->nNumberOfLinks = 1;
        return TRUE;
    }

    DWORD GetVirtualFileType(HANDLE hFile) { return FILE_TYPE_DISK; }

    static HANDLE VirtualFindFirstFileW_Internal(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData) {
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

    HANDLE VirtualFindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData) {
        __try { return VirtualFindFirstFileW_Internal(lpFileName, lpFindFileData); }
        __except(EXCEPTION_EXECUTE_HANDLER) { Utils::Log("[VFS-Find] Crash"); return INVALID_HANDLE_VALUE; }
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

    HANDLE VirtualFindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData) {
        wchar_t w[MAX_PATH]; MultiByteToWideChar(Config::LE_Codepage, 0, lpFileName, -1, w, MAX_PATH);
        WIN32_FIND_DATAW dw; HANDLE h = VirtualFindFirstFileW(w, &dw);
        if (h != INVALID_HANDLE_VALUE) {
            WideCharToMultiByte(Config::LE_Codepage, 0, dw.cFileName, -1, lpFindFileData->cFileName, MAX_PATH, NULL, NULL);
            lpFindFileData->dwFileAttributes = dw.dwFileAttributes; lpFindFileData->nFileSizeLow = dw.nFileSizeLow;
        }
        return h;
    }

    BOOL VirtualFindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData) {
        WIN32_FIND_DATAW dw;
        if (VirtualFindNextFileW(hFindFile, &dw)) {
            WideCharToMultiByte(Config::LE_Codepage, 0, dw.cFileName, -1, lpFindFileData->cFileName, MAX_PATH, NULL, NULL);
            lpFindFileData->dwFileAttributes = dw.dwFileAttributes; lpFindFileData->nFileSizeLow = dw.nFileSizeLow;
            return TRUE;
        }
        return FALSE;
    }

    BOOL VirtualFindClose(HANDLE hFindFile) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FindMap.find(hFindFile);
        if (it == g_FindMap.end()) return g_OrigFindClose(hFindFile);
        if (it->second->realHandle != INVALID_HANDLE_VALUE) g_OrigFindClose(it->second->realHandle);
        delete it->second; g_FindMap.erase(it); return TRUE;
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
        if (sz < it->second.decompressedSize) { dec.resize(it->second.decompressedSize); if (DecompressData(buf, it->second.decompressedSize, dec.data())) { d = dec.data(); sz = it->second.decompressedSize; } }
        WriteFile(h, d, sz, &w, NULL); CloseHandle(h); return true;
    }
}
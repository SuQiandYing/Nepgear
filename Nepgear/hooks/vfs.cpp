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
#include <set>

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

// Shared Global State
static pReadFile g_OrigReadFile = nullptr;
static pSetFilePointerEx g_OrigSetFilePointerEx = nullptr;
static pCloseHandle g_OrigCloseHandle = nullptr;
static pFindFirstFileW g_OrigFindFirstFileW = nullptr;
static pFindNextFileW g_OrigFindNextFileW = nullptr;
static pFindClose g_OrigFindClose = nullptr;
static pFindFirstFileA g_OrigFindFirstFileA = nullptr;
static pFindNextFileA g_OrigFindNextFileA = nullptr;

static pReadFile g_RawReadFile = nullptr;
static pSetFilePointerEx g_RawSetFilePointerEx = nullptr;
static pCloseHandle g_RawCloseHandle = nullptr;
static pCreateFileW g_RawCreateFileW = nullptr;

namespace {
    struct VirtualFindState {
        HANDLE realHandle;
        bool usingRealHandle;
        std::vector<std::wstring> seenFiles;
        std::vector<VFS::VirtualFileEntry*> matches;
        size_t matchIndex;

        VirtualFindState() : realHandle(INVALID_HANDLE_VALUE), usingRealHandle(false), matchIndex(0) {}
        ~VirtualFindState() {
            // We should use the original CloseHandle if we have it
            // but the state doesn't know about it. 
            // In practice, VirtualFindClose handles the realHandle.
        }
    };

    std::unordered_map<std::wstring, VFS::VirtualFileEntry> g_FileIndex;
    std::unordered_map<std::wstring, std::vector<VFS::VirtualFileEntry*>> g_DirectoryIndex;
    std::unordered_map<HANDLE, std::unique_ptr<VFS::VirtualFileHandle>> g_HandleMap;
    std::unordered_map<HANDLE, std::unique_ptr<VirtualFindState>> g_FindMap;
    std::unordered_map<HANDLE, std::wstring> g_MixedHandleMap; // Modern mode cache mapping
    std::recursive_mutex g_Mutex;

    // RAII helper for Windows handles using the raw CloseHandle
    struct ScopedRawHandle {
        HANDLE h;
        ScopedRawHandle(HANDLE handle = INVALID_HANDLE_VALUE) : h(handle) {}
        ~ScopedRawHandle() { if (h != INVALID_HANDLE_VALUE && h != NULL && g_RawCloseHandle) g_RawCloseHandle(h); }
        operator HANDLE() const { return h; }
        HANDLE release() { HANDLE tmp = h; h = INVALID_HANDLE_VALUE; return tmp; }
    };

    // RAII helper for Decompressor
    struct ScopedDecompressor {
        DECOMPRESSOR_HANDLE h;
        ScopedDecompressor(DECOMPRESSOR_HANDLE handle = NULL) : h(handle) {}
        ~ScopedDecompressor() { if (h) CloseDecompressor(h); }
        operator DECOMPRESSOR_HANDLE() const { return h; }
    };

    HANDLE g_ArchiveHandle = INVALID_HANDLE_VALUE;
    wchar_t g_ArchivePath[MAX_PATH] = { 0 };
    wchar_t g_LooseFolderPath[MAX_PATH] = { 0 };
    wchar_t g_HybridCacheDir[MAX_PATH] = { 0 };
    bool g_IsActive = false;
    uintptr_t g_VirtualHandleCounter = 0xBF000000;
}

// Utility Functions
static std::wstring NormalizePath(const wchar_t* path) {
    if (!path || path[0] == L'\0') return L"";
    
    std::wstring normalized;
    normalized.reserve(MAX_PATH);
    
    // Skip leading backslashes
    const wchar_t* p = path;
    while (*p == L'\\' || *p == L'/') p++;
    
    for (; *p != L'\0'; ++p) {
        wchar_t c = *p;
        if (c == L'/') c = L'\\';
        else if (c >= L'A' && c <= L'Z') c += (L'a' - L'A');
        normalized += c;
    }
    return normalized;
}

static std::wstring NormalizePathA(const char* path) {
    if (!path || path[0] == '\0') return L"";
    wchar_t wpath[MAX_PATH];
    if (MultiByteToWideChar(Config::LE_Codepage, 0, path, -1, wpath, MAX_PATH) == 0) return L"";
    return NormalizePath(wpath);
}

static void RebuildDirectoryIndex() {
    g_DirectoryIndex.clear();
    for (auto& kv : g_FileIndex) {
        std::wstring path = kv.second.relativePath;
        std::replace(path.begin(), path.end(), L'/', L'\\');
        
        size_t lastSlash = path.find_last_of(L'\\');
        std::wstring dir;
        if (lastSlash != std::wstring::npos) {
            dir = NormalizePath(path.substr(0, lastSlash).c_str());
        } else {
            dir = L""; // Root
        }
        g_DirectoryIndex[dir].push_back(&kv.second);
    }
}

static bool DecompressData(const std::vector<BYTE>& input, DWORD decompressedSize, PBYTE output) {
    ScopedDecompressor decompressor;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, NULL, (PDECOMPRESSOR_HANDLE)&decompressor.h)) return false;

    SIZE_T actualDecompressedSize = 0;
    return Decompress(decompressor, input.data(), input.size(), output, decompressedSize, &actualDecompressedSize) 
           && (actualDecompressedSize == decompressedSize);
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
        if (relativeBase[0] == L'\0') wcscpy_s(relativePath, fd.cFileName);
        else {
            wcscpy_s(relativePath, relativeBase);
            PathAppendW(relativePath, fd.cFileName);
        }

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ScanLooseFiles(basePath, fullPath, relativePath);
        } else {
            VFS::VirtualFileEntry entry;
            entry.relativePath = relativePath;
            entry.offset = 0;
            entry.size = fd.nFileSizeLow;
            entry.decompressedSize = fd.nFileSizeLow;
            entry.isLooseFile = true;
            entry.looseFilePath = fullPath;

            g_FileIndex[NormalizePath(relativePath)] = entry;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

namespace VFS {
    bool Initialize(HMODULE hModule) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        if (g_IsActive) return true;

        g_RawReadFile = (pReadFile)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ReadFile");
        g_RawSetFilePointerEx = (pSetFilePointerEx)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetFilePointerEx");
        g_RawCloseHandle = (pCloseHandle)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CloseHandle");
        g_RawCreateFileW = (pCreateFileW)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateFileW");

        if (!g_RawReadFile || !g_RawSetFilePointerEx || !g_RawCloseHandle || !g_RawCreateFileW) return false;
        if (!Config::EnableFileHook) return false;

        // Setup paths
        wchar_t baseDir[MAX_PATH];
        GetModuleFileNameW(hModule, baseDir, MAX_PATH);
        PathRemoveFileSpecW(baseDir);

        if (Config::VFSMode == 0) { // Modern mode cache
            GetTempPathW(MAX_PATH, g_HybridCacheDir);
            PathAppendW(g_HybridCacheDir, L"VFS_CHS_Cache");
            if (!PathIsDirectoryW(g_HybridCacheDir)) CreateDirectoryW(g_HybridCacheDir, NULL);
        }

        wcscpy_s(g_LooseFolderPath, baseDir);
        PathAppendW(g_LooseFolderPath, Config::RedirectFolderW);

        if (PathFileExistsW(g_LooseFolderPath) && PathIsDirectoryW(g_LooseFolderPath)) {
            ScanLooseFiles(g_LooseFolderPath, g_LooseFolderPath, L"");
        }

        wcscpy_s(g_ArchivePath, baseDir);
        PathAppendW(g_ArchivePath, Config::ArchiveFileName);
        if (!PathFileExistsW(g_ArchivePath)) {
            wchar_t fb[MAX_PATH]; wcscpy_s(fb, baseDir);
            PathAppendW(fb, Config::RedirectFolderW); PathAppendW(fb, Config::ArchiveFileName);
            if (PathFileExistsW(fb)) wcscpy_s(g_ArchivePath, fb);
        }

        if (PathFileExistsW(g_ArchivePath)) {
            ScopedRawHandle hArchive(g_RawCreateFileW(g_ArchivePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
            if (hArchive != INVALID_HANDLE_VALUE) {
                DWORD br; int count = 0;
                if (g_RawReadFile(hArchive, &count, sizeof(int), &br, NULL)) {
                    for (int i = 0; i < count; i++) {
                        int pLen = 0; g_RawReadFile(hArchive, &pLen, sizeof(int), &br, NULL);
                        std::vector<char> pBuf(pLen + 1, '\0'); g_RawReadFile(hArchive, pBuf.data(), pLen, &br, NULL);
                        wchar_t wPath[MAX_PATH]; MultiByteToWideChar(CP_UTF8, 0, pBuf.data(), -1, wPath, MAX_PATH);
                        int dSize = 0; g_RawReadFile(hArchive, &dSize, sizeof(int), &br, NULL);
                        int sSize = 0; g_RawReadFile(hArchive, &sSize, sizeof(int), &br, NULL);
                        LARGE_INTEGER cur; LARGE_INTEGER zero = { 0 };
                        g_RawSetFilePointerEx(hArchive, zero, &cur, FILE_CURRENT);
                        std::wstring norm = NormalizePath(wPath);
                        if (g_FileIndex.find(norm) == g_FileIndex.end()) {
                            VirtualFileEntry e; e.relativePath = wPath; e.offset = cur.QuadPart;
                            e.size = sSize; e.decompressedSize = dSize; e.isLooseFile = false;
                            g_FileIndex[norm] = e;
                        }
                        LARGE_INTEGER skip; skip.QuadPart = sSize;
                        g_RawSetFilePointerEx(hArchive, skip, NULL, FILE_CURRENT);
                    }
                }
                g_ArchiveHandle = hArchive.release();
            }
        }

        g_IsActive = !g_FileIndex.empty();
        if (g_IsActive) {
            RebuildDirectoryIndex();
            Utils::Log("[VFS] Initialized in %s mode with %zu files (%zu directories)", 
                (Config::VFSMode == 0 ? "Modern" : "Legacy"), g_FileIndex.size(), g_DirectoryIndex.size());
        }
        return g_IsActive;
    }

    void Shutdown() {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        g_HandleMap.clear(); // std::unique_ptr will handle deletion
        g_FindMap.clear();

        for (auto& p : g_MixedHandleMap) {
            if (g_RawCloseHandle) g_RawCloseHandle(p.first);
            DeleteFileW(p.second.c_str());
        }
        g_MixedHandleMap.clear();
        g_FileIndex.clear();
        g_DirectoryIndex.clear();
        if (g_ArchiveHandle != INVALID_HANDLE_VALUE) {
            if (g_RawCloseHandle) g_RawCloseHandle(g_ArchiveHandle);
            g_ArchiveHandle = INVALID_HANDLE_VALUE;
        }
        g_IsActive = false;
    }

    bool IsActive() { return g_IsActive; }

    void SetOriginalFunctions(void* r, void* s, void* c) {
        g_OrigReadFile = (pReadFile)r; g_OrigSetFilePointerEx = (pSetFilePointerEx)s; g_OrigCloseHandle = (pCloseHandle)c;
    }

    void SetFindFunctions(void* f1, void* f2, void* f3, void* f4, void* f5) {
        g_OrigFindFirstFileW = (pFindFirstFileW)f1; g_OrigFindNextFileW = (pFindNextFileW)f2; g_OrigFindClose = (pFindClose)f3;
        g_OrigFindFirstFileA = (pFindFirstFileA)f4; g_OrigFindNextFileA = (pFindNextFileA)f5;
    }

    bool HasVirtualFile(const wchar_t* p) {
        if (!g_IsActive || !p) return false;
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        return g_FileIndex.find(NormalizePath(p)) != g_FileIndex.end();
    }

    bool HasVirtualFileA(const char* p) {
        if (!g_IsActive || !p) return false;
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        return g_FileIndex.find(NormalizePathA(p)) != g_FileIndex.end();
    }

    HANDLE OpenVirtualFile(const wchar_t* relativePath) {
        if (!g_IsActive || !relativePath) return INVALID_HANDLE_VALUE;
        std::wstring norm = NormalizePath(relativePath);
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FileIndex.find(norm);
        if (it == g_FileIndex.end()) return INVALID_HANDLE_VALUE;

        // Legacy special handling for certain extensions (returns real handle directly)
        if (Config::VFSMode != 0) {
            const wchar_t* ext = PathFindExtensionW(relativePath);
            if (ext && (_wcsicmp(ext, L".dll") == 0 || _wcsicmp(ext, L".exe") == 0 || _wcsicmp(ext, L".asi") == 0)) {
                if (it->second.isLooseFile) return g_RawCreateFileW(it->second.looseFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            }
        }

        // Modern mode loose file optimization
        if (Config::VFSMode == 0 && it->second.isLooseFile) {
            return g_RawCreateFileW(it->second.looseFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        }

        // Modern mode cache extraction
        if (Config::VFSMode == 0 && !it->second.isLooseFile) {
            wchar_t cName[MAX_PATH]; swprintf_s(cName, L"vfs_%u.tmp", (UINT)it->second.offset);
            wchar_t cPath[MAX_PATH]; wcscpy_s(cPath, g_HybridCacheDir); PathAppendW(cPath, cName);
            if (!PathFileExistsW(cPath)) {
                if (!ExtractFile(relativePath, cPath)) return INVALID_HANDLE_VALUE;
            }
            HANDLE hReal = g_RawCreateFileW(cPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hReal != INVALID_HANDLE_VALUE) { g_MixedHandleMap[hReal] = cPath; return hReal; }
        }

        // Fallback or Legacy mode emulated handle
        auto vfh = std::make_unique<VirtualFileHandle>();
        vfh->entry = &it->second; 
        vfh->position = 0; 
        vfh->isLooseFile = it->second.isLooseFile;
        vfh->archiveHandle = g_ArchiveHandle;
        vfh->looseFileHandle = INVALID_HANDLE_VALUE;

        if (vfh->isLooseFile) {
            vfh->looseFileHandle = g_RawCreateFileW(it->second.looseFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        } else if (it->second.size < it->second.decompressedSize) {
            // Memory decompression for Legacy or fallback
            std::vector<BYTE> comp(it->second.size); 
            LARGE_INTEGER s; s.QuadPart = it->second.offset;
            if (g_RawSetFilePointerEx(g_ArchiveHandle, s, NULL, FILE_BEGIN)) {
                DWORD br; 
                if (g_RawReadFile(g_ArchiveHandle, comp.data(), it->second.size, &br, NULL)) {
                    vfh->decompressedBuffer.resize(it->second.decompressedSize);
                    if (!DecompressData(comp, it->second.decompressedSize, vfh->decompressedBuffer.data())) {
                        vfh->decompressedBuffer.clear();
                    }
                }
            }
        }

        HANDLE hFake = (HANDLE)(++g_VirtualHandleCounter);
        g_HandleMap[hFake] = std::move(vfh);
        return hFake;
    }

    HANDLE OpenVirtualFileA(const char* p) {
        wchar_t w[MAX_PATH]; MultiByteToWideChar(Config::LE_Codepage, 0, p, -1, w, MAX_PATH);
        return OpenVirtualFile(w);
    }

    bool IsVirtualHandle(HANDLE h) {
        if (!g_IsActive) return false;
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        return g_HandleMap.find(h) != g_HandleMap.end();
    }

    BOOL ReadVirtualFile(HANDLE h, LPVOID b, DWORD n, LPDWORD r, LPOVERLAPPED o) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(h); if (it == g_HandleMap.end()) return FALSE;
        VirtualFileHandle* vfh = it->second.get();
        LONGLONG rem = vfh->entry->decompressedSize - vfh->position;
        if (rem <= 0) { if (r) *r = 0; return TRUE; }
        DWORD toRead = (DWORD)min((LONGLONG)n, rem); DWORD br = 0;

        if (!vfh->decompressedBuffer.empty()) {
            memcpy(b, vfh->decompressedBuffer.data() + vfh->position, toRead); br = toRead;
        } else {
            HANDLE hSrc = vfh->isLooseFile ? vfh->looseFileHandle : vfh->archiveHandle;
            LARGE_INTEGER s; s.QuadPart = (vfh->isLooseFile ? 0 : vfh->entry->offset) + vfh->position;
            g_RawSetFilePointerEx(hSrc, s, NULL, FILE_BEGIN);
            g_RawReadFile(hSrc, b, toRead, &br, NULL);
        }
        vfh->position += br; if (r) *r = br;
        return TRUE;
    }

    DWORD SetVirtualFilePointer(HANDLE h, LONG d, PLONG dh, DWORD m) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(h); if (it == g_HandleMap.end()) return INVALID_SET_FILE_POINTER;
        VirtualFileHandle* vfh = it->second.get();
        LONGLONG dist = d; if (dh) dist |= ((LONGLONG)*dh) << 32;
        LONGLONG nPos = 0;
        if (m == FILE_BEGIN) nPos = dist;
        else if (m == FILE_CURRENT) nPos = vfh->position + dist;
        else if (m == FILE_END) nPos = vfh->entry->decompressedSize + dist;
        if (nPos < 0) nPos = 0; if (nPos > (LONGLONG)vfh->entry->decompressedSize) nPos = vfh->entry->decompressedSize;
        vfh->position = nPos;
        if (dh) *dh = (LONG)(nPos >> 32);
        return (DWORD)(nPos & 0xFFFFFFFF);
    }

    BOOL SetVirtualFilePointerEx(HANDLE h, LARGE_INTEGER d, PLARGE_INTEGER np, DWORD m) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(h); if (it == g_HandleMap.end()) return FALSE;
        VirtualFileHandle* vfh = it->second.get();
        LONGLONG nPos = 0;
        if (m == FILE_BEGIN) nPos = d.QuadPart;
        else if (m == FILE_CURRENT) nPos = vfh->position + d.QuadPart;
        else if (m == FILE_END) nPos = vfh->entry->decompressedSize + d.QuadPart;
        if (nPos < 0) nPos = 0; if (nPos > (LONGLONG)vfh->entry->decompressedSize) nPos = vfh->entry->decompressedSize;
        vfh->position = nPos; if (np) np->QuadPart = nPos;
        return TRUE;
    }

    DWORD GetVirtualFileSize(HANDLE h, LPDWORD hs) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(h); if (it == g_HandleMap.end()) return INVALID_FILE_SIZE;
        if (hs) *hs = (DWORD)((ULONGLONG)it->second->entry->decompressedSize >> 32);
        return (DWORD)(it->second->entry->decompressedSize & 0xFFFFFFFF);
    }

    BOOL GetVirtualFileSizeEx(HANDLE h, PLARGE_INTEGER s) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(h); if (it == g_HandleMap.end()) return FALSE;
        if (s) s->QuadPart = it->second->entry->decompressedSize;
        return TRUE;
    }

    BOOL CloseVirtualHandle(HANDLE h) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it_m = g_MixedHandleMap.find(h);
        if (it_m != g_MixedHandleMap.end()) { 
            if (g_RawCloseHandle) g_RawCloseHandle(h); 
            g_MixedHandleMap.erase(it_m); 
            return TRUE; 
        }
        auto it = g_HandleMap.find(h); if (it == g_HandleMap.end()) return FALSE;
        VirtualFileHandle* vfh = it->second.get();
        if (vfh->isLooseFile && vfh->looseFileHandle != INVALID_HANDLE_VALUE) {
            if (g_RawCloseHandle) g_RawCloseHandle(vfh->looseFileHandle);
        }
        // std::unique_ptr will handle memory deletion and vector cleanup
        g_HandleMap.erase(it);
        return TRUE;
    }

    BOOL GetVirtualFileInformationByHandle(HANDLE h, LPBY_HANDLE_FILE_INFORMATION i) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_HandleMap.find(h); if (it == g_HandleMap.end()) return FALSE;
        ZeroMemory(i, sizeof(BY_HANDLE_FILE_INFORMATION));
        i->dwFileAttributes = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY;
        i->nFileSizeLow = (DWORD)(it->second->entry->decompressedSize & 0xFFFFFFFF);
        i->nFileSizeHigh = (DWORD)((ULONGLONG)it->second->entry->decompressedSize >> 32);
        i->nNumberOfLinks = 1;
        return TRUE;
    }

    DWORD GetVirtualFileType(HANDLE h) { return IsVirtualHandle(h) ? FILE_TYPE_DISK : FILE_TYPE_UNKNOWN; }

    HANDLE VirtualFindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData) {
        if (!g_IsActive) return g_OrigFindFirstFileW ? g_OrigFindFirstFileW(lpFileName, lpFindFileData) : INVALID_HANDLE_VALUE;
        
        wchar_t fPath[MAX_PATH]; 
        if (!GetFullPathNameW(lpFileName, MAX_PATH, fPath, NULL)) return g_OrigFindFirstFileW(lpFileName, lpFindFileData);
        
        std::wstring fstr = fPath; 
        size_t last = fstr.find_last_of(L"\\/");
        if (last == std::wstring::npos) return g_OrigFindFirstFileW(lpFileName, lpFindFileData);
        
        std::wstring sDir = NormalizePath(fstr.substr(0, last).c_str());
        std::wstring pattern = fstr.substr(last + 1);
        std::transform(pattern.begin(), pattern.end(), pattern.begin(), ::towlower);

        auto state = std::make_unique<VirtualFindState>();
        state->realHandle = g_OrigFindFirstFileW ? g_OrigFindFirstFileW(lpFileName, lpFindFileData) : INVALID_HANDLE_VALUE;
        state->usingRealHandle = (state->realHandle != INVALID_HANDLE_VALUE);
        state->matchIndex = 0;
        if (state->usingRealHandle) state->seenFiles.push_back(lpFindFileData->cFileName);

        // Get game root directory to match relative paths
        wchar_t gameRoot[MAX_PATH]; 
        GetModuleFileNameW(NULL, gameRoot, MAX_PATH); 
        PathRemoveFileSpecW(gameRoot);
        std::wstring gameRootDir = NormalizePath(gameRoot);

        // Find the relative directory within the game root
        std::wstring relDir;
        if (sDir.length() >= gameRootDir.length() && _wcsnicmp(sDir.c_str(), gameRootDir.c_str(), gameRootDir.length()) == 0) {
            relDir = sDir.substr(gameRootDir.length());
            while (!relDir.empty() && relDir[0] == L'\\') relDir.erase(0, 1);
        } else {
            relDir = sDir; // Fallback
        }

        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto itDir = g_DirectoryIndex.find(relDir);
        if (itDir != g_DirectoryIndex.end()) {
            for (VFS::VirtualFileEntry* entry : itDir->second) {
                const wchar_t* fileName = PathFindFileNameW(entry->relativePath.c_str());
                if (PathMatchSpecW(fileName, pattern.c_str())) {
                    state->matches.push_back(entry);
                }
            }
        }

        if (state->matches.empty() && !state->usingRealHandle) { return INVALID_HANDLE_VALUE; }
        if (!state->usingRealHandle) {
            VFS::VirtualFileEntry* m = state->matches[0];
            wcscpy_s(lpFindFileData->cFileName, PathFindFileNameW(m->relativePath.c_str()));
            lpFindFileData->nFileSizeLow = m->decompressedSize;
            lpFindFileData->dwFileAttributes = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY;
            state->matchIndex++;
        }
        HANDLE hFake = (HANDLE)(++g_VirtualHandleCounter);
        g_FindMap[hFake] = std::move(state);
        return hFake;
    }

    BOOL VirtualFindNextFileW(HANDLE h, LPWIN32_FIND_DATAW fd) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FindMap.find(h); if (it == g_FindMap.end()) return g_OrigFindNextFileW ? g_OrigFindNextFileW(h, fd) : FALSE;
        VirtualFindState* s = it->second.get();
        if (s->usingRealHandle && g_OrigFindNextFileW(s->realHandle, fd)) { s->seenFiles.push_back(fd->cFileName); return TRUE; }
        s->usingRealHandle = false;
        while (s->matchIndex < s->matches.size()) {
            VFS::VirtualFileEntry* m = s->matches[s->matchIndex++];
            const wchar_t* name = PathFindFileNameW(m->relativePath.c_str());
            bool seen = false; for (auto& f : s->seenFiles) if (_wcsicmp(f.c_str(), name) == 0) { seen = true; break; }
            if (seen) continue;
            wcscpy_s(fd->cFileName, name);
            fd->nFileSizeLow = m->decompressedSize;
            fd->dwFileAttributes = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY;
            return TRUE;
        }
        SetLastError(ERROR_NO_MORE_FILES); return FALSE;
    }

    BOOL VirtualFindClose(HANDLE h) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FindMap.find(h); if (it == g_FindMap.end()) return g_OrigFindClose ? g_OrigFindClose(h) : FALSE;
        if (it->second->realHandle != INVALID_HANDLE_VALUE && g_OrigFindClose) g_OrigFindClose(it->second->realHandle);
        g_FindMap.erase(it); return TRUE;
    }

    HANDLE VirtualFindFirstFileA(LPCSTR n, LPWIN32_FIND_DATAA fd) {
        wchar_t w[MAX_PATH]; MultiByteToWideChar(Config::LE_Codepage, 0, n, -1, w, MAX_PATH);
        WIN32_FIND_DATAW fw; HANDLE h = VirtualFindFirstFileW(w, &fw);
        if (h != INVALID_HANDLE_VALUE) {
            WideCharToMultiByte(Config::LE_Codepage, 0, fw.cFileName, -1, fd->cFileName, MAX_PATH, NULL, NULL);
            fd->dwFileAttributes = fw.dwFileAttributes; fd->nFileSizeLow = fw.nFileSizeLow;
            fd->ftLastWriteTime = fw.ftLastWriteTime;
        }
        return h;
    }

    BOOL VirtualFindNextFileA(HANDLE h, LPWIN32_FIND_DATAA fd) {
        WIN32_FIND_DATAW fw; if (VirtualFindNextFileW(h, &fw)) {
            WideCharToMultiByte(Config::LE_Codepage, 0, fw.cFileName, -1, fd->cFileName, MAX_PATH, NULL, NULL);
            fd->dwFileAttributes = fw.dwFileAttributes; fd->nFileSizeLow = fw.nFileSizeLow;
            return TRUE;
        }
        return FALSE;
    }

    bool ExtractFile(const wchar_t* relativePath, const wchar_t* destPath) {
        if (!g_IsActive || !relativePath || !destPath) return false;
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_FileIndex.find(NormalizePath(relativePath)); if (it == g_FileIndex.end()) return false;
        if (it->second.isLooseFile) return CopyFileW(it->second.looseFilePath.c_str(), destPath, FALSE);
        if (g_ArchiveHandle == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER s; s.QuadPart = it->second.offset; g_RawSetFilePointerEx(g_ArchiveHandle, s, NULL, FILE_BEGIN);
        std::vector<BYTE> buf(it->second.size); DWORD br; g_RawReadFile(g_ArchiveHandle, buf.data(), it->second.size, &br, NULL);
        
        ScopedRawHandle hDest(g_RawCreateFileW(destPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
        if (hDest == INVALID_HANDLE_VALUE) return false;

        const BYTE* data = buf.data(); 
        DWORD size = it->second.size; 
        std::vector<BYTE> dec;
        if (size < it->second.decompressedSize) {
            dec.resize(it->second.decompressedSize);
            if (DecompressData(buf, it->second.decompressedSize, dec.data())) { 
                data = dec.data(); 
                size = it->second.decompressedSize; 
            }
        }
        DWORD bw; 
        WriteFile(hDest, data, size, &bw, NULL); 
        return bw == size;
    }

    void GetVirtualFileList(std::vector<std::wstring>& list) {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        for (const auto& kv : g_FileIndex) list.push_back(kv.second.relativePath);
    }
}

#include "../pch.h"
#include "rioshiina_hook.h"
#include "utils.h"
#include "config.h"
#include "../detours.h"
#include "vfs.h"

#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <psapi.h>

namespace fs = std::filesystem;


//
// Encodings & Utils
//
static std::wstring SJIS_to_Wide(const char* sjis) {
    if (!sjis) return L"";
    int len = MultiByteToWideChar(932, 0, sjis, -1, NULL, 0);
    if (len <= 0) return L"";
    std::wstring wstr(len, 0);
    MultiByteToWideChar(932, 0, sjis, -1, &wstr[0], len);
    if (!wstr.empty() && wstr.back() == 0) wstr.pop_back();
    return wstr;
}

static std::string Wide_to_SJIS(const std::wstring& wide) {
    if (wide.empty()) return "";
    int len = WideCharToMultiByte(932, 0, wide.c_str(), -1, NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    std::string str(len, 0);
    WideCharToMultiByte(932, 0, wide.c_str(), -1, &str[0], len, NULL, NULL);
    if (!str.empty() && str.back() == 0) str.pop_back();
    return str;
}


//
// API Function Pointers
//
typedef void* (__cdecl* pRioGetRes)(const char* lpFileName, uint32_t* pBytesRead);
static pRioGetRes orgRioGetRes = nullptr;

typedef void* (__cdecl* pRioReadFile)(const char* currentWorkDir, const char* logicFileName, uint32_t* pBytesRead);
static pRioReadFile orgRioReadFile = nullptr;

typedef HANDLE(__cdecl* pFindAndOpenFileInWarc)(const char* warcName, char* pureFileName, DWORD dwDesiredAccess, void* a4, void* a5, void* a6, char** pPureFileName);
static pFindAndOpenFileInWarc orgFindAndOpenFileInWarc = nullptr;

typedef LSTATUS(WINAPI* pRegOpenKeyExA)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
static pRegOpenKeyExA orgRegOpenKeyExA = RegOpenKeyExA;

typedef HANDLE(WINAPI* pCreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static pCreateFileA orgRSCreateFileA = CreateFileA;

typedef DWORD(WINAPI* pSetFilePointer)(HANDLE, LONG, PLONG, DWORD);
static pSetFilePointer orgRSSetFilePointer = SetFilePointer;

typedef DWORD(WINAPI* pGetFileSize)(HANDLE, LPDWORD);
static pGetFileSize orgRSGetFileSize = GetFileSize;

typedef int(__stdcall* pEntryPoint)();
static pEntryPoint orgEntryPoint = nullptr;

static void* oepJumpDest = nullptr;


//
// Signatures & State
//
static bool g_RioShiinaHooked = false;
static HANDLE hSpecDvdFile = nullptr;
static std::unordered_map<std::wstring, std::unordered_set<std::string>> fileNamesInArchives;
static std::unordered_set<std::wstring> archivesToExtract;
static std::wstring currentArchivePath;
static std::optional<int> entrySizeOpt;

constexpr std::string_view rioGetResSignNew = "81 EC 04 01 00 00 53 55 56";
constexpr std::string_view rioReadFileSignNew = "81 EC 04 01 00 00 53 55 8B AC 24 10 01 00 00 8A 45 00";
constexpr std::string_view rioGetResSignOld = "55 8B EC 83 EC 14 53 56 57 89 4D F4";
constexpr std::string_view rioReadFileSignOld = "55 8B EC 83 EC 0C 53 56 8B 75 08";
constexpr std::string_view oepJumpSign = "74 01 C3 E9";
constexpr std::string_view pushWarcStrSign = "6A 06 8D ?? ?? 68 ?? ?? ?? ?? 5? E8";
constexpr std::string_view getDvdDrivePathSign = "C6 00 00 33 C0 5B C3";

// ============================================================================
// Mode 2: Unpacking Logic
// ============================================================================
static void ExtractWarc(const std::wstring& warcPath) {
    if (fileNamesInArchives.find(warcPath) == fileNamesInArchives.end()) return;
    
    const auto& fileList = fileNamesInArchives[warcPath];
    fs::path outputDir = fs::path(warcPath).parent_path() / (fs::path(warcPath).stem().wstring() + L"__UNPACKED");
    fs::create_directories(outputDir);

    Utils::Log("[RioShiina] Extracting %zu files from %ls...", fileList.size(), warcPath.c_str());

    for (const auto& fileNameSJIS : fileList) {
        // Skip invalid filenames if configured
        if (Config::RioShiinaSkipInvalidFileName) {
            bool invalid = false;
            for (char c : fileNameSJIS) { if (c < 0x20 || c > 0x7E) { invalid = true; break; } }
            if (invalid) continue;
        }

        uint32_t bytesRead = 0;
        void* pFileData = orgRioGetRes(fileNameSJIS.c_str(), &bytesRead);
        if (pFileData) {
            std::wstring outName = SJIS_to_Wide(fileNameSJIS.c_str());
            fs::path outPath = outputDir / outName;
            std::ofstream ofs(outPath, std::ios::binary);
            if (ofs) {
                ofs.write((char*)pFileData, bytesRead);
                ofs.close();
            }
            GlobalFree(pFileData);
        }
    }
    Utils::Log("[RioShiina] Extraction finished for %ls", warcPath.c_str());
}

static void* __cdecl MyRioGetResMode2(const char* lpFileName, uint32_t* pBytesRead) {
    void* ret = orgRioGetRes(lpFileName, pBytesRead);
    
    static bool unpackTriggered = false;
    if (!unpackTriggered && lpFileName) {
        std::string name = lpFileName;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name.find("efclib.scn") != std::string::npos || name.find("start.scn") != std::string::npos) {
            unpackTriggered = true;
            Utils::Log("[RioShiina] Engine initialized. Starting auto-unpack...");
            for (const auto& archive : archivesToExtract) {
                ExtractWarc(archive);
            }
            Utils::Log("[RioShiina] Auto-unpack completed.");
        }
    }
    return ret;
}

static HANDLE __cdecl MyFindAndOpenFileInWarc(const char* warcName, char* pureFileName, DWORD dwDesiredAccess, void* a4, void* a5, void* a6, char** pPureFileName) {
    // Scan archives to build index
    for (const auto& archive : archivesToExtract) {
        currentArchivePath = archive;
        std::string sjisPath = Wide_to_SJIS(archive);
        HANDLE h = orgFindAndOpenFileInWarc(sjisPath.c_str(), pureFileName, dwDesiredAccess, nullptr, nullptr, nullptr, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    return orgFindAndOpenFileInWarc(warcName, pureFileName, dwDesiredAccess, a4, a5, a6, pPureFileName);
}

static void MyAfterDecompressCallInner(uint8_t* entryAddr) {
    if (currentArchivePath.empty() || !entryAddr) return;
    auto& fileList = fileNamesInArchives[currentArchivePath];
    if (!fileList.empty()) return;

    if (!entrySizeOpt.has_value()) {
        std::string firstFile = (char*)entryAddr;
        entrySizeOpt = (firstFile.length() + 8 >= 0x20) ? 0x38 : 0x28;
    }

    while (*entryAddr != 0) {
        fileList.insert(std::string((char*)entryAddr));
        entryAddr += entrySizeOpt.value();
    }
    Utils::Log("[RioShiina] Successfully indexed archive: %ls", currentArchivePath.c_str());
}

#ifdef _M_IX86
void __declspec(naked) MyAfterDecompressCall() {
    __asm {
        pushad
        pushfd
        mov eax, [esp + 0x24] // Get first param from stack
        push eax
        call MyAfterDecompressCallInner
        add esp, 4
        popfd
        popad
        ret
    }
}
#else
void MyAfterDecompressCall() {
    // TODO: Implement x64 version if needed
}
#endif


//
// Registry & Virtual DVD
//
LSTATUS WINAPI MyRegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult) {
    return RegOpenKeyExW(hKey, SJIS_to_Wide(lpSubKey).c_str(), ulOptions, samDesired, phkResult);
}

DWORD WINAPI MyRSGetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh) {
    if (hFile == hSpecDvdFile && hFile != nullptr) return (DWORD)Config::RioShiinaSpecDvdFileSize;
    return orgRSGetFileSize(hFile, lpFileSizeHigh);
}

DWORD WINAPI MyRSSetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) {
    if (hFile == hSpecDvdFile && hFile != nullptr) return (DWORD)lDistanceToMove;
    return orgRSSetFilePointer(hFile, lDistanceToMove, lpDistanceToMoveHigh, dwMoveMethod);
}

HANDLE WINAPI MyRSCreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    if (lpFileName && std::string_view(lpFileName).starts_with("W:\\")) {
        if (!hSpecDvdFile) hSpecDvdFile = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        return hSpecDvdFile;
    }
    return orgRSCreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}


//
// Resource Redirection
//
static void* redirectFile(const char* lpFileName, uint32_t* pBytesRead) {
    if (!lpFileName) return nullptr;
    std::wstring fileName = SJIS_to_Wide(lpFileName);
    size_t lastSlash = fileName.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) fileName = fileName.substr(lastSlash + 1);

    static std::wstring exeDir = []() {
        wchar_t path[MAX_PATH]; GetModuleFileNameW(NULL, path, MAX_PATH);
        std::wstring ws(path); return ws.substr(0, ws.find_last_of(L"\\/"));
    }();

    fs::path targetPath = fs::path(exeDir) / L"unencrypted" / fileName;
    if (!fs::exists(targetPath)) targetPath = fs::path(exeDir) / Config::RedirectFolderW / fileName;

    if (fs::exists(targetPath)) {
        std::ifstream ifs(targetPath, std::ios::binary);
        if (ifs) {
            auto fileSize = (SIZE_T)fs::file_size(targetPath);
            void* pBuffer = GlobalAlloc(GMEM_FIXED, fileSize);
            if (pBuffer) {
                ifs.read((char*)pBuffer, fileSize);
                if (pBytesRead) *pBytesRead = (uint32_t)ifs.gcount();
                Utils::Log("[RioShiina] Redirected: %s -> %ls", lpFileName, targetPath.c_str());
                return pBuffer;
            }
        }
    }
    return nullptr;
}

void* __cdecl MyRioGetResMode1(const char* lpFileName, uint32_t* pBytesRead) {
    if (void* pFileData = redirectFile(lpFileName, pBytesRead)) return pFileData;
    return orgRioGetRes(lpFileName, pBytesRead);
}

void* __cdecl MyRioReadFile(const char* currentWorkDir, const char* logicFileName, uint32_t* pBytesRead) {
    if (void* pFileData = redirectFile(logicFileName, pBytesRead)) return pFileData;
    return orgRioReadFile(currentWorkDir, logicFileName, pBytesRead);
}


//
// Hook Management
//
namespace Hooks {
    void EnsureRioShiinaHooked() {
        if (!Config::EnableRioShiinaHook || g_RioShiinaHooked) return;
        HMODULE hExe = GetModuleHandleW(NULL);
        
        void* rioGetResAddr = Utils::FindPattern(hExe, rioGetResSignNew.data());
        if (!rioGetResAddr) rioGetResAddr = Utils::FindPattern(hExe, rioGetResSignOld.data());
        
        void* rioReadFileAddr = Utils::FindPattern(hExe, rioReadFileSignNew.data());
        if (!rioReadFileAddr) rioReadFileAddr = Utils::FindPattern(hExe, rioReadFileSignOld.data());

        if (!rioGetResAddr && !rioReadFileAddr) return;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (rioReadFileAddr) {
            orgRioReadFile = (pRioReadFile)rioReadFileAddr;
            DetourAttach(&(PVOID&)orgRioReadFile, MyRioReadFile);
        }

        if (rioGetResAddr) {
            orgRioGetRes = (pRioGetRes)rioGetResAddr;
            if (Config::RioShiinaMode == 2) {
                DetourAttach(&(PVOID&)orgRioGetRes, MyRioGetResMode2);
                
                // Mode 2 extra logic
                void* pushWarcAddr = Utils::FindPattern(hExe, pushWarcStrSign.data());
                if (pushWarcAddr) {
                    // Search back for function start
                    uint8_t* funcStart = nullptr;
                    for (int i = 0; i < 0x300; i++) {
                        if (*(uint32_t*)((uint8_t*)pushWarcAddr - i) == 0xEC8B5590) {
                            funcStart = (uint8_t*)pushWarcAddr - i + 1; break;
                        }
                    }
                    if (funcStart) {
                        orgFindAndOpenFileInWarc = (pFindAndOpenFileInWarc)funcStart;
                        DetourAttach(&(PVOID&)orgFindAndOpenFileInWarc, MyFindAndOpenFileInWarc);
                        
                        // Hook AfterDecompressCall
                        void* pushParam = Utils::FindPatternInBlock(pushWarcAddr, 0x200, "51 68 ?? ?? ?? ?? 68 ?? ?? ?? ?? 68");
                        if (pushParam) {
                            void* callAddr = Utils::FindPatternInBlock((uint8_t*)pushParam + 0x10, 0x10, "E8 ?? ?? ?? ?? 83 C4 14");
                            if (callAddr) {
                                void* target = (uint8_t*)callAddr + 5;
#ifdef _M_IX86
                                DetourAttach(&(PVOID&)target, MyAfterDecompressCall);
#endif
                            }
                        }
                    }
                }
            } else {
                DetourAttach(&(PVOID&)orgRioGetRes, MyRioGetResMode1);
            }
        }
        DetourTransactionCommit();
        g_RioShiinaHooked = true;
        Utils::Log("[RioShiina] Engine hooks installed (Mode %d).", Config::RioShiinaMode);
    }

    int __stdcall MyEntryPointHook() {
        EnsureRioShiinaHooked();
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&)orgEntryPoint, MyEntryPointHook);
        DetourTransactionCommit();
        return orgEntryPoint();
    }

    void InstallRioShiinaHook() {
        if (!Config::EnableRioShiinaHook) return;

        // Parse ArchivesToExtract
        std::wstring archives = Config::RioShiinaArchivesToExtract;
        size_t start = 0, end;
        while ((end = archives.find(L'|', start)) != std::wstring::npos) {
            archivesToExtract.insert(archives.substr(start, end - start));
            start = end + 1;
        }
        if (start < archives.length()) archivesToExtract.insert(archives.substr(start));

        HMODULE hExe = GetModuleHandleW(NULL);
        MODULEINFO modInfo; GetModuleInformation(GetCurrentProcess(), hExe, &modInfo, sizeof(modInfo));
        orgEntryPoint = (pEntryPoint)modInfo.EntryPoint;

        // Install base hooks
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)orgRegOpenKeyExA, MyRegOpenKeyExA);
        if (Config::EnableRioShiinaProcessDvd && Config::RioShiinaSpecDvdFileSize > 0) {
            DetourAttach(&(PVOID&)orgRSCreateFileA, MyRSCreateFileA);
            DetourAttach(&(PVOID&)orgRSSetFilePointer, MyRSSetFilePointer);
            DetourAttach(&(PVOID&)orgRSGetFileSize, MyRSGetFileSize);
        }
        if (orgEntryPoint) DetourAttach(&(PVOID&)orgEntryPoint, MyEntryPointHook);
        DetourTransactionCommit();
    }
}

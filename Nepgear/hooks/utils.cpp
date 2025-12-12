#include "../pch.h"
#include "utils.h"
#include "config.h"
#include "vfs.h"
#include <Shlwapi.h>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdarg.h>
#include "archive.h"

#pragma comment(lib, "Shlwapi.lib")

static std::vector<std::wstring> g_deployedLeFiles;

static void DebugPrintW(const wchar_t* format, ...) {
    wchar_t wbuf[2048];
    va_list args;
    va_start(args, format);
    vswprintf_s(wbuf, _countof(wbuf), format, args);
    va_end(args);

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
    if (utf8Len > 0) {
        std::vector<char> utf8Buf(utf8Len);
        WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, utf8Buf.data(), utf8Len, NULL, NULL);
        printf("%s", utf8Buf.data());
    }
}

namespace Utils {
    BOOL LoadCustomFont(HMODULE hModule) {
        WCHAR rootDir[MAX_PATH];
        GetModuleFileNameW(hModule, rootDir, MAX_PATH);
        PathRemoveFileSpecW(rootDir);

        WCHAR fontPath[MAX_PATH] = { 0 };

        if (Config::EnableFileHook) {
            const wchar_t* tempRoot = Archive::GetTempRootW();
            if (tempRoot) {
                swprintf_s(fontPath, MAX_PATH, L"%s\\%s", tempRoot, Config::FontFileName);
                if (!PathFileExistsW(fontPath)) {
                    fontPath[0] = 0;
                }
            }
            if (fontPath[0] == 0) {
                swprintf_s(fontPath, MAX_PATH, L"%s\\%s\\%s", rootDir, Config::RedirectFolderW, Config::FontFileName);
                if (!PathFileExistsW(fontPath)) {
                    fontPath[0] = 0;
                }
            }
        }

        if (fontPath[0] == 0) {
            swprintf_s(fontPath, MAX_PATH, L"%s\\%s", rootDir, Config::FontFileName);
        }

        if (!PathFileExistsW(fontPath)) {
            if (Config::EnableDebug) {
                DebugPrintW(L"[Font] Font file not found: %s\n", fontPath);
            }
            return FALSE;
        }

        if (Config::EnableDebug) {
            DebugPrintW(L"[Font] Loading font from: %s\n", fontPath);
        }

        int fontsAdded = AddFontResourceExW(fontPath, FR_PRIVATE, NULL);
        if (fontsAdded > 0) {
            if (Config::EnableDebug) {
                DebugPrintW(L"[Font] AddFontResourceExW succeeded, %d fonts added\n", fontsAdded);
            }
        }

        HANDLE hFile = CreateFileW(fontPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            if (Config::EnableDebug) {
                DebugPrintW(L"[Font] Failed to open font file\n");
            }
            return fontsAdded > 0;
        }

        DWORD fileSize = GetFileSize(hFile, NULL);
        if (fileSize == INVALID_FILE_SIZE) {
            CloseHandle(hFile);
            return fontsAdded > 0;
        }

        BYTE* fontData = (BYTE*)HeapAlloc(GetProcessHeap(), 0, fileSize);
        if (!fontData) {
            CloseHandle(hFile);
            return fontsAdded > 0;
        }

        DWORD bytesRead;
        if (!ReadFile(hFile, fontData, fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
            HeapFree(GetProcessHeap(), 0, fontData);
            CloseHandle(hFile);
            return fontsAdded > 0;
        }
        CloseHandle(hFile);

        DWORD numFonts = 0;
        HANDLE hFont = AddFontMemResourceEx(fontData, fileSize, NULL, &numFonts);
        HeapFree(GetProcessHeap(), 0, fontData);

        BOOL fontLoaded = (hFont != NULL || fontsAdded > 0);

        if (fontLoaded) {
            HFONT testFont = CreateFontW(
                -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                Config::ForcedCharset, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                Config::ForcedFontNameW
            );

            if (testFont) {
                HDC hdc = GetDC(NULL);
                if (hdc) {
                    HFONT oldFont = (HFONT)SelectObject(hdc, testFont);
                    WCHAR actualFaceName[LF_FACESIZE] = { 0 };
                    GetTextFaceW(hdc, LF_FACESIZE, actualFaceName);
                    SelectObject(hdc, oldFont);
                    ReleaseDC(NULL, hdc);

                    if (_wcsicmp(actualFaceName, Config::ForcedFontNameW) != 0) {
                        if (Config::EnableDebug) {
                            DebugPrintW(L"[Font] WARNING: Requested '%s' but got '%s'. Font name mismatch!\n",
                                Config::ForcedFontNameW, actualFaceName);
                        }
                        fontLoaded = FALSE;
                    } else {
                        if (Config::EnableDebug) {
                            DebugPrintW(L"[Font] Verified: Font '%s' is available\n", actualFaceName);
                        }
                    }
                }
                DeleteObject(testFont);
            } else {
                if (Config::EnableDebug) {
                    DebugPrintW(L"[Font] Failed to create test font\n");
                }
                fontLoaded = FALSE;
            }
        }

        if (Config::EnableDebug) {
            if (hFont) {
                DebugPrintW(L"[Font] AddFontMemResourceEx succeeded, %lu fonts in memory\n", numFonts);
            } else {
                DebugPrintW(L"[Font] AddFontMemResourceEx failed\n");
            }
            DebugPrintW(L"[Font] Target FaceName: %S\n", Config::ForcedFontNameA);
            DebugPrintW(L"[Font] Final status: %s\n", fontLoaded ? L"ENABLED" : L"DISABLED");
        }

        return fontLoaded;
    }

    void ShowStartupPopup() {
        if (!Config::IsSystemEnabled) return;
        if (!Config::AuthorInfo::ShowPopup) return;

        std::wstring msg;
        msg += L"【补丁作者】\n";
        for (int i = 0; i < Config::AuthorInfo::AUTHOR_IDS_COUNT; ++i) {
            msg += L" - ";
            msg += Config::AuthorInfo::AUTHOR_IDS[i];
            msg += L"\n";
        }
        msg += L"\n【补丁声明】\n";
        msg += Config::AuthorInfo::ADDITIONAL_NOTES;

        int result = MessageBoxW(NULL, msg.c_str(), L"游玩通知", MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST);
        if (result == IDNO) {
            CleanupLeFiles();
            ExitProcess(0);
        }
    }

    void Log(const char* format, ...) {
        char buffer[2048];
        va_list args;
        va_start(args, format);
        vsnprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
        va_end(args);

        OutputDebugStringA("[Hook] ");
        OutputDebugStringA(buffer);
        OutputDebugStringA("\n");

        if (Config::EnableDebug) {
            int targetLen = MultiByteToWideChar(CP_ACP, 0, buffer, -1, NULL, 0);
            if (targetLen > 0) {
                std::vector<wchar_t> wBuf(targetLen);
                MultiByteToWideChar(CP_ACP, 0, buffer, -1, wBuf.data(), targetLen);

                int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wBuf.data(), -1, NULL, 0, NULL, NULL);
                if (utf8Len > 0) {
                    std::vector<char> utf8Buf(utf8Len);
                    WideCharToMultiByte(CP_UTF8, 0, wBuf.data(), -1, utf8Buf.data(), utf8Len, NULL, NULL);
                    printf("[Hook] %s\n", utf8Buf.data());
                }
            }
            else {
                printf("[Hook] %s\n", buffer);
            }
        }
    }

    void InitConsole() {
        if (Config::EnableDebug) {
            if (AllocConsole()) {
                FILE* fp;
                freopen_s(&fp, "CONOUT$", "w", stdout);
                freopen_s(&fp, "CONOUT$", "w", stderr);
                freopen_s(&fp, "CONIN$", "r", stdin);
                SetConsoleTitleW(L"Hook Debug Console");
                SetConsoleOutputCP(65001);
                printf("[Console] Debug Console Initialized.\n");
            }
        }
    }

    BOOL DeployLeFiles(HMODULE hModule) {
        if (!Config::EnableLE) {
            return FALSE;
        }

        const wchar_t* filesToDeploy[] = { L"LoaderDll.dll", L"LocaleEmulator.dll" };
        const int requiredFileCount = sizeof(filesToDeploy) / sizeof(filesToDeploy[0]);

        wchar_t rootPath[MAX_PATH];
        GetModuleFileNameW(NULL, rootPath, MAX_PATH);
        PathRemoveFileSpecW(rootPath);

        int filesDeployed = 0;
        for (const auto& fileName : filesToDeploy) {
            wchar_t dstPath[MAX_PATH];
            swprintf_s(dstPath, MAX_PATH, L"%s\\%s", rootPath, fileName);

            bool extracted = false;

            if (VFS::IsActive()) {
                extracted = VFS::ExtractFile(fileName, dstPath);
                if (extracted && Config::EnableDebug) {
                    Log("[LE] Extracted %S from VFS", fileName);
                }
            }

            if (!extracted) {
                wchar_t patchPath[MAX_PATH];
                const wchar_t* tempRoot = Archive::GetTempRootW();
                if (tempRoot) {
                    wcscpy_s(patchPath, MAX_PATH, tempRoot);
                }
                else {
                    GetModuleFileNameW(hModule, patchPath, MAX_PATH);
                    PathRemoveFileSpecW(patchPath);
                    PathAppendW(patchPath, Config::RedirectFolderW);
                }

                wchar_t srcPath[MAX_PATH];
                swprintf_s(srcPath, MAX_PATH, L"%s\\%s", patchPath, fileName);
                if (PathFileExistsW(srcPath) && CopyFileW(srcPath, dstPath, FALSE)) {
                    extracted = true;
                    if (Config::EnableDebug) {
                        Log("[LE] Copied %S from patch folder", fileName);
                    }
                }
            }

            if (extracted) {
                g_deployedLeFiles.push_back(dstPath);
                filesDeployed++;
            }
        }

        if (filesDeployed < requiredFileCount) {
            CleanupLeFiles();
            return FALSE;
        }

        return TRUE;
    }

    void CleanupLeFiles() {
        for (const auto& filePath : g_deployedLeFiles) {
            DeleteFileW(filePath.c_str());
            if (Config::EnableDebug) {
                Log("[LE] Cleaned up: %S", filePath.c_str());
            }
        }
        g_deployedLeFiles.clear();

        wchar_t rootPath[MAX_PATH];
        GetModuleFileNameW(NULL, rootPath, MAX_PATH);
        PathRemoveFileSpecW(rootPath);

        const wchar_t* leFiles[] = { L"LoaderDll.dll", L"LocaleEmulator.dll" };
        for (const auto& fileName : leFiles) {
            wchar_t filePath[MAX_PATH];
            swprintf_s(filePath, MAX_PATH, L"%s\\%s", rootPath, fileName);
            DeleteFileW(filePath);
        }
    }
}

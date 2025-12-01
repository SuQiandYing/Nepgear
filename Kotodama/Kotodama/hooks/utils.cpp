#include "pch.h"
#include "utils.h"
#include "config.h"
#include <Shlwapi.h>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdarg.h>
#include "archive.h"

#pragma comment(lib, "Shlwapi.lib")

static std::vector<std::wstring> g_deployedLeFiles;

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

        HANDLE hFile = CreateFileW(fontPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            return FALSE;
        }

        DWORD fileSize = GetFileSize(hFile, NULL);
        if (fileSize == INVALID_FILE_SIZE) {
            CloseHandle(hFile);
            return FALSE;
        }

        BYTE* fontData = (BYTE*)HeapAlloc(GetProcessHeap(), 0, fileSize);
        if (!fontData) {
            CloseHandle(hFile);
            return FALSE;
        }

        DWORD bytesRead;
        if (!ReadFile(hFile, fontData, fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
            HeapFree(GetProcessHeap(), 0, fontData);
            CloseHandle(hFile);
            return FALSE;
        }
        CloseHandle(hFile);

        DWORD numFonts = 0;
        HANDLE hFont = AddFontMemResourceEx(fontData, fileSize, NULL, &numFonts);
        HeapFree(GetProcessHeap(), 0, fontData);

        return hFont != NULL;
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

    BOOL DeployLeFiles(HMODULE hModule) {
        const wchar_t* filesToDeploy[] = { L"LoaderDll.dll", L"LocaleEmulator.dll" };
        const int requiredFileCount = sizeof(filesToDeploy) / sizeof(filesToDeploy[0]);
        int filesFound = 0;

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

        for (const auto& fileName : filesToDeploy) {
            wchar_t srcPath[MAX_PATH];
            swprintf_s(srcPath, MAX_PATH, L"%s\\%s", patchPath, fileName);
            if (PathFileExistsW(srcPath)) {
                filesFound++;
            }
        }

        if (filesFound < requiredFileCount) {
            return FALSE;
        }

        wchar_t rootPath[MAX_PATH];
        GetModuleFileNameW(NULL, rootPath, MAX_PATH);
        PathRemoveFileSpecW(rootPath);

        for (const auto& fileName : filesToDeploy) {
            wchar_t srcPath[MAX_PATH];
            wchar_t dstPath[MAX_PATH];
            swprintf_s(srcPath, MAX_PATH, L"%s\\%s", patchPath, fileName);
            swprintf_s(dstPath, MAX_PATH, L"%s\\%s", rootPath, fileName);

            if (CopyFileW(srcPath, dstPath, FALSE)) {
                g_deployedLeFiles.push_back(dstPath);
            }
            else {
                CleanupLeFiles();
                return FALSE;
            }
        }
        return TRUE;
    }

    void CleanupLeFiles() {
        for (const auto& filePath : g_deployedLeFiles) {
            DeleteFileW(filePath.c_str());
        }
        g_deployedLeFiles.clear();
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
}

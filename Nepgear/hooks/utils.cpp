#include "../pch.h"
#include "utils.h"
#include "config.h"
#include "vfs.h"
#include <Shlwapi.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdarg.h>
#include "archive.h"

#pragma comment(lib, "Shlwapi.lib")

#include <set>

static std::vector<std::wstring> g_deployedFiles;

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
            CleanupPatchFiles();
            ExitProcess(0);
        }
    }

    static void LogInternal(LogLevel level, const wchar_t* message) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t timeBuf[64];
        swprintf_s(timeBuf, L"[%02d:%02d:%02d.%03d]", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        const wchar_t* levelPrefix = L"[INFO ]";
        WORD consoleAttr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

        if (level == LOG_WARN) {
            levelPrefix = L"[WARN ]";
            consoleAttr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        }
        else if (level == LOG_ERROR) {
            levelPrefix = L"[ERROR]";
            consoleAttr = FOREGROUND_RED | FOREGROUND_INTENSITY;
        }

        std::wstring logLine;
        if (level == LOG_ERROR) {
            logLine += L"============================================================\n";
        }
        logLine += timeBuf;
        logLine += L" ";
        logLine += levelPrefix;
        logLine += L" ";
        logLine += message;
        logLine += L"\n";
        if (level == LOG_ERROR) {
            logLine += L"============================================================\n";
        }

        OutputDebugStringW(logLine.c_str());

        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, logLine.c_str(), -1, NULL, 0, NULL, NULL);
        if (utf8Len > 0) {
            std::vector<char> utf8Buf(utf8Len);
            WideCharToMultiByte(CP_UTF8, 0, logLine.c_str(), -1, utf8Buf.data(), utf8Len, NULL, NULL);
            std::string utf8Str(utf8Buf.data());

            if (Config::EnableLogToFile) {
                static wchar_t logPath[MAX_PATH] = { 0 };
                static bool logRecreated = false;

                if (logPath[0] == 0) {
                    GetModuleFileNameW(NULL, logPath, MAX_PATH);
                    PathRemoveFileSpecW(logPath);
                    PathAppendW(logPath, L"Nepgear.log");
                }

                DWORD dwCreationDisposition = OPEN_ALWAYS;
                if (!logRecreated) {
                    dwCreationDisposition = CREATE_ALWAYS;
                    logRecreated = true;
                }

                HANDLE hFile = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, dwCreationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hFile != INVALID_HANDLE_VALUE) {
                    DWORD bytesWritten;
                    WriteFile(hFile, utf8Str.c_str(), (DWORD)utf8Str.length(), &bytesWritten, NULL);
                    CloseHandle(hFile);
                }
            }

            if (Config::EnableDebug) {
                HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                if (hConsole != INVALID_HANDLE_VALUE) {
                    SetConsoleTextAttribute(hConsole, consoleAttr);
                    printf("%s", utf8Str.c_str());
                    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                }
            }
        }
    }

    void Log(const char* format, ...) {
        char buffer[2048];
        va_list args;
        va_start(args, format);
        vsnprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
        va_end(args);

        int wLen = MultiByteToWideChar(CP_ACP, 0, buffer, -1, NULL, 0);
        if (wLen > 0) {
            std::vector<wchar_t> wBuf(wLen);
            MultiByteToWideChar(CP_ACP, 0, buffer, -1, wBuf.data(), wLen);
            LogInternal(LOG_INFO, wBuf.data());
        }
    }

    void Log(LogLevel level, const char* format, ...) {
        char buffer[2048];
        va_list args;
        va_start(args, format);
        vsnprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
        va_end(args);

        int wLen = MultiByteToWideChar(CP_ACP, 0, buffer, -1, NULL, 0);
        if (wLen > 0) {
            std::vector<wchar_t> wBuf(wLen);
            MultiByteToWideChar(CP_ACP, 0, buffer, -1, wBuf.data(), wLen);
            LogInternal(level, wBuf.data());
        }
    }

    void LogW(const wchar_t* format, ...) {
        wchar_t buffer[4096];
        va_list args;
        va_start(args, format);
        _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
        va_end(args);
        LogInternal(LOG_INFO, buffer);
    }

    void LogW(LogLevel level, const wchar_t* format, ...) {
        wchar_t buffer[4096];
        va_list args;
        va_start(args, format);
        _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
        va_end(args);
        LogInternal(level, buffer);
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

    BOOL DeployPatchFiles(HMODULE hModule) {
        wchar_t rootPath[MAX_PATH];
        GetModuleFileNameW(NULL, rootPath, MAX_PATH);
        PathRemoveFileSpecW(rootPath);

        std::set<std::wstring> filesToDeploy;

        auto IsTargetFile = [](const wchar_t* fileName) {
            const wchar_t* ext = PathFindExtensionW(fileName);
            if (!ext) return false;
            return _wcsicmp(ext, L".dll") == 0 || _wcsicmp(ext, L".ini") == 0;
        };

        auto ShouldDeploy = [](const wchar_t* fileName) {
            if (_wcsicmp(fileName, L"LoaderDll.dll") == 0 || _wcsicmp(fileName, L"LocaleEmulator.dll") == 0) {
                return Config::EnableLE;
            }
            return true;
        };

        if (VFS::IsActive()) {
            std::vector<std::wstring> vfsFiles;
            VFS::GetVirtualFileList(vfsFiles);
            for (const auto& relPath : vfsFiles) {
                const wchar_t* fileName = PathFindFileNameW(relPath.c_str());
                if (IsTargetFile(fileName) && ShouldDeploy(fileName)) {
                    filesToDeploy.insert(fileName);
                }
            }
        }

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

        if (PathFileExistsW(patchPath)) {
            wchar_t searchPath[MAX_PATH];
            wcscpy_s(searchPath, patchPath);
            PathAppendW(searchPath, L"*");

            WIN32_FIND_DATAW fd;
            HANDLE hFind = FindFirstFileW(searchPath, &fd);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        if (IsTargetFile(fd.cFileName) && ShouldDeploy(fd.cFileName)) {
                            filesToDeploy.insert(fd.cFileName);
                        }
                    }
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        }

        for (const auto& fileName : filesToDeploy) {
            wchar_t dstPath[MAX_PATH];
            swprintf_s(dstPath, MAX_PATH, L"%s\\%s", rootPath, fileName.c_str());

            bool deployed = false;

            if (VFS::IsActive() && VFS::HasVirtualFile(fileName.c_str())) {
                deployed = VFS::ExtractFile(fileName.c_str(), dstPath);
                if (deployed && Config::EnableDebug) {
                    Log("[Deploy] Extracted %S from VFS", fileName.c_str());
                }
            }

            if (!deployed) {
                wchar_t srcPath[MAX_PATH];
                swprintf_s(srcPath, MAX_PATH, L"%s\\%s", patchPath, fileName.c_str());
                if (PathFileExistsW(srcPath)) {
                    if (CopyFileW(srcPath, dstPath, FALSE)) {
                        deployed = true;
                        if (Config::EnableDebug) {
                            Log("[Deploy] Copied %S from patch folder", fileName.c_str());
                        }
                    } else if (GetLastError() == ERROR_SHARING_VIOLATION) {
                        // File might already be there and in use (e.g. another instance or system dll)
                        if (Config::EnableDebug) {
                            Log("[Deploy] Skipped %S (already in use)", fileName.c_str());
                        }
                    }
                }
            }

            if (deployed || PathFileExistsW(dstPath)) {
                bool alreadyTracked = false;
                for (const auto& f : g_deployedFiles) {
                    if (_wcsicmp(f.c_str(), dstPath) == 0) {
                        alreadyTracked = true;
                        break;
                    }
                }
                if (!alreadyTracked) {
                    g_deployedFiles.push_back(dstPath);
                }
            }
        }

        return TRUE;
    }

    void CleanupPatchFiles() {
        wchar_t rootPath[MAX_PATH];
        GetModuleFileNameW(NULL, rootPath, MAX_PATH);
        PathRemoveFileSpecW(rootPath);

        std::vector<std::wstring> failedFiles;

        // 1. Clean up explicitly deployed files
        for (const auto& filePath : g_deployedFiles) {
            if (DeleteFileW(filePath.c_str())) {
                if (Config::EnableDebug) {
                    Log("[Deploy] Cleaned up: %S", filePath.c_str());
                }
            } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
                failedFiles.push_back(filePath);
            }
        }
        g_deployedFiles.clear();

        // 2. Clean up LE files (fallback/extra check)
        const wchar_t* leFiles[] = { L"LoaderDll.dll", L"LocaleEmulator.dll" };
        for (const auto& fileName : leFiles) {
            wchar_t filePath[MAX_PATH];
            swprintf_s(filePath, MAX_PATH, L"%s\\%s", rootPath, fileName);
            if (!DeleteFileW(filePath) && GetLastError() != ERROR_FILE_NOT_FOUND) {
                // Only add if not already in failed list
                bool alreadyAdded = false;
                for (const auto& f : failedFiles) {
                    if (_wcsicmp(f.c_str(), filePath) == 0) {
                        alreadyAdded = true;
                        break;
                    }
                }
                if (!alreadyAdded) failedFiles.push_back(filePath);
            }
        }

        // 3. Use cmd.exe for delayed cleanup of files that are still in use
        if (!failedFiles.empty()) {
            std::wstring cmdLine = L"cmd.exe /c \"ping 127.0.0.1 -n 2 > nul";
            for (const auto& f : failedFiles) {
                cmdLine += L" & del /f /q \"";
                cmdLine += f;
                cmdLine += L"\"";
            }
            cmdLine += L"\"";

            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi = { 0 };
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;

            if (CreateProcessW(NULL, (LPWSTR)cmdLine.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                if (Config::EnableDebug) {
                    Log("[Deploy] Launched cmd.exe for delayed cleanup of %zu files", failedFiles.size());
                }
            } else if (Config::EnableDebug) {
                Log(LOG_ERROR, "[Deploy] Failed to launch cmd.exe for cleanup (Error: %lu)", GetLastError());
            }
        }
    }
}

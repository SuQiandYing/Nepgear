#include "pch.h"
#include "archive.h"
#include "config.h"
#include "utils.h"
#include <shlwapi.h>
#include <stdio.h>
#include <vector>

#pragma comment(lib, "Shlwapi.lib")

namespace Archive {
    static wchar_t g_TempRootW[MAX_PATH] = { 0 };
    static char    g_TempRootA[MAX_PATH] = { 0 };
    static bool    g_IsActive = false;

    void CreateDirRecursive(const wchar_t* path) {
        wchar_t temp[MAX_PATH];
        wcscpy_s(temp, path);
        PathRemoveFileSpecW(temp);
        if (!PathIsDirectoryW(temp) && temp[0] != '\0') {
            CreateDirRecursive(temp);
            CreateDirectoryW(temp, NULL);
        }
    }

    void DeleteDirRecursive(const wchar_t* path) {
        WIN32_FIND_DATAW fd;
        wchar_t search[MAX_PATH];
        swprintf_s(search, L"%s\\*.*", path);
        HANDLE hFind = FindFirstFileW(search, &fd);
        if (hFind == INVALID_HANDLE_VALUE) return;
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            wchar_t subPath[MAX_PATH];
            swprintf_s(subPath, L"%s\\%s", path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                DeleteDirRecursive(subPath);
            }
            else {
                SetFileAttributesW(subPath, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(subPath);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
        RemoveDirectoryW(path);
    }

    bool Extract(HMODULE hModule) {
        if (!Config::EnableFileHook) {
            Utils::Log("[Archive] File Hook Disabled.");
            return false;
        }

        wchar_t archivePath[MAX_PATH];
        GetModuleFileNameW(hModule, archivePath, MAX_PATH);
        PathRemoveFileSpecW(archivePath);
        PathAppendW(archivePath, Config::ArchiveFileName);

        Utils::Log("[Archive] Looking for dat at: %S", archivePath);

        if (!PathFileExistsW(archivePath)) {
            Utils::Log("[Archive] ERROR: %S NOT FOUND!", archivePath);
            return false;
        }

        wchar_t tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);
        PathAppendW(tempPath, Config::RedirectFolderW);

        wcscpy_s(g_TempRootW, tempPath);
        WideCharToMultiByte(CP_ACP, 0, g_TempRootW, -1, g_TempRootA, MAX_PATH, NULL, NULL);

        Utils::Log("[Archive] Extracting to: %S", g_TempRootW);

        if (PathIsDirectoryW(g_TempRootW)) DeleteDirRecursive(g_TempRootW);
        if (!CreateDirectoryW(g_TempRootW, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            Utils::Log("[Archive] ERROR: Cannot create temp dir. Error: %d", GetLastError());
            return false;
        }

        HANDLE hFile = CreateFileW(archivePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            Utils::Log("[Archive] ERROR: Cannot open %S", Config::ArchiveFileName);
            return false;
        }

        DWORD bytesRead;
        int fileCount = 0;
        if (!ReadFile(hFile, &fileCount, sizeof(int), &bytesRead, NULL)) {
            Utils::Log("[Archive] ERROR: Failed to read header");
            CloseHandle(hFile);
            return false;
        }

        wchar_t* fileExt = PathFindExtensionW(Config::ArchiveFileName);
        if (fileExt && *fileExt == L'.') fileExt++;
        Utils::Log("[Archive] Found %d files in %S.", fileCount, fileExt);

        const int CHUNK_SIZE = 4096;
        std::vector<char> buffer(CHUNK_SIZE);

        for (int i = 0; i < fileCount; i++) {
            int pathLen = 0;
            if (!ReadFile(hFile, &pathLen, sizeof(int), &bytesRead, NULL)) break;

            std::vector<char> relPath(pathLen + 1, '\0');
            if (!ReadFile(hFile, relPath.data(), pathLen, &bytesRead, NULL)) break;

            wchar_t relPathW[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, relPath.data(), -1, relPathW, MAX_PATH);

            wchar_t destPath[MAX_PATH];
            wcscpy_s(destPath, g_TempRootW);
            PathAppendW(destPath, relPathW);

            int dataSize = 0;
            if (!ReadFile(hFile, &dataSize, sizeof(int), &bytesRead, NULL)) break;

            CreateDirRecursive(destPath);
            HANDLE hDest = CreateFileW(destPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

            if (hDest != INVALID_HANDLE_VALUE) {
                int remaining = dataSize;
                DWORD bytesWritten = 0;
                bool errorOccurred = false;
                while (remaining > 0) {
                    int toRead = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
                    if (!ReadFile(hFile, buffer.data(), toRead, &bytesRead, NULL) || bytesRead == 0) {
                        errorOccurred = true;
                        break;
                    }
                    if (!WriteFile(hDest, buffer.data(), bytesRead, &bytesWritten, NULL) || bytesWritten != bytesRead) {
                        errorOccurred = true;
                        break;
                    }
                    remaining -= bytesRead;
                }
                CloseHandle(hDest);
                if (errorOccurred) {
                    DeleteFileW(destPath);
                    Utils::Log("[Archive] ERROR: Failed during file copy: %S", relPathW);
                }
            }
            else {
                Utils::Log("[Archive] ERROR: Failed to create file for writing: %S", relPathW);
                SetFilePointer(hFile, dataSize, NULL, FILE_CURRENT);
            }
        }

        CloseHandle(hFile);
        g_IsActive = true;
        Utils::Log("[Archive] Extraction Complete.");
        return true;
    }

    const wchar_t* GetTempRootW() { return g_IsActive ? g_TempRootW : NULL; }
    const char* GetTempRootA() { return g_IsActive ? g_TempRootA : NULL; }

    void Cleanup() {
        if (g_IsActive && g_TempRootW[0] != '\0') {
            DeleteDirRecursive(g_TempRootW);
            g_IsActive = false;
        }
    }
}

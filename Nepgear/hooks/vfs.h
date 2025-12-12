#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>

namespace VFS {
    struct VirtualFileEntry {
        std::wstring relativePath;
        LONGLONG offset;
        DWORD size;
        bool isLooseFile;
        std::wstring looseFilePath;
    };

    struct VirtualFileHandle {
        VirtualFileEntry* entry;
        LONGLONG position;
        HANDLE archiveHandle;
        HANDLE looseFileHandle;
        bool isLooseFile;
    };

    bool Initialize(HMODULE hModule);
    void Shutdown();
    bool IsActive();
    void SetOriginalFunctions(void* readFile, void* setFilePointerEx, void* closeHandle);

    bool HasVirtualFile(const wchar_t* relativePath);
    bool HasVirtualFileA(const char* relativePath);

    HANDLE OpenVirtualFile(const wchar_t* relativePath);
    HANDLE OpenVirtualFileA(const char* relativePath);

    bool IsVirtualHandle(HANDLE hFile);
    BOOL ReadVirtualFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
    DWORD SetVirtualFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod);
    BOOL SetVirtualFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod);
    DWORD GetVirtualFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
    BOOL GetVirtualFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize);
    BOOL CloseVirtualHandle(HANDLE hFile);
    BOOL GetVirtualFileInformationByHandle(HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation);
    DWORD GetVirtualFileType(HANDLE hFile);

    bool ExtractFile(const wchar_t* relativePath, const wchar_t* destPath);
}

#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>

namespace VFS {
    struct VirtualFileEntry {
        std::wstring relativePath;
        LONGLONG offset;
        DWORD size;
        DWORD decompressedSize;
        bool isLooseFile;
        std::wstring looseFilePath;
    };

    struct VirtualFileHandle {
        VirtualFileEntry* entry;
        LONGLONG position;
        HANDLE archiveHandle;
        HANDLE looseFileHandle;
        PBYTE decompressedBuffer;
        bool isLooseFile;
    };

    bool Initialize(HMODULE hModule);
    void Shutdown();
    bool IsActive();
    void SetOriginalFunctions(void* readFile, void* setFilePointerEx, void* closeHandle);
    void SetFindFunctions(void* findFirstW, void* findNextW, void* findClose, void* findFirstA, void* findNextA);

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

    HANDLE VirtualFindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData);
    BOOL VirtualFindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData);
    
    HANDLE VirtualFindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData);
    BOOL VirtualFindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData);

    BOOL VirtualFindClose(HANDLE hFindFile);

    bool ExtractFile(const wchar_t* relativePath, const wchar_t* destPath);
}

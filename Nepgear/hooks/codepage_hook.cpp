#include "../pch.h"
#include "codepage_hook.h"
#include "config.h"
#include "utils.h"
#include "../detours.h"

#ifdef _WIN64
#pragma comment(lib, "detours_x64.lib")
#else
#pragma comment(lib, "detours.lib")
#endif

typedef int (WINAPI* pMultiByteToWideChar)(UINT, DWORD, LPCCH, int, LPWSTR, int);
typedef int (WINAPI* pWideCharToMultiByte)(UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCCH, LPBOOL);

static pMultiByteToWideChar orgMultiByteToWideChar = MultiByteToWideChar;
static pWideCharToMultiByte orgWideCharToMultiByte = WideCharToMultiByte;

int WINAPI newMultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar) {
    if (Config::EnableCodePageHook && CodePage == Config::FromCodePage) {
        CodePage = Config::ToCodePage;
    }
    return orgMultiByteToWideChar(CodePage, dwFlags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar);
}

int WINAPI newWideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCCH lpDefaultChar, LPBOOL lpUsedDefaultChar) {
    if (Config::EnableCodePageHook && CodePage == Config::FromCodePage) {
        CodePage = Config::ToCodePage;
    }
    return orgWideCharToMultiByte(CodePage, dwFlags, lpWideCharStr, cchWideChar, lpMultiByteStr, cbMultiByte, lpDefaultChar, lpUsedDefaultChar);
}

namespace Hooks {
    void InstallCodePageHook() {
        if (!Config::EnableCodePageHook) return;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)orgMultiByteToWideChar, newMultiByteToWideChar);
        DetourAttach(&(PVOID&)orgWideCharToMultiByte, newWideCharToMultiByte);
        
        if (DetourTransactionCommit() == NO_ERROR) {
            Utils::Log("[CodePage] MultiByteToWideChar/WideCharToMultiByte hook installed. (%u -> %u)", 
                Config::FromCodePage, Config::ToCodePage);
        } else {
            Utils::Log("[CodePage] Failed to install hook.");
        }
    }
}

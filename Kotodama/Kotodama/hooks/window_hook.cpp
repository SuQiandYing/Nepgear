#include "pch.h"
#include "window_hook.h"
#include "config.h"
#include "utils.h"
#include "../detours.h"

#pragma comment(lib, "detours.lib")

typedef HWND(WINAPI* pCreateWindowExA)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
typedef BOOL(WINAPI* pSetWindowTextA)(HWND, LPCSTR);
typedef BOOL(WINAPI* pSetWindowTextW)(HWND, LPCWSTR);

static pCreateWindowExA orgCreateWindowExA = CreateWindowExA;
static pSetWindowTextA orgSetWindowTextA = SetWindowTextA;
static pSetWindowTextW orgSetWindowTextW = SetWindowTextW;

bool ShouldModifyWindow(HWND hWnd) {
    if (!hWnd) return false;

    LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);

    if (style & WS_CHILD) {
        return false;
    }

    if ((style & WS_CAPTION) != WS_CAPTION) {
        return false;
    }
    return true;
}

void ForceUnicodeTitle(HWND hWnd) {
    if (!ShouldModifyWindow(hWnd)) return;

    if (Config::CustomTitleW[0] != L'\0' && wcsstr(Config::CustomTitleW, L"????") == NULL) {
        DefWindowProcW(hWnd, WM_SETTEXT, 0, (LPARAM)Config::CustomTitleW);

        SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }
}

HWND WINAPI newCreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    HWND hWnd = orgCreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);

    if (hWnd) {
        ForceUnicodeTitle(hWnd);
    }
    return hWnd;
}

BOOL WINAPI newSetWindowTextA(HWND hWnd, LPCSTR lpString)
{
    if (Config::CustomTitleW[0] != L'\0' && ShouldModifyWindow(hWnd)) {
        if (Config::EnableDebug) {
            Utils::Log("[Window] Replacing Title (A): '%s' -> Custom", lpString ? lpString : "NULL");
        }
        ForceUnicodeTitle(hWnd);
        return TRUE;
    }
    return orgSetWindowTextA(hWnd, lpString);
}

BOOL WINAPI newSetWindowTextW(HWND hWnd, LPCWSTR lpString)
{
    if (Config::CustomTitleW[0] != L'\0' && ShouldModifyWindow(hWnd)) {
        if (Config::EnableDebug) {
            Utils::Log("[Window] Replacing Title (W): '%S' -> Custom", lpString ? lpString : L"NULL");
        }
        ForceUnicodeTitle(hWnd);
        return TRUE;
    }
    return orgSetWindowTextW(hWnd, lpString);
}

static HANDLE g_hTitleThread = NULL;
static volatile bool g_bStopThread = false;

BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) {
    DWORD processId;
    GetWindowThreadProcessId(hWnd, &processId);
    if (processId == GetCurrentProcessId() && IsWindowVisible(hWnd) && ShouldModifyWindow(hWnd)) {
        wchar_t currentTitle[256] = { 0 };
        DefWindowProcW(hWnd, WM_GETTEXT, 255, (LPARAM)currentTitle);

        if (Config::CustomTitleW[0] != L'\0' &&
            wcsstr(Config::CustomTitleW, L"????") == NULL &&
            wcscmp(currentTitle, Config::CustomTitleW) != 0)
        {
            ForceUnicodeTitle(hWnd);
        }
    }
    return TRUE;
}

DWORD WINAPI TitleCorrectionThread(LPVOID lpParam) {
    while (!g_bStopThread) {
        EnumWindows(EnumWindowsProc, NULL);
        Sleep(0); 
    }
    return 0;
}

namespace Hooks {
    void InstallWindowHook() {
        if (!Config::EnableWindowTitleHook) return;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)orgCreateWindowExA, newCreateWindowExA);
        DetourAttach(&(PVOID&)orgSetWindowTextA, newSetWindowTextA);
        DetourAttach(&(PVOID&)orgSetWindowTextW, newSetWindowTextW);
        DetourTransactionCommit();

        g_hTitleThread = CreateThread(NULL, 0, TitleCorrectionThread, NULL, 0, NULL);
        Utils::Log("[Core] Window Hook Installed.");
    }
}

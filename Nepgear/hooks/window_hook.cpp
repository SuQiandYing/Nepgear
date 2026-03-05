#include "../pch.h"
#include "window_hook.h"
#include "config.h"
#include "utils.h"
#include "../detours.h"

#ifdef _WIN64
#pragma comment(lib, "detours_x64.lib")
#else
#pragma comment(lib, "detours.lib")
#endif


typedef HWND(WINAPI* pCreateWindowExA)(DWORD, LPCSTR,  LPCSTR,  DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
typedef HWND(WINAPI* pCreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
typedef BOOL(WINAPI* pSetWindowTextA)(HWND, LPCSTR);
typedef BOOL(WINAPI* pSetWindowTextW)(HWND, LPCWSTR);

static pCreateWindowExA orgCreateWindowExA = CreateWindowExA;
static pCreateWindowExW orgCreateWindowExW = CreateWindowExW;
static pSetWindowTextA  orgSetWindowTextA  = SetWindowTextA;
static pSetWindowTextW  orgSetWindowTextW  = SetWindowTextW;

static HWINEVENTHOOK g_hWinEventHook    = NULL;
static bool          g_bHooksInstalled  = false;


bool ShouldModifyWindow(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return false;
    LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
    if (style & WS_CHILD) return false;
    if ((style & WS_CAPTION) != WS_CAPTION) return false;
    return true;
}


void ForceUnicodeTitle(HWND hWnd) {
    if (!ShouldModifyWindow(hWnd)) return;
    
    if (Config::CustomTitleW[0] != L'\0') {

        wchar_t currentTitle[512] = { 0 };

        DefWindowProcW(hWnd, WM_GETTEXT, 512, (LPARAM)currentTitle);

        if (wcscmp(currentTitle, Config::CustomTitleW) != 0) {

            DefWindowProcW(hWnd, WM_SETTEXT, 0, (LPARAM)Config::CustomTitleW);
            

            SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        }
    }
}


void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
    LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{

    if (idObject == OBJID_WINDOW && idChild == CHILDID_SELF && hwnd) {
        DWORD processId = 0;
        GetWindowThreadProcessId(hwnd, &processId);

        if (processId == GetCurrentProcessId()) {
            ForceUnicodeTitle(hwnd);
        }
    }
}



HWND WINAPI newCreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName,
    DWORD dwStyle, int X, int Y, int nWidth, int nHeight,
    HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    HWND hWnd = orgCreateWindowExA(dwExStyle, lpClassName, lpWindowName,
        dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (hWnd) ForceUnicodeTitle(hWnd);
    return hWnd;
}

HWND WINAPI newCreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName,
    DWORD dwStyle, int X, int Y, int nWidth, int nHeight,
    HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    HWND hWnd = orgCreateWindowExW(dwExStyle, lpClassName, lpWindowName,
        dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (hWnd) ForceUnicodeTitle(hWnd);
    return hWnd;
}

BOOL WINAPI newSetWindowTextA(HWND hWnd, LPCSTR lpString)
{
    if (Config::CustomTitleW[0] != L'\0' && ShouldModifyWindow(hWnd)) {
        ForceUnicodeTitle(hWnd);
        return TRUE;
    }
    return orgSetWindowTextA(hWnd, lpString);
}

BOOL WINAPI newSetWindowTextW(HWND hWnd, LPCWSTR lpString)
{
    if (Config::CustomTitleW[0] != L'\0' && ShouldModifyWindow(hWnd)) {
        ForceUnicodeTitle(hWnd);
        return TRUE;
    }
    return orgSetWindowTextW(hWnd, lpString);
}



namespace Hooks {

    void InstallWindowHook() {
        if (!Config::EnableWindowTitleHook) return;


        

        if (Config::WindowTitleMode == 0 || Config::WindowTitleMode == 2) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID&)orgCreateWindowExA, newCreateWindowExA);
            DetourAttach(&(PVOID&)orgCreateWindowExW, newCreateWindowExW);
            DetourAttach(&(PVOID&)orgSetWindowTextA,  newSetWindowTextA);
            DetourAttach(&(PVOID&)orgSetWindowTextW,  newSetWindowTextW);
            DetourTransactionCommit();
            g_bHooksInstalled = true;
            Utils::Log("[Window] API Hook mode installed.");
        }

        if (Config::WindowTitleMode == 1 || Config::WindowTitleMode == 2) {
            g_hWinEventHook = SetWinEventHook(
                EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE,
                NULL, WinEventProc, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT
            );
            Utils::Log("[Window] WinEvent listener installed.");
        }


        EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
            DWORD pid;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) {
                ForceUnicodeTitle(hwnd);
            }
            return TRUE;
        }, 0);
    }

    void UninstallWindowHook() {

        if (g_bHooksInstalled) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourDetach(&(PVOID&)orgCreateWindowExA, newCreateWindowExA);
            DetourDetach(&(PVOID&)orgCreateWindowExW, newCreateWindowExW);
            DetourDetach(&(PVOID&)orgSetWindowTextA,  newSetWindowTextA);
            DetourDetach(&(PVOID&)orgSetWindowTextW,  newSetWindowTextW);
            DetourTransactionCommit();
            g_bHooksInstalled = false;
        }


        if (g_hWinEventHook) {
            UnhookWinEvent(g_hWinEventHook);
            g_hWinEventHook = NULL;
        }
    }
}
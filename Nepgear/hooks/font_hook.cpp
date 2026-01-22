#include "../pch.h"
#include <windows.h>
#include <unknwn.h>
#include <stdio.h>
#include <set>
#include <string>
#include <mutex>
#include <intrin.h>
#include <psapi.h>
#include "font_hook.h"
#include "config.h"
#include "utils.h"
#include "../detours.h"
#include "vfs.h"
#include "archive.h"
#include <Shlwapi.h>
#include <dwrite.h>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "psapi.lib")

#pragma intrinsic(_ReturnAddress)

typedef HFONT(WINAPI* pCreateFontA)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCSTR);
typedef HFONT(WINAPI* pCreateFontIndirectA)(const LOGFONTA*);
typedef HFONT(WINAPI* pCreateFontW)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR);
typedef HFONT(WINAPI* pCreateFontIndirectW)(const LOGFONTW*);
typedef HMODULE(WINAPI* pLoadLibraryW)(LPCWSTR);
typedef HMODULE(WINAPI* pLoadLibraryExW)(LPCWSTR, HANDLE, DWORD);

typedef BOOL(WINAPI* pTextOutA)(HDC, int, int, LPCSTR, int);
typedef BOOL(WINAPI* pTextOutW)(HDC, int, int, LPCWSTR, int);
typedef BOOL(WINAPI* pExtTextOutA)(HDC, int, int, UINT, const RECT*, LPCSTR, UINT, const int*);
typedef BOOL(WINAPI* pExtTextOutW)(HDC, int, int, UINT, const RECT*, LPCWSTR, UINT, const int*);
typedef int (WINAPI* pDrawTextA)(HDC, LPCSTR, int, LPRECT, UINT);
typedef int (WINAPI* pDrawTextW)(HDC, LPCWSTR, int, LPRECT, UINT);
typedef int (WINAPI* pDrawTextExA)(HDC, LPSTR, int, LPRECT, UINT, LPDRAWTEXTPARAMS);
typedef int (WINAPI* pDrawTextExW)(HDC, LPWSTR, int, LPRECT, UINT, LPDRAWTEXTPARAMS);
typedef BOOL(WINAPI* pPolyTextOutA)(HDC, const POLYTEXTA*, int);
typedef BOOL(WINAPI* pPolyTextOutW)(HDC, const POLYTEXTW*, int);
typedef LONG(WINAPI* pTabbedTextOutA)(HDC, int, int, LPCSTR, int, int, const int*, int);
typedef LONG(WINAPI* pTabbedTextOutW)(HDC, int, int, LPCWSTR, int, int, const int*, int);

static pCreateFontA orgCreateFontA = CreateFontA;
static pCreateFontIndirectA orgCreateFontIndirectA = CreateFontIndirectA;
static pCreateFontW orgCreateFontW = CreateFontW;
static pCreateFontIndirectW orgCreateFontIndirectW = CreateFontIndirectW;
static pLoadLibraryW orgLoadLibraryW = LoadLibraryW;
static pLoadLibraryExW orgLoadLibraryExW = LoadLibraryExW;

static pTextOutA orgTextOutA = TextOutA;
static pTextOutW orgTextOutW = TextOutW;
static pExtTextOutA orgExtTextOutA = ExtTextOutA;
static pExtTextOutW orgExtTextOutW = ExtTextOutW;
static pDrawTextA orgDrawTextA = DrawTextA;
static pDrawTextW orgDrawTextW = DrawTextW;
static pDrawTextExA orgDrawTextExA = DrawTextExA;
static pDrawTextExW orgDrawTextExW = DrawTextExW;
static pPolyTextOutA orgPolyTextOutA = PolyTextOutA;
static pPolyTextOutW orgPolyTextOutW = PolyTextOutW;
static pTabbedTextOutA orgTabbedTextOutA = TabbedTextOutA;
static pTabbedTextOutW orgTabbedTextOutW = TabbedTextOutW;

typedef int GpStatus;
typedef void GpFontFamily;
typedef void GpFont;
typedef void GpFontCollection;
typedef void GpGraphics;
typedef void GpBrush;
typedef void GpStringFormat;

typedef GpStatus(WINAPI* pGdipCreateFontFamilyFromName)(const WCHAR* name, GpFontCollection* fontCollection, GpFontFamily** FontFamily);
typedef GpStatus(WINAPI* pGdipCreateFontFromLogfontW)(HDC hdc, const LOGFONTW* logfont, GpFont** font);
typedef GpStatus(WINAPI* pGdipCreateFontFromLogfontA)(HDC hdc, const LOGFONTA* logfont, GpFont** font);
typedef GpStatus(WINAPI* pGdipCreateFontFromHFONT)(HDC hdc, HFONT hfont, GpFont** font);
typedef GpStatus(WINAPI* pGdipCreateFontFromDC)(HDC hdc, GpFont** font);
typedef GpStatus(WINAPI* pGdipCreateFont)(const GpFontFamily* fontFamily, float emSize, int style, int unit, GpFont** font);
typedef GpStatus(WINAPI* pGdipNewPrivateFontCollection)(GpFontCollection** fontCollection);
typedef GpStatus(WINAPI* pGdipPrivateAddFontFile)(GpFontCollection* fontCollection, const WCHAR* filename);
typedef GpStatus(WINAPI* pGdipPrivateAddMemoryFont)(GpFontCollection* fontCollection, const void* memory, int length);
typedef GpStatus(WINAPI* pGdipDrawString)(GpGraphics* graphics, const WCHAR* string, int length, const GpFont* font, const void* layoutRect, const GpStringFormat* stringFormat, const GpBrush* brush);
typedef GpStatus(WINAPI* pGdipDrawDriverString)(GpGraphics* graphics, const UINT16* text, int length, const GpFont* font, const GpBrush* brush, const void* positions, int flags, const void* matrix);
typedef GpStatus(WINAPI* pGdipGetLogFontW)(GpFont* font, GpGraphics* graphics, LOGFONTW* logfont);

struct GdiplusStartupInput {
    UINT32 GdiplusVersion;
    void* DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
    GdiplusStartupInput() {
        GdiplusVersion = 1;
        DebugEventCallback = NULL;
        SuppressBackgroundThread = FALSE;
        SuppressExternalCodecs = FALSE;
    }
};
typedef GpStatus(WINAPI* pGdiplusStartup)(ULONG_PTR* token, const GdiplusStartupInput* input, void* output);

static pGdipCreateFontFamilyFromName orgGdipCreateFontFamilyFromName = NULL;
static pGdipCreateFontFromLogfontW orgGdipCreateFontFromLogfontW = NULL;
static pGdipCreateFontFromLogfontA orgGdipCreateFontFromLogfontA = NULL;
static pGdipCreateFontFromHFONT orgGdipCreateFontFromHFONT = NULL;
static pGdipCreateFontFromDC orgGdipCreateFontFromDC = NULL;
static pGdipCreateFont orgGdipCreateFont = NULL;
static pGdipNewPrivateFontCollection ptrGdipNewPrivateFontCollection = NULL;
static pGdipPrivateAddFontFile ptrGdipPrivateAddFontFile = NULL;
static pGdipPrivateAddMemoryFont ptrGdipPrivateAddMemoryFont = NULL;
static pGdiplusStartup ptrGdiplusStartup = NULL;
static pGdipDrawString orgGdipDrawString = NULL;
static pGdipDrawDriverString orgGdipDrawDriverString = NULL;
static pGdipGetLogFontW ptrGdipGetLogFontW = NULL;

static HMODULE g_hModule = NULL;
static bool g_FontInitialized = false;
static bool g_CustomFontLoaded = false;
static bool g_GdiPlusHooksInstalled = false;
static GpFontCollection* g_PrivateFontCollection = NULL;
static wchar_t g_FontFilePath[MAX_PATH] = { 0 };
static ULONG_PTR g_GdiplusToken = 0;

static std::wstring g_lastFontLog;
static std::set<std::wstring> g_loggedFonts;
static std::mutex g_logMutex;

static void LogGdiFont(HDC hdc, const wchar_t* funcName, void* caller) {
    if (!Config::EnableDebug) return;

    HFONT hFont = (HFONT)GetCurrentObject(hdc, OBJ_FONT);
    if (!hFont) return;

    LOGFONTW lf;
    if (GetObjectW(hFont, sizeof(lf), &lf) == 0) return;

    wchar_t addrBuf[64] = { 0 };
    if (caller) {
        static HMODULE hExe = GetModuleHandleW(NULL);
        static MODULEINFO mi = { 0 };
        static bool gotMi = false;
        if (!gotMi) {
            gotMi = (GetModuleInformation(GetCurrentProcess(), hExe, &mi, sizeof(mi)) != 0);
        }
        
        bool isFromGame = false;
        if (gotMi && mi.lpBaseOfDll != NULL && mi.SizeOfImage > 0) {
            if (caller >= mi.lpBaseOfDll && (BYTE*)caller < (BYTE*)mi.lpBaseOfDll + mi.SizeOfImage) {
                isFromGame = true;
            }
        }
        swprintf_s(addrBuf, L"[%s:0x%p] ", isFromGame ? L"G" : L"S", caller);
    }

    wchar_t logBuf[512];
    swprintf_s(logBuf, L"%s%s: Font=\"%s\", Height=%ld, Weight=%ld, CharSet=%d", 
        addrBuf, funcName, lf.lfFaceName, lf.lfHeight, lf.lfWeight, lf.lfCharSet);

    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_loggedFonts.find(logBuf) == g_loggedFonts.end()) {
        Utils::LogW(L"%s", logBuf);
        g_loggedFonts.insert(logBuf);
    }
}

static void LogGdipFont(const GpFont* font, GpGraphics* graphics, const wchar_t* funcName, void* caller) {
    if (!Config::EnableDebug || !font || !ptrGdipGetLogFontW) return;

    LOGFONTW lf;
    if (ptrGdipGetLogFontW((GpFont*)font, graphics, &lf) != 0) return;

    wchar_t addrBuf[64] = { 0 };
    if (caller) {
        static HMODULE hExe = GetModuleHandleW(NULL);
        static MODULEINFO mi = { 0 };
        static bool gotMi = false;
        if (!gotMi) {
            gotMi = (GetModuleInformation(GetCurrentProcess(), hExe, &mi, sizeof(mi)) != 0);
        }
        
        bool isFromGame = false;
        if (gotMi && mi.lpBaseOfDll != NULL && mi.SizeOfImage > 0) {
            if (caller >= mi.lpBaseOfDll && (BYTE*)caller < (BYTE*)mi.lpBaseOfDll + mi.SizeOfImage) {
                isFromGame = true;
            }
        }
        swprintf_s(addrBuf, L"[%s:0x%p] ", isFromGame ? L"G" : L"S", caller);
    }

    wchar_t logBuf[512];
    swprintf_s(logBuf, L"%s%s: Font=\"%s\", Height=%ld, Weight=%ld, CharSet=%d (GDI+)", 
        addrBuf, funcName, lf.lfFaceName, lf.lfHeight, lf.lfWeight, lf.lfCharSet);

    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_loggedFonts.find(logBuf) == g_loggedFonts.end()) {
        Utils::LogW(L"%s", logBuf);
        g_loggedFonts.insert(logBuf);
    }
}

static void LogFontChange(const wchar_t* format, ...) {
    if (!Config::EnableDebug) return;
    wchar_t buffer[2048];
    va_list args;
    va_start(args, format);
    vswprintf_s(buffer, _countof(buffer), format, args);
    va_end(args);
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_loggedFonts.find(buffer) == g_loggedFonts.end()) {
        Utils::LogW(L"%s", buffer);
        g_loggedFonts.insert(buffer);
    }
}

BOOL WINAPI newTextOutA(HDC hdc, int x, int y, LPCSTR lpString, int nCount) {
    LogGdiFont(hdc, L"TextOutA", _ReturnAddress());
    return orgTextOutA(hdc, x, y, lpString, nCount);
}

BOOL WINAPI newTextOutW(HDC hdc, int x, int y, LPCWSTR lpString, int nCount) {
    LogGdiFont(hdc, L"TextOutW", _ReturnAddress());
    return orgTextOutW(hdc, x, y, lpString, nCount);
}

BOOL WINAPI newExtTextOutA(HDC hdc, int x, int y, UINT options, const RECT* lprect, LPCSTR lpString, UINT nCount, const int* lpDx) {
    LogGdiFont(hdc, L"ExtTextOutA", _ReturnAddress());
    return orgExtTextOutA(hdc, x, y, options, lprect, lpString, nCount, lpDx);
}

BOOL WINAPI newExtTextOutW(HDC hdc, int x, int y, UINT options, const RECT* lprect, LPCWSTR lpString, UINT nCount, const int* lpDx) {
    LogGdiFont(hdc, L"ExtTextOutW", _ReturnAddress());
    return orgExtTextOutW(hdc, x, y, options, lprect, lpString, nCount, lpDx);
}

int WINAPI newDrawTextA(HDC hdc, LPCSTR lpchText, int nCount, LPRECT lpRect, UINT format) {
    LogGdiFont(hdc, L"DrawTextA", _ReturnAddress());
    return orgDrawTextA(hdc, lpchText, nCount, lpRect, format);
}

int WINAPI newDrawTextW(HDC hdc, LPCWSTR lpchText, int nCount, LPRECT lpRect, UINT format) {
    LogGdiFont(hdc, L"DrawTextW", _ReturnAddress());
    return orgDrawTextW(hdc, lpchText, nCount, lpRect, format);
}

int WINAPI newDrawTextExA(HDC hdc, LPSTR lpchText, int nCount, LPRECT lpRect, UINT format, LPDRAWTEXTPARAMS lpdtp) {
    LogGdiFont(hdc, L"DrawTextExA", _ReturnAddress());
    return orgDrawTextExA(hdc, lpchText, nCount, lpRect, format, lpdtp);
}

int WINAPI newDrawTextExW(HDC hdc, LPWSTR lpchText, int nCount, LPRECT lpRect, UINT format, LPDRAWTEXTPARAMS lpdtp) {
    LogGdiFont(hdc, L"DrawTextExW", _ReturnAddress());
    return orgDrawTextExW(hdc, lpchText, nCount, lpRect, format, lpdtp);
}

BOOL WINAPI newPolyTextOutA(HDC hdc, const POLYTEXTA* ppt, int nTexts) {
    LogGdiFont(hdc, L"PolyTextOutA", _ReturnAddress());
    return orgPolyTextOutA(hdc, ppt, nTexts);
}

BOOL WINAPI newPolyTextOutW(HDC hdc, const POLYTEXTW* ppt, int nTexts) {
    LogGdiFont(hdc, L"PolyTextOutW", _ReturnAddress());
    return orgPolyTextOutW(hdc, ppt, nTexts);
}

LONG WINAPI newTabbedTextOutA(HDC hdc, int x, int y, LPCSTR lpString, int nCount, int nTabPositions, const int* lpnTabPositions, int nTabOrigin) {
    LogGdiFont(hdc, L"TabbedTextOutA", _ReturnAddress());
    return orgTabbedTextOutA(hdc, x, y, lpString, nCount, nTabPositions, lpnTabPositions, nTabOrigin);
}

LONG WINAPI newTabbedTextOutW(HDC hdc, int x, int y, LPCWSTR lpString, int nCount, int nTabPositions, const int* lpnTabPositions, int nTabOrigin) {
    LogGdiFont(hdc, L"TabbedTextOutW", _ReturnAddress());
    return orgTabbedTextOutW(hdc, x, y, lpString, nCount, nTabPositions, lpnTabPositions, nTabOrigin);
}

GpStatus WINAPI newGdipDrawString(GpGraphics* graphics, const WCHAR* string, int length, const GpFont* font, const void* layoutRect, const GpStringFormat* stringFormat, const GpBrush* brush) {
    LogGdipFont(font, graphics, L"GdipDrawString", _ReturnAddress());
    return orgGdipDrawString(graphics, string, length, font, layoutRect, stringFormat, brush);
}

GpStatus WINAPI newGdipDrawDriverString(GpGraphics* graphics, const UINT16* text, int length, const GpFont* font, const GpBrush* brush, const void* positions, int flags, const void* matrix) {
    LogGdipFont(font, graphics, L"GdipDrawDriverString", _ReturnAddress());
    return orgGdipDrawDriverString(graphics, text, length, font, brush, positions, flags, matrix);
}

GpStatus WINAPI newGdipCreateFontFamilyFromName(const WCHAR* name, GpFontCollection* fontCollection, GpFontFamily** FontFamily);
GpStatus WINAPI newGdipCreateFontFromLogfontW(HDC hdc, const LOGFONTW* logfont, GpFont** font);
GpStatus WINAPI newGdipCreateFontFromLogfontA(HDC hdc, const LOGFONTA* logfont, GpFont** font);
GpStatus WINAPI newGdipCreateFontFromHFONT(HDC hdc, HFONT hfont, GpFont** font);
GpStatus WINAPI newGdipCreateFontFromDC(HDC hdc, GpFont** font);
GpStatus WINAPI newGdipCreateFont(const GpFontFamily* fontFamily, float emSize, int style, int unit, GpFont** font);

static bool FindFontFilePath() {
    if (g_FontFilePath[0] != 0) return true;
    WCHAR rootDir[MAX_PATH];
    GetModuleFileNameW(g_hModule, rootDir, MAX_PATH);
    PathRemoveFileSpecW(rootDir);
    if (Config::EnableFileHook) {
        const wchar_t* tempRoot = Archive::GetTempRootW();
        if (tempRoot) {
            swprintf_s(g_FontFilePath, MAX_PATH, L"%s\\%s", tempRoot, Config::FontFileName);
            if (PathFileExistsW(g_FontFilePath)) return true;
        }
        swprintf_s(g_FontFilePath, MAX_PATH, L"%s\\%s\\%s", rootDir, Config::RedirectFolderW, Config::FontFileName);
        if (PathFileExistsW(g_FontFilePath)) return true;
    }
    swprintf_s(g_FontFilePath, MAX_PATH, L"%s\\%s", rootDir, Config::FontFileName);
    if (PathFileExistsW(g_FontFilePath)) return true;
    g_FontFilePath[0] = 0;
    return false;
}

static bool LoadGdiPlusPrivateFont() {
    if (g_PrivateFontCollection != NULL) return true;
    HMODULE hGdiPlus = GetModuleHandleW(L"gdiplus.dll");
    if (!hGdiPlus) hGdiPlus = LoadLibraryW(L"gdiplus.dll");
    if (!hGdiPlus) return false;
    ptrGdipNewPrivateFontCollection = (pGdipNewPrivateFontCollection)GetProcAddress(hGdiPlus, "GdipNewPrivateFontCollection");
    ptrGdipPrivateAddFontFile = (pGdipPrivateAddFontFile)GetProcAddress(hGdiPlus, "GdipPrivateAddFontFile");
    ptrGdipPrivateAddMemoryFont = (pGdipPrivateAddMemoryFont)GetProcAddress(hGdiPlus, "GdipPrivateAddMemoryFont");
    ptrGdiplusStartup = (pGdiplusStartup)GetProcAddress(hGdiPlus, "GdiplusStartup");
    ptrGdipGetLogFontW = (pGdipGetLogFontW)GetProcAddress(hGdiPlus, "GdipGetLogFontW");
    if (!ptrGdipNewPrivateFontCollection || !ptrGdipPrivateAddFontFile) return false;
    if (!FindFontFilePath()) return false;
    if (ptrGdiplusStartup && g_GdiplusToken == 0) {
        GdiplusStartupInput input;
        ptrGdiplusStartup(&g_GdiplusToken, &input, NULL);
    }
    GpStatus status = ptrGdipNewPrivateFontCollection(&g_PrivateFontCollection);
    if (status != 0 || g_PrivateFontCollection == NULL) return false;
    status = ptrGdipPrivateAddFontFile(g_PrivateFontCollection, g_FontFilePath);
    if (status != 0) { g_PrivateFontCollection = NULL; return false; }
    Utils::LogW(L"[Font] GDI+ Private font loaded: %s", g_FontFilePath);
    return true;
}

typedef HRESULT(WINAPI* pDWriteCreateFactory)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
static pDWriteCreateFactory orgDWriteCreateFactory = NULL;
typedef HRESULT(STDMETHODCALLTYPE* pCreateTextLayout)(IDWriteFactory*, const WCHAR*, UINT32, IDWriteTextFormat*, FLOAT, FLOAT, IDWriteTextLayout**);
static pCreateTextLayout orgCreateTextLayout = NULL;
typedef HRESULT(STDMETHODCALLTYPE* pIDWriteTextLayout_Draw)(IDWriteTextLayout*, void*, IDWriteTextRenderer*, FLOAT, FLOAT);
static pIDWriteTextLayout_Draw orgIDWriteTextLayout_Draw = NULL;

HRESULT STDMETHODCALLTYPE newIDWriteTextLayout_Draw(IDWriteTextLayout* This, void* clientDrawingContext, IDWriteTextRenderer* renderer, FLOAT originX, FLOAT originY) {
    if (Config::EnableDebug) {
        IDWriteTextFormat* format = NULL;
        if (SUCCEEDED(This->GetFontFamilyName(NULL, 0))) {
            LogFontChange(L"IDWriteTextLayout::Draw: DirectWrite Rendering...");
        }
    }
    return orgIDWriteTextLayout_Draw(This, clientDrawingContext, renderer, originX, originY);
}

HRESULT STDMETHODCALLTYPE newCreateTextLayout(IDWriteFactory* This, const WCHAR* string, UINT32 stringLength, IDWriteTextFormat* textFormat, FLOAT maxWidth, FLOAT maxHeight, IDWriteTextLayout** textLayout) {
    HRESULT hr = orgCreateTextLayout(This, string, stringLength, textFormat, maxWidth, maxHeight, textLayout);
    if (SUCCEEDED(hr) && textLayout && *textLayout) {
        if (Config::EnableDebug && textFormat) {
            WCHAR name[LF_FACESIZE];
            textFormat->GetFontFamilyName(name, LF_FACESIZE);
            LogFontChange(L"IDWriteFactory::CreateTextLayout: Font=\"%s\", Size=%.2f", name, textFormat->GetFontSize());
        }
        static std::mutex dwriteMutex;
        std::lock_guard<std::mutex> lock(dwriteMutex);
        if (orgIDWriteTextLayout_Draw == NULL) {
            void** vtable = *(void***)(*textLayout);
            orgIDWriteTextLayout_Draw = (pIDWriteTextLayout_Draw)vtable[18];
            DetourTransactionBegin(); DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID&)orgIDWriteTextLayout_Draw, newIDWriteTextLayout_Draw);
            DetourTransactionCommit();
        }
    }
    return hr;
}

HRESULT WINAPI newDWriteCreateFactory(DWRITE_FACTORY_TYPE factoryType, REFIID iid, IUnknown** factory) {
    HRESULT hr = orgDWriteCreateFactory(factoryType, iid, factory);
    if (SUCCEEDED(hr) && factory && *factory) {
        LogFontChange(L"[DWriteCreateFactory] DirectWrite Factory created.");
        if (orgCreateTextLayout == NULL) {
            void** vtable = *(void***)(*factory);
            orgCreateTextLayout = (pCreateTextLayout)vtable[12];
            DetourTransactionBegin(); DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID&)orgCreateTextLayout, newCreateTextLayout);
            DetourTransactionCommit();
        }
    }
    return hr;
}

void InstallGdiPlusHooks() {
    if (g_GdiPlusHooksInstalled) return;
    HMODULE hGdiPlus = GetModuleHandleW(L"gdiplus.dll");
    if (!hGdiPlus) return;
    orgGdipCreateFontFamilyFromName = (pGdipCreateFontFamilyFromName)GetProcAddress(hGdiPlus, "GdipCreateFontFamilyFromName");
    orgGdipCreateFontFromLogfontW = (pGdipCreateFontFromLogfontW)GetProcAddress(hGdiPlus, "GdipCreateFontFromLogfontW");
    orgGdipCreateFontFromLogfontA = (pGdipCreateFontFromLogfontA)GetProcAddress(hGdiPlus, "GdipCreateFontFromLogfontA");
    orgGdipCreateFontFromHFONT = (pGdipCreateFontFromHFONT)GetProcAddress(hGdiPlus, "GdipCreateFontFromHFONT");
    orgGdipCreateFontFromDC = (pGdipCreateFontFromDC)GetProcAddress(hGdiPlus, "GdipCreateFontFromDC");
    orgGdipCreateFont = (pGdipCreateFont)GetProcAddress(hGdiPlus, "GdipCreateFont");
    orgGdipDrawString = (pGdipDrawString)GetProcAddress(hGdiPlus, "GdipDrawString");
    orgGdipDrawDriverString = (pGdipDrawDriverString)GetProcAddress(hGdiPlus, "GdipDrawDriverString");
    ptrGdipGetLogFontW = (pGdipGetLogFontW)GetProcAddress(hGdiPlus, "GdipGetLogFontW");
    LoadGdiPlusPrivateFont();
    DetourTransactionBegin(); DetourUpdateThread(GetCurrentThread());
    if (orgGdipCreateFontFamilyFromName) DetourAttach(&(PVOID&)orgGdipCreateFontFamilyFromName, newGdipCreateFontFamilyFromName);
    if (orgGdipCreateFontFromLogfontW) DetourAttach(&(PVOID&)orgGdipCreateFontFromLogfontW, newGdipCreateFontFromLogfontW);
    if (orgGdipCreateFontFromLogfontA) DetourAttach(&(PVOID&)orgGdipCreateFontFromLogfontA, newGdipCreateFontFromLogfontA);
    if (orgGdipCreateFontFromHFONT) DetourAttach(&(PVOID&)orgGdipCreateFontFromHFONT, newGdipCreateFontFromHFONT);
    if (orgGdipCreateFontFromDC) DetourAttach(&(PVOID&)orgGdipCreateFontFromDC, newGdipCreateFontFromDC);
    if (orgGdipCreateFont) DetourAttach(&(PVOID&)orgGdipCreateFont, newGdipCreateFont);
    if (orgGdipDrawString) DetourAttach(&(PVOID&)orgGdipDrawString, newGdipDrawString);
    if (orgGdipDrawDriverString) DetourAttach(&(PVOID&)orgGdipDrawDriverString, newGdipDrawDriverString);
    DetourTransactionCommit();
    g_GdiPlusHooksInstalled = true;
}

static void EnsureFontLoaded() {
    if (g_FontInitialized) return;
    g_FontInitialized = true;
    if (g_hModule) g_CustomFontLoaded = Utils::LoadCustomFont(g_hModule) != FALSE;
    InstallGdiPlusHooks();
}

GpStatus WINAPI newGdipCreateFontFamilyFromName(const WCHAR* name, GpFontCollection* fontCollection, GpFontFamily** FontFamily) {
    EnsureFontLoaded();
    if (Config::EnableFontHook && Config::EnableFaceNameReplace && g_PrivateFontCollection) {
        GpStatus result = orgGdipCreateFontFamilyFromName(Config::ForcedFontNameW, g_PrivateFontCollection, FontFamily);
        if (result == 0) return result;
    }
    if (Config::EnableFontHook && Config::EnableFaceNameReplace) return orgGdipCreateFontFamilyFromName(Config::ForcedFontNameW, NULL, FontFamily);
    return orgGdipCreateFontFamilyFromName(name, fontCollection, FontFamily);
}

GpStatus WINAPI newGdipCreateFontFromLogfontW(HDC hdc, const LOGFONTW* logfont, GpFont** font) {
    EnsureFontLoaded();
    if (Config::EnableFontHook && logfont && g_CustomFontLoaded) {
        LOGFONTW lf = *logfont;
        if (Config::EnableFaceNameReplace) wcsncpy_s(lf.lfFaceName, Config::ForcedFontNameW, LF_FACESIZE - 1);
        lf.lfCharSet = Config::EnableCharsetReplace ? (BYTE)Config::ForcedCharset : 1;
        LogFontChange(L"[GdipCreateFontFromLogfontW] '%s' -> '%s'", logfont->lfFaceName, lf.lfFaceName);
        return orgGdipCreateFontFromLogfontW(hdc, &lf, font);
    }
    return orgGdipCreateFontFromLogfontW(hdc, logfont, font);
}

GpStatus WINAPI newGdipCreateFontFromLogfontA(HDC hdc, const LOGFONTA* logfont, GpFont** font) {
    EnsureFontLoaded();
    if (Config::EnableFontHook && logfont && g_CustomFontLoaded) {
        LOGFONTA lf = *logfont;
        if (Config::EnableFaceNameReplace) strncpy_s(lf.lfFaceName, Config::ForcedFontNameA, LF_FACESIZE - 1);
        lf.lfCharSet = Config::EnableCharsetReplace ? (BYTE)Config::ForcedCharset : 1;
        LogFontChange(L"[GdipCreateFontFromLogfontA] '%S' -> '%S'", logfont->lfFaceName, lf.lfFaceName);
        return orgGdipCreateFontFromLogfontA(hdc, &lf, font);
    }
    return orgGdipCreateFontFromLogfontA(hdc, logfont, font);
}

GpStatus WINAPI newGdipCreateFontFromHFONT(HDC hdc, HFONT hfont, GpFont** font) { return orgGdipCreateFontFromHFONT(hdc, hfont, font); }
GpStatus WINAPI newGdipCreateFontFromDC(HDC hdc, GpFont** font) { return orgGdipCreateFontFromDC(hdc, font); }
GpStatus WINAPI newGdipCreateFont(const GpFontFamily* fontFamily, float emSize, int style, int unit, GpFont** font) { return orgGdipCreateFont(fontFamily, emSize, style, unit, font); }

HFONT WINAPI newCreateFontA(int nH, int nW, int nE, int nO, int nWt, DWORD fI, DWORD fU, DWORD fS, DWORD fC, DWORD fOP, DWORD fCP, DWORD fQ, DWORD fPF, LPCSTR lpszF) {
    EnsureFontLoaded();
    if (!Config::EnableFontHook) return orgCreateFontA(nH, nW, nE, nO, nWt, fI, fU, fS, fC, fOP, fCP, fQ, fPF, lpszF);
    LPCSTR fF = (Config::EnableFaceNameReplace && g_CustomFontLoaded) ? Config::ForcedFontNameA : lpszF;
    DWORD fCs = Config::EnableCharsetReplace ? Config::ForcedCharset : (fC == 128 ? 1 : fC);
    int fH = Config::EnableFontHeightScale ? (int)(nH * Config::FontHeightScale) : nH;
    int fW = Config::EnableFontWidthScale ? (int)(nW * Config::FontWidthScale) : nW;
    int fWt = (Config::EnableFontWeight && Config::FontWeight > 0) ? Config::FontWeight : nWt;
    LogFontChange(L"[CreateFontA] '%S' -> '%S' cs=%d", lpszF ? lpszF : "NULL", fF, fCs);
    return orgCreateFontA(fH, fW, nE, nO, fWt, fI, fU, fS, fCs, fOP, fCP, fQ, fPF, fF);
}

HFONT WINAPI newCreateFontIndirectA(const LOGFONTA* lplf) {
    if (!lplf) return orgCreateFontIndirectA(lplf);
    EnsureFontLoaded();
    if (!Config::EnableFontHook) return orgCreateFontIndirectA(lplf);
    LOGFONTA lf = *lplf;
    if (Config::EnableFaceNameReplace && g_CustomFontLoaded) strncpy_s(lf.lfFaceName, Config::ForcedFontNameA, LF_FACESIZE - 1);
    lf.lfCharSet = Config::EnableCharsetReplace ? (BYTE)Config::ForcedCharset : (lf.lfCharSet == 128 ? 1 : lf.lfCharSet);
    if (Config::EnableFontHeightScale) lf.lfHeight = (LONG)(lf.lfHeight * Config::FontHeightScale);
    if (Config::EnableFontWidthScale) lf.lfWidth = (LONG)(lf.lfWidth * Config::FontWidthScale);
    if (Config::EnableFontWeight && Config::FontWeight > 0) lf.lfWeight = Config::FontWeight;
    LogFontChange(L"[CreateFontIndirectA] '%S' -> '%S'", lplf->lfFaceName, lf.lfFaceName);
    return orgCreateFontIndirectA(&lf);
}

HFONT WINAPI newCreateFontW(int nH, int nW, int nE, int nO, int nWt, DWORD fI, DWORD fU, DWORD fS, DWORD fC, DWORD fOP, DWORD fCP, DWORD fQ, DWORD fPF, LPCWSTR lpszF) {
    EnsureFontLoaded();
    if (!Config::EnableFontHook) return orgCreateFontW(nH, nW, nE, nO, nWt, fI, fU, fS, fC, fOP, fCP, fQ, fPF, lpszF);
    LPCWSTR fF = (Config::EnableFaceNameReplace && g_CustomFontLoaded) ? Config::ForcedFontNameW : lpszF;
    DWORD fCs = Config::EnableCharsetReplace ? Config::ForcedCharset : (fC == 128 ? 1 : fC);
    int fH = Config::EnableFontHeightScale ? (int)(nH * Config::FontHeightScale) : nH;
    int fW = Config::EnableFontWidthScale ? (int)(nW * Config::FontWidthScale) : nW;
    int fWt = (Config::EnableFontWeight && Config::FontWeight > 0) ? Config::FontWeight : nWt;
    LogFontChange(L"[CreateFontW] '%s' -> '%s' cs=%d", lpszF ? lpszF : L"NULL", fF, fCs);
    return orgCreateFontW(fH, fW, nE, nO, fWt, fI, fU, fS, fCs, fOP, fCP, fQ, fPF, fF);
}

HFONT WINAPI newCreateFontIndirectW(const LOGFONTW* lplf) {
    if (!lplf) return orgCreateFontIndirectW(lplf);
    EnsureFontLoaded();
    if (!Config::EnableFontHook) return orgCreateFontIndirectW(lplf);
    LOGFONTW lf = *lplf;
    if (Config::EnableFaceNameReplace && g_CustomFontLoaded) wcsncpy_s(lf.lfFaceName, Config::ForcedFontNameW, LF_FACESIZE - 1);
    lf.lfCharSet = Config::EnableCharsetReplace ? (BYTE)Config::ForcedCharset : (lf.lfCharSet == 128 ? 1 : lf.lfCharSet);
    if (Config::EnableFontHeightScale) lf.lfHeight = (LONG)(lf.lfHeight * Config::FontHeightScale);
    if (Config::EnableFontWidthScale) lf.lfWidth = (LONG)(lf.lfWidth * Config::FontWidthScale);
    if (Config::EnableFontWeight && Config::FontWeight > 0) lf.lfWeight = Config::FontWeight;
    LogFontChange(L"[CreateFontIndirectW] '%s' -> '%s'", lplf->lfFaceName, lf.lfFaceName);
    return orgCreateFontIndirectW(&lf);
}

HMODULE WINAPI newLoadLibraryW(LPCWSTR name) {
    HMODULE h = orgLoadLibraryW(name);
    if (h && name && (wcsstr(name, L"gdiplus") || wcsstr(name, L"GDIPLUS"))) InstallGdiPlusHooks();
    return h;
}

HMODULE WINAPI newLoadLibraryExW(LPCWSTR name, HANDLE f, DWORD fl) {
    HMODULE h = orgLoadLibraryExW(name, f, fl);
    if (h && name && (wcsstr(name, L"gdiplus") || wcsstr(name, L"GDIPLUS"))) InstallGdiPlusHooks();
    return h;
}

void SetFontHookModule(HMODULE hModule) { g_hModule = hModule; }

namespace Hooks {
    void InstallFontHook() {
        if (!Config::EnableFontHook) return;
        DetourTransactionBegin(); DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)orgCreateFontA, newCreateFontA);
        DetourAttach(&(PVOID&)orgCreateFontIndirectA, newCreateFontIndirectA);
        DetourAttach(&(PVOID&)orgCreateFontW, newCreateFontW);
        DetourAttach(&(PVOID&)orgCreateFontIndirectW, newCreateFontIndirectW);
        DetourAttach(&(PVOID&)orgLoadLibraryW, newLoadLibraryW);
        DetourAttach(&(PVOID&)orgLoadLibraryExW, newLoadLibraryExW);
        DetourAttach(&(PVOID&)orgTextOutA, newTextOutA); DetourAttach(&(PVOID&)orgTextOutW, newTextOutW);
        DetourAttach(&(PVOID&)orgExtTextOutA, newExtTextOutA); DetourAttach(&(PVOID&)orgExtTextOutW, newExtTextOutW);
        DetourAttach(&(PVOID&)orgDrawTextA, newDrawTextA); DetourAttach(&(PVOID&)orgDrawTextW, newDrawTextW);
        DetourAttach(&(PVOID&)orgDrawTextExA, newDrawTextExA); DetourAttach(&(PVOID&)orgDrawTextExW, newDrawTextExW);
        DetourAttach(&(PVOID&)orgPolyTextOutA, newPolyTextOutA); DetourAttach(&(PVOID&)orgPolyTextOutW, newPolyTextOutW);
        DetourAttach(&(PVOID&)orgTabbedTextOutA, newTabbedTextOutA); DetourAttach(&(PVOID&)orgTabbedTextOutW, newTabbedTextOutW);
        HMODULE hDWrite = GetModuleHandleW(L"dwrite.dll");
        if (hDWrite) {
            orgDWriteCreateFactory = (pDWriteCreateFactory)GetProcAddress(hDWrite, "DWriteCreateFactory");
            if (orgDWriteCreateFactory) DetourAttach(&(PVOID&)orgDWriteCreateFactory, newDWriteCreateFactory);
        }
        DetourTransactionCommit();
        Utils::Log("[Core] Font Analysis Mode Active.");
    }
}

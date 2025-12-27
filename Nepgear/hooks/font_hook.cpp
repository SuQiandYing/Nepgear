#include "../pch.h"
#include <windows.h>
#include <stdio.h>
#include "font_hook.h"
#include "config.h"
#include "utils.h"
#include "../detours.h"

typedef HFONT(WINAPI* pCreateFontA)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCSTR);
typedef HFONT(WINAPI* pCreateFontIndirectA)(const LOGFONTA*);
typedef HFONT(WINAPI* pCreateFontW)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR);
typedef HFONT(WINAPI* pCreateFontIndirectW)(const LOGFONTW*);

static pCreateFontA orgCreateFontA = CreateFontA;
static pCreateFontIndirectA orgCreateFontIndirectA = CreateFontIndirectA;
static pCreateFontW orgCreateFontW = CreateFontW;
static pCreateFontIndirectW orgCreateFontIndirectW = CreateFontIndirectW;

static HMODULE g_hModule = NULL;
static bool g_FontInitialized = false;
static bool g_CustomFontLoaded = false;
#include <set>
#include <string>
#include <mutex>

static std::set<std::string> g_LoggedFontsA;
static std::set<std::wstring> g_LoggedFontsW;
static std::mutex g_LogMutex;

static std::string AnsiToUtf8(const char* ansiStr) {
    if (!ansiStr || !ansiStr[0]) return "";
    int wideLen = MultiByteToWideChar(CP_ACP, 0, ansiStr, -1, NULL, 0);
    if (wideLen <= 0) return ansiStr;
    std::wstring wideStr(wideLen, 0);
    MultiByteToWideChar(CP_ACP, 0, ansiStr, -1, &wideStr[0], wideLen);
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, NULL, 0, NULL, NULL);
    if (utf8Len <= 0) return ansiStr;
    std::string utf8Str(utf8Len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, &utf8Str[0], utf8Len, NULL, NULL);
    if (!utf8Str.empty() && utf8Str.back() == '\0') utf8Str.pop_back();
    return utf8Str;
}

static void EnsureFontLoaded() {
    if (g_FontInitialized) return;
    g_FontInitialized = true;

    if (g_hModule) {
        g_CustomFontLoaded = Utils::LoadCustomFont(g_hModule) != FALSE;
        if (Config::EnableDebug) {
            Utils::Log("[Font] Lazy load complete: CustomFontLoaded=%s", g_CustomFontLoaded ? "true" : "false");
        }
    }
}

void SetFontHookModule(HMODULE hModule) {
    g_hModule = hModule;
}

HFONT WINAPI newCreateFontA(int nHeight, int nWidth, int nEscapement, int nOrientation, int fnWeight, DWORD fdwItalic, DWORD fdwUnderline, DWORD fdwStrikeOut, DWORD fdwCharSet, DWORD fdwOutputPrecision, DWORD fdwClipPrecision, DWORD fdwQuality, DWORD fdwPitchAndFamily, LPCSTR lpszFace) {
    if (!Config::EnableFontHook) {
        return orgCreateFontA(nHeight, nWidth, nEscapement, nOrientation, fnWeight, fdwItalic, fdwUnderline, fdwStrikeOut, fdwCharSet, fdwOutputPrecision, fdwClipPrecision, fdwQuality, fdwPitchAndFamily, lpszFace);
    }
    EnsureFontLoaded();
    if (!g_CustomFontLoaded) {
        return orgCreateFontA(nHeight, nWidth, nEscapement, nOrientation, fnWeight, fdwItalic, fdwUnderline, fdwStrikeOut, fdwCharSet, fdwOutputPrecision, fdwClipPrecision, fdwQuality, fdwPitchAndFamily, lpszFace);
    }
    if (Config::EnableDebug) {
        LPCSTR faceName = lpszFace ? lpszFace : "NULL";
        std::lock_guard<std::mutex> lock(g_LogMutex);
        if (g_LoggedFontsA.find(faceName) == g_LoggedFontsA.end()) {
            std::string utf8Face = AnsiToUtf8(faceName);
            std::string utf8Target = Config::EnableFaceNameReplace ? AnsiToUtf8(Config::ForcedFontNameA) : utf8Face;
            Utils::Log("[FontA] '%s' -> '%s'", utf8Face.c_str(), utf8Target.c_str());
            g_LoggedFontsA.insert(faceName);
        }
    }
    int finalHeight = Config::EnableFontHeightScale ? (int)(nHeight * Config::FontHeightScale) : nHeight;
    int finalWidth = Config::EnableFontWidthScale ? (int)(nWidth * Config::FontWidthScale) : nWidth;
    int finalWeight = Config::EnableFontWeight && Config::FontWeight > 0 ? Config::FontWeight : fnWeight;
    LPCSTR finalFace = Config::EnableFaceNameReplace ? Config::ForcedFontNameA : lpszFace;
    DWORD finalCharset = Config::EnableCharsetReplace ? Config::ForcedCharset : fdwCharSet;
    HFONT result = orgCreateFontA(finalHeight, finalWidth, nEscapement, nOrientation, finalWeight, fdwItalic, fdwUnderline, fdwStrikeOut, finalCharset, fdwOutputPrecision, fdwClipPrecision, fdwQuality, fdwPitchAndFamily, finalFace);
    if (!result) {
        if (Config::EnableDebug) {
            Utils::Log("[FontA] CreateFontA failed for custom font, falling back to original");
        }
        result = orgCreateFontA(nHeight, nWidth, nEscapement, nOrientation, fnWeight, fdwItalic, fdwUnderline, fdwStrikeOut, fdwCharSet, fdwOutputPrecision, fdwClipPrecision, fdwQuality, fdwPitchAndFamily, lpszFace);
    }
    return result;
}

HFONT WINAPI newCreateFontIndirectA(const LOGFONTA* lplf) {
    if (!lplf) return orgCreateFontIndirectA(lplf);
    if (!Config::EnableFontHook) {
        return orgCreateFontIndirectA(lplf);
    }

    EnsureFontLoaded();
    if (!g_CustomFontLoaded) {
        return orgCreateFontIndirectA(lplf);
    }

    if (Config::EnableDebug) {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        if (g_LoggedFontsA.find(lplf->lfFaceName) == g_LoggedFontsA.end()) {
            std::string utf8Face = AnsiToUtf8(lplf->lfFaceName);
            std::string utf8Target = Config::EnableFaceNameReplace ? AnsiToUtf8(Config::ForcedFontNameA) : utf8Face;
            Utils::Log("[FontIndirectA] '%s' -> '%s'", utf8Face.c_str(), utf8Target.c_str());
            g_LoggedFontsA.insert(lplf->lfFaceName);
        }
    }

    LOGFONTA modifiedLf = *lplf;
    if (Config::EnableFaceNameReplace) {
        strncpy_s(modifiedLf.lfFaceName, Config::ForcedFontNameA, LF_FACESIZE - 1);
    }
    if (Config::EnableCharsetReplace) {
        modifiedLf.lfCharSet = (BYTE)Config::ForcedCharset;
    }
    if (Config::EnableFontHeightScale) {
        modifiedLf.lfHeight = (LONG)(lplf->lfHeight * Config::FontHeightScale);
    }
    if (Config::EnableFontWidthScale) {
        modifiedLf.lfWidth = (LONG)(lplf->lfWidth * Config::FontWidthScale);
    }
    if (Config::EnableFontWeight && Config::FontWeight > 0) {
        modifiedLf.lfWeight = Config::FontWeight;
    }

    return orgCreateFontIndirectA(&modifiedLf);
}

HFONT WINAPI newCreateFontW(int nHeight, int nWidth, int nEscapement, int nOrientation, int fnWeight, DWORD fdwItalic, DWORD fdwUnderline, DWORD fdwStrikeOut, DWORD fdwCharSet, DWORD fdwOutputPrecision, DWORD fdwClipPrecision, DWORD fdwQuality, DWORD fdwPitchAndFamily, LPCWSTR lpszFace) {
    if (!Config::EnableFontHook) {
        return orgCreateFontW(nHeight, nWidth, nEscapement, nOrientation, fnWeight, fdwItalic, fdwUnderline, fdwStrikeOut, fdwCharSet, fdwOutputPrecision, fdwClipPrecision, fdwQuality, fdwPitchAndFamily, lpszFace);
    }
    EnsureFontLoaded();
    if (!g_CustomFontLoaded) {
        return orgCreateFontW(nHeight, nWidth, nEscapement, nOrientation, fnWeight, fdwItalic, fdwUnderline, fdwStrikeOut, fdwCharSet, fdwOutputPrecision, fdwClipPrecision, fdwQuality, fdwPitchAndFamily, lpszFace);
    }
    if (Config::EnableDebug) {
        LPCWSTR faceName = lpszFace ? lpszFace : L"NULL";
        std::lock_guard<std::mutex> lock(g_LogMutex);
        if (g_LoggedFontsW.find(faceName) == g_LoggedFontsW.end()) {
            Utils::Log("[FontW] '%S' -> '%S'", faceName, Config::EnableFaceNameReplace ? Config::ForcedFontNameW : faceName);
            g_LoggedFontsW.insert(faceName);
        }
    }
    int finalHeight = Config::EnableFontHeightScale ? (int)(nHeight * Config::FontHeightScale) : nHeight;
    int finalWidth = Config::EnableFontWidthScale ? (int)(nWidth * Config::FontWidthScale) : nWidth;
    int finalWeight = Config::EnableFontWeight && Config::FontWeight > 0 ? Config::FontWeight : fnWeight;
    LPCWSTR finalFace = Config::EnableFaceNameReplace ? Config::ForcedFontNameW : lpszFace;
    DWORD finalCharset = Config::EnableCharsetReplace ? Config::ForcedCharset : fdwCharSet;
    HFONT result = orgCreateFontW(finalHeight, finalWidth, nEscapement, nOrientation, finalWeight, fdwItalic, fdwUnderline, fdwStrikeOut, finalCharset, fdwOutputPrecision, fdwClipPrecision, fdwQuality, fdwPitchAndFamily, finalFace);
    if (!result) {
        if (Config::EnableDebug) {
            Utils::Log("[FontW] CreateFontW failed for custom font, falling back to original");
        }
        result = orgCreateFontW(nHeight, nWidth, nEscapement, nOrientation, fnWeight, fdwItalic, fdwUnderline, fdwStrikeOut, fdwCharSet, fdwOutputPrecision, fdwClipPrecision, fdwQuality, fdwPitchAndFamily, lpszFace);
    }
    return result;
}

HFONT WINAPI newCreateFontIndirectW(const LOGFONTW* lplf) {
    if (!lplf) return orgCreateFontIndirectW(lplf);
    if (!Config::EnableFontHook) {
        return orgCreateFontIndirectW(lplf);
    }

    EnsureFontLoaded();
    if (!g_CustomFontLoaded) {
        return orgCreateFontIndirectW(lplf);
    }

    if (Config::EnableDebug) {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        if (g_LoggedFontsW.find(lplf->lfFaceName) == g_LoggedFontsW.end()) {
            Utils::Log("[FontIndirectW] '%S' -> '%S'", lplf->lfFaceName, Config::EnableFaceNameReplace ? Config::ForcedFontNameW : lplf->lfFaceName);
            g_LoggedFontsW.insert(lplf->lfFaceName);
        }
    }

    LOGFONTW modifiedLf = *lplf;
    if (Config::EnableFaceNameReplace) {
        wcsncpy_s(modifiedLf.lfFaceName, Config::ForcedFontNameW, LF_FACESIZE - 1);
    }
    if (Config::EnableCharsetReplace) {
        modifiedLf.lfCharSet = (BYTE)Config::ForcedCharset;
    }
    if (Config::EnableFontHeightScale) {
        modifiedLf.lfHeight = (LONG)(lplf->lfHeight * Config::FontHeightScale);
    }
    if (Config::EnableFontWidthScale) {
        modifiedLf.lfWidth = (LONG)(lplf->lfWidth * Config::FontWidthScale);
    }
    if (Config::EnableFontWeight && Config::FontWeight > 0) {
        modifiedLf.lfWeight = Config::FontWeight;
    }

    return orgCreateFontIndirectW(&modifiedLf);
}

namespace Hooks {
    void InstallFontHook() {
        if (!Config::EnableFontHook) return;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)orgCreateFontA, newCreateFontA);
        DetourAttach(&(PVOID&)orgCreateFontIndirectA, newCreateFontIndirectA);
        DetourAttach(&(PVOID&)orgCreateFontW, newCreateFontW);
        DetourAttach(&(PVOID&)orgCreateFontIndirectW, newCreateFontIndirectW);
        DetourTransactionCommit();
        Utils::Log("[Core] Font Hooks Installed (lazy load pending).");
    }
}
